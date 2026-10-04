// A small composed study of the two standalone instruments. Edit the notes and
// patches here; render the mix and each part separately for sound development.
#include "music/bass_voice.hpp"
#include "music/kick_voice.hpp"
#include "music/recorder.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {
constexpr double bpm = 116;
constexpr unsigned bars = 16;
constexpr double step_seconds = 60 / bpm / 4;
struct Note { unsigned step, midi; double length, velocity; };
const std::array<std::vector<Note>, 4> phrase{{
  {{2,41,1.1,.94}, {5,41,.65,.63}, {7,44,.6,.72}, {10,41,1.2,.88}, {13,39,.7,.66}, {15,41,.6,.80}},
  {{2,41,1.1,.91}, {6,48,.7,.75}, {9,44,.7,.61}, {11,41,1.1,.89}, {14,39,.7,.69}},
  {{2,41,1.0,.94}, {5,41,.65,.62}, {7,44,.6,.71}, {10,41,1.2,.86}, {13,39,.7,.66}, {15,48,.5,.77}},
  {{2,41,1.1,.91}, {5,41,.65,.59}, {7,44,.6,.71}, {10,48,.7,.79}, {13,39,.6,.64}, {15,40,.5,.72}},
}};
enum class Kind { off, kick, bass };
struct Event { std::uint64_t frame; Kind kind; unsigned note; double velocity; };
std::uint64_t frame_at(double seconds) {
  return std::uint64_t(std::llround(seconds * music::sample_rate));
}
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: music-groove OUTPUT_PREFIX (writes mix, bass and kick WAVs)\n";
      return 1;
    }
    const std::string prefix = argv[1];
    for (const auto suffix : {".wav", "-bass.wav", "-kick.wav"})
      if (std::filesystem::exists(prefix + suffix)) throw std::runtime_error("A study output already exists: " + prefix + suffix);
    music::BassVoice bass;
    music::BassPatch bass_patch;
    bass_patch.sub = 0;             // Leave the octave below the bass for the kick.
    bass_patch.cutoff = 240;
    bass_patch.filter_amount = 2.7;
    bass_patch.filter_decay_ms = 190;
    bass_patch.decay_ms = 180;
    bass_patch.sustain = .62;
    bass_patch.release_ms = 40;
    bass_patch.glide_ms = 25;
    bass_patch.drive_db = 4;
    bass_patch.level = .65;
    bass.patch(bass_patch);
    music::KickVoice kick;
    music::KickPatch kick_patch;
    kick_patch.pitch = 43.6535289;  // F1; main bass motif starts at F2.
    kick_patch.decay_ms = 260;
    kick_patch.click = .22;
    kick_patch.level = .7;
    kick.patch(kick_patch);

    std::vector<Event> events;
    for (unsigned bar = 0; bar < bars; ++bar) {
      for (unsigned beat = 0; beat < 4; ++beat) {
        // A breath at the end of each eight-bar section.
        if (bar % 8 == 7 && beat == 3) continue;
        events.push_back({frame_at((bar * 16 + beat * 4) * step_seconds), Kind::kick, 0,
                          beat == 0 ? .94 : beat == 2 ? .87 : .82});
      }
      for (const auto& note : phrase[bar % phrase.size()]) {
        // Slightly delay the odd sixteenths; note lengths remain explicit.
        const auto onset = (bar * 16 + note.step + (note.step % 2 ? .12 : 0)) * step_seconds;
        events.push_back({frame_at(onset), Kind::bass, note.midi, note.velocity});
        events.push_back({frame_at(onset + note.length * step_seconds), Kind::off, 0, 0});
      }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
      return a.frame == b.frame ? a.kind < b.kind : a.frame < b.frame;
    });
    music::Recorder mix(prefix + ".wav"), bass_file(prefix + "-bass.wav"), kick_file(prefix + "-kick.wav");
    std::array<music::StereoFrame, music::block_size> mixed{}, bass_samples{}, kick_samples{};
    const auto total = frame_at(bars * 16 * step_seconds + 1);
    std::size_t next = 0;
    double kick_age = 10, peak = 0;
    for (std::uint64_t frame = 0; frame < total;) {
      const auto count = std::min<std::uint64_t>(mixed.size(), total - frame);
      for (std::size_t i = 0; i < count; ++i, ++frame) {
        while (next < events.size() && events[next].frame == frame) {
          const auto& event = events[next++];
          if (event.kind == Kind::kick) { kick.trigger(event.velocity); kick_age = 0; }
          else if (event.kind == Kind::off) bass.note_off();
          else bass.note_on(event.note, event.velocity);
        }
        // Mild, short ducking only around the kick's attack. Most bass onsets
        // occupy the spaces between the kicks rather than depending on ducking.
        const auto b = float(bass.render() * (1 - .3 * std::exp(-kick_age / .035)) * 1.3);
        const auto k = kick.render() * 1.3f;
        const auto value = b + k;
        if (!std::isfinite(value) || std::abs(value) >= .99f) throw std::runtime_error("Invalid or clipping study output");
        mixed[i] = {value, value}; bass_samples[i] = {b,b}; kick_samples[i] = {k,k};
        peak = std::max(peak, double(std::abs(value)));
        kick_age += 1. / music::sample_rate;
      }
      mix.push(mixed.data(), count); bass_file.push(bass_samples.data(), count); kick_file.push(kick_samples.data(), count);
    }
    for (auto* recorder : {&mix, &bass_file, &kick_file}) {
      recorder->finish();
      if (recorder->failed() || recorder->dropped()) throw std::runtime_error("Study recording failed");
    }
    std::cout << "Study: " << bars << " bars, " << bpm << " BPM, " << total
              << " frames, peak=" << peak << ", " << prefix << ".wav\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
