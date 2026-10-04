#include "music/bass_voice.hpp"
#include "music/kick_voice.hpp"
#include "music/commands.hpp"
#include "music/recorder.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <type_traits>

namespace {
template<class Patch> struct Field { const char* name; double Patch::*value; };
template<class Patch, std::size_t N>
Patch read_patch(const char* path, const Field<Patch> (&fields)[N]) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot open instrument patch");
  Patch patch;
  std::set<std::string> seen;
  for (std::string line; std::getline(input, line);) {
    line.resize(line.find('#') == std::string::npos ? line.size() : line.find('#'));
    std::istringstream row(line);
    std::string key, value, extra;
    if (!(row >> key)) continue;
    if (!(row >> value) || (row >> extra) || !seen.insert(key).second)
      throw std::runtime_error("Expected one value per unique patch control: " + key);
    if constexpr (std::is_same_v<Patch, music::BassPatch>) if (key == "oscillator") {
      if (value == "saw") patch.oscillator = music::BassPatch::Oscillator::saw;
      else if (value == "pulse") patch.oscillator = music::BassPatch::Oscillator::pulse;
      else if (value == "sine") patch.oscillator = music::BassPatch::Oscillator::sine;
      else throw std::runtime_error("oscillator must be saw, pulse or sine");
      continue;
    }
    bool found = false;
    for (const auto& field : fields) if (key == field.name) {
      patch.*field.value = music::number_in_range(value, 0, 10000);
      found = true;
    }
    if (!found) throw std::runtime_error("Unknown instrument control: " + key);
  }
  patch.validate();
  return patch;
}
music::BassPatch bass_patch(const char* path) {
  const Field<music::BassPatch> fields[] = {
    {"pulse-width", &music::BassPatch::pulse_width}, {"sub", &music::BassPatch::sub},
    {"cutoff", &music::BassPatch::cutoff}, {"resonance", &music::BassPatch::resonance},
    {"filter-amount", &music::BassPatch::filter_amount}, {"filter-decay", &music::BassPatch::filter_decay_ms},
    {"attack", &music::BassPatch::attack_ms}, {"decay", &music::BassPatch::decay_ms},
    {"sustain", &music::BassPatch::sustain}, {"release", &music::BassPatch::release_ms},
    {"drive", &music::BassPatch::drive_db}, {"glide", &music::BassPatch::glide_ms},
    {"level", &music::BassPatch::level},
  };
  return read_patch(path, fields);
}
music::KickPatch kick_patch(const char* path) {
  const Field<music::KickPatch> fields[] = {
    {"pitch", &music::KickPatch::pitch}, {"sweep", &music::KickPatch::sweep},
    {"pitch-decay", &music::KickPatch::pitch_decay_ms}, {"attack", &music::KickPatch::attack_ms},
    {"decay", &music::KickPatch::decay_ms}, {"click", &music::KickPatch::click},
    {"click-decay", &music::KickPatch::click_decay_ms}, {"drive", &music::KickPatch::drive_db},
    {"level", &music::KickPatch::level},
  };
  return read_patch(path, fields);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 4 || (std::string(argv[1]) != "bass" && std::string(argv[1]) != "kick")) {
      std::cerr << "Usage: music-sound bass PATCH.voice OUTPUT.wav [--note 24..96] [--gate .02..10] [--velocity 0..1]\n"
                   "       music-sound kick PATCH.voice OUTPUT.wav [--hits 1..32] [--spacing .1..10] [--velocity 0..1]\n";
      return 1;
    }
    const bool is_kick = std::string(argv[1]) == "kick";
    unsigned note = 41, hits = 1;
    double gate = 1, velocity = .85, spacing = .75;
    for (int i = 4; i < argc; i += 2) {
      if (i + 1 == argc) throw std::runtime_error("Missing option value");
      const std::string option = argv[i];
      if (option == "--note" && !is_kick) {
        const auto value = music::number_in_range(argv[i + 1], 24, 96);
        if (value != std::floor(value)) throw std::runtime_error("MIDI note must be an integer");
        note = unsigned(value);
      } else if (option == "--gate" && !is_kick) gate = music::number_in_range(argv[i + 1], .02, 10);
      else if (option == "--hits" && is_kick) {
        const auto value = music::number_in_range(argv[i + 1], 1, 32);
        if (value != std::floor(value)) throw std::runtime_error("Hit count must be an integer");
        hits = unsigned(value);
      } else if (option == "--spacing" && is_kick) spacing = music::number_in_range(argv[i + 1], .1, 10);
      else if (option == "--velocity") velocity = music::number_in_range(argv[i + 1], 0, 1);
      else throw std::runtime_error("Unknown option: " + option);
    }
    if (std::filesystem::path(argv[3]).extension() != ".wav") throw std::runtime_error("Output needs a .wav extension");
    music::BassVoice bass(music::sample_rate);
    music::KickVoice kick(music::sample_rate);
    const auto gate_frames = std::uint64_t(gate * music::sample_rate);
    const auto spacing_frames = std::uint64_t(std::llround(spacing * music::sample_rate));
    std::uint64_t total = 0;
    if (is_kick) {
      const auto patch = kick_patch(argv[2]);
      kick.patch(patch);
      const auto tail = (patch.attack_ms + 3 * std::max(patch.decay_ms, patch.click_decay_ms)) * .001 + .25;
      total = (hits - 1) * spacing_frames + std::uint64_t(std::llround(tail * music::sample_rate));
    } else {
      const auto patch = bass_patch(argv[2]);
      bass.patch(patch); bass.note_on(note, velocity);
      total = gate_frames + std::uint64_t((patch.release_ms * .003 + .25) * music::sample_rate);
    }
    music::Recorder recorder(argv[3]);
    std::array<music::StereoFrame, music::block_size> block{};
    double energy = 0, peak = 0;
    for (std::uint64_t frame = 0; frame < total;) {
      const auto count = std::min<std::uint64_t>(block.size(), total - frame);
      for (std::size_t i = 0; i < count; ++i, ++frame) {
        if (is_kick && frame % spacing_frames == 0 && frame / spacing_frames < hits) kick.trigger(velocity);
        if (!is_kick && frame == gate_frames) bass.note_off();
        const auto value = is_kick ? kick.render() : bass.render();
        if (!std::isfinite(value) || std::abs(value) >= .99) throw std::runtime_error("Instrument output is invalid or clipping");
        block[i] = {value, value};
        energy += value * value;
        peak = std::max(peak, double(std::abs(value)));
      }
      recorder.push(block.data(), count);
    }
    recorder.finish();
    if (recorder.failed() || recorder.dropped()) throw std::runtime_error("Instrument recording failed");
    std::cout << (is_kick ? "Kick hits=" : "Bass note=") << (is_kick ? hits : note) << " frames=" << total << " peak=" << peak
      << " rms=" << std::sqrt(energy / total) << " output=" << argv[3] << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
