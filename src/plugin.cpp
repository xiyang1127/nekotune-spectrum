#include "analyzer.h"
#include "leases.h"
#include <QElapsedTimer>
#include <QTimer>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <nekotune/sdk/extension_plugin.h>
#include <thread>
#include <utility>

using namespace nekotune;
namespace {
class Worker {
  public:
    Worker() : thread([this] { run(); }) {}
    ~Worker() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        condition.notify_one();
        thread.join();
    }
    void submit(const sdk::PcmBlock &block) {
        std::unique_lock lock(mutex, std::try_to_lock);
        if (!lock || block.samples.size() > block.sampleRate / 2)
            return;
        if (generation != block.generation) {
            pending.clear();
            samples = 0;
            latest.reset();
            generation = block.generation;
        }
        // At most 250ms. Dropped chunks create a frame discontinuity and reset the FFT.
        while (!pending.empty() && samples + block.samples.size() > block.sampleRate / 2) {
            samples -= pending.front().samples.size();
            pending.pop_front();
        }
        pending.push_back(block);
        samples += block.samples.size();
        condition.notify_one();
    }
    std::optional<spectrum::Frame> take() {
        std::lock_guard lock(mutex);
        if (std::chrono::steady_clock::now() - completed > std::chrono::milliseconds(500))
            latest.reset();
        return std::exchange(latest, {});
    }

  private:
    void run() {
        spectrum::Analyzer analyzer;
        for (;;) {
            sdk::PcmBlock block;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [this] { return stopping || !pending.empty(); });
                if (stopping)
                    return;
                block = std::move(pending.front());
                pending.pop_front();
                samples -= block.samples.size();
            }
            if (auto frame = analyzer.push(block)) {
                std::lock_guard lock(mutex);
                if (frame->generation == generation) {
                    latest = *frame;
                    completed = std::chrono::steady_clock::now();
                }
            }
        }
    }
    std::mutex mutex;
    std::condition_variable condition;
    bool stopping = false;
    qsizetype samples = 0;
    quint64 generation = 0;
    std::deque<sdk::PcmBlock> pending;
    std::optional<spectrum::Frame> latest;
    std::chrono::steady_clock::time_point completed{};
    std::thread thread;
};
} // namespace
class SpectrumPlugin final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    SpectrumPlugin() {
        timer.setInterval(33);
        connect(&timer, &QTimer::timeout, this, [this] { tick(); });
    }
    void activate(sdk::IExtensionContext &value, std::function<void(Result<void>)> done) override {
        context = &value;
        pcm = sdk::pcmContext(value);
        clock.start();
        registrations.push_back(value.registerService("status", [this](const sdk::Params &, sdk::Reply reply) {
            reconcile();
            reply(status());
        }));
        registrations.push_back(value.registerService("acquire", [this](const sdk::Params &, sdk::Reply reply) {
            const auto token = leases.acquire(clock.elapsed());
            reconcile();
            if (token.isEmpty()) {
                reply(failure("Too many spectrum consumers"));
                return;
            }
            auto response = status();
            response.insert("token", token);
            response.insert("ttlMs", spectrum::Leases::ttlMs);
            reply(response);
        }));
        registrations.push_back(value.registerService("renew", [this](const sdk::Params &params, sdk::Reply reply) {
            if (!leases.renew(params.value("token").toString(), clock.elapsed())) {
                reply(failure("Spectrum lease expired"));
                return;
            }
            reconcile();
            reply(status());
        }));
        registrations.push_back(value.registerService("release", [this](const sdk::Params &params, sdk::Reply reply) {
            leases.release(params.value("token").toString());
            reconcile();
            reply(status());
        }));
        events = value.on("*", [this](const sdk::Params &event) {
            const auto name = event.value("event").toString();
            if (name == "player.state_changed")
                playing = event.value("state").toString() == "playing";
            if (name.startsWith("player."))
                reconcile();
        });
        const auto requestGeneration = ++activation;
        value.hostCall("player.status", {}, [this, requestGeneration](Result<sdk::Params> result) {
            if (!context || requestGeneration != activation || !result)
                return;
            playing = result.value().value("state").toString() == "playing";
            reconcile();
        });
        timer.start();
        done({});
    }
    void deactivate() override {
        ++activation;
        timer.stop();
        subscription.reset();
        worker.reset();
        events.reset();
        registrations.clear();
        leases.clear();
        pcm = nullptr;
        context = nullptr;
        playing = false;
        lastStatus.clear();
        sequence = 0;
    }

  private:
    sdk::Params status() const {
        const auto capability = pcm ? pcm->pcmCapabilities() : sdk::PcmCapabilities{false, {}, "host_pcm_unsupported"};
        return {{"version", 1},
                {"available", capability.available},
                {"backend", capability.backend},
                {"reason", capability.reason},
                {"consumers", leases.size()},
                {"playing", playing},
                {"analyzing", bool(subscription)},
                {"bands", 48},
                {"hz", 30}};
    }
    void reconcile() {
        if (!context)
            return;
        leases.expire(clock.elapsed());
        const bool wanted = pcm && pcm->pcmCapabilities().available && playing && leases.size() > 0;
        if (wanted && !subscription) {
            worker = std::make_unique<Worker>();
            subscription = pcm->subscribePcm([this](const sdk::PcmBlock &block) {
                if (worker)
                    worker->submit(block);
            });
            if (!subscription)
                worker.reset();
        } else if (!wanted) {
            subscription.reset();
            worker.reset();
        }
        const auto current = status();
        if (current != lastStatus) {
            lastStatus = current;
            context->publish("status", current);
        }
    }
    void tick() {
        reconcile();
        if (!worker)
            return;
        const auto frame = worker->take();
        if (!frame)
            return;
        QVariantList bands;
        bands.reserve(48);
        for (const auto value : frame->bands)
            bands.append(value);
        context->publish("frame", {{"version", 1},
                                   {"generation", frame->generation},
                                   {"sequence", ++sequence},
                                   {"positionMs", frame->positionMs},
                                   {"bands", bands}});
    }
    sdk::IExtensionContext *context = nullptr;
    sdk::IPcmExtensionContext *pcm = nullptr;
    std::vector<sdk::RegistrationPtr> registrations;
    sdk::RegistrationPtr subscription, events;
    std::unique_ptr<Worker> worker;
    spectrum::Leases leases;
    QElapsedTimer clock;
    QTimer timer;
    sdk::Params lastStatus;
    quint64 sequence = 0, activation = 0;
    bool playing = false;
};
#include "plugin.moc"
