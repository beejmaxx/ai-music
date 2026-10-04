#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace music {
// Shared output stage for instruments running their nonlinear path at 4x rate.
// Push four internal samples, then read one output sample. No allocations.
class VoiceOutput {
 public:
  static constexpr unsigned oversampling = 4;
  explicit VoiceOutput(double rate) {
    if (!std::isfinite(rate) || rate < 8000 || rate > 192000)
      throw std::invalid_argument("Sample rate must be between 8000 and 192000");
    constexpr double q[] = {.5176380902050415, .7071067811865476, 1.9318516525781366};
    const auto angle = 2 * std::numbers::pi * std::min(16000., rate * .4) / (rate * oversampling);
    for (unsigned i = 0; i < filters_.size(); ++i) {
      auto& f = filters_[i];
      const auto cosine = std::cos(angle), alpha = std::sin(angle) / (2 * q[i]);
      f.b0 = (1 - cosine) / (2 * (1 + alpha)); f.b1 = 2 * f.b0; f.b2 = f.b0;
      f.a1 = -2 * cosine / (1 + alpha); f.a2 = (1 - alpha) / (1 + alpha);
    }
    dc_coef_ = std::exp(-2 * std::numbers::pi * 15 / rate);
  }
  void push(double value) noexcept {
    for (auto& f : filters_) {
      const auto out = f.b0 * value + f.z1;
      f.z1 = f.b1 * value - f.a1 * out + f.z2;
      f.z2 = f.b2 * value - f.a2 * out;
      value = out;
    }
    filtered_ = value;
  }
  float read() noexcept {
    const auto clean = filtered_ - dc_input_ + dc_coef_ * dc_output_;
    dc_input_ = filtered_; dc_output_ = clean;
    return float(clean);
  }
  void reset() noexcept {
    filtered_ = dc_input_ = dc_output_ = 0;
    for (auto& f : filters_) f.z1 = f.z2 = 0;
  }
 private:
  struct Filter { double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
  std::array<Filter, 3> filters_;
  double filtered_ = 0, dc_coef_ = 0, dc_input_ = 0, dc_output_ = 0;
};
}  // namespace music
