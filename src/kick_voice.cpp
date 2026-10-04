#include "music/kick_voice.hpp"
#include <string>

namespace music {
namespace {
constexpr double tau = 2 * std::numbers::pi;
void range(double value, double low, double high, const char* name) {
  if (!std::isfinite(value) || value < low || value > high)
    throw std::invalid_argument(std::string(name) + " must be between " +
      std::to_string(low) + " and " + std::to_string(high));
}
double decay(double milliseconds, double rate) {
  return std::exp(std::log(.001) / (milliseconds * .001 * rate));
}
}
void KickPatch::validate() const {
  range(pitch, 30, 100, "pitch (Hz)"); range(sweep, 0, 36, "sweep (semitones)");
  range(pitch_decay_ms, 5, 300, "pitch-decay (ms)");
  range(attack_ms, .2, 10, "attack (ms)"); range(decay_ms, 50, 2000, "decay (ms)");
  range(click, 0, 1, "click"); range(click_decay_ms, 2, 50, "click-decay (ms)");
  range(drive_db, 0, 18, "drive (dB)"); range(level, 0, 1, "level");
}
KickVoice::KickVoice(double sample_rate) : rate_(sample_rate * VoiceOutput::oversampling), output_(sample_rate) {
  noise_coef_ = std::exp(-tau * 900 / rate_);
  click_low_coef_ = std::exp(-tau * std::min(7000., sample_rate * .35) / rate_);
  velocity_coef_ = std::exp(-1 / (.001 * rate_));
  patch(patch_);
}
void KickVoice::patch(const KickPatch& value) {
  value.validate(); patch_ = value;
  attack_step_ = 1 / (value.attack_ms * .001 * rate_);
  decay_coef_ = decay(value.decay_ms, rate_);
  pitch_coef_ = decay(value.pitch_decay_ms, rate_);
  click_coef_ = decay(value.click_decay_ms, rate_);
  drive_gain_ = std::pow(10., value.drive_db / 20);
}
bool KickVoice::trigger(double velocity) noexcept {
  if (!std::isfinite(velocity) || velocity < 0 || velocity > 1) return false;
  if (velocity == 0) return true;  // A zero-velocity strike does not interrupt the tail.
  if (amplitude_ < 1e-5) { phase_ = amplitude_ = 0; velocity_ = velocity; }
  // Keep phase and current amplitude during fast retriggers.
  target_velocity_ = velocity;
  pitch_env_ = click_env_ = 1;
  click_attack_ = 0;
  attacking_ = true;
  return true;
}
void KickVoice::reset() noexcept {
  phase_ = amplitude_ = pitch_env_ = click_env_ = click_attack_ = 0;
  velocity_ = target_velocity_ = noise_low_ = click_low_ = 0;
  noise_ = 0x34d27a19; attacking_ = false; output_.reset();
}
float KickVoice::render() noexcept {
  for (unsigned i = 0; i < VoiceOutput::oversampling; ++i) {
    if (attacking_) {
      amplitude_ = std::min(1., amplitude_ + attack_step_);
      if (amplitude_ == 1) attacking_ = false;
    } else {
      amplitude_ *= decay_coef_;
      if (amplitude_ < 1e-8) amplitude_ = 0;
    }
    const auto hz = patch_.pitch * std::exp2(patch_.sweep * pitch_env_ / 12);
    phase_ += hz / rate_; phase_ -= std::floor(phase_);
    pitch_env_ *= pitch_coef_;
    const auto sine = std::sin(tau * phase_);
    const auto body = (patch_.drive_db == 0 ? sine : std::tanh(sine * drive_gain_) / std::tanh(drive_gain_)) * amplitude_;
    noise_ ^= noise_ << 13; noise_ ^= noise_ >> 17; noise_ ^= noise_ << 5;
    const auto noise = double(noise_) / UINT32_MAX * 2 - 1;
    noise_low_ = noise + (noise_low_ - noise) * noise_coef_;
    const auto high = noise - noise_low_;
    click_low_ = high + (click_low_ - high) * click_low_coef_;
    click_attack_ = std::min(1., click_attack_ + 1 / (.0002 * rate_));
    const auto click = click_low_ * click_env_ * click_attack_ * patch_.click;
    click_env_ *= click_coef_;
    if (click_env_ < 1e-8) click_env_ = 0;
    velocity_ = target_velocity_ + (velocity_ - target_velocity_) * velocity_coef_;
    // Leave headroom for the transient without reducing the body as click rises.
    output_.push(.35 * (body + .5 * click) * velocity_ * patch_.level);
  }
  return output_.read();
}
}  // namespace music
