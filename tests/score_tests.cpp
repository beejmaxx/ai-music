#include "music/commands.hpp"
#include "music/radio.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
  bool rejected = false;
  try { fn(); } catch (const std::exception&) { rejected = true; }
  check(rejected, "Malformed score was accepted");
}
struct Target : music::ScoreTarget {
  std::array<float, unsigned(music::Parameter::none)> values{};
  std::uint64_t melody = 0;
  void write_control(const music::Control& control) noexcept override {
    values[unsigned(control.parameter)] = control.value;
    if (control.parameter == music::Parameter::melody) melody = control.pattern;
  }
  float read_control(music::Parameter parameter) const noexcept override { return values[unsigned(parameter)]; }
};
music::Score score(const std::string& text) {
  auto parsed = music::parse_commands(text);
  music::validate_controls(parsed, false, true, true);
  return music::compile_score(parsed);
}
void scheduling() {
  Target target;
  music::ScorePlayer player;
  target.values[unsigned(music::Parameter::filter)] = 100;
  check(player.submit(score("at 0 mix kick .8\nramp 0 4 volume 1\nramp 0 4 filter 10000\nat 4 mix kick 0")), "Submit score");
  player.tick(5, target);  // Next bar starts at beat 8.
  check(player.status().start_beat == 8 && target.read_control(music::Parameter::kick) == 0, "Cue waits for bar boundary");
  player.tick(8, target);
  check(target.read_control(music::Parameter::kick) == .8f, "Cue starts on downbeat");
  player.tick(16, target);
  check(std::abs(target.read_control(music::Parameter::volume) - .5f) < 1e-6, "Ramp halfway by beat, independent of wall time");
  check(std::abs(target.read_control(music::Parameter::filter) - 1000) < .01, "Cutoff follows logarithmic curve");
  player.tick(24, target);
  check(target.read_control(music::Parameter::volume) == 1 && target.read_control(music::Parameter::kick) == 0, "Exact final values and ordered endpoint cue");
  check(player.status().remaining == 0, "Score ends, values remain held");

  player.submit(score("ramp 0 4 volume 0\nat 2 mix kick .9"));
  player.tick(24, target);
  player.tick(28, target);
  check(target.read_control(music::Parameter::volume) == .75f, "Replacement fades from current level");
  player.submit(score("volume .4"));
  player.tick(29, target);
  player.tick(40, target);
  check(target.read_control(music::Parameter::volume) == .4f, "Manual override cancels its ramp");
  check(target.read_control(music::Parameter::kick) == .9f, "Other controls keep their scheduled events");

  player.submit(score("quantize 8\nat 0 mix kick .1\nat 8 volume 0"));
  player.tick(41, target);
  check(player.status().start_beat == 64, "Eight-bar quantization");
  player.submit(score("cancel"));
  player.tick(42, target);
  player.tick(128, target);
  check(target.read_control(music::Parameter::kick) == .9f && target.read_control(music::Parameter::volume) == .4f, "Cancel preserves current sound, removes future changes");

  // Out-of-order input, overlapping ramps, and delayed callbacks remain ordered.
  player.submit(score("at 4 mix lead .7\nramp 2 2 volume 1\nramp 0 4 volume 0"));
  player.tick(128, target);
  player.tick(140, target);
  check(std::abs(target.read_control(music::Parameter::volume) - .6f) < 1e-6, "New ramp captures preceding ramp at its own start");
  player.tick(144, target);
  check(target.read_control(music::Parameter::lead) == .7f, "Input cues are sorted by musical time");
}
void validation() {
  for (const char* input : {"at -1 volume .5", "at nan volume .5", "at 0 quit", "at 0 status", "at 0 cancel",
      "ramp 0 0 volume .5", "ramp 0 2 style trance", "quantize 0", "quantize 1.5", "quantize 4",
      "melody 0 1", "bassline 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 2", "root 44.5", "cancel\nvolume .5"})
    rejects([&] { score(input); });
  rejects([] { music::validate_controls(music::parse_commands("at 0 volume .4"), true); });
  std::string oversized;
  for (int i = 0; i < 257; ++i) oversized += "at 0 mix kick .5\n";
  rejects([&] { score(oversized); });
  const auto silent = score("melody - - - - - - - - - - - - - - - -");
  check(silent.events[0].control.pattern == UINT64_MAX, "Every melody rest survives packing");
  music::RadioDirector radio(17);
  auto first = radio.next();
  bool varied = false;
  for (int i = 0; i < 100; ++i) {
    const auto next = radio.next();
    check(next.count > 20 && next.count <= 256 && next.quantum == 8, "Radio produces bounded, valid programs");
    for (std::size_t j = 0; j < next.count; ++j)
      if (next.events[j].control.parameter == music::Parameter::melody)
        for (std::size_t k = 0; k < first.count; ++k)
          if (first.events[k].control.parameter == music::Parameter::melody)
            varied |= first.events[k].control.pattern != next.events[j].control.pattern;
  }
  check(varied, "Station composes new motifs");
}
void audio_clock() {
  auto source = music::make_synth_source();
  music::Engine engine(*source);
  source->tempo(240);
  const auto plan = score("at 0 melody - - - - - - - - - - - - - - - -\nat 1 tempo 120\nat 2 mix bass 0");
  engine.score(plan);
  std::array<float, 960> audio{};
  for (int i = 0; i < 300; ++i) {
    engine.render(audio.data(), audio.size() / 2);
    for (auto value : audio) check(std::isfinite(value) && std::abs(value) <= 1, "Automated audio is finite and bounded");
  }
  check(std::abs(source->beat() - 8) < .02, "Tempo cue changes elapsed beats without resetting transport");
  check(engine.frames() == 144000 && engine.underruns() == 0 && engine.invalid_samples() == 0, "Continuous audio across automation");
  engine.render(audio.data(), audio.size() / 2);
  check(source->synth_value(music::Parameter::bass) == 0, "Later cue follows changed tempo");
}
}  // namespace
int main() {
  try { scheduling(); validation(); audio_clock(); std::cout << "Passed: score timing, ramps, replacement, cancellation, radio variations, audio transport.\n"; }
  catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
