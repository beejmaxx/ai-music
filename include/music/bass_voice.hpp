#pragma once

#include "music/voice_output.hpp"

namespace music {

struct BassPatch {
  enum class Oscillator { saw, pulse, sine };
  Oscillator oscillator = Oscillator::saw;
  double pulse_width = .5, sub = .2;
  double cutoff = 180, resonance = .8, filter_amount = 3, filter_decay_ms = 240;
  double attack_ms = 3, decay_ms = 240, sustain = .65, release_ms = 100;
  double drive_db = 3, glide_ms = 0, level = .55;
  void validate() const;
};

// One monophonic instrument, independent of transport, drums and harmony.
// Call controls and render on the same thread. render() allocates nothing.
class BassVoice {
 public:
  explicit BassVoice(double sample_rate = 48000);
  void patch(const BassPatch& value);
  bool note_on(unsigned midi, double velocity = 1, bool retrigger = true) noexcept;
  void note_off() noexcept;
  float render() noexcept;
  void reset() noexcept;

 private:
  enum class Envelope { idle, attack, decay, sustain, release };
  static constexpr unsigned oversampling = VoiceOutput::oversampling;
  double rate_, internal_rate_;
  BassPatch patch_;
  VoiceOutput output_;
  Envelope stage_ = Envelope::idle;
  double phase_ = 0, sub_phase_ = 0, current_note_ = 36, target_note_ = 36;
  double velocity_ = 0, target_velocity_ = 0, amp_ = 0, filter_env_ = 0, cutoff_ = 180;
  double integrator1_ = 0, integrator2_ = 0;
  double attack_step_ = 0, decay_coef_ = 0, release_coef_ = 0, filter_coef_ = 0;
  double glide_coef_ = 0, cutoff_coef_ = 0, velocity_coef_ = 0, drive_gain_ = 1;
};
}  // namespace music
