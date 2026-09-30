#include "music/source.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace music {
namespace {
constexpr double tau = 2 * std::numbers::pi;
double frequency(int midi) { return 440 * std::exp2((midi - 69) / 12.0); }
double advance(double& phase, double increment) {
  phase += increment;
  phase -= std::floor(phase);
  return phase;
}
double saw(double phase, double increment) {
  // PolyBLEP smooths the saw's discontinuity to reduce aliasing.
  double correction = 0;
  if (phase < increment) {
    const auto t = phase / increment;
    correction = t + t - t * t - 1;
  } else if (phase > 1 - increment) {
    const auto t = (phase - 1) / increment;
    correction = t * t + t + t + 1;
  }
  return 2 * phase - 1 - correction;
}

class SynthSource final : public Source {
 public:
  SynthSource() {
    const float defaults[] = {.85f, .35f, .25f, .55f, .32f, .3f};
    for (std::size_t i = 0; i < levels_.size(); ++i) levels_[i] = smooth_levels_[i] = defaults[i];
  }

  bool read(float* left, float* right, std::size_t count) noexcept override {
    const auto step_increment = bpm_.load(std::memory_order_relaxed) * 4 / (60.0 * sample_rate);
    const bool trance = trance_.load(std::memory_order_relaxed);
    const bool percussion = drums_.load(std::memory_order_relaxed);
    std::array<float, 6> levels;
    for (std::size_t j = 0; j < levels.size(); ++j) levels[j] = levels_[j].load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < count; ++i) {
      if (first_ || step_phase_ >= 1) {
        if (!first_) { step_phase_ -= 1; ++step_; }
        first_ = false;
        trigger(trance);
      }
      for (std::size_t j = 0; j < levels.size(); ++j)
        smooth_levels_[j] += (levels[j] - smooth_levels_[j]) * .002f;

      // Every sound is synthesized here, on the current musical clock.
      const auto kick_hz = 48 + 120 * std::exp(-kick_age_ * 65);
      advance(kick_phase_, kick_hz / sample_rate);
      const auto kick = std::sin(tau * kick_phase_) * std::exp(-kick_age_ * 13)
        * (1 - std::exp(-kick_age_ * 2500));
      const auto noise = random();
      const auto high_noise = noise - noise_previous_;
      noise_previous_ = noise;
      double clap_env = 0;
      for (double offset : {0., .012, .024}) {
        const auto age = clap_age_ - offset;
        if (age >= 0) clap_env += std::exp(-age * 95);
      }
      const auto clap = high_noise * clap_env * .23;
      const auto hat = high_noise * std::exp(-hat_age_ * (hat_open_ ? 26 : 145)) * .24;

      // Duck the tonal parts on each kick; bass and arp retain continuous phase.
      const double beat_phase = (step_ % 4 + step_phase_) / 4;
      const auto duck = percussion ? .27 + .73 * (1 - std::exp(-beat_phase * 7)) : 1;
      const auto bass_target = bass_on_ && step_phase_ < .58 ? 1. : 0.;
      bass_env_ += (bass_target - bass_env_) * (bass_target ? .012 : .0015);
      advance(bass_phase_, bass_increment_);
      const auto bass = (.8 * std::sin(tau * bass_phase_) + .2 * saw(bass_phase_, bass_increment_)) * bass_env_ * .65;

      const auto lead_target = lead_on_ && step_phase_ < (trance ? .5 : .35) ? 1. : 0.;
      lead_env_ += (lead_target - lead_env_) * (lead_target ? .007 : .0011);
      double lead_l = 0, lead_r = 0;
      constexpr double detunes[] = {.996, 1., 1.004};
      for (std::size_t j = 0; j < 3; ++j) {
        const auto inc = lead_increment_ * detunes[j];
        advance(lead_phase_[j], inc);
        const auto voice = trance ? saw(lead_phase_[j], inc)
                                 : std::sin(tau * lead_phase_[j]) + .25 * std::sin(2 * tau * lead_phase_[j]);
        lead_l += voice * (j == 0 ? .5 : .25);
        lead_r += voice * (j == 2 ? .5 : .25);
      }
      lead_l *= lead_env_ * .45; lead_r *= lead_env_ * .45;

      double pad_l = 0, pad_r = 0;
      for (std::size_t j = 0; j < 3; ++j) {
        advance(pad_phase_[j], pad_increment_[j]);
        const auto voice = std::sin(tau * pad_phase_[j]) * .22;
        pad_l += voice * (j == 0 ? .9 : .6);
        pad_r += voice * (j == 2 ? .9 : .6);
      }
      const auto drums = percussion ? kick * smooth_levels_[0] + clap * smooth_levels_[1] + hat * smooth_levels_[2] : 0;
      const auto tonal_l = bass * smooth_levels_[3] + lead_l * smooth_levels_[4] + pad_l * smooth_levels_[5];
      const auto tonal_r = bass * smooth_levels_[3] + lead_r * smooth_levels_[4] + pad_r * smooth_levels_[5];
      left[i] = float(std::tanh((drums + tonal_l * duck) * 1.1) * .85);
      right[i] = float(std::tanh((drums + tonal_r * duck) * 1.1) * .85);
      kick_age_ = std::min(kick_age_ + 1. / sample_rate, 2.);
      clap_age_ = std::min(clap_age_ + 1. / sample_rate, 2.);
      hat_age_ = std::min(hat_age_ + 1. / sample_rate, 2.);
      step_phase_ += step_increment;
    }
    beat_.store((double(step_) + step_phase_) / 4, std::memory_order_relaxed);
    return true;
  }

  void style(const std::string& text) override {
    if (text != "house" && text != "trance") throw std::runtime_error("Synth styles are house and trance");
    trance_.store(text == "trance");
  }
  void drums(bool enabled) override { drums_.store(enabled); }
  void tempo(float bpm) override { bpm_.store(bpm); }
  void temperature(float) override { throw std::runtime_error("Temperature applies to AI; this is a live synth"); }
  void mix(const std::string& layer, float value) override {
    constexpr const char* names[] = {"kick", "clap", "hats", "bass", "lead", "pad"};
    for (std::size_t i = 0; i < levels_.size(); ++i)
      if (layer == names[i]) { levels_[i].store(value); return; }
    throw std::runtime_error("Mix layers: kick, clap, hats, bass, lead, pad");
  }
  const char* name() const override { return "house/trance synth (live procedural generation, not AI)"; }
  bool is_synth() const override { return true; }
  double beat() const noexcept override { return beat_.load(std::memory_order_relaxed); }
  void synth_control(const Control& control) noexcept override {
    const auto parameter = control.parameter;
    if (parameter >= Parameter::kick && parameter <= Parameter::pad) {
      levels_[unsigned(parameter) - unsigned(Parameter::kick)].store(control.value);
      return;
    }
    switch (parameter) {
      case Parameter::tempo: bpm_.store(control.value); break;
      case Parameter::style: trance_.store(control.value != 0); break;
      case Parameter::drums: drums_.store(control.value != 0); break;
      case Parameter::root: root_.store(int(control.value)); break;
      case Parameter::melody:
        melody_.store(control.pattern); custom_melody_.store(true); break;
      case Parameter::bassline:
        bassline_.store(control.pattern); custom_bass_.store(true); break;
      default: break;
    }
  }
  float synth_value(Parameter parameter) const noexcept override {
    if (parameter >= Parameter::kick && parameter <= Parameter::pad)
      return levels_[unsigned(parameter) - unsigned(Parameter::kick)].load();
    switch (parameter) {
      case Parameter::tempo: return bpm_.load();
      case Parameter::style: return trance_.load();
      case Parameter::drums: return drums_.load();
      case Parameter::root: return float(root_.load());
      default: return 0;
    }
  }

 private:
  void trigger(bool trance) {
    if (step_ % 4 == 0) { kick_age_ = 0; kick_phase_ = 0; }
    const bool fill = step_ % 256 >= 252;
    if (step_ % 8 == 4 || (fill && step_ % 2 == 1)) clap_age_ = 0;
    if (step_ % 2 == 0 || fill) { hat_age_ = 0; hat_open_ = step_ % 4 == 2; }

    constexpr int roots[] = {45, 41, 48, 43};  // Am, F, C, G; four bars each.
    const auto chord = (step_ / 64) % 4;
    const auto root = roots[chord] + root_.load(std::memory_order_relaxed) - 45;
    const int third = chord == 0 ? 3 : 4;
    const int notes[] = {root + 12, root + 12 + third, root + 19};
    for (std::size_t j = 0; j < 3; ++j) pad_increment_[j] = frequency(notes[j]) / sample_rate;

    constexpr bool house_bass[] = {false, false, true, true, false, true, true, false,
                                  false, false, true, true, false, true, true, false};
    bass_on_ = trance ? step_ % 4 != 0 : house_bass[step_ % 16];
    if (custom_bass_.load(std::memory_order_relaxed))
      bass_on_ = (bassline_.load(std::memory_order_relaxed) >> (step_ % 16)) & 1;
    const int bass_note = root - 12 + (step_ % 32 == 31 ? 7 : 0);
    bass_increment_ = frequency(bass_note) / sample_rate;

    const int arp[] = {0, 7, 12, third, 19, 12, 7, 12 + third};
    const auto position = (step_ + (step_ / 128) * 3) % 8;
    lead_increment_ = frequency(root + 24 + arp[position]) / sample_rate;
    lead_on_ = trance || step_ % 2 == 0;
    if (custom_melody_.load(std::memory_order_relaxed)) {
      const auto degree = (melody_.load(std::memory_order_relaxed) >> (4 * (step_ % 16))) & 15;
      lead_on_ = degree != 15;
      const int triad[] = {0, third, 7};
      if (lead_on_) lead_increment_ = frequency(root + 12 + triad[degree % 3] + 12 * int(degree / 3)) / sample_rate;
    }
  }
  double random() {
    noise_ ^= noise_ << 13; noise_ ^= noise_ >> 17; noise_ ^= noise_ << 5;
    return double(noise_) / UINT32_MAX * 2 - 1;
  }
  std::atomic<float> bpm_{128};
  std::atomic<double> beat_{0};
  std::atomic<int> root_{45};
  std::atomic<bool> trance_{false}, drums_{true};
  std::atomic<bool> custom_melody_{false}, custom_bass_{false};
  std::atomic<std::uint64_t> melody_{0}, bassline_{0};
  std::array<std::atomic<float>, 6> levels_;
  std::array<float, 6> smooth_levels_{};
  std::array<double, 3> lead_phase_{}, pad_phase_{}, pad_increment_{};
  std::uint64_t step_ = 0;
  std::uint32_t noise_ = 0x92189281;
  double step_phase_ = 0, kick_age_ = 2, clap_age_ = 2, hat_age_ = 2;
  double kick_phase_ = 0, bass_phase_ = 0, bass_increment_ = 0, lead_increment_ = 0;
  double bass_env_ = 0, lead_env_ = 0, noise_previous_ = 0;
  bool first_ = true, bass_on_ = false, lead_on_ = false, hat_open_ = false;
};
}  // namespace
std::unique_ptr<Source> make_synth_source() { return std::make_unique<SynthSource>(); }
}  // namespace music
