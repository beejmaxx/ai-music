#pragma once
#include "music/voice_output.hpp"
#include <cstdint>

namespace music {
struct KickPatch {
  double pitch = 52, sweep = 24, pitch_decay_ms = 55;
  double attack_ms = .5, decay_ms = 380;
  double click = .12, click_decay_ms = 12, drive_db = 3, level = .65;
  void validate() const;
};

// Single kick instrument. Controls and render belong to the same audio thread.
class KickVoice {
 public:
  explicit KickVoice(double sample_rate = 48000);
  void patch(const KickPatch& value);
  bool trigger(double velocity = 1) noexcept;
  float render() noexcept;
  void reset() noexcept;
 private:
  double rate_;
  VoiceOutput output_;
  KickPatch patch_;
  std::uint32_t noise_ = 0x34d27a19;
  double phase_ = 0, amplitude_ = 0, pitch_env_ = 0, click_env_ = 0, click_attack_ = 0;
  double velocity_ = 0, target_velocity_ = 0, noise_low_ = 0, click_low_ = 0;
  double attack_step_ = 0, decay_coef_ = 0, pitch_coef_ = 0, click_coef_ = 0;
  double noise_coef_ = 0, click_low_coef_ = 0, velocity_coef_ = 0, drive_gain_ = 1;
  bool attacking_ = false;
};
}  // namespace music
