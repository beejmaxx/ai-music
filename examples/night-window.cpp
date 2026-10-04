// Night Window: a second composition using Side Street's instrument palette.
// The bass moves through G minor, E-flat and F; the hook has a falling answer.
#include "music/bass_voice.hpp"
#include "music/kick_voice.hpp"
#include "music/effects.hpp"
#include "music/recorder.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

namespace {
constexpr double bpm = 118, step_seconds = 60 / bpm / 4;
constexpr unsigned bars = 64;
struct Note { unsigned step, midi; double length, velocity; };
const std::array<std::vector<Note>, 4> bass_phrase{{
  {{0,43,1.6,.92}, {3,50,.65,.63}, {6,43,.95,.86}, {10,41,.65,.62}, {12,43,1.4,.83}, {15,46,.5,.62}},
  {{0,43,1.1,.89}, {3,43,.6,.56}, {6,50,1.0,.80}, {9,46,.7,.67}, {12,43,1.4,.87}, {15,38,.5,.64}},
  {{0,39,1.6,.94}, {3,46,.7,.65}, {6,39,.9,.82}, {10,43,.65,.64}, {12,46,1.2,.77}, {15,40,.5,.60}},
  {{0,41,1.5,.90}, {3,48,.7,.64}, {6,41,.9,.83}, {9,45,.65,.65}, {12,48,.9,.76}, {15,42,.5,.60}},
}};
const std::array<std::vector<Note>, 4> hook_phrase{{
  {{2,74,.7,.82}, {4,70,1.4,.70}, {7,67,1.8,.82}, {12,70,.7,.68}, {14,74,1.2,.80}},
  {{2,77,1.8,.78}, {6,74,1.2,.70}, {10,70,2.3,.84}, {15,69,.6,.55}},
  {{0,67,1.6,.84}, {4,70,.75,.70}, {7,74,1.4,.76}, {12,70,2.6,.75}},
  {{2,69,1.6,.82}, {6,72,.7,.65}, {9,67,1.1,.68}, {12,69,.7,.70}, {14,74,.6,.62}},
}};
const std::array<std::vector<Note>, 4> hook_answer{{
  {{2,74,1.4,.82}, {6,77,1.1,.73}, {10,74,.8,.70}, {13,70,1.8,.80}},
  {{2,74,1.0,.79}, {5,70,.9,.70}, {9,67,2.4,.83}, {14,65,.6,.58}},
  {{0,67,1.4,.80}, {4,70,.8,.69}, {7,74,1.7,.80}, {12,79,1.9,.74}},
  {{2,77,1.3,.78}, {6,72,.9,.70}, {10,69,1.3,.77}, {14,67,1.1,.82}},
}};
constexpr unsigned roots[] = {43,43,39,41};
enum class Kind { bass_off, hook_off, section, kick, bass, hook };
struct Event { std::uint64_t frame; Kind kind; unsigned note; double velocity; };
std::uint64_t frame_at(double seconds) { return std::uint64_t(std::llround(seconds * music::sample_rate)); }
struct Section { unsigned bar; const char* name; };
constexpr Section sections[] = {
  {0,"Hook glimpse"}, {4,"Groove"}, {12,"Main hook"}, {20,"Answer"},
  {24,"Bass feature"}, {28,"Hook returns"}, {32,"High answer"},
  {36,"Breakdown"}, {40,"Rebuild"}, {44,"Full return"}, {52,"Final answer"},
  {56,"Last refrain"}, {60,"Outro"}, {63,"Last hit"}, {64,"Tail"},
};
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: music-night-window OUTPUT_PREFIX (mix, four stems, and section timings)\n";
      return 1;
    }
    const std::string prefix = argv[1];
    constexpr const char* suffixes[] = {".wav", "-bass.wav", "-kick.wav", "-percussion.wav", "-hook.wav"};
    for (const auto suffix : suffixes)
      if (std::filesystem::exists(prefix + suffix)) throw std::runtime_error("Output already exists: " + prefix + suffix);
    if (std::filesystem::exists(prefix + "-sections.txt")) throw std::runtime_error("Section notes already exist");

    music::BassVoice bass, hook;
    music::BassPatch bass_patch;
    bass_patch.sub = 0; bass_patch.cutoff = 240; bass_patch.filter_amount = 2.7;
    bass_patch.filter_decay_ms = 190; bass_patch.decay_ms = 180; bass_patch.sustain = .62;
    bass_patch.release_ms = 40; bass_patch.glide_ms = 25; bass_patch.drive_db = 4; bass_patch.level = .65;
    bass.patch(bass_patch);
    // Reuse the oscillator/filter instrument in a higher register for the pluck.
    music::BassPatch hook_patch;
    hook_patch.oscillator = music::BassPatch::Oscillator::pulse;
    hook_patch.pulse_width = .42; hook_patch.sub = 0; hook_patch.cutoff = 950;
    hook_patch.resonance = .7; hook_patch.filter_amount = 2; hook_patch.filter_decay_ms = 260;
    hook_patch.attack_ms = 4; hook_patch.decay_ms = 650; hook_patch.sustain = .18;
    hook_patch.release_ms = 180; hook_patch.drive_db = 1; hook_patch.level = .43;
    hook.patch(hook_patch);
    music::KickVoice kick;
    music::KickPatch kick_patch;
    kick_patch.pitch = 48.9994295; kick_patch.decay_ms = 260;
    kick_patch.click = .22; kick_patch.level = .7; kick.patch(kick_patch);

    // Use the existing drum sound design, with every tonal part and kick muted.
    // The percussion bus remains fully muted for the first two bars, including
    // the kit's initial control smoothing. Its audio clock runs from sample zero.
    auto kit = music::make_synth_source();
    kit->tempo(float(bpm));
    for (const auto layer : {"kick", "clap", "hats", "bass", "lead", "pad"}) kit->mix(layer, 0);
    music::Effects echo;
    echo.tempo(float(bpm)); echo.delay(.24f); echo.filter(6500);

    std::vector<Event> events;
    const auto add_note = [&](unsigned bar, const Note& note, bool lead, double velocity_scale = 1.) {
      const auto onset = (bar * 16 + note.step + (note.step % 2 ? (lead ? .06 : .11) : 0)) * step_seconds;
      events.push_back({frame_at(onset), lead ? Kind::hook : Kind::bass, note.midi, note.velocity * velocity_scale});
      events.push_back({frame_at(onset + note.length * step_seconds), lead ? Kind::hook_off : Kind::bass_off, 0, 0});
    };
    for (unsigned bar = 0; bar <= bars; ++bar) {
      events.push_back({frame_at(bar * 16 * step_seconds), Kind::section, bar, 0});
      if (bar == bars) continue;
      for (unsigned beat = 0; beat < 4; ++beat) {
        if (bar >= 36 && bar < 42) continue;
        if ((bar < 4 || (bar >= 42 && bar < 44)) && beat % 2) continue;
        if (((bar == 23 || bar == 35 || bar == 55) && beat == 3) || (bar == 63 && beat != 0)) continue;
        events.push_back({frame_at((bar * 16 + beat * 4) * step_seconds), Kind::kick, 0,
                          beat == 0 ? .94 : beat == 2 ? .87 : .82});
      }
      if (bar == 63) {
        add_note(bar, {0,43,3.0,.85}, false);
        add_note(bar, {0,67,3.0,.72}, true);
        continue;
      }
      if (bar >= 36 && bar < 40) {
        add_note(bar, {0,roots[bar % 4],4.5,.68}, false);
      } else {
        for (auto note : bass_phrase[bar % 4]) {
          if (bar < 4 && note.step != 0 && note.step != 6 && note.step != 12) continue;
          if (bar >= 60 && note.step > 10) continue;
          if (bar == 43 && note.step >= 12) continue;
          // A less insistent turnaround on the first half of the eight-bar phrase.
          if (bar % 8 == 3 && note.step == 15) note.midi = 48;
          add_note(bar, note, false, bar >= 40 && bar < 44 ? .82 : 1.);
        }
      }
      if (bar >= 60 || (bar >= 24 && bar < 28)) continue;
      const bool answer = (bar >= 20 && bar < 24) || (bar >= 32 && bar < 36) || (bar >= 52 && bar < 56);
      for (auto note : (answer ? hook_answer : hook_phrase)[bar % 4]) {
        if (bar < 4) {
          if (bar % 2 || note.step > 4) continue;
        } else if (bar < 12) {
          if (bar % 4 != 3 || note.step < 9) continue;
        }
        if (bar >= 36 && bar < 44) {
          // Let the last part of each melodic answer ring over the changing roots.
          if (note.step < 9 || note.step > 12) continue;
          note.length = 2.8;
        }
        if (bar >= 56 && note.step < 7) continue;
        // Land on the tonic at the end of each larger statement.
        if (bar % 8 == 7 && note.step == 14) note.midi = 67;
        add_note(bar, note, true, bar < 12 ? .74 : bar >= 36 && bar < 44 ? .8 : 1.);
      }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
      return a.frame == b.frame ? a.kind < b.kind : a.frame < b.frame;
    });

    std::array<std::unique_ptr<music::Recorder>, 5> files;
    for (unsigned i = 0; i < files.size(); ++i) files[i] = std::make_unique<music::Recorder>(prefix + suffixes[i]);
    std::array<std::array<music::StereoFrame, music::block_size>, 5> output{};
    std::array<float, music::block_size> kit_l{}, kit_r{}, unused_l{}, unused_r{};
    const auto song_end = frame_at(bars * 16 * step_seconds);
    const auto total = song_end + 3 * music::sample_rate;
    std::size_t next = 0;
    double kick_age = 10, peak = 0, percussion_gain = 0, target_percussion = 0;
    auto section = [&](unsigned bar) {
      bass_patch.cutoff = bar >= 36 && bar < 40 ? 140 : bar >= 40 && bar < 44 ? 180 + 30 * (bar - 40)
        : bar >= 24 && bar < 28 ? 320 : bar >= 44 && bar < 56 ? 285 : 240;
      bass.patch(bass_patch);
      const bool sparse = bar >= 36 && bar < 40;
      const bool tail = bar >= 63;
      const bool bass_feature = bar >= 24 && bar < 28;
      const float hats = tail || sparse ? 0 : bar < 4 ? .12f : bass_feature || bar >= 60 ? .15f : .23f;
      const float clap = tail || sparse || bass_feature || bar < 4 ? 0 : bar >= 40 && bar < 44 ? .13f : .29f;
      kit->mix("hats", hats); kit->mix("clap", clap);
      kit->synth_control({music::Parameter::rhythm, bar == 35 || bar == 55 ? 1.f : 0.f});
      target_percussion = bar < 2 || bar >= bars ? 0 : 1;
      hook_patch.cutoff = sparse ? 700 : bar >= 40 && bar < 44 ? 750 + 75 * (bar - 40) : 950;
      hook.patch(hook_patch);
      echo.delay(sparse ? .38f : bar >= 44 && bar < 56 ? .20f : .24f);
    };
    for (std::uint64_t frame = 0; frame < total;) {
      while (next < events.size() && events[next].frame == frame) {
        const auto& event = events[next++];
        switch (event.kind) {
          case Kind::section: section(event.note); break;
          case Kind::kick: kick.trigger(event.velocity); kick_age = 0; break;
          case Kind::bass_off: bass.note_off(); break;
          case Kind::hook_off: hook.note_off(); break;
          case Kind::bass: bass.note_on(event.note, event.velocity); break;
          case Kind::hook: hook.note_on(event.note, event.velocity); break;
        }
      }
      auto count = std::min<std::uint64_t>(music::block_size, total - frame);
      if (next < events.size()) count = std::min(count, events[next].frame - frame);
      if (!count) throw std::runtime_error("Track event clock stopped progressing");
      kit->read_buses(unused_l.data(), unused_r.data(), kit_l.data(), kit_r.data(), count);
      echo.begin_block();
      for (std::size_t i = 0; i < count; ++i, ++frame) {
        percussion_gain += (target_percussion - percussion_gain) / 960;
        const auto fade = frame > total - music::sample_rate / 2
          ? float(total - frame) / (music::sample_rate / 2) : 1.f;
        const auto b = float(bass.render() * (1 - .3 * std::exp(-kick_age / .035)) * 1.3) * fade;
        const auto k = kick.render() * 1.3f * fade;
        const auto p = float(kit_l[i] * percussion_gain * 1.1) * fade;
        const auto h = hook.render() * 1.2f;
        const auto wet = echo.process(h, h * .72f);
        const auto duck = float(1 - .12 * std::exp(-kick_age / .055));
        output[1][i] = {b,b}; output[2][i] = {k,k}; output[3][i] = {p,p};
        output[4][i] = {wet.left * duck * fade, wet.right * duck * fade};
        output[0][i] = {b + k + p + output[4][i].left, b + k + p + output[4][i].right};
        for (const auto& bus : output) {
          const auto& value = bus[i];
          if (!std::isfinite(value.left) || !std::isfinite(value.right) ||
              std::max(std::abs(value.left), std::abs(value.right)) >= .99f)
            throw std::runtime_error("Invalid or clipping track output");
        }
        peak = std::max({peak, double(std::abs(output[0][i].left)), double(std::abs(output[0][i].right))});
        kick_age += 1. / music::sample_rate;
      }
      for (unsigned i = 0; i < files.size(); ++i) files[i]->push(output[i].data(), count);
    }
    for (auto& file : files) {
      file->finish();
      if (file->failed() || file->dropped()) throw std::runtime_error("Track recording failed");
    }
    std::ofstream notes(prefix + "-sections.txt");
    if (!notes) throw std::runtime_error("Could not write section timings");
    notes << "Night Window — second composition\n118 BPM / G minor / 64 bars\n"
          << "Bass roots: G / G / E-flat / F; final hit resolves to G.\n\n" << std::fixed << std::setprecision(2);
    for (const auto& cue : sections) notes << cue.bar * 16 * step_seconds << "s  bar " << cue.bar + 1 << "  " << cue.name << '\n';
    notes << "\nStems include mix gain, ducking and hook echo; sum them at unity.\n";
    notes.close();
    if (!notes) throw std::runtime_error("Could not finish section timings");
    std::cout << "Night Window: " << bars << " bars, " << bpm << " BPM, " << total
              << " frames, peak=" << peak << ", " << prefix << ".wav\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
