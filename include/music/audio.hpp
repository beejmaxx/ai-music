#pragma once

#include "music/recorder.hpp"
#include "music/effects.hpp"
#include "music/source.hpp"
#include "music/stream.hpp"
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <cstdint>

namespace music {
class Engine : private ScoreTarget {
 public:
  Engine(Source& source, Recorder* recorder = nullptr, StreamOutput* stream = nullptr)
    : source_(source), recorder_(recorder), stream_(stream) {}
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
  StreamOutput* stream_;
  Effects effects_;
  ScorePlayer score_;
  std::atomic<float> volume_{0.25f}, peak_{0};
  std::atomic<bool> muted_{false};
  std::atomic<std::uint64_t> frames_{0}, underruns_{0}, invalid_samples_{0};
  float gain_ = 0;
};

struct AudioPhaseMetrics {
  std::uint64_t callbacks = 0, timestamp_discontinuities = 0, invalid_timestamps = 0;
  std::uint64_t callback_overruns = 0, callback_errors = 0, device_overloads = 0;
  double max_callback_ms = 0;
};

struct AudioOutputMetrics : AudioPhaseMetrics {
  bool overload_monitoring = false, active_started = false;
  AudioPhaseMetrics warmup, active;
};

// The actual render-callback body, separated from device setup for deterministic
// tests. One callback thread owns rendering/timestamps; metrics are atomic reads.
class AudioCallbackState {
 public:
  explicit AudioCallbackState(Engine& engine, bool silent_output = false,
                              std::uint64_t frame_limit = UINT64_MAX, bool initially_active = true)
    : engine_(engine), silent_output_(silent_output), frame_limit_(frame_limit),
      callback_active_(initially_active), activation_requested_(initially_active),
      phase_and_warmup_overloads_(initially_active ? active_bit : 0),
      done_(initially_active && frame_limit == 0) {}
  OSStatus render(const AudioTimeStamp*, UInt32 count, AudioBufferList*,
                  AudioUnitRenderActionFlags* flags = nullptr) noexcept;
  // One-way transition, consumed at the next callback entry. Until then only
  // hardware silence is produced: source, recorder, and frame limit are untouched.
  void activate() noexcept { activation_requested_.store(true, std::memory_order_release); }
  bool active() const noexcept {
    return (phase_and_warmup_overloads_.load(std::memory_order_acquire) & active_bit) != 0;
  }
  void note_device_overload() noexcept;
  bool done() const noexcept { return done_.load(std::memory_order_acquire); }
  // Counts are cumulative and partitioned by callback-entry phase. Timestamps
  // remain continuous across activation; an entry gap is an active-phase error.
  AudioOutputMetrics metrics() const noexcept;

 private:
  struct PhaseCounters {
    std::atomic<std::uint64_t> callbacks{0}, timestamp_discontinuities{0}, invalid_timestamps{0};
    std::atomic<std::uint64_t> callback_overruns{0}, callback_errors{0}, max_callback_ns{0};
    AudioPhaseMetrics snapshot() const noexcept;
  };
  static constexpr std::uint64_t active_bit = std::uint64_t(1) << 63;
  Engine& engine_;
  const bool silent_output_;
  const std::uint64_t frame_limit_;
  std::uint64_t rendered_frames_ = 0;
  bool have_timestamp_ = false, callback_active_;
  double next_sample_time_ = 0;
  std::atomic<bool> activation_requested_;
  // Sharing one atomic gives overload attribution the same boundary as render.
  // A notification racing with activation retries into the active phase.
  std::atomic<std::uint64_t> phase_and_warmup_overloads_, active_device_overloads_{0};
  std::atomic<bool> done_;
  PhaseCounters warmup_, active_;
};

class AudioOutput {
 public:
  explicit AudioOutput(Engine& engine, bool silent_output = false,
                       std::uint64_t frame_limit = UINT64_MAX, bool initially_active = true);
  ~AudioOutput();
  AudioOutput(const AudioOutput&) = delete;
  AudioOutput& operator=(const AudioOutput&) = delete;
  void start();
  void stop() noexcept;
  void activate() noexcept { callback_state_.activate(); }
  bool active() const noexcept { return callback_state_.active(); }
  bool done() const noexcept { return callback_state_.done(); }
  AudioOutputMetrics metrics() const noexcept;

 private:
  static OSStatus callback(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                           UInt32, UInt32, AudioBufferList*);
  static OSStatus overload(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void*);
  static void device_changed(void*, AudioUnit, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement);
  AudioUnit unit_ = nullptr;
  AudioCallbackState callback_state_;
  AudioDeviceID device_ = kAudioObjectUnknown;
  bool overload_listener_ = false, device_listener_ = false;
  std::atomic<bool> device_changed_{false};
};
}  // namespace music
