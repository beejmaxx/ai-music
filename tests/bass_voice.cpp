#include "music/bass_voice.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
  bool rejected = false;
  try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
  check(rejected, "Invalid instrument settings accepted");
}
double energy(music::BassVoice& voice, unsigned frames) {
  double result = 0;
  for (unsigned i = 0; i < frames; ++i) {
    const auto value = voice.render();
    if (!std::isfinite(value) || std::abs(value) >= .99)
      throw std::runtime_error("Bass became invalid or clipped: sample=" + std::to_string(i) + " value=" + std::to_string(value));
    result += value * value;
  }
  return result / frames;
}
void tuning_and_release() {
  for (const auto rate : {44100., 48000., 96000.}) {
    music::BassVoice voice(rate);
    music::BassPatch patch;
    patch.oscillator = music::BassPatch::Oscillator::sine;
    patch.sub = 0; patch.cutoff = 3000; patch.filter_amount = 0;
    patch.drive_db = 0; patch.sustain = 1; patch.release_ms = 80;
    voice.patch(patch);
    check(energy(voice, unsigned(rate / 10)) == 0, "Idle voice must be silent");
    check(voice.note_on(45, .8), "Valid note rejected");  // A2 = 110 Hz.
    check(energy(voice, unsigned(rate / 5)) > .001, "Missing note attack");
    unsigned crossings = 0;
    float previous = voice.render();
    for (unsigned i = 0; i < unsigned(rate); ++i) {
      const auto value = voice.render();
      crossings += previous <= 0 && value > 0;
      previous = value;
    }
    check(std::abs(double(crossings) - 110) <= 1, "Oscillator pitch drifted");
    voice.note_off();
    energy(voice, unsigned(rate * .3));
    check(energy(voice, unsigned(rate * .1)) < 1e-12, "Release left a stuck note or DC tail");
  }
}
void articulation() {
  music::BassVoice voice;
  voice.note_on(41);
  energy(voice, 24000);
  const auto before = voice.render();
  voice.note_on(48);
  check(std::abs(voice.render() - before) < .03, "Retrigger introduced an instantaneous click");
  energy(voice, 24000);
  voice.note_off();
  energy(voice, 24000);
  check(energy(voice, 4800) < 1e-12, "Retrigger release did not settle");

  music::BassPatch patch;
  patch.oscillator = music::BassPatch::Oscillator::sine;
  patch.sub = 0; patch.drive_db = 0; patch.cutoff = 3000;
  patch.filter_amount = 0; patch.sustain = 1; patch.glide_ms = 120;
  voice.patch(patch); voice.reset(); voice.note_on(45);
  energy(voice, 24000);
  voice.note_on(57, 1, false);  // Legato octave up, without retriggering amplitude.
  energy(voice, 24000);
  unsigned crossings = 0;
  float previous = voice.render();
  for (unsigned i = 0; i < 24000; ++i) {
    const auto sample = voice.render();
    crossings += previous <= 0 && sample > 0;
    previous = sample;
  }
  check(std::abs(int(crossings) - 110) <= 1, "Glide failed to reach its target note");

  auto velocity_energy = [&](double velocity) {
    voice.reset(); voice.note_on(45, velocity);
    energy(voice, 24000);
    return energy(voice, 24000);
  };
  const auto soft = velocity_energy(.5), loud = velocity_energy(1);
  check(std::abs(loud / soft - 4) < .01, "Velocity does not control note amplitude predictably");
  voice.note_on(45, 0);
  energy(voice, 24000);
  check(energy(voice, 4800) < 1e-12, "Zero velocity did not release the note");
  voice.note_on(45); energy(voice, 2400); voice.note_off();
  energy(voice, 1000); voice.note_on(45, .2, false);
  check(energy(voice, 24000) > .0001, "New legato note during release failed to reopen the envelope");
}
void extremes_and_validation() {
  for (const auto waveform : {music::BassPatch::Oscillator::saw, music::BassPatch::Oscillator::pulse,
                             music::BassPatch::Oscillator::sine}) {
    for (const auto note : {24u, 41u, 72u, 96u}) {
      music::BassVoice voice;
      music::BassPatch patch;
      patch.oscillator = waveform;
      patch.pulse_width = note % 2 ? .1 : .9;
      patch.cutoff = 10000; patch.resonance = 6; patch.drive_db = 24;
      patch.filter_amount = 6; patch.level = 1; patch.sub = 1; patch.attack_ms = .5;
      voice.patch(patch); voice.note_on(note);
      check(energy(voice, 12000) > 1e-7, "Extreme patch produced no sound");
      voice.note_off(); energy(voice, 24000);
      check(energy(voice, 4800) < 1e-12, "Extreme patch did not release");
    }
  }
  for (const auto value : {-1., std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
    music::BassPatch patch;
    patch.cutoff = value;
    rejects([&] { patch.validate(); });
  }
  rejects([] { music::BassVoice bad(0); });
  rejects([] { music::BassPatch patch; patch.release_ms = 0; patch.validate(); });
  rejects([] { music::BassPatch patch; patch.resonance = 7; patch.validate(); });
  music::BassVoice voice;
  check(!voice.note_on(23) && !voice.note_on(97) && !voice.note_on(45, 2) &&
    !voice.note_on(45, std::numeric_limits<double>::quiet_NaN()), "Invalid note accepted");
  check(energy(voice, 1000) == 0, "Rejected notes changed idle state");
}
}  // namespace

int main() {
  try {
    tuning_and_release(); std::cout << "Tuning and release passed.\n";
    articulation(); std::cout << "Articulation passed.\n";
    extremes_and_validation();
    std::cout << "Bass tuning, envelopes, glide, velocity and stability passed.\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
