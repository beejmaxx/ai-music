#include "music/commands.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace music {
namespace {
std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \r\t");
  return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \r\t") - first + 1);
}
bool rampable(Action action) {
  return action == Action::volume || action == Action::tempo || action == Action::mix ||
         action == Action::filter || action == Action::delay;
}
Control control(const Command& command) {
  const auto value = command.number;
  switch (command.action) {
    case Action::style: return {Parameter::style, command.text == "trance" ? 1.f : 0.f};
    case Action::volume: return {Parameter::volume, value};
    case Action::tempo: return {Parameter::tempo, value};
    case Action::filter: return {Parameter::filter, value};
    case Action::delay: return {Parameter::delay, value};
    case Action::drums: return {Parameter::drums, value};
    case Action::mute: return {Parameter::mute, 1};
    case Action::unmute: return {Parameter::mute, 0};
    case Action::root: return {Parameter::root, value};
    case Action::melody: return {Parameter::melody, 0, command.pattern};
    case Action::bassline: return {Parameter::bassline, 0, command.pattern};
    case Action::mix: {
      const char* names[] = {"kick", "clap", "hats", "bass", "lead", "pad"};
      for (unsigned i = 0; i < 6; ++i)
        if (command.text == names[i]) return {Parameter(unsigned(Parameter::kick) + i), value};
      break;
    }
    default: break;
  }
  return {};
}
}  // namespace

float number_in_range(const std::string& text, float minimum, float maximum) {
  try {
    std::size_t end = 0;
    const float n = std::stof(text, &end);
    if (end != text.size() || !std::isfinite(n) || n < minimum || n > maximum) throw std::invalid_argument("range");
    return n;
  } catch (const std::exception&) {
    throw std::runtime_error("Expected a finite number between " + std::to_string(minimum) + " and " + std::to_string(maximum));
  }
}

std::vector<Command> parse_commands(const std::string& text) {
  if (text.size() > 65536) throw std::runtime_error("Command file is too large (64 KiB maximum)");
  std::vector<Command> commands;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line.empty() || line.front() == '#') continue;
    const auto split = line.find_first_of(" \t");
    const auto name = line.substr(0, split);
    const auto value = split == std::string::npos ? "" : trim(line.substr(split));
    if (name == "at" || name == "ramp") {
      std::istringstream args(value);
      std::string start, duration, rest;
      if (!(args >> start)) throw std::runtime_error("Use at BAR COMMAND or ramp BAR DURATION COMMAND");
      const auto bar = number_in_range(start, 0, 4096);
      float length = 0;
      if (name == "ramp") {
        if (!(args >> duration)) throw std::runtime_error("Ramp needs a duration in bars");
        length = number_in_range(duration, .25f, 1024);
      }
      std::getline(args, rest);
      rest = trim(rest);
      if (rest.empty() || rest.starts_with("at ") || rest.starts_with("ramp "))
        throw std::runtime_error("Timing must be followed by one musical control");
      auto inner = parse_commands(rest);
      if (inner.size() != 1 || control(inner.front()).parameter == Parameter::none)
        throw std::runtime_error("Only musical controls can be scheduled");
      auto command = inner.front();
      if (command.bar >= 0) throw std::runtime_error("Nested timing is not supported");
      if (length > 0 && !rampable(command.action)) throw std::runtime_error("Ramp supports mix, volume, tempo, filter, and delay");
      command.bar = bar; command.duration = length;
      commands.push_back(command);
    } else if (name == "melody" || name == "bassline") {
      std::istringstream args(value);
      std::string token;
      std::uint64_t pattern = 0;
      for (unsigned i = 0; i < 16; ++i) {
        if (!(args >> token)) throw std::runtime_error("Patterns need exactly 16 steps");
        const auto degree = name == "melody" && token == "-" ? 15.f
          : number_in_range(token, 0, name == "melody" ? 7 : 1);
        if (std::floor(degree) != degree) throw std::runtime_error("Pattern steps must be whole numbers (or - for melody rests)");
        pattern |= std::uint64_t(degree) << (i * (name == "melody" ? 4 : 1));
      }
      if (args >> token) throw std::runtime_error("Patterns need exactly 16 steps");
      commands.push_back({name == "melody" ? Action::melody : Action::bassline, {}, 0, -1, 0, pattern});
    } else if (name == "root" || name == "quantize") {
      const auto number = number_in_range(value, name == "root" ? 36 : 1, name == "root" ? 60 : 64);
      if (std::floor(number) != number) throw std::runtime_error("Root and quantize require whole numbers");
      commands.push_back({name == "root" ? Action::root : Action::quantize, {}, number});
    } else if (name == "style") {
      if (value.empty() || value.size() > 2048) throw std::runtime_error("Style requires 1–2048 characters");
      commands.push_back({Action::style, value});
    } else if (name == "volume" || name == "tempo" || name == "temperature") {
      const auto kind = name == "volume" ? Action::volume : name == "tempo" ? Action::tempo : Action::temperature;
      const auto low = kind == Action::volume ? 0.0f : kind == Action::tempo ? 30.0f : 0.1f;
      const auto high = kind == Action::volume ? 1.0f : kind == Action::tempo ? 240.0f : 2.0f;
      commands.push_back({kind, {}, number_in_range(value, low, high)});
    } else if (name == "drums") {
      if (value != "on" && value != "off") throw std::runtime_error("Use 'drums on' or 'drums off'");
      commands.push_back({Action::drums, {}, value == "on" ? 1.0f : 0.0f});
    } else if (name == "filter" || name == "delay") {
      commands.push_back({name == "filter" ? Action::filter : Action::delay, {},
        number_in_range(value, name == "filter" ? 20 : 0, name == "filter" ? 20000 : 1)});
    } else if (name == "mix") {
      std::istringstream args(value);
      std::string layer, level, extra;
      if (!(args >> layer >> level) || (args >> extra)) throw std::runtime_error("Use mix LAYER 0..1");
      if (layer != "kick" && layer != "clap" && layer != "hats" && layer != "bass" && layer != "lead" && layer != "pad")
        throw std::runtime_error("Mix layers: kick, clap, hats, bass, lead, pad");
      commands.push_back({Action::mix, layer, number_in_range(level, 0, 1)});
    } else {
      if (!value.empty()) throw std::runtime_error("Unexpected argument to " + name);
      if (name == "mute") commands.push_back({Action::mute, {}});
      else if (name == "unmute") commands.push_back({Action::unmute, {}});
      else if (name == "status") commands.push_back({Action::status, {}});
      else if (name == "help") commands.push_back({Action::help, {}});
      else if (name == "quit") commands.push_back({Action::quit, {}});
      else if (name == "cancel") commands.push_back({Action::cancel, {}});
      else throw std::runtime_error("Unknown command: " + name + ". Type help.");
    }
  }
  return commands;
}

void validate_controls(const std::vector<Command>& commands, bool ai, bool watched_file, bool synth) {
  for (const auto& command : commands) {
    if (!synth && (command.bar >= 0 || command.action == Action::melody || command.action == Action::bassline ||
        command.action == Action::root || command.action == Action::quantize || command.action == Action::cancel))
      throw std::runtime_error("Scores, patterns, root, and cancel require --source synth");
    if (command.action == Action::cancel && commands.size() != 1)
      throw std::runtime_error("Use cancel on its own");
    if (watched_file && (command.action == Action::quit || command.action == Action::help || command.action == Action::status))
      throw std::runtime_error("Watched files contain musical controls only; use the terminal for help/status/quit");
    if (ai && command.action == Action::tempo)
      throw std::runtime_error("Exact tempo control is available for synth/demo; describe AI tempo in your style prompt");
    if (!ai && command.action == Action::temperature)
      throw std::runtime_error("Temperature is only available with the AI source");
    if (command.action == Action::mix && !synth) throw std::runtime_error("Instrument mixing requires --source synth");
    if (synth && command.action == Action::style && command.text != "house" && command.text != "trance")
      throw std::runtime_error("Synth styles are house and trance");
    if (!ai && !synth && command.action == Action::style && command.text != "ambient" && command.text != "pulse")
      throw std::runtime_error("Demo styles are 'ambient' and 'pulse'; free-text styles need --source magenta");
  }
}

void apply_control(const Command& c, Source& source, Engine& engine) {
  switch (c.action) {
    case Action::style: source.style(c.text); break;
    case Action::volume: engine.volume(c.number); break;
    case Action::tempo: source.tempo(c.number); engine.tempo(c.number); break;
    case Action::temperature: source.temperature(c.number); break;
    case Action::drums: source.drums(c.number != 0); break;
    case Action::mix: source.mix(c.text, c.number); break;
    case Action::filter: engine.filter(c.number); break;
    case Action::delay: engine.delay(c.number); break;
    case Action::mute: engine.mute(true); break;
    case Action::unmute: engine.mute(false); break;
    case Action::melody: case Action::bassline: case Action::root: source.synth_control(control(c)); break;
    default: break;
  }
}

Score compile_score(const std::vector<Command>& commands, bool replace) {
  Score score;
  score.replace = replace;
  bool quantize = false, timed = false;
  for (const auto& command : commands) {
    if (command.action == Action::cancel) { score.cancel = true; continue; }
    if (command.action == Action::quantize) {
      if (quantize) throw std::runtime_error("Use one quantize directive per score");
      quantize = true; score.quantum = unsigned(command.number); continue;
    }
    const auto ctl = control(command);
    if (ctl.parameter == Parameter::none) continue;
    if (score.count == score.events.size()) throw std::runtime_error("Score exceeds 256 musical controls");
    score.events[score.count++] = {command.bar, command.duration, ctl};
    timed |= command.bar >= 0;
  }
  if (quantize && !timed) throw std::runtime_error("Quantize needs at least one at/ramp command");
  score.replace |= timed;
  std::stable_sort(score.events.begin(), score.events.begin() + score.count,
                   [](const ScoreEvent& a, const ScoreEvent& b) { return a.bar < b.bar; });
  return score;
}

const char* command_help() {
  return "Commands (one per line):\n"
         "  style TEXT        change direction without restarting generation\n"
         "  volume 0..1       smoothly change output gain\n"
         "  drums on|off      allow percussion / request drumless music\n"
         "  mix LAYER 0..1    synth: kick, clap, hats, bass, lead, pad\n"
         "  filter 20..20000  low-pass cutoff in Hz; 20000 opens it fully\n"
         "  delay 0..1        tempo-synced stereo echo level\n"
         "  temperature .1..2 AI sampling temperature\n"
         "  tempo 30..240     synth/demo BPM\n"
         "  melody STEPS     synth: 16 chord degrees 0..7, or - for rests\n"
         "  bassline STEPS   synth: 16 steps, 0 (rest) or 1 (note)\n"
         "  root 36..60      synth: MIDI root; 45 = A minor (default)\n"
         "  at BAR COMMAND   synth: cue relative to the next bar\n"
         "  ramp BAR BARS COMMAND  fade toward a numeric target\n"
         "  quantize BARS    score starts at a 1..64-bar boundary (default 1)\n"
         "  cancel           clear future cues/ramps; keep playing\n"
         "  mute / unmute     keep generating while changing audibility\n"
         "  status / help / quit\n";
}
}  // namespace music
