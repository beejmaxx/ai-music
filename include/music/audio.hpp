#pragma once

#include "music/recorder.hpp"
#include "music/effects.hpp"
#include "music/source.hpp"
#include <AudioUnit/AudioUnit.h>
#include <atomic>
#include <cstdint>

namespace music {
class Engine : private ScoreTarget {
 public:
  Engine(Source& source, Recorder* recorder = nullptr) : source_(source), recorder_(recorder) {}
  void render(float* interleaved, std::size_t count, bool offline = false) noexcept;
  void volume(float value) { volume_.store(value); }
  float volume() const { return volume_.load(); }
  void mute(bool value) { muted_.store(value); }
  bool muted() const { return muted_.load(); }
  std::uint64_t frames() const { return frames_.load(); }
  std::uint64_t underruns() const { return underruns_.load(); }
  std::uint64_t invalid_samples() const { return invalid_samples_.load(); }
  float peak() const { return peak_.load(); }
  void filter(float hz) { effects_.filter(hz); }
  void delay(float wet) { effects_.delay(wet); }
  void tempo(float bpm) { effects_.tempo(bpm); }
  bool score(const Score& score) { return score_.submit(score); }
  ScoreStatus score_status() const { return score_.status(); }

 private:
  void write_control(const Control&) noexcept override;
  float read_control(Parameter) const noexcept override;
  Source& source_;
  Recorder* recorder_;
  Effects effects_;
  ScorePlayer score_;
  std::atomic<float> volume_{0.25f}, peak_{0};
  std::atomic<bool> muted_{false};
  std::atomic<std::uint64_t> frames_{0}, underruns_{0}, invalid_samples_{0};
  float gain_ = 0;
};

class AudioOutput {
 public:
  explicit AudioOutput(Engine& engine);
  ~AudioOutput();
  AudioOutput(const AudioOutput&) = delete;
  AudioOutput& operator=(const AudioOutput&) = delete;
  void start();
  void stop() noexcept;

 private:
  static OSStatus callback(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                           UInt32, UInt32, AudioBufferList*);
  AudioUnit unit_ = nullptr;
  Engine& engine_;
};
}  // namespace music
