#pragma once
#include <map>
#include <nekotune/sdk/pcm_provider.h>
#include <optional>

class FakeContext : public nekotune::sdk::IExtensionContext {
  public:
    using Params = nekotune::sdk::Params;
    using Reply = nekotune::sdk::Reply;
    Params configuration{{"frequency", 0}, {"variety", 0}};
    QMap<QString, nekotune::sdk::Handler> services;
    std::map<int, std::pair<QString, nekotune::sdk::EventHandler>> listeners;
    QStringList hostCalls;
    bool saveFails = false;
    QString failHost;
    QString id() const override { return "nekotune.spectrum"; }
    QString version() const override { return "1.0.0"; }
    QString directory() const override { return {}; }
    QString dataDirectory() const override { return {}; }
    Params config() const override { return configuration; }
    void updateConfig(const Params &next, std::function<void(nekotune::Result<void>)> done) override {
        if (saveFails) {
            done(nekotune::failure("injected save failure"));
            return;
        }
        configuration = next;
        const auto copy = listeners;
        for (const auto &[key, entry] : copy)
            if (entry.first == "extension.config_changed")
                entry.second({{"config", next}});
        done({});
    }
    void hostCall(const QString &method, const Params &, Reply done) override {
        hostCalls.append(method);
        if (method == failHost) {
            done(nekotune::failure("injected host failure"));
            return;
        }
        done(Params{{"state", "playing"}});
    }
    nekotune::sdk::RegistrationPtr registerService(const QString &id, nekotune::sdk::Handler handler) override {
        services.insert(id, std::move(handler));
        return handle([this, id] { services.remove(id); });
    }
    void callService(const QString &, const QString &, const Params &, Reply done) override {
        done(nekotune::failure("unused"));
    }
    nekotune::sdk::RegistrationPtr registerCommand(const QString &, nekotune::sdk::Handler, const Params &) override {
        return {};
    }
    nekotune::sdk::RegistrationPtr registerUi(const QString &, const QString &, const Params &) override { return {}; }
    nekotune::sdk::RegistrationPtr registerMusicProvider(const QString &, nekotune::sdk::IMusicProvider *,
                                                         const Params &) override {
        return {};
    }
    nekotune::sdk::RegistrationPtr registerLyricsProvider(const QString &, nekotune::sdk::ILyricsProvider *,
                                                          const Params &) override {
        return {};
    }
    nekotune::sdk::RegistrationPtr on(const QString &event, nekotune::sdk::EventHandler handler) override {
        const auto key = ++m_next;
        listeners.emplace(key, std::pair{event, std::move(handler)});
        return handle([this, key] { listeners.erase(key); });
    }
    QList<QPair<QString, Params>> published;
    void publish(const QString &name, const Params &data) override { published.append({name, data}); }
    void state(const QString &value) {
        const auto copy = listeners;
        for (const auto &[key, listener] : copy)
            listener.second({{"event", "player.state_changed"}, {"state", value}});
    }
    void storageGet(const QString &, std::function<void(nekotune::Result<QVariant>)> done) override {
        done(QVariant{});
    }
    void storageSet(const QString &, const QVariant &, std::function<void(nekotune::Result<void>)> done) override {
        done({});
    }
    void secretGet(const QString &, std::function<void(nekotune::Result<QString>)> done) override { done(QString{}); }
    void secretSet(const QString &, const QString &, std::function<void(nekotune::Result<void>)> done) override {
        done({});
    }
    void secretDelete(const QString &, std::function<void(nekotune::Result<void>)> done) override { done({}); }
    void reportTask(const QString &, const Params &) override {}
    void log(QtMsgType, const QString &) override {}
    nekotune::Result<Params> call(const QString &id, const Params &params = {}) {
        const auto handler = services.value(id);
        if (!handler)
            return nekotune::failure("Missing service");
        std::optional<nekotune::Result<Params>> result;
        handler(params, [&](nekotune::Result<Params> value) { result = std::move(value); });
        return result ? *result : nekotune::Result<Params>(nekotune::failure("No reply"));
    }

  protected:
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
    nekotune::sdk::RegistrationPtr handle(std::function<void()> dispose) {
        return std::make_unique<Handle>(std::move(dispose));
    }

  private:
    int m_next = 0;
};

class PcmContext final : public FakeContext, public nekotune::sdk::IPcmExtensionContext {
  public:
    bool supported = true;
    nekotune::sdk::PcmHandler consumer;
    int subscriptions = 0;
    nekotune::sdk::PcmCapabilities pcmCapabilities() const override {
        return {supported, supported ? "ffmpeg" : "qt", supported ? "" : "playback_backend_unsupported"};
    }
    nekotune::sdk::RegistrationPtr subscribePcm(nekotune::sdk::PcmHandler handler) override {
        if (consumer || !supported)
            return {};
        ++subscriptions;
        consumer = std::move(handler);
        return handle([this] { consumer = {}; });
    }
};
