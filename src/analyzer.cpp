#include "analyzer.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
namespace spectrum {
namespace {
void fft(std::vector<std::complex<double>> &data) {
    const auto n = data.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (size_t length = 2; length <= n; length *= 2) {
        const auto step = std::polar(1.0, -2 * std::numbers::pi / double(length));
        for (size_t start = 0; start < n; start += length) {
            std::complex<double> phase = 1;
            for (size_t j = 0; j < length / 2; ++j) {
                const auto a = data[start + j], b = data[start + j + length / 2] * phase;
                data[start + j] = a + b;
                data[start + j + length / 2] = a - b;
                phase *= step;
            }
        }
    }
}
} // namespace
void Analyzer::reset() { *this = Analyzer{}; }
std::optional<Frame> Analyzer::push(const nekotune::sdk::PcmBlock &block) {
    if (block.channels != 2 || block.sampleRate < 8000 || block.sampleRate > 384000 || block.samples.isEmpty() ||
        block.samples.size() % 2)
        return {};
    if (rate != block.sampleRate || generation != block.generation || expectedFrame != block.framePosition) {
        reset();
        rate = block.sampleRate;
        generation = block.generation;
        size = 1;
        while (size < size_t(rate * .085))
            size *= 2;
        left.resize(size);
        right.resize(size);
        window.resize(size);
        for (size_t i = 0; i < size; ++i)
            window[i] = float(.5 - .5 * std::cos(2 * std::numbers::pi * double(i) / double(size - 1)));
    }
    std::optional<Frame> frame;
    const auto frames = block.samples.size() / 2;
    for (qsizetype i = 0; i < frames; ++i) {
        const auto l = block.samples[i * 2], r = block.samples[i * 2 + 1];
        left[cursor] = std::isfinite(l) ? l : 0;
        right[cursor] = std::isfinite(r) ? r : 0;
        cursor = (cursor + 1) % size;
        count = std::min(size, count + 1);
        ++sinceFrame;
        if (count == size && sinceFrame >= size_t(rate / 30)) {
            frame = calculate(block.positionMs + i * 1000 / rate);
            sinceFrame = 0;
        }
    }
    expectedFrame = block.framePosition + frames;
    return frame;
}
Frame Analyzer::calculate(qint64 positionMs) {
    std::vector<std::complex<double>> l(size), r(size);
    double sum = 0;
    for (size_t i = 0; i < size; ++i) {
        l[i] = left[(cursor + i) % size] * window[i];
        r[i] = right[(cursor + i) % size] * window[i];
        sum += window[i];
    }
    fft(l);
    fft(r);
    Frame result;
    result.generation = generation;
    result.positionMs = positionMs;
    const double maximum = std::min(16000., rate / 2.);
    for (size_t band = 0; band < result.bands.size(); ++band) {
        const double low = 40 * std::pow(maximum / 40, double(band) / 48);
        const double high = 40 * std::pow(maximum / 40, double(band + 1) / 48);
        const auto first = std::clamp<size_t>(size_t(std::ceil(low * double(size) / rate)), 1, size / 2);
        const auto last =
            std::clamp<size_t>(std::max(first, size_t(std::floor(high * double(size) / rate))), first, size / 2);
        double power = 0;
        for (auto bin = first; bin <= last; ++bin)
            power = std::max(power, (std::norm(l[bin]) + std::norm(r[bin])) * 2 / (sum * sum));
        const double db = 10 * std::log10(std::max(power, 1e-12));
        const float target = float(std::clamp((db + 72) / 72, 0., 1.));
        smoothed[band] += (target - smoothed[band]) * (target > smoothed[band] ? .85f : .22f);
        result.bands[band] = smoothed[band];
    }
    return result;
}
} // namespace spectrum
