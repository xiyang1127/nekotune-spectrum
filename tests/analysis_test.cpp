#include "analyzer.h"
#include "leases.h"
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <numbers>
using namespace spectrum;
using nekotune::sdk::PcmBlock;
class AnalysisTest : public QObject {
    Q_OBJECT
    static Frame tone(double frequency, bool antiphase = false, double amplitude = .5, int rate = 48000,
                      double secondFrequency = 0) {
        Analyzer analyzer;
        std::optional<Frame> result;
        for (int first = 0; first < rate / 3; first += 512) {
            PcmBlock block;
            block.sampleRate = rate;
            block.generation = 1;
            block.framePosition = first;
            block.samples.resize(1024);
            for (int i = 0; i < 512; ++i) {
                const auto phase = 2 * std::numbers::pi * (first + i) / rate;
                const auto value = float(amplitude * (std::sin(phase * frequency) +
                                                      (secondFrequency > 0 ? std::sin(phase * secondFrequency) : 0)));
                block.samples[i * 2] = value;
                block.samples[i * 2 + 1] = antiphase ? -value : value;
            }
            if (auto frame = analyzer.push(block))
                result = frame;
        }
        return result.value();
    }
  private slots:
    void silence() {
        for (float band : tone(1000, false, 0).bands)
            QCOMPARE(band, 0.f);
    }
    void frequencyAndAntiphase() {
        const auto normal = tone(1000), anti = tone(1000, true);
        QCOMPARE(normal.bands, anti.bands);
        const auto index = std::max_element(normal.bands.begin(), normal.bands.end()) - normal.bands.begin();
        QVERIFY(index >= 24 && index <= 27);
        QVERIFY(normal.bands[size_t(index)] > .85);
        for (float value : normal.bands)
            QVERIFY(std::isfinite(value) && value >= 0 && value <= 1);
    }
    void twoTones() {
        const auto frame = tone(440, false, .25, 48000, 4000);
        QVERIFY(*std::max_element(frame.bands.begin() + 17, frame.bands.begin() + 23) > .7);
        QVERIFY(*std::max_element(frame.bands.begin() + 34, frame.bands.begin() + 40) > .7);
    }
    void sampleRates() {
        for (const auto rate : {8000, 44100, 48000, 96000, 192000}) {
            const auto frame = tone(1000, false, .5, rate);
            QVERIFY(*std::max_element(frame.bands.begin(), frame.bands.end()) > .7);
        }
    }
    void discontinuityAndMalformed() {
        Analyzer analyzer;
        PcmBlock block;
        block.sampleRate = 48000;
        block.samples.fill(1, 1024);
        for (int i = 0; i < 10; ++i) {
            block.framePosition = i * 512;
            analyzer.push(block);
        }
        block.generation = 2;
        block.framePosition = 0;
        block.samples.fill(0);
        QVERIFY(!analyzer.push(block));
        block.sampleRate = -1;
        QVERIFY(!analyzer.push(block));
        block.sampleRate = 48000;
        block.channels = 1;
        QVERIFY(!analyzer.push(block));
    }
    void leases() {
        Leases leases;
        const auto a = leases.acquire(0), b = leases.acquire(0);
        QVERIFY(a != b);
        QCOMPARE(leases.size(), 2);
        QVERIFY(leases.renew(a, 10000));
        leases.expire(15000);
        QCOMPARE(leases.size(), 1);
        QVERIFY(!leases.renew(b, 15000));
        leases.release(a);
        QCOMPARE(leases.size(), 0);
        leases.acquire(0);
        leases.expire(15000);
        QCOMPARE(leases.size(), 0);
    }
};
QTEST_GUILESS_MAIN(AnalysisTest)
#include "analysis_test.moc"
