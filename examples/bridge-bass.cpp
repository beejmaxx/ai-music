// A contrasting eight-bar bass passage for Side Street's middle section.
#include "music/bass_voice.hpp"
#include "music/recorder.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
constexpr double step = 60. / 116 / 4;
std::uint64_t at(double steps) { return std::llround(steps * step * music::sample_rate); }
struct Event { std::uint64_t frame; int kind, midi; double velocity; };
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: music-bridge-bass NEW_OUTPUT.wav");
    if (std::filesystem::exists(argv[1])) throw std::runtime_error("Output already exists");
    music::BassVoice bass;
    music::BassPatch patch;
    patch.sub = 0; patch.cutoff = 150; patch.filter_amount = 2.7;
    patch.filter_decay_ms = 190; patch.decay_ms = 180; patch.sustain = .62;
    patch.release_ms = 40; patch.glide_ms = 25; patch.drive_db = 4; patch.level = .65;
    bass.patch(patch);
    std::vector<Event> events;
    auto note = [&](unsigned bar, unsigned position, unsigned midi, double length, double velocity) {
      const double onset = bar * 16 + position + (position % 2 ? .12 : 0);
      events.push_back({at(onset), 1, int(midi), velocity});
      events.push_back({at(onset + length), 0, 0, 0});
    };
    constexpr int roots[] = {41,41,37,37,44,44,39,39};
    for (unsigned bar = 0; bar < 8; ++bar) {
      const unsigned root = roots[bar], third = root + (bar < 2 ? 3 : 4);
      const unsigned seventh = root - (bar < 2 || bar >= 6 ? 2 : 1);
      if (bar < 4) {
        note(bar,2,root,2.0,.80);
        if (bar % 2 == 0) {
          note(bar,10,bar == 2 ? root+7 : root,1.25,.72);
          note(bar,14,bar == 2 ? third : seventh,.70,.61);
        } else {
          note(bar,9,third,.80,.62);
          note(bar,12,root,1.25,.73);
          note(bar,15,roots[bar+1]-1,.45,.53);
        }
      } else {
        note(bar,2,root,1.10,.76);
        note(bar,5,root,.65,.53);
        note(bar,7,third,.60,.61);
        note(bar,10,root,1.20,.71);
        if (bar < 7) {
          note(bar,13,seventh,.65,.55);
          note(bar,15,bar%2 ? roots[bar+1]-1 : root+7,.45,.62);
        } else note(bar,12,40,.60,.56); // E approaches the returning F.
      }
      if (bar >= 6) for (unsigned beat : {0u,8u}) events.push_back({at(bar*16+beat),2,0,0});
    }
    std::stable_sort(events.begin(),events.end(),[](const Event& a,const Event& b) {
      return a.frame==b.frame ? a.kind<b.kind : a.frame<b.frame;
    });
    music::Recorder out(argv[1]);
    std::array<music::StereoFrame,music::block_size> buffer{};
    const auto total = at(128) + music::sample_rate/2, pause = at(126);
    std::size_t next = 0; unsigned previous_bar = 0;
    double kick_age = 10, peak = 0;
    for (std::uint64_t frame = 0; frame < total;) {
      const auto count = std::min<std::uint64_t>(buffer.size(),total-frame);
      for (std::size_t i = 0; i < count; ++i,++frame) {
        const auto bar = std::min(7u,unsigned(frame/(16*step*music::sample_rate)));
        if (bar != previous_bar) {
          patch.cutoff = bar < 4 ? 150 : 140 + 35*(bar-4);
          bass.patch(patch); previous_bar = bar;
        }
        while (next < events.size() && events[next].frame == frame) {
          const auto e = events[next++];
          if (e.kind == 0) bass.note_off();
          else if (e.kind == 1) bass.note_on(e.midi,e.velocity);
          else kick_age = 0;
        }
        const auto fade = frame >= pause ? 0. : std::min(1.,double(pause-frame)/(.012*music::sample_rate));
        const float value = bass.render() * (1-.3*std::exp(-kick_age/.035)) * 1.3 * fade;
        if (!std::isfinite(value) || std::abs(value) >= .98) throw std::runtime_error("Invalid bass output");
        buffer[i] = {value,value}; peak = std::max(peak,double(std::abs(value)));
        kick_age += 1./music::sample_rate;
      }
      out.push(buffer.data(),count);
    }
    out.finish();
    if (out.failed() || out.dropped()) throw std::runtime_error("Bass recording failed");
    std::cout << "Eight-bar F / D-flat / A-flat / E-flat bass passage; peak " << 20*std::log10(peak) << " dBFS\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
