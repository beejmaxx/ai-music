#include "music/bass_voice.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace music {
namespace {
constexpr double tau = 2 * std::numbers::pi;
void range(double value, double low, double high, const char* name) {
  if (!std::isfinite(value) || value < low || value > high)
    throw std::invalid_argument(std::string(name) + " must be between " +
      std::to_string(low) + " and " + std::to_string(high));
}
double blep(double phase, double increment) noexcept {
  if (phase < increment) {
    const auto x = phase / increment;
    return x + x - x * x - 1;
  }
  if (phase > 1 - increment) {
    const auto x = (phase - 1) / increment;
    return x * x + x + x + 1;
  }
  return 0;
}
double advance(double& phase, double increment) noexcept {
  phase += increment;
  phase -= std::floor(phase);
  return phase;
}
double decay(double milliseconds, double rate) {
  // Time to fall to 0.1% of the starting difference (60 dB).
  return std::exp(std::log(.001) / (milliseconds * .001 * rate));
}
}  // namespace

void BassPatch::validate() const {
  if (oscillator != Oscillator::saw && oscillator != Oscillator::pulse && oscillator != Oscillator::sine)
    throw std::invalid_argument("Unknown bass oscillator");
  range(pulse_width, .1, .9, "pulse-width"); range(sub, 0, 1, "sub");
  range(cutoff, 40, 10000, "cutoff"); range(resonance, .5, 6, "resonance (Q)");
  range(filter_amount, 0, 6, "filter-amount (octaves)");
  range(filter_decay_ms, 10, 3000, "filter-decay (ms)");
  range(attack_ms, .5, 500, "attack (ms)"); range(decay_ms, 10, 3000, "decay (ms)");
  range(sustain, 0, 1, "sustain"); range(release_ms, 5, 3000, "release (ms)");
  range(drive_db, 0, 24, "drive (dB)"); range(glide_ms, 0, 500, "glide (ms)");
  range(level, 0, 1, "level");
}

BassVoice::BassVoice(double sample_rate)
    : rate_(sample_rate), internal_rate_(sample_rate * oversampling), output_(sample_rate) {
  range(sample_rate, 8000, 192000, "sample rate");
  velocity_coef_ = std::exp(-1 / (.002 * internal_rate_));
  patch(patch_);
  reset();
}
void BassVoice::patch(const BassPatch& value) {
  value.validate();
  patch_ = value;
  attack_step_ = 1 / (value.attack_ms * .001 * internal_rate_);
  decay_coef_ = decay(value.decay_ms, internal_rate_);
  release_coef_ = decay(value.release_ms, internal_rate_);
  filter_coef_ = decay(value.filter_decay_ms, internal_rate_);
  glide_coef_ = value.glide_ms ? decay(value.glide_ms, internal_rate_) : 0;
  cutoff_coef_ = std::exp(-1 / (.001 * internal_rate_));
  drive_gain_ = std::pow(10., value.drive_db / 20);
}
bool BassVoice::note_on(unsigned midi, double velocity, bool retrigger) noexcept {
  if (midi < 24 || midi > 96 || !std::isfinite(velocity) || velocity < 0 || velocity > 1) return false;
  if (velocity == 0) { note_off(); return true; }
  const bool silent = stage_ == Envelope::idle;
  target_note_ = midi;
  if (silent || !patch_.glide_ms) current_note_ = target_note_;
  target_velocity_ = velocity;
  if (silent) velocity_ = velocity;
  if (retrigger || silent || stage_ == Envelope::release) {
    // Preserve phase and current amplitude on retrigger to avoid hard edges.
    stage_ = Envelope::attack;
    filter_env_ = 1;
  }
  return true;
}
void BassVoice::note_off() noexcept {
  if (stage_ != Envelope::idle) stage_ = Envelope::release;
}
void BassVoice::reset() noexcept {
  stage_ = Envelope::idle;
  phase_ = sub_phase_ = amp_ = filter_env_ = velocity_ = target_velocity_ = 0;
  current_note_ = target_note_ = 36;
  cutoff_ = patch_.cutoff;
  integrator1_ = integrator2_ = 0;
  output_.reset();
}
float BassVoice::render() noexcept {
  for (unsigned step = 0; step < oversampling; ++step) {
    switch (stage_) {
      case Envelope::idle: amp_ = 0; break;
      case Envelope::attack:
        amp_ = std::min(1., amp_ + attack_step_);
        if (amp_ == 1) stage_ = Envelope::decay;
        break;
      case Envelope::decay:
        amp_ = patch_.sustain + (amp_ - patch_.sustain) * decay_coef_;
        if (std::abs(amp_ - patch_.sustain) < 1e-6) stage_ = Envelope::sustain;
        break;
      case Envelope::sustain: amp_ = patch_.sustain; break;
      case Envelope::release:
        amp_ *= release_coef_;
        if (amp_ < 1e-7) { amp_ = 0; stage_ = Envelope::idle; }
        break;
    }
    current_note_ = target_note_ + (current_note_ - target_note_) * glide_coef_;
    velocity_ = target_velocity_ + (velocity_ - target_velocity_) * velocity_coef_;
    const auto increment = 440 * std::exp2((current_note_ - 69) / 12) / internal_rate_;
    const auto phase = advance(phase_, increment);
    double oscillator = std::sin(tau * phase);
    if (patch_.oscillator == BassPatch::Oscillator::saw)
      oscillator = 2 * phase - 1 - blep(phase, increment);
    if (patch_.oscillator == BassPatch::Oscillator::pulse) {
      auto edge = phase - patch_.pulse_width;
      if (edge < 0) edge += 1;
      oscillator = (phase < patch_.pulse_width ? 1. : -1.) + blep(phase, increment) - blep(edge, increment)
        - (2 * patch_.pulse_width - 1);
    }
    const auto sub = std::sin(tau * advance(sub_phase_, increment * .5));
    const auto mixed = (oscillator + patch_.sub * sub) / (1 + patch_.sub);
    const auto driven = patch_.drive_db == 0 ? mixed : std::tanh(mixed * drive_gain_) / std::tanh(drive_gain_);
    const auto target = std::min({patch_.cutoff * std::exp2(patch_.filter_amount * filter_env_),
      12000., rate_ * .4});
    cutoff_ = target + (cutoff_ - target) * cutoff_coef_;
    filter_env_ *= filter_coef_;
    const auto g = std::tan(std::numbers::pi * cutoff_ / internal_rate_);
    const auto damping = 1 / patch_.resonance;
    const auto band = (integrator1_ + g * (driven - integrator2_)) / (1 + g * (g + damping));
    const auto low = integrator2_ + g * band;
    integrator1_ = 2 * band - integrator1_; integrator2_ = 2 * low - integrator2_;
    // Keep resonance bounded before anti-alias filtering, with headroom.
    output_.push(.45 * std::tanh(low) * amp_ * velocity_ * patch_.level);
  }
  return output_.read();
}
}  // namespace music
