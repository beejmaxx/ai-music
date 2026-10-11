// Corner Light: a slower D-minor composition using the same instrument palette.
// An eight-bar Dm / B-flat / Gm / A7 progression gives the hook a changing context.
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
constexpr double bpm = 112, step_seconds = 60 / bpm / 4;
constexpr unsigned bars = 64;
struct Note { unsigned step, midi; double length, velocity; };
constexpr unsigned roots[] = {38,38,34,34,43,43,33,33};
const auto bass_phrase = [] {
  std::array<std::vector<Note>,8> phrases;
  for (unsigned bar=0;bar<8;++bar) {
    const auto root=roots[bar], third=root+((bar<2 || (bar>=4 && bar<6))?3:4);
    const auto next=roots[(bar+1)%8];
    phrases[bar] = bar%2 == 0
      ? std::vector<Note>{{0,root,1.35,.91},{3,root,.60,.55},{6,root+7,.80,.68},
                         {9,third,.70,.70},{12,root,1.10,.83},{15,root+7,.45,.56}}
      : std::vector<Note>{{0,root,1.10,.87},{3,third,.65,.61},{7,root,1.30,.80},
                         {11,root+7,.80,.70},{14,next-1,.55,.53}};
  }
  return phrases;
}();
const std::array<std::vector<Note>,8> hook_phrase{{
  {{3,62,1.2,.78},{7,65,.8,.67},{11,69,2.4,.82}},
  {{5,67,.9,.64},{11,65,2.2,.76}},
  {{3,62,1.2,.76},{7,65,.8,.65},{11,69,2.4,.80}},
  {{5,72,1.0,.72},{11,69,2.2,.78}},
  {{3,62,1.2,.77},{7,65,.8,.67},{11,70,2.3,.81}},
  {{5,69,.9,.65},{11,65,2.1,.76}},
  {{3,61,1.2,.76},{7,64,.8,.66},{11,67,2.2,.80}},
  {{5,64,1.0,.65},{11,61,1.1,.70}},
}};
const auto hook_answer = [] {
  auto notes=hook_phrase;
  notes[1]={{5,69,.8,.73},{9,67,.7,.66},{12,65,2.0,.80}};
  notes[3]={{5,74,1.2,.78},{11,72,2.0,.73}};
  notes[5]={{5,74,1.0,.76},{9,72,.7,.67},{12,70,1.8,.79}};
  notes[7]={{5,67,1.0,.71},{9,64,.8,.65},{12,61,1.1,.73}};
  return notes;
}();
enum class Kind { bass_off, hook_off, section, kick, bass, hook };
struct Event { std::uint64_t frame; Kind kind; unsigned note; double velocity; };
std::uint64_t frame_at(double seconds) { return std::uint64_t(std::llround(seconds * music::sample_rate)); }
struct Section { unsigned bar; const char* name; };
constexpr Section sections[] = {
  {0,"Hook glimpse"}, {4,"Piano groove"}, {8,"Main hook"}, {16,"Answer"},
  {24,"Bass and keys"}, {28,"Hook returns"}, {32,"Breakdown"},
  {36,"Rebuild"}, {40,"Full return"}, {48,"Final answer"},
  {56,"Last refrain"}, {60,"Outro"}, {63,"Last hit"}, {64,"Tail"},
};
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: music-corner-light OUTPUT_PREFIX (mix, four stems, and section timings)\n";
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
    hook_patch.pulse_width = .46; hook_patch.sub = 0; hook_patch.cutoff = 800;
    hook_patch.resonance = .65; hook_patch.filter_amount = 1.8; hook_patch.filter_decay_ms = 300;
    hook_patch.attack_ms = 6; hook_patch.decay_ms = 720; hook_patch.sustain = .16;
    hook_patch.release_ms = 220; hook_patch.drive_db = 1; hook_patch.level = .40;
    hook.patch(hook_patch);
    music::KickVoice kick;
    music::KickPatch kick_patch;
    kick_patch.pitch = 36.70809599; kick_patch.decay_ms = 235;
    kick_patch.click = .22; kick_patch.level = .7; kick.patch(kick_patch);

    // Use the existing drum sound design, with every tonal part and kick muted.
    // The percussion bus remains fully muted for the first two bars, including
    // the kit's initial control smoothing. Its audio clock runs from sample zero.
    auto kit = music::make_synth_source();
    kit->tempo(float(bpm));
    for (const auto layer : {"kick", "clap", "hats", "bass", "lead", "pad"}) kit->mix(layer, 0);
    music::Effects echo;
    echo.tempo(float(bpm)); echo.delay(.23f); echo.filter(5900);

    std::vector<Event> events;
    const auto add_note = [&](unsigned bar, const Note& note, bool lead, double velocity_scale = 1.) {
      const auto onset = (bar * 16 + note.step + (note.step % 2 ? (lead ? .08 : .14) : 0)) * step_seconds;
      events.push_back({frame_at(onset), lead ? Kind::hook : Kind::bass, note.midi, note.velocity * velocity_scale});
      events.push_back({frame_at(onset + note.length * step_seconds), lead ? Kind::hook_off : Kind::bass_off, 0, 0});
    };
    for (unsigned bar = 0; bar <= bars; ++bar) {
      events.push_back({frame_at(bar * 16 * step_seconds), Kind::section, bar, 0});
      if (bar == bars) continue;
      for (unsigned beat = 0; beat < 4; ++beat) {
        if (bar >= 32 && bar < 38) continue;
        if ((bar < 4 || (bar >= 38 && bar < 40)) && beat % 2) continue;
        if (((bar == 31 || bar == 39 || bar == 55) && beat == 3) || (bar == 63 && beat != 0)) continue;
        events.push_back({frame_at((bar * 16 + beat * 4) * step_seconds), Kind::kick, 0,
                          beat == 0 ? .94 : beat == 2 ? .87 : .82});
      }
      if (bar == 63) {
        add_note(bar, {0,38,3.0,.85}, false);
        add_note(bar, {0,62,3.0,.72}, true);
        continue;
      }
      if (bar >= 32 && bar < 36) {
        add_note(bar, {0,roots[bar % 8],4.5,.68}, false);
      } else {
        for (auto note : bass_phrase[bar % 8]) {
          if (bar < 4 && note.step != 0 && note.step != 6 && note.step != 12) continue;
          if (bar >= 60 && note.step > 10) continue;
          if (bar == 39 && note.step >= 12) continue;
          add_note(bar, note, false, bar >= 36 && bar < 40 ? .82 : 1.);
        }
      }
      if (bar >= 60 || (bar >= 24 && bar < 28)) continue;
      const bool answer = (bar >= 16 && bar < 24) || (bar >= 48 && bar < 56);
      for (auto note : (answer ? hook_answer : hook_phrase)[bar % 8]) {
        if (bar < 4) {
          if (bar % 2 || note.step > 4) continue;
        } else if (bar < 8) {
          if (bar % 8 != 7 || note.step < 9) continue;
        }
        if (bar >= 32 && bar < 40) {
          // Let the last part of each melodic answer ring over the changing roots.
          if (note.step < 9 || note.step > 12) continue;
          note.length = 2.8;
        }
        if (bar >= 56 && note.step < 7) continue;
        add_note(bar, note, true, bar < 8 ? .74 : bar >= 32 && bar < 40 ? .8 : 1.);
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
    const auto pause_start = frame_at((40*16-2)*step_seconds), return_start = frame_at(40*16*step_seconds);
    std::size_t next = 0;
    double kick_age = 10, peak = 0, percussion_gain = 0, target_percussion = 0;
    auto section = [&](unsigned bar) {
      bass_patch.cutoff = bar >= 32 && bar < 36 ? 140 : bar >= 36 && bar < 40 ? 180 + 30 * (bar - 36)
        : bar >= 24 && bar < 28 ? 300 : bar >= 40 && bar < 56 ? 265 : 220;
      bass.patch(bass_patch);
      const bool sparse = bar >= 32 && bar < 36;
      const bool tail = bar >= 63;
      const bool bass_feature = bar >= 24 && bar < 28;
      const float hats = tail || sparse ? 0 : bar < 4 ? .12f : bass_feature || bar >= 60 ? .15f : .23f;
      const float clap = tail || sparse || bass_feature || bar < 4 ? 0 : bar >= 36 && bar < 40 ? .13f : .28f;
      kit->mix("hats", hats); kit->mix("clap", clap);
      kit->synth_control({music::Parameter::rhythm, bar == 31 || bar == 55 ? 1.f : 0.f});
      target_percussion = bar < 2 || bar >= bars ? 0 : 1;
      hook_patch.cutoff = sparse ? 620 : bar >= 36 && bar < 40 ? 650 + 65 * (bar - 36) : bar>=40 && bar<56 ? 1000 : 800;
      hook.patch(hook_patch);
      echo.delay(sparse ? .36f : bar >= 40 && bar < 56 ? .20f : .23f);
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
        auto fade = frame > total - music::sample_rate / 2
          ? float(total - frame) / (music::sample_rate / 2) : 1.f;
        if (frame>=pause_start-frame_at(.012) && frame<pause_start)
          fade*=float(pause_start-frame)/frame_at(.012);
        else if (frame>=pause_start && frame<return_start) fade=0;
        else if (frame>=return_start && frame<return_start+frame_at(.002))
          fade*=float(frame-return_start)/frame_at(.002);
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
    notes << "Corner Light — companion composition\n112 BPM / D minor / 64 bars\n"
          << "Bass roots: D / B-flat / G / A; final hit resolves to D.\n\n" << std::fixed << std::setprecision(2);
    for (const auto& cue : sections) notes << cue.bar * 16 * step_seconds << "s  bar " << cue.bar + 1 << "  " << cue.name << '\n';
    notes << "\nStems include mix gain, ducking and hook echo; sum them at unity.\n";
    notes.close();
    if (!notes) throw std::runtime_error("Could not finish section timings");
    std::cout << "Corner Light: " << bars << " bars, " << bpm << " BPM, " << total
              << " frames, peak=" << peak << ", " << prefix << ".wav\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
