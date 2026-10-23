#pragma once
#include <array>
#include <nekotune/sdk/pcm_provider.h>
#include <optional>
#include <vector>

namespace spectrum {
struct Frame {
    std::array<float, 48> bands{};
    quint64 generation = 0;
    qint64 positionMs = 0;
};
class Analyzer {
  public:
    std::optional<Frame> push(const nekotune::sdk::PcmBlock &block);
    void reset();

  private:
    Frame calculate(qint64 positionMs);
    int rate = 0;
    size_t size = 0, cursor = 0, count = 0, sinceFrame = 0;
    quint64 generation = 0;
    qint64 expectedFrame = -1;
    std::vector<float> left, right, window;
    std::array<float, 48> smoothed{};
};
} // namespace spectrum
