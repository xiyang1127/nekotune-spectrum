#include "fake_context.h"
#include <QPluginLoader>
#include <QtTest>
#include <cmath>
#include <nekotune/sdk/extension_plugin.h>
#include <numbers>
using namespace nekotune;
class PluginTest : public QObject {
    Q_OBJECT
    QPluginLoader loader;
    sdk::IExtensionPlugin *plugin = nullptr;
  private slots:
    void initTestCase() {
        loader.setFileName(QCoreApplication::arguments().at(1));
        loader.setLoadHints(QLibrary::PreventUnloadHint);
        plugin = qobject_cast<sdk::IExtensionPlugin *>(loader.instance());
        QVERIFY2(plugin, qPrintable(loader.errorString()));
    }
    void oldHostAndRepeatedActivation() {
        FakeContext old;
        for (int i = 0; i < 2; ++i) {
            bool success = false;
            plugin->activate(old, [&](Result<void> result) { success = bool(result); });
            QVERIFY(success);
            const auto status = old.call("status");
            QVERIFY(status);
            QCOMPARE(status.value().value("reason").toString(), QString("host_pcm_unsupported"));
            plugin->deactivate();
            QVERIFY(old.services.isEmpty());
            QVERIFY(old.listeners.empty());
        }
    }
    void multiConsumerFramesPauseReleaseAndFallback() {
        PcmContext context;
        plugin->activate(context, [](Result<void>) {});
        QVERIFY(!context.consumer);
        const auto a = context.call("acquire").value().value("token").toString();
        const auto b = context.call("acquire").value().value("token").toString();
        QVERIFY(!a.isEmpty() && a != b);
        QVERIFY(context.consumer);
        QCOMPARE(context.subscriptions, 1);
        QVERIFY(!context.call("renew", {{"token", "invalid"}}));
        QVERIFY(context.call("renew", {{"token", a}}));
        sdk::PcmBlock block;
        block.sampleRate = 48000;
        block.generation = 1;
        block.samples.resize(1024);
        for (int chunk = 0; chunk < 40; ++chunk) {
            block.framePosition = chunk * 512;
            block.positionMs = chunk * 512 * 1000 / 48000;
            for (int i = 0; i < 512; ++i) {
                float sample = float(.5 * std::sin(2 * std::numbers::pi * 1000 * (chunk * 512 + i) / 48000));
                block.samples[i * 2] = sample;
                block.samples[i * 2 + 1] = -sample;
            }
            context.consumer(block);
            QTest::qWait(5);
        }
        QTRY_VERIFY(std::any_of(context.published.begin(), context.published.end(),
                                [](const auto &event) { return event.first == "frame"; }));
        for (const auto &event : context.published)
            if (event.first == "frame") {
                QCOMPARE(event.second.value("bands").toList().size(), 48);
                QVERIFY(event.second.value("sequence").toULongLong() > 0);
            }
        context.state("paused");
        QVERIFY(!context.consumer);
        const auto before = context.published.size();
        QTest::qWait(80);
        QCOMPARE(context.published.size(), before);
        context.state("playing");
        QVERIFY(context.consumer);
        context.supported = false;
        QVERIFY(!context.call("status").value().value("available").toBool());
        QVERIFY(!context.consumer);
        context.supported = true;
        context.call("status");
        QVERIFY(context.consumer);
        context.call("release", {{"token", a}});
        QVERIFY(context.consumer);
        context.call("release", {{"token", b}});
        QVERIFY(!context.consumer);
        context.call("acquire");
        QVERIFY(context.consumer);
        plugin->deactivate();
        QVERIFY(!context.consumer);
        QVERIFY(context.services.isEmpty());
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 1;
    PluginTest test;
    return QTest::qExec(&test, 1, argv);
}
#include "plugin_test.moc"
