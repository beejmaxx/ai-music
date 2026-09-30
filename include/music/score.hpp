#pragma once

#include "music/ring.hpp"
#include <array>
#include <atomic>
#include <cstdint>

namespace music {
// Fixed-size messages: parsing and allocation stay on the controller thread.
enum class Parameter { volume, tempo, filter, delay, kick, clap, hats, bass, lead,
                       pad, style, drums, mute, melody, bassline, root, harmony,
                       bassnotes, voice, rhythm, chords, chord_voice, chord_bars, none };
struct Control {
  Parameter parameter = Parameter::none;
  float value = 0;
  std::uint64_t pattern = 0;
};
struct ScoreEvent {
  float bar = -1;  // -1 = immediate; otherwise relative to the quantized start.
  float duration = 0;
  Control control;
};
struct Score {
  std::array<ScoreEvent, 256> events{};
  std::size_t count = 0;
  unsigned quantum = 1;
  bool replace = false, cancel = false;
};
struct ScoreStatus {
  std::uint64_t revision;
  double start_beat;
  double end_beat;
  unsigned remaining;
};
class ScoreTarget {
 public:
  virtual void write_control(const Control&) noexcept = 0;
  virtual float read_control(Parameter) const noexcept = 0;
  virtual ~ScoreTarget() = default;
};
class ScorePlayer {
 public:
  bool submit(const Score& score) { return incoming_.write(&score, 1) == 1; }
  void tick(double beat, ScoreTarget& target) noexcept;
  ScoreStatus status() const {
    for (;;) {
      const auto before = publication_.load();
      if (before & 1) continue;
      const ScoreStatus result{revision_.load(), start_beat_.load(), end_beat_.load(), remaining_.load()};
      if (publication_.load() == before) return result;
    }
  }
 private:
  struct Ramp { Control to; float from = 0; double start = 0, end = 0; bool active = false; };
  void advance(double beat, ScoreTarget& target) noexcept;
  void cancel_parameter(Parameter parameter) noexcept;
  Ring<Score> incoming_{32};
  Score received_, active_;
  std::array<Ramp, static_cast<unsigned>(Parameter::none)> ramps_{};
  std::size_t next_ = 0;
  double origin_ = 0;
  std::atomic<std::uint64_t> revision_{0};
  std::atomic<double> start_beat_{0};
  std::atomic<double> end_beat_{0};
  std::atomic<unsigned> remaining_{0};
  std::atomic<unsigned> publication_{0};
};
}  // namespace music
