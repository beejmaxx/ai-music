#pragma once

#include "music/audio.hpp"
#include <string>
#include <vector>

namespace music {
enum class Action { style, volume, tempo, temperature, drums, mix, filter, delay,
                    mute, unmute, melody, bassline, root, harmony, bassnotes, voice, rhythm, chords, chord_voice, chord_bars,
                    quantize, cancel, radio, next, status, help, quit };
struct Command {
  Action action;
  std::string text;
  float number = 0;
  float bar = -1, duration = 0;
  std::uint64_t pattern = 0;
};
float number_in_range(const std::string& text, float minimum, float maximum);
std::vector<Command> parse_commands(const std::string& text);
void validate_controls(const std::vector<Command>& commands, bool ai, bool watched_file = false, bool synth = false);
void apply_control(const Command& command, Source& source, Engine& engine);
Score compile_score(const std::vector<Command>& commands, bool replace = false);
const char* command_help();
}  // namespace music
