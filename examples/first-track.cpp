// Side Street: a short arrangement around the bass/kick study.
// The original study and its patches stay independent of this arrangement.
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
constexpr double bpm = 116, step_seconds = 60 / bpm / 4;
constexpr unsigned bars = 64;
struct Note { unsigned step, midi; double length, velocity; };
const std::array<std::vector<Note>, 4> bass_phrase{{
  {{2,41,1.1,.94}, {5,41,.65,.63}, {7,44,.6,.72}, {10,41,1.2,.88}, {13,39,.7,.66}, {15,41,.6,.80}},
  {{2,41,1.1,.91}, {6,48,.7,.75}, {9,44,.7,.61}, {11,41,1.1,.89}, {14,39,.7,.69}},
  {{2,41,1.0,.94}, {5,41,.65,.62}, {7,44,.6,.71}, {10,41,1.2,.86}, {13,39,.7,.66}, {15,48,.5,.77}},
  {{2,41,1.1,.91}, {5,41,.65,.59}, {7,44,.6,.71}, {10,48,.7,.79}, {13,39,.6,.64}, {15,40,.5,.72}},
}};
const std::array<std::vector<Note>, 4> hook_phrase{{
  {{3,65,1.4,.84}, {6,68,.8,.68}, {11,72,2.1,.82}},
  {{2,70,1.0,.70}, {5,68,.9,.62}, {9,65,2.7,.82}, {14,63,.6,.54}},
  {{3,65,1.4,.82}, {6,68,.8,.66}, {11,72,2.1,.84}},
  {{2,68,.8,.70}, {5,65,1.2,.78}, {11,63,1.5,.62}, {14,65,.9,.80}},
}};
// A small phrasing experiment: repeat the high note, then answer on beat four.
// The default phrase above remains the approved Side Street composition.
const std::array<std::vector<Note>, 4> hook_variation{{
  {{3,65,.75,.84}, {6,68,.65,.68}, {10,72,.65,.72}, {12,72,2.1,.82}},
  {{2,70,.85,.70}, {5,68,.65,.62}, {9,65,2.7,.82}, {14,63,.6,.54}},
  {{3,65,.75,.82}, {6,68,.65,.66}, {10,72,.65,.72}, {12,72,2.1,.84}},
  {{2,68,.8,.70}, {5,65,1.2,.78}, {10,63,.65,.62}, {12,65,2.1,.80}},
}};
const std::array<std::vector<Note>, 4> breakdown_bass{{
  {{2,41,2.0,.80}, {10,41,1.3,.72}, {14,39,.75,.63}},
  {{2,41,2.2,.77}, {9,44,.8,.62}, {12,41,1.5,.75}},
  {{2,41,2.0,.80}, {10,48,1.25,.73}, {14,44,.75,.61}},
  {{2,41,2.0,.76}, {9,39,1.25,.66}, {14,40,.6,.62}},
}};
const std::array<std::vector<Note>, 4> breakdown_hook{{
  {{11,72,1.2,.68}},
  {},
  {{5,68,.9,.58}, {11,65,1.6,.70}},
  {{9,63,1.2,.61}, {14,65,.75,.66}},
}};
const std::array<std::vector<Note>, 4> rebuild_hook{{
  {{9,65,1.5,.67}, {14,68,.75,.63}},
  {{6,68,.8,.65}, {10,70,1.3,.70}, {14,72,.9,.76}},
  {{3,68,1.0,.72}, {7,70,.75,.64}, {11,72,1.1,.79}, {14,75,.75,.75}},
  {{2,72,1.3,.78}, {7,70,.75,.70}, {10,67,.8,.64}},
}};
// Two familiar bars lead into this higher answer at the full return.
const std::array<std::vector<Note>, 2> return_answer{{
  {{3,68,1.1,.80}, {6,72,.75,.70}, {10,75,1.35,.84}, {14,72,.75,.67}},
  {{2,70,.95,.73}, {5,68,.9,.65}, {9,67,1.25,.64}, {12,65,2.1,.84}},
}};
enum class HookVersion { original, melody, expression };
enum class Kind { bass_off, hook_off, section, kick, bass, hook };
struct Event { std::uint64_t frame; Kind kind; unsigned note; double velocity, length = 0; };
std::uint64_t frame_at(double seconds) { return std::uint64_t(std::llround(seconds * music::sample_rate)); }
struct Section { unsigned bar; const char* name; };
constexpr Section sections[] = {
  {0,"Foundation"}, {4,"Percussion enters"}, {8,"Hook"}, {16,"Full groove"},
  {24,"Development"}, {32,"Breakdown"}, {36,"Rebuild"}, {40,"Return"},
  {48,"Final variation"}, {56,"Outro"}, {63,"Last hit"}, {64,"Tail"},
};
}

int main(int argc, char** argv) {
  try {
    if (argc < 2 || argc > 6 || argc % 2 != 0) {
      std::cerr << "Usage: music-track OUTPUT_PREFIX [--hook original|melody|expression]\n"
                   "                   [--arrangement original|v2]\n"
                   "Writes a mix, four stems, and section timings.\n";
      return 1;
    }
    const std::string prefix = argv[1];
    std::string hook_name = "original", arrangement_name = "original";
    bool have_hook = false, have_arrangement = false;
    for (int argument = 2; argument < argc; argument += 2) {
      const std::string option = argv[argument];
      if (option == "--hook" && !have_hook) {
        hook_name = argv[argument + 1]; have_hook = true;
      } else if (option == "--arrangement" && !have_arrangement) {
        arrangement_name = argv[argument + 1]; have_arrangement = true;
      } else throw std::runtime_error("Unknown or duplicate option: " + option);
    }
    const auto hook_version = hook_name == "original" ? HookVersion::original
      : hook_name == "melody" ? HookVersion::melody : hook_name == "expression" ? HookVersion::expression
      : throw std::runtime_error("Hook must be original, melody, or expression");
    if (arrangement_name != "original" && arrangement_name != "v2")
      throw std::runtime_error("Arrangement must be original or v2");
    const bool revised = arrangement_name == "v2";
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
    kick_patch.pitch = 43.6535289; kick_patch.decay_ms = 260;
    kick_patch.click = .22; kick_patch.level = .7; kick.patch(kick_patch);

    // Use the existing drum sound design, with every tonal part and kick muted.
    // The percussion bus remains fully muted for the first four bars, including
    // the kit's initial control smoothing. Its audio clock runs from sample zero.
    auto kit = music::make_synth_source();
    kit->tempo(float(bpm));
    for (const auto layer : {"kick", "clap", "hats", "bass", "lead", "pad"}) kit->mix(layer, 0);
    music::Effects echo;
    echo.tempo(float(bpm)); echo.delay(.24f); echo.filter(6500);

    std::vector<Event> events;
    const auto add_note = [&](unsigned bar, const Note& note, bool lead, double velocity_scale = 1.) {
      const auto onset = (bar * 16 + note.step + (note.step % 2 ? (lead ? .07 : .12) : 0)) * step_seconds;
      events.push_back({frame_at(onset), lead ? Kind::hook : Kind::bass, note.midi, note.velocity * velocity_scale, note.length});
      events.push_back({frame_at(onset + note.length * step_seconds), lead ? Kind::hook_off : Kind::bass_off, 0, 0});
    };
    for (unsigned bar = 0; bar <= bars; ++bar) {
      events.push_back({frame_at(bar * 16 * step_seconds), Kind::section, bar, 0});
      if (bar == bars) continue;
      for (unsigned beat = 0; beat < 4; ++beat) {
        if (bar >= 32 && bar < 38) continue;
        if (bar >= 38 && bar < 40 && beat % 2) continue;
        if ((bar % 8 == 7 && beat == 3) || (bar == 63 && beat != 0)) continue;
        events.push_back({frame_at((bar * 16 + beat * 4) * step_seconds), Kind::kick, 0,
                          beat == 0 ? .94 : beat == 2 ? .87 : .82});
      }
      if (bar == 63) {
        add_note(bar, {0,41,3.0,.85}, false);
        add_note(bar, {0,65,3.0,.72}, true);
        continue;
      }
      if (bar >= 32 && bar < 36) {
        if (revised) {
          for (const auto& note : breakdown_bass[bar % 4]) add_note(bar, note, false);
        } else if (bar % 2 == 0) add_note(bar, {2,41,3.5,.65}, false);
      } else {
        for (auto note : bass_phrase[bar % 4]) {
          if (bar >= 60 && note.step > 10) continue;
          if (revised && bar == 39 && note.step >= 12) continue;
          if (revised && bar == 40 && note.step == 2) { note.step = 0; note.length = 1.6; }
          add_note(bar, note, false, bar >= 36 && bar < 40 ? .8 : 1.);
        }
      }
      if (bar < 8 || bar >= 60) continue;
      if (revised && bar >= 32 && bar < 40) {
        const auto& phrase = bar < 36 ? breakdown_hook : rebuild_hook;
        for (const auto& note : phrase[bar % 4]) add_note(bar, note, true);
        continue;
      }
      if (revised && bar >= 40 && bar < 48 && bar % 4 >= 2) {
        for (const auto& note : return_answer[bar % 4 - 2]) add_note(bar, note, true);
        continue;
      }
      for (auto note : (hook_version == HookVersion::melody ? hook_variation : hook_phrase)[bar % 4]) {
        if (bar >= 32 && bar < 40) {
          if (note.step < 9) continue;
          note.length *= 1.5;
        }
        if (bar >= 56 && (bar % 2 || note.step < 9)) continue;
        // A brief answer an octave lower before the final return of the motif.
        if (bar >= 24 && bar < 32 && bar % 4 == 3) note.midi -= 12;
        if (bar >= 48 && bar < 56 && bar % 4 == 3 && note.step == 14) note.midi += 12;
        add_note(bar, note, true, bar < 16 ? .85 : bar >= 32 && bar < 40 ? .8 : 1.);
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
    const auto total = song_end + 2 * music::sample_rate;
    const auto pause_start = frame_at((40 * 16 - 2) * step_seconds);
    const auto return_start = frame_at(40 * 16 * step_seconds);
    const auto pause_fade_out = frame_at(.012), pause_fade_in = frame_at(.002);
    std::size_t next = 0;
    double kick_age = 10, peak = 0, percussion_gain = 0, target_percussion = 0;
    auto section = [&](unsigned bar) {
      bass_patch.cutoff = bar >= 32 && bar < 36 ? 130 : bar >= 36 && bar < 40 ? 180 + 25 * (bar - 36)
        : bar >= 24 && bar < 32 ? 300 : bar >= 40 && bar < 56 ? 280 : 240;
      if (revised && bar >= 32 && bar < 40)
        bass_patch.cutoff = bar < 36 ? 150 : 140 + 35 * (bar - 36);
      bass.patch(bass_patch);
      const bool sparse = bar >= 32 && bar < 36;
      const bool tail = bar >= 63;
      float hats = tail ? 0 : sparse ? .065f : bar < 8 ? .16f : bar >= 60 ? .12f : .23f;
      float clap = tail || sparse || bar < 8 ? 0 : bar >= 36 && bar < 40 ? .14f : .29f;
      if (revised && bar >= 32 && bar < 40) {
        hats = bar < 36 ? 0 : .08f + .035f * (bar - 36);
        clap = bar < 38 ? 0 : .12f + .04f * (bar - 38);
      }
      kit->mix("hats", hats); kit->mix("clap", clap);
      kit->synth_control({music::Parameter::rhythm, (!revised && bar == 39) || bar == 55 ? 1.f : 0.f});
      target_percussion = bar < 4 || bar >= bars ? 0 : 1;
      echo.delay(sparse ? .36f : .24f);
      if (revised) {
        if (sparse) target_percussion = 0;
        echo.delay(sparse ? .44f : bar >= 36 && bar < 40 ? .32f - .02f * (bar - 36) : .24f);
        echo.filter(sparse ? 4000 : bar >= 36 && bar < 40 ? 4200 + 700 * (bar - 36) : 6500);
      }
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
          case Kind::hook:
            if (hook_version == HookVersion::expression) {
              auto expressive = hook_patch;
              const auto accent = std::clamp((event.velocity - .54) / .30, 0., 1.);
              const bool held = event.length >= 1.4;
              expressive.cutoff = 600 + 650 * accent;
              expressive.filter_amount = 1.5 + accent;
              expressive.filter_decay_ms = held ? 420 : 180;
              expressive.resonance = .85;
              expressive.attack_ms = held ? 8 : 3;
              expressive.decay_ms = held ? 850 : 400;
              expressive.sustain = held ? .20 : .12;
              expressive.release_ms = held ? 250 : 110;
              expressive.drive_db = 2.5;
              hook.patch(expressive);
            }
            hook.note_on(event.note, event.velocity);
            break;
        }
      }
      auto count = std::min<std::uint64_t>(music::block_size, total - frame);
      if (next < events.size()) count = std::min(count, events[next].frame - frame);
      if (!count) throw std::runtime_error("Track event clock stopped progressing");
      kit->read_buses(unused_l.data(), unused_r.data(), kit_l.data(), kit_r.data(), count);
      echo.begin_block();
      for (std::size_t i = 0; i < count; ++i, ++frame) {
        percussion_gain += (target_percussion - percussion_gain) / 960;
        auto fade = frame > total - music::sample_rate / 2
          ? float(total - frame) / (music::sample_rate / 2) : 1.f;
        // A half-beat breath before the return. Every stem shares the same
        // envelope while the instruments and delay continue advancing in time.
        if (revised) {
          if (frame >= pause_start - pause_fade_out && frame < pause_start)
            fade *= float(pause_start - frame) / pause_fade_out;
          else if (frame >= pause_start && frame < return_start) fade = 0;
          else if (frame >= return_start && frame < return_start + pause_fade_in)
            fade *= float(frame - return_start) / pause_fade_in;
        }
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
    notes << (revised ? "Side Street — arrangement v2\n" : "Side Street — first arrangement\n")
          << "116 BPM / F minor / 64 bars\n\n" << std::fixed << std::setprecision(2);
    if (hook_version != HookVersion::original) notes << "Hook experiment: " << hook_name << "\n\n";
    for (const auto& cue : sections) {
      if (revised && cue.bar == 40) notes << double(pause_start) / music::sample_rate << "s  Last half-beat: pause\n";
      const auto name = revised && cue.bar == 32 ? "Bass and echo" : revised && cue.bar == 40 ? "Full return" : cue.name;
      notes << cue.bar * 16 * step_seconds << "s  bar " << cue.bar + 1 << "  " << name << '\n';
      if (revised && cue.bar == 40) notes << 42 * 16 * step_seconds << "s  bar 43  New melodic answer\n";
    }
    notes << "\nStems include mix gain, ducking and hook echo; sum them at unity.\n";
    notes.close();
    if (!notes) throw std::runtime_error("Could not finish section timings");
    std::cout << "Side Street: " << bars << " bars, " << bpm << " BPM, " << total
              << " frames, peak=" << peak << ", " << prefix << ".wav\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
