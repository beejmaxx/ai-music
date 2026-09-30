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
struct ResonantFilter {
  double first = 0, second = 0;
  double process(double input, double cutoff, double damping, bool bandpass = false) noexcept {
    const auto g = std::tan(std::numbers::pi * cutoff / sample_rate);
    const auto v1 = (first + g * (input - second)) / (1 + g * (g + damping));
    const auto v2 = second + g * v1;
    first = 2 * v1 - first; second = 2 * v2 - second;
    return bandpass ? v1 : v2;
  }
};
unsigned midi_step(std::uint64_t low, std::uint64_t high, unsigned position) noexcept {
  position %= 16;
  return ((position < 8 ? low : high) >> (8 * (position % 8))) & 255;
}

class SynthSource final : public Source {
 public:
  SynthSource() {
    const float defaults[] = {.85f, .35f, .25f, .55f, .32f, .3f};
    for (std::size_t i = 0; i < levels_.size(); ++i) levels_[i] = smooth_levels_[i] = defaults[i];
  }

  bool read(float* left, float* right, std::size_t count) noexcept override {
    return render(left, right, nullptr, nullptr, count);
  }
  bool read_buses(float* left, float* right, float* dry_left, float* dry_right,
                  std::size_t count) noexcept override {
    return render(left, right, dry_left, dry_right, count);
  }
  bool render(float* left, float* right, float* dry_left, float* dry_right,
              std::size_t count) noexcept {
    const auto step_increment = bpm_.load(std::memory_order_relaxed) * 4 / (60.0 * sample_rate);
    const bool trance = trance_.load(std::memory_order_relaxed);
    const bool percussion = drums_.load(std::memory_order_relaxed);
    const auto voice_type = custom_voice_.load(std::memory_order_relaxed) ? voice_.load() : trance ? 1 : 0;
    const bool sustain_chords = chord_steps_.load(std::memory_order_relaxed) >> 32;
    const bool keys = keys_.load(std::memory_order_relaxed);
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
      for (unsigned j = 0; j < voice_weights_.size(); ++j)
        voice_weights_[j] += ((voice_type == int(j) ? 1. : 0.) - voice_weights_[j]) * .002;

      // Every sound is synthesized here, on the current musical clock.
      const auto noise = random();
      const auto high_noise = noise - noise_previous_;
      noise_previous_ = noise;
      const auto kick_hz = 49 + 150 * std::exp(-kick_age_ * 60);
      advance(kick_phase_, kick_hz / sample_rate);
      const auto kick_body = std::tanh(1.5 * std::sin(tau * kick_phase_)) / 1.25;
      const auto kick = (kick_body * std::exp(-kick_age_ * 18)
        + high_noise * .055 * std::exp(-kick_age_ * 650))
        * (1 - std::exp(-kick_age_ * 2500));
      double clap_env = 0;
      for (double offset : {0., .012, .024}) {
        const auto age = clap_age_ - offset;
        if (age >= 0) clap_env += std::exp(-age * 95);
      }
      const auto clap = high_noise * clap_env * .23;
      const auto hat = high_noise * std::exp(-hat_age_ * (hat_open_ ? 26 : 145)) * .24 * hat_gain_;

      // Duck the tonal parts on each kick; bass and arp retain continuous phase.
      const auto duck_depth = percussion ? std::clamp(smooth_levels_[0] / .85f, 0.f, 1.f) : 0.f;
      const auto duck = 1 - .73 * duck_depth * std::exp(-kick_age_ * step_increment * sample_rate * 1.75);
      const auto bass_target = bass_on_ && (bass_held_ || step_phase_ < .58) ? 1. : 0.;
      bass_env_ += (bass_target - bass_env_) * (bass_target ? .012 : .0015);
      advance(bass_phase_, bass_increment_);
      const auto bass_input = .55 * std::sin(tau * bass_phase_) - .45 * saw(bass_phase_, bass_increment_);
      // Stable two-pole state-variable filter, opened by each bass note.
      const auto cutoff = 260 + 1900 * std::exp(-bass_age_ * 18);
      const auto v2 = bass_filter_.process(bass_input, cutoff, 1.1);
      const auto bass = std::tanh(v2 * 1.65) * bass_env_ * .62;

      const auto lead_target = lead_on_ && (lead_held_ || step_phase_ < (trance ? .5 : .35)) ? 1. : 0.;
      lead_env_ += (lead_target - lead_env_) * (lead_target ? .007 : .0011);
      lead_increment_ += (lead_pitch_ - lead_increment_) * (voice_type == 3 ? .0012 : 1.);
      double lead_l = 0, lead_r = 0;
      constexpr double detunes[] = {.996, 1., 1.004};
      for (std::size_t j = 0; j < 3; ++j) {
        const auto inc = lead_increment_ * detunes[j];
        advance(lead_phase_[j], inc);
        const auto sine = std::sin(tau * lead_phase_[j]);
        const auto voice = voice_weights_[0] * (sine + .3 * std::sin(2 * tau * lead_phase_[j]))
          + voice_weights_[1] * saw(lead_phase_[j], inc) + voice_weights_[2] * sine * .8;
        lead_l += voice * (j == 0 ? .5 : .25);
        lead_r += voice * (j == 2 ? .5 : .25);
      }
      // A fixed fourth, band-pass envelope, and overdrive form the gritty voice.
      const auto fourth_increment = lead_increment_ * std::exp2(5. / 12);
      advance(grit_fourth_phase_, fourth_increment);
      const auto pulse = [](double phase, double increment) {
        const auto shifted = phase < .5 ? phase + .5 : phase - .5;
        return .5 * (saw(phase, increment) - saw(shifted, increment));
      };
      const auto raw_grit = .6 * pulse(lead_phase_[1], lead_increment_)
                         + .4 * pulse(grit_fourth_phase_, fourth_increment);
      const auto wah = (1 - std::exp(-lead_age_ / .09)) * std::exp(-lead_age_ / 4);
      const auto resonant = grit_filter_.process(raw_grit, 650 + 1500 * wah, .65, true);
      const auto grit = std::tanh(resonant * 2.8) * .8 * voice_weights_[3];
      lead_l += grit; lead_r += grit;
      lead_l *= lead_env_ * .45; lead_r *= lead_env_ * .45;

      double pad_l = 0, pad_r = 0;
      keys_blend_ += ((keys ? 1. : 0.) - keys_blend_) * .002;
      const auto chord_target = sustain_chords ? 1. : chord_velocity_ * std::exp(-chord_age_ * 13)
        * (1 - std::exp(-chord_age_ * 1100));
      chord_env_ += (chord_target - chord_env_) * .008;
      for (std::size_t j = 0; j < 4; ++j) {
        advance(pad_phase_[j], pad_increment_[j]);
        const auto angle = tau * pad_phase_[j];
        const auto sine = std::sin(angle);
        const auto tine = std::sin(angle + (.25 + .9 * chord_env_) * std::sin(2 * angle));
        const auto voice = ((1 - keys_blend_) * sine + keys_blend_ * tine) * .22 * chord_env_
          * (j == 3 ? keys_blend_ * .75 : 1);
        pad_l += voice * (j == 0 ? .9 : .6);
        pad_r += voice * (j == 2 ? .9 : .6);
      }
      const auto drums = percussion ? kick * smooth_levels_[0] + clap * smooth_levels_[1] + hat * smooth_levels_[2] : 0;
      const auto dry = drums + bass * smooth_levels_[3] * duck;
      const auto tonal_l = (lead_l * smooth_levels_[4] + pad_l * smooth_levels_[5]) * duck;
      const auto tonal_r = (lead_r * smooth_levels_[4] + pad_r * smooth_levels_[5]) * duck;
      if (dry_left) {
        dry_left[i] = dry_right[i] = float(dry);
        left[i] = float(tonal_l); right[i] = float(tonal_r);
      } else {
        left[i] = float(std::tanh((dry + tonal_l) * 1.1) * .85);
        right[i] = float(std::tanh((dry + tonal_r) * 1.1) * .85);
      }
      kick_age_ = std::min(kick_age_ + 1. / sample_rate, 2.);
      clap_age_ = std::min(clap_age_ + 1. / sample_rate, 2.);
      hat_age_ = std::min(hat_age_ + 1. / sample_rate, 2.);
      chord_age_ = std::min(chord_age_ + 1. / sample_rate, 2.);
      bass_age_ = std::min(bass_age_ + 1. / sample_rate, 2.);
      lead_age_ = std::min(lead_age_ + 1. / sample_rate, 2.);
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
  std::size_t frames_to_tick() const noexcept override {
    const auto distance = (step_phase_ >= 1 ? 2. : 1.) - step_phase_;
    const auto increment = bpm_.load(std::memory_order_relaxed) * 4 / (60. * sample_rate);
    return std::max<std::size_t>(1, std::size_t(std::ceil(distance / increment)));
  }
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
        melody_.store(control.pattern); custom_melody_.store(true); midi_lead_ = false; break;
      case Parameter::lead_midi:
        lead_midi_low_ = control.pattern; lead_midi_high_ = control.pattern_high;
        custom_melody_.store(true); midi_lead_ = true; break;
      case Parameter::bass_midi:
        bass_midi_low_ = control.pattern; bass_midi_high_ = control.pattern_high;
        midi_bass_ = true; break;
      case Parameter::bassline:
        bassline_.store(control.pattern); custom_bass_.store(true); custom_bass_notes_.store(false); midi_bass_ = false; break;
      case Parameter::bassnotes:
        bass_notes_.store(control.pattern); custom_bass_notes_.store(true); midi_bass_ = false; break;
      case Parameter::harmony: harmony_.store(control.pattern); break;
      case Parameter::voice: voice_.store(int(control.value)); custom_voice_.store(true); break;
      case Parameter::rhythm: rhythm_.store(int(control.value)); break;
      case Parameter::chords: chord_steps_.store(control.pattern); break;
      case Parameter::chord_voice: keys_.store(control.value != 0); break;
      case Parameter::chord_bars: chord_bars_.store(unsigned(control.value)); break;
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
    const auto chord_level = (chord_steps_.load(std::memory_order_relaxed) >> (2 * (step_ % 16))) & 3;
    if (chord_level) { chord_age_ = 0; chord_velocity_ = chord_level == 1 ? .55 : chord_level == 2 ? .8 : 1; }
    const bool fill = step_ % 256 >= 252;
    const auto rhythm = rhythm_.load(std::memory_order_relaxed);
    const auto kick_step = step_ % 16;
    const bool kick = rhythm == 3 ? kick_step == 0 || kick_step == 6 || kick_step == 8 || kick_step == 11
                                  : step_ % 4 == 0;
    if (kick) { kick_age_ = 0; kick_phase_ = 0; }
    if (step_ % 8 == 4 || (fill && step_ % 2 == 1)) clap_age_ = 0;
    if (rhythm == 2 && step_ % (step_ % 128 < 64 ? 4 : step_ % 128 < 96 ? 2 : 1) == 0) clap_age_ = 0;
    if (step_ % 2 == 0 || fill || rhythm == 1) {
      hat_age_ = 0; hat_open_ = step_ % 4 == 2; hat_gain_ = step_ % 2 ? .4 : 1;
    }

    constexpr int roots[] = {45, 47, 48, 50, 52, 41, 43};
    const auto chord = (harmony_.load(std::memory_order_relaxed) >> (4 * ((step_ / (16 * chord_bars_.load())) % 8))) & 15;
    const auto root = roots[chord] + root_.load(std::memory_order_relaxed) - 45;
    const int third = chord == 0 || chord == 1 || chord == 3 || chord == 4 ? 3 : 4;
    const int fifth = chord == 1 ? 6 : 7;
    const int seventh = chord == 2 || chord == 5 ? 11 : 10;
    const int notes[] = {root + 12, root + 12 + third, root + 12 + fifth, root + 12 + seventh};
    for (std::size_t j = 0; j < 4; ++j) pad_increment_[j] = frequency(notes[j]) / sample_rate;

    constexpr bool house_bass[] = {false, false, true, true, false, true, true, false,
                                  false, false, true, true, false, true, true, false};
    if (midi_bass_) {
      const auto note = midi_step(bass_midi_low_, bass_midi_high_, unsigned(step_ % 16));
      if (note == 254) bass_held_ = bass_on_;
      else {
        bass_on_ = note != 255;
        bass_held_ = bass_on_ && midi_step(bass_midi_low_, bass_midi_high_, unsigned((step_ + 1) % 16)) == 254;
        if (bass_on_) { bass_increment_ = frequency(int(note)) / sample_rate; bass_age_ = 0; }
      }
    } else {
      bass_held_ = false;
      bass_on_ = trance ? step_ % 4 != 0 : house_bass[step_ % 16];
      if (custom_bass_.load(std::memory_order_relaxed))
        bass_on_ = (bassline_.load(std::memory_order_relaxed) >> (step_ % 16)) & 1;
      int bass_note = root - 12 + (step_ % 32 == 31 ? fifth : 0);
      if (custom_bass_notes_.load(std::memory_order_relaxed)) {
        const auto degree = (bass_notes_.load() >> (4 * (step_ % 16))) & 15;
        bass_on_ = degree != 15;
        bass_note = root - 12 + (degree == 2 ? 12 : degree == 1 ? fifth : degree == 3 ? third : degree == 4 ? seventh : 0);
      }
      bass_increment_ = frequency(bass_note) / sample_rate;
      if (bass_on_) bass_age_ = 0;
    }

    const int arp[] = {0, 7, 12, third, 19, 12, 7, 12 + third};
    const auto position = (step_ + (step_ / 128) * 3) % 8;
    if (custom_melody_.load(std::memory_order_relaxed)) {
      const auto pattern = melody_.load(std::memory_order_relaxed);
      const auto degree = midi_lead_ ? midi_step(lead_midi_low_, lead_midi_high_, unsigned(step_ % 16))
                                     : (pattern >> (4 * (step_ % 16))) & 15;
      if (degree == (midi_lead_ ? 254 : 14)) {
        lead_held_ = lead_on_;  // A tie never starts or retunes a note.
      } else {
        lead_on_ = degree != (midi_lead_ ? 255 : 15);
        const auto next = midi_lead_ ? midi_step(lead_midi_low_, lead_midi_high_, unsigned((step_ + 1) % 16))
                                     : (pattern >> (4 * ((step_ + 1) % 16))) & 15;
        lead_held_ = lead_on_ && next == (midi_lead_ ? 254 : 14);
        const int triad[] = {0, third, fifth};
        if (lead_on_) {
          const auto note = midi_lead_ ? int(degree) : root + 12 + triad[degree % 3] + 12 * int(degree / 3);
          lead_pitch_ = frequency(note) / sample_rate; lead_age_ = 0;
          if (lead_increment_ == 0) lead_increment_ = lead_pitch_;
        }
      }
    } else {
      lead_pitch_ = frequency(root + 24 + arp[position]) / sample_rate;
      lead_on_ = trance || step_ % 2 == 0;
      lead_held_ = false;
      if (lead_on_) lead_age_ = 0;
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
  std::atomic<bool> custom_bass_notes_{false}, custom_voice_{false};
  std::atomic<std::uint64_t> melody_{0}, bassline_{0};
  std::atomic<std::uint64_t> harmony_{0x62506250}, bass_notes_{0};
  std::atomic<int> voice_{1}, rhythm_{0};
  std::atomic<std::uint64_t> chord_steps_{UINT64_C(1) << 32};
  std::atomic<bool> keys_{false};
  std::atomic<unsigned> chord_bars_{4};
  std::array<std::atomic<float>, 6> levels_;
  std::array<float, 6> smooth_levels_{};
  std::array<double, 3> lead_phase_{};
  std::array<double, 4> pad_phase_{}, pad_increment_{};
  std::array<double, 4> voice_weights_{1, 0, 0, 0};
  std::uint64_t step_ = 0;
  std::uint32_t noise_ = 0x92189281;
  double step_phase_ = 0, kick_age_ = 2, clap_age_ = 2, hat_age_ = 2;
  double kick_phase_ = 0, bass_phase_ = 0, bass_increment_ = 0, lead_increment_ = 0;
  double bass_env_ = 0, lead_env_ = 0, noise_previous_ = 0;
  double bass_age_ = 2, lead_age_ = 2, lead_pitch_ = 0, grit_fourth_phase_ = 0;
  ResonantFilter bass_filter_, grit_filter_;
  bool midi_lead_ = false, midi_bass_ = false, bass_held_ = false;
  std::uint64_t lead_midi_low_ = 0, lead_midi_high_ = 0, bass_midi_low_ = 0, bass_midi_high_ = 0;
  double hat_gain_ = 1;
  double chord_age_ = 2, chord_velocity_ = 1, chord_env_ = 1, keys_blend_ = 0;
  bool first_ = true, bass_on_ = false, lead_on_ = false, lead_held_ = false, hat_open_ = false;
};
}  // namespace
std::unique_ptr<Source> make_synth_source() { return std::make_unique<SynthSource>(); }
}  // namespace music
