#pragma once
#include "music/ring.hpp"
#include "music/source.hpp"
#include <atomic>
#include <cmath>
#include <numbers>
#include <vector>

namespace music {
// Storage is allocated before playback. The audio thread owns all DSP state.
class Effects {
 public:
  void filter(float hz) { cutoff_.store(hz); }
  void delay(float wet) { wet_.store(wet); }
  void tempo(float bpm) { bpm_.store(bpm); }
  float filter() const { return cutoff_.load(); }
  float delay() const { return wet_.load(); }
  void begin_block() noexcept {
    const auto cutoff = cutoff_.load(std::memory_order_relaxed);
    target_alpha_ = cutoff >= 19500 ? 1 : 1 - std::exp(-2 * std::numbers::pi_v<float> * cutoff / sample_rate);
    target_wet_ = wet_.load(std::memory_order_relaxed);
    target_samples_ = .75f * 60 * sample_rate / bpm_.load(std::memory_order_relaxed);
  }
  StereoFrame process(float left, float right) noexcept {
    alpha_ += (target_alpha_ - alpha_) / 480;
    wet_gain_ += (target_wet_ - wet_gain_) / 480;
    delay_samples_ += (target_samples_ - delay_samples_) * .00005f;
    low_l_ += alpha_ * (left - low_l_); low_r_ += alpha_ * (right - low_r_);
    low2_l_ += alpha_ * (low_l_ - low2_l_); low2_r_ += alpha_ * (low_r_ - low2_r_);
    float position = float(cursor_ + memory_.size()) - delay_samples_;
    if (position >= memory_.size()) position -= float(memory_.size());
    const auto a = std::size_t(position), b = (a + 1) % memory_.size();
    const auto fraction = position - float(a);
    const auto echo_l = memory_[a].left + fraction * (memory_[b].left - memory_[a].left);
    const auto echo_r = memory_[a].right + fraction * (memory_[b].right - memory_[a].right);
    memory_[cursor_] = {low2_l_ + echo_r * .42f, low2_r_ + echo_l * .42f};
    cursor_ = (cursor_ + 1) % memory_.size();
    return {low2_l_ + wet_gain_ * echo_l, low2_r_ + wet_gain_ * echo_r};
  }
 private:
  std::atomic<float> cutoff_{20000}, wet_{0}, bpm_{128};
  std::vector<StereoFrame> memory_{sample_rate * 2};
  std::size_t cursor_ = 0;
  float low_l_ = 0, low_r_ = 0, low2_l_ = 0, low2_r_ = 0;
  float alpha_ = 1, target_alpha_ = 1, wet_gain_ = 0, target_wet_ = 0;
  float delay_samples_ = .75f * 60 * sample_rate / 128, target_samples_ = delay_samples_;
};
}  // namespace music
