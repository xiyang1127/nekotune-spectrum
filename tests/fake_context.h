#pragma once
#include <nekotune/sdk/extension_context.h>
#include <map>
#include <optional>

/// Synchronous in-memory SDK 3 context for plugin tests.
class FakeContext final : public nekotune::sdk::ExtensionContext {
  public:
    using Params = nekotune::sdk::Params;
    using Reply = nekotune::sdk::Reply;
    using Done = nekotune::sdk::Done;
    QString extensionId = "nekotune.spectrum";
    Params configuration{{"frequency", 0}, {"variety", 0}};
    QMap<QString, nekotune::sdk::Handler> serviceHandlers;
    std::map<int, std::pair<QString, nekotune::sdk::EventHandler>> listeners;
    QStringList hostCalls;
    Params hostReply{{"state", "playing"}};
    bool saveFails = false;
    QString failHost;
    QList<QPair<QString, Params>> published;
    // Shuffle
    nekotune::sdk::IShuffleProvider *provider = nullptr;
    int registrations = 0;
    // PCM
    bool supported = true;
    nekotune::sdk::PcmHandler consumer;
    int subscriptions = 0;

    void state(const QString &value) {
        const auto copy = listeners;
        for (const auto &[key, listener] : copy)
            listener.second({{"event", "player.state_changed"}, {"state", value}});
    }
    nekotune::Result<Params> call(const QString &id, const Params &params = {}) {
        const auto handler = serviceHandlers.value(id);
        if (!handler)
            return nekotune::failure("Missing service");
        std::optional<nekotune::Result<Params>> result;
        handler(params, [&](nekotune::Result<Params> value) { result = std::move(value); });
        return result ? *result : nekotune::Result<Params>(nekotune::failure("No reply"));
    }

    const nekotune::sdk::ExtensionInfo &info() const override {
        m_info = {extensionId, "1.0.0", {}, {}};
        return m_info;
    }
    nekotune::sdk::Host &host() override { return m_host; }
    nekotune::sdk::Events &events() override { return m_events; }
    nekotune::sdk::Services &services() override { return m_services; }
    nekotune::sdk::Commands &commands() override { return m_commands; }
    nekotune::sdk::Ui &ui() override { return m_ui; }
    nekotune::sdk::Music &music() override { return m_music; }
    nekotune::sdk::Lyrics &lyrics() override { return m_lyrics; }
    nekotune::sdk::Shuffle &shuffle() override { return m_shuffle; }
    nekotune::sdk::Pcm &pcm() override { return m_pcm; }
    nekotune::sdk::Config &config() override { return m_config; }
    nekotune::sdk::Storage &storage() override { return m_storage; }
    nekotune::sdk::Secrets &secrets() override { return m_secrets; }
    nekotune::sdk::Tasks &tasks() override { return m_tasks; }
    nekotune::sdk::Logger &log() override { return m_logger; }
    std::unique_ptr<nekotune::sdk::ThreadAttribution> attributeThread() override {
        return std::make_unique<nekotune::sdk::ThreadAttribution>();
    }

  private:
    class Handle final : public nekotune::sdk::Registration {
      public:
        explicit Handle(std::function<void()> dispose) : m_dispose(std::move(dispose)) {}
        ~Handle() override { dispose(); }
        void dispose() override {
            if (auto dispose = std::exchange(m_dispose, {}))
                dispose();
        }

      private:
        std::function<void()> m_dispose;
    };
    static nekotune::sdk::RegistrationPtr handle(std::function<void()> dispose) {
        return std::make_unique<Handle>(std::move(dispose));
    }

    struct HostFacet final : nekotune::sdk::Host {
        FakeContext &c;
        explicit HostFacet(FakeContext &c) : c(c) {}
        void call(const QString &method, const Params &, Reply done) override {
            c.hostCalls.append(method);
            if (method == c.failHost) {
                done(nekotune::failure("injected host failure"));
                return;
            }
            done(c.hostReply);
        }
    };
    struct EventsFacet final : nekotune::sdk::Events {
        FakeContext &c;
        explicit EventsFacet(FakeContext &c) : c(c) {}
        nekotune::sdk::RegistrationPtr on(const QString &event, nekotune::sdk::EventHandler handler) override {
            const auto key = ++c.m_next;
            c.listeners.emplace(key, std::pair{event, std::move(handler)});
            return handle([&c = c, key] { c.listeners.erase(key); });
        }
        void publish(const QString &name, const Params &data) override { c.published.append({name, data}); }
    };
    struct ServicesFacet final : nekotune::sdk::Services {
        FakeContext &c;
        explicit ServicesFacet(FakeContext &c) : c(c) {}
        nekotune::sdk::RegistrationPtr add(const QString &id, nekotune::sdk::Handler handler) override {
            c.serviceHandlers.insert(id, std::move(handler));
            return handle([&c = c, id] { c.serviceHandlers.remove(id); });
        }
        void call(const QString &, const QString &, const Params &, Reply done) override {
            done(nekotune::failure("unused"));
        }
    };
    struct CommandsFacet final : nekotune::sdk::Commands {
        nekotune::sdk::RegistrationPtr add(const QString &, nekotune::sdk::Handler,
                                           const nekotune::sdk::CommandInfo &) override {
            return {};
        }
    };
    struct UiFacet final : nekotune::sdk::Ui {
        nekotune::sdk::RegistrationPtr add(const QString &, const QString &, const Params &) override { return {}; }
    };
    struct MusicFacet final : nekotune::sdk::Music {
        nekotune::sdk::RegistrationPtr add(const QString &, nekotune::sdk::IMusicProvider *,
                                           const nekotune::sdk::MusicSourceInfo &) override {
            return {};
        }
    };
    struct LyricsFacet final : nekotune::sdk::Lyrics {
        nekotune::sdk::RegistrationPtr add(const QString &, nekotune::sdk::ILyricsProvider *,
                                           const nekotune::sdk::LyricsSourceInfo &) override {
            return {};
        }
    };
    struct ShuffleFacet final : nekotune::sdk::Shuffle {
        FakeContext &c;
        explicit ShuffleFacet(FakeContext &c) : c(c) {}
        nekotune::sdk::RegistrationPtr add(const QString &, nekotune::sdk::IShuffleProvider *next,
                                           const nekotune::sdk::ShuffleInfo &info) override {
            if (c.provider || info.name.trimmed().isEmpty())
                return {};
            c.provider = next;
            ++c.registrations;
            return handle([&c = c] { c.provider = nullptr; });
        }
    };
    struct PcmFacet final : nekotune::sdk::Pcm {
        FakeContext &c;
        explicit PcmFacet(FakeContext &c) : c(c) {}
        nekotune::sdk::PcmCapabilities capabilities() const override {
            return {c.supported, c.supported ? "ffmpeg" : "qt", c.supported ? "" : "playback_backend_unsupported"};
        }
        nekotune::sdk::RegistrationPtr subscribe(nekotune::sdk::PcmHandler handler) override {
            if (c.consumer || !c.supported)
                return {};
            ++c.subscriptions;
            c.consumer = std::move(handler);
            return handle([&c = c] { c.consumer = {}; });
        }
    };
    struct ConfigFacet final : nekotune::sdk::Config {
        FakeContext &c;
        explicit ConfigFacet(FakeContext &c) : c(c) {}
        Params get() const override { return c.configuration; }
        void update(const Params &next, Done done) override {
            if (c.saveFails) {
                done(nekotune::failure("injected save failure"));
                return;
            }
            c.configuration = next;
            const auto copy = c.listeners;
            for (const auto &[key, entry] : copy)
                if (entry.first == "extension.config_changed")
                    entry.second({{"config", next}});
            done({});
        }
    };
    struct StorageFacet final : nekotune::sdk::Storage {
        void get(const QString &, std::function<void(nekotune::Result<QVariant>)> done) override { done(QVariant{}); }
        void set(const QString &, const QVariant &, Done done) override { done({}); }
    };
    struct SecretsFacet final : nekotune::sdk::Secrets {
        void get(const QString &, std::function<void(nekotune::Result<QString>)> done) override { done(QString{}); }
        void set(const QString &, const QString &, Done done) override { done({}); }
        void remove(const QString &, Done done) override { done({}); }
    };
    struct TasksFacet final : nekotune::sdk::Tasks {
        void report(const QString &, const nekotune::sdk::TaskProgress &) override {}
    };
    struct LoggerFacet final : nekotune::sdk::Logger {
        void write(QtMsgType, const QString &) override {}
    };

    mutable nekotune::sdk::ExtensionInfo m_info;
    int m_next = 0;
    HostFacet m_host{*this};
    EventsFacet m_events{*this};
    ServicesFacet m_services{*this};
    CommandsFacet m_commands;
    UiFacet m_ui;
    MusicFacet m_music;
    LyricsFacet m_lyrics;
    ShuffleFacet m_shuffle{*this};
    PcmFacet m_pcm{*this};
    ConfigFacet m_config{*this};
    StorageFacet m_storage;
    SecretsFacet m_secrets;
    TasksFacet m_tasks;
    LoggerFacet m_logger;
};
