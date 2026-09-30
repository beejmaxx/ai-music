#pragma once

#include "music/score.hpp"
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>

namespace music {
inline constexpr unsigned sample_rate = 48000;
inline constexpr std::size_t block_size = 512;

struct SourceMetrics {
  float generation_ms = 0;
  std::size_t buffered_frames = 0;
  int prompt_status = 2;  // 0 idle, 1 encoding, 2 ready, 3 failed.
};

// One audio consumer. Lifecycle and commands run on the controller thread.
// read() must never allocate, acquire a mutex, do I/O, or wait for inference.
class Source {
 public:
  virtual ~Source() = default;
  virtual void start() {}
  virtual void stop() {}
  virtual bool read(float* left, float* right, std::size_t frames) noexcept = 0;
  // Optional dry bus bypasses the host's musical filter/echo (synth drums/bass).
  // Other sources send their complete output through the normal effects bus.
  virtual bool read_buses(float* left, float* right, float* dry_left, float* dry_right,
                          std::size_t frames) noexcept {
    for (std::size_t i = 0; i < frames; ++i) dry_left[i] = dry_right[i] = 0;
    return read(left, right, frames);
  }
  // Offline consumer only: may wait for generation. Never use in an audio callback.
  virtual bool read_offline(float* left, float* right, std::size_t frames) noexcept {
    return read(left, right, frames);
  }
  virtual void style(const std::string& text) = 0;
  virtual void drums(bool enabled) = 0;
  virtual void tempo(float bpm) = 0;
  virtual void temperature(float value) = 0;
  virtual void mix(const std::string&, float) { throw std::runtime_error("This source has no instrument mixer"); }
  // Synth transport and prevalidated controls; safe in the audio callback.
  virtual double beat() const noexcept { return 0; }
  // Audio thread only: split rendering before the next sequencer note trigger.
  virtual std::size_t frames_to_tick() const noexcept { return block_size; }
  virtual void synth_control(const Control&) noexcept {}
  virtual float synth_value(Parameter) const noexcept { return 0; }
  virtual SourceMetrics metrics() { return {}; }
  virtual bool is_ai() const { return false; }
  virtual bool is_synth() const { return false; }
  virtual const char* name() const = 0;
};

std::unique_ptr<Source> make_demo_source();
std::unique_ptr<Source> make_synth_source();
std::unique_ptr<Source> make_magenta_source(const std::string& model_root,
                                         const std::string& initial_style,
                                         unsigned buffer_ms);
}  // namespace music
