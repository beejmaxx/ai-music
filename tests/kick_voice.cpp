#include "music/kick_voice.hpp"
#include <iostream>
#include <limits>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
double energy(music::KickVoice& voice, unsigned frames) {
  double sum = 0;
  for (unsigned i = 0; i < frames; ++i) {
    const auto sample = voice.render();
    check(std::isfinite(sample) && std::abs(sample) < .99, "Kick became invalid or clipped");
    sum += sample * sample;
  }
  return sum / frames;
}
void tuning_and_tail() {
  for (const auto rate : {44100., 48000., 96000.}) {
    music::KickVoice voice(rate);
    music::KickPatch patch;
    patch.sweep = 0; patch.click = 0; patch.drive_db = 0; patch.decay_ms = 2000;
    voice.patch(patch);
    check(energy(voice, 1000) == 0, "Idle kick is not silent");
    voice.trigger(); energy(voice, unsigned(rate * .15));
    float previous = voice.render();
    unsigned crossings = 0;
    for (unsigned i = 0; i < unsigned(rate); ++i) {
      const auto sample = voice.render();
      crossings += previous <= 0 && sample > 0;
      previous = sample;
    }
    check(std::abs(int(crossings) - 52) <= 1, "Kick body is not tuned to 52 Hz");
    energy(voice, unsigned(rate * 5));
    check(energy(voice, 1000) < 1e-12, "Kick tail left DC or stuck audio");
  }
}
void controls_and_retrigger() {
  const auto late_energy = [](double decay) {
    music::KickVoice voice;
    music::KickPatch patch; patch.decay_ms = decay;
    voice.patch(patch); voice.trigger(); energy(voice, 12000);
    return energy(voice, 4800);
  };
  check(late_energy(900) > late_energy(180) * 100, "Decay does not extend the audible body");
  const auto velocity_energy = [](double velocity) {
    music::KickVoice voice; voice.trigger(velocity);
    return energy(voice, 24000);
  };
  check(std::abs(velocity_energy(1) / velocity_energy(.5) - 4) < .001, "Velocity is not linear in amplitude");
  music::KickVoice voice;
  for (unsigned i = 0; i < 200; ++i) {
    voice.trigger(i % 2 ? .3 : 1.);
    energy(voice, 480);  // Rapid 100 Hz retrigger stress, including velocity changes.
  }
  energy(voice, 96000);
  check(energy(voice, 4800) < 1e-12, "Rapid retriggers left a stuck tail");
  voice.reset(); voice.trigger();
  std::array<float, 1000> reference;
  for (auto& sample : reference) sample = voice.render();
  voice.reset(); voice.trigger();
  for (const auto sample : reference) check(sample == voice.render(), "Reset must reproduce the same strike");
  music::KickPatch patch; patch.click = 0;
  voice.patch(patch); voice.reset(); voice.trigger();
  for (auto& sample : reference) sample = voice.render();
  energy(voice, 42200);  // Next strike at 0.9 s, when the preceding body is inaudible.
  voice.trigger();
  for (const auto sample : reference)
    check(std::abs(sample - voice.render()) < 1e-5, "Spaced strikes should retain a consistent body attack");
}
void extremes_and_validation() {
  for (const auto rate : {8000., 48000., 192000.}) {
    music::KickVoice voice(rate);
    music::KickPatch patch;
    patch.pitch = 100; patch.sweep = 36; patch.pitch_decay_ms = 300;
    patch.attack_ms = .2; patch.decay_ms = 50; patch.click = 1;
    patch.click_decay_ms = 50; patch.drive_db = 18; patch.level = 1;
    voice.patch(patch); voice.trigger();
    check(energy(voice, unsigned(rate * .5)) > .0001, "Extreme kick did not sound");
    energy(voice, unsigned(rate));
    check(energy(voice, 1000) < 1e-12, "Extreme kick did not settle");
  }
  for (const auto invalid : {-1., std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
    music::KickPatch patch; patch.pitch = invalid;
    bool rejected = false;
    try { patch.validate(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Invalid kick pitch accepted");
  }
  music::KickVoice voice;
  check(!voice.trigger(-1) && !voice.trigger(2) && !voice.trigger(std::numeric_limits<double>::quiet_NaN()),
        "Invalid kick velocity accepted");
  voice.trigger(0);
  check(energy(voice, 1000) == 0, "Invalid or zero velocity strike activated the kick");
}
}
int main() {
  try {
    tuning_and_tail(); controls_and_retrigger(); extremes_and_validation();
    std::cout << "Kick tuning, decay, velocity, retrigger and boundary checks passed.\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
