#include "music/source.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace music {
namespace {
class DemoSource final : public Source {
 public:
  bool read(float* left, float* right, std::size_t count) noexcept override {
    const double step = bpm_.load(std::memory_order_relaxed) / (60.0 * sample_rate);
    const bool percussion = drums_.load(std::memory_order_relaxed);
    const bool pulse = pulse_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < count; ++i) {
      if (beat_phase_ >= 1 || beat_ == UINT64_MAX) {
        beat_phase_ = beat_ == UINT64_MAX ? 0 : beat_phase_ - 1;
        ++beat_;
        constexpr int roots[] = {50, 46, 53, 48};
        const int root = roots[(beat_ / 16) % 4];
        const std::array<int, 3> chord{root, root + ((beat_ / 16) % 4 == 0 ? 3 : 4), root + 7};
        for (int j = 0; j < 3; ++j) increments_[j] = hz(chord[j]) / sample_rate;
        arp_increment_ = hz(chord[beat_ % 3] + 12) / sample_rate;
      }
      float l = 0, r = 0;
      for (int j = 0; j < 3; ++j) {
        phase_[j] += increments_[j]; phase_[j] -= std::floor(phase_[j]);
        const auto value = float(std::sin(2 * std::numbers::pi * phase_[j]) * 0.12);
        l += value * (0.6f + 0.15f * j); r += value * (0.9f - 0.15f * j);
      }
      arp_phase_ += arp_increment_; arp_phase_ -= std::floor(arp_phase_);
      const double envelope = (1 - std::exp(-beat_phase_ * 100)) * std::exp(-beat_phase_ * (pulse ? 5 : 9));
      const auto arp = float(std::sin(2 * std::numbers::pi * arp_phase_) * envelope * (pulse ? 0.3 : 0.1));
      float drum = 0;
      if (percussion) {
        const double seconds = beat_phase_ * 60 / bpm_.load(std::memory_order_relaxed);
        if (beat_ % 2 == 0) drum = float(0.35 * std::sin(2 * std::numbers::pi * 55 * seconds) * std::exp(-seconds * 18));
        noise_ ^= noise_ << 13; noise_ ^= noise_ >> 17; noise_ ^= noise_ << 5;
        drum += float((double(noise_) / UINT32_MAX - 0.5) * std::exp(-seconds * 100) * 0.1);
      }
      left[i] = l + arp + drum; right[i] = r + arp + drum;
      beat_phase_ += step;
    }
    return true;
  }
  void style(const std::string& text) override {
    if (text != "ambient" && text != "pulse") throw std::runtime_error("Demo styles are 'ambient' and 'pulse'; free-text styles need --source magenta");
    pulse_.store(text == "pulse");
  }
  void drums(bool enabled) override { drums_.store(enabled); }
  void tempo(float bpm) override { bpm_.store(bpm); }
  void temperature(float) override { throw std::runtime_error("Temperature applies to the AI source"); }
  const char* name() const override { return "demo (procedural, not AI)"; }

 private:
  static double hz(int note) { return 440 * std::exp2((note - 69) / 12.0); }
  std::atomic<float> bpm_{90};
  std::atomic<bool> drums_{false}, pulse_{false};
  std::array<double, 3> phase_{}, increments_{};
  double beat_phase_ = 0, arp_phase_ = 0, arp_increment_ = 0;
  std::uint64_t beat_ = UINT64_MAX;
  std::uint32_t noise_ = 0x13772abc;
};
}  // namespace
std::unique_ptr<Source> make_demo_source() { return std::make_unique<DemoSource>(); }
}  // namespace music
