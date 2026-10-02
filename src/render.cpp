#include "music/audio.hpp"
#include "music/commands.hpp"
#include "music/radio.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
  try {
    const bool program = argc > 1 && std::string(argv[1]) == "--program";
    const int offset = program ? 1 : 0;
    if (argc != 4 + offset && argc != 5 + offset) {
      std::cerr << "Usage: music-render SCORE.commands OUTPUT.wav SECONDS [VOLUME]\n"
                   "       music-render --program NAME OUTPUT.wav SECONDS [VOLUME]\n";
      return 1;
    }
    const std::string input = argv[1 + offset];
    const std::string output = argv[2 + offset];
    const auto seconds = music::number_in_range(argv[3 + offset], .01f, 3600);
    const auto volume = argc == 5 + offset ? music::number_in_range(argv[4 + offset], 0, 1) : .36f;
    if (std::filesystem::path(output).extension() != ".wav")
      throw std::runtime_error("Output must have a .wav extension");
    music::Score score;
    music::RadioDirector radio(0);
    float bpm = 128;
    if (program) {
      radio.program(input);
      bpm = input == "funk-study" ? 111.f : input == "french-house" ? 124.f : 132.f;
      if (seconds > radio.bars() * 240 / bpm + .01)
        throw std::runtime_error("Program auditions are limited to one chapter");
      score = radio.next();
    } else {
      if (std::filesystem::file_size(input) > 65536)
        throw std::runtime_error("Score exceeds 64 KiB");
      std::ifstream file(input);
      if (!file) throw std::runtime_error("Cannot open score");
      const std::string text((std::istreambuf_iterator<char>(file)), {});
      const auto commands = music::parse_commands(text);
      music::validate_controls(commands, false, true, true);
      for (const auto& command : commands)
        if (command.action == music::Action::radio || command.action == music::Action::next || command.action == music::Action::cancel)
          throw std::runtime_error("Auditions need a musical score; radio, next, and cancel are live controls");
      score = music::compile_score(commands, true);
      if (!score.count) throw std::runtime_error("Score contains no musical controls");
    }
    auto source = music::make_synth_source();
    source->tempo(bpm);
    music::Recorder recorder(output);
    music::Engine engine(*source, &recorder);
    engine.tempo(bpm);
    engine.volume(volume);
    if (!engine.score(score)) throw std::runtime_error("Cannot submit score");
    std::array<float, music::block_size * 2> audio{};
    const auto frames = std::uint64_t(double(seconds) * music::sample_rate);
    while (engine.frames() < frames) {
      const auto count = std::min<std::uint64_t>(music::block_size, frames - engine.frames());
      // Use the same render path as playback, including the dry rhythm bus.
      engine.render(audio.data(), count);
    }
    recorder.finish();
    if (engine.invalid_samples() || engine.underruns() || recorder.failed() || recorder.dropped())
      throw std::runtime_error("Render encountered invalid samples, missing audio, or recording drops");
    std::cout << "Rendered " << engine.frames() << " stereo frames to " << output << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
