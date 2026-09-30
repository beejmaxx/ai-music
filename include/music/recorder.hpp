#pragma once

#include "music/ring.hpp"
#include "music/source.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

namespace music {
class Recorder {
 public:
  explicit Recorder(const std::filesystem::path& path,
                    std::uint64_t segment_frames = std::uint64_t(sample_rate) * 3600);
  ~Recorder();
  void push(const StereoFrame* frames, std::size_t count) noexcept;
  void finish();  // Stop the audio producer before calling this.
  std::uint64_t dropped() const { return dropped_.load(); }
  std::uint64_t written() const { return written_.load(); }
  bool failed() const { return failed_.load(); }
  const std::string& error_after_finish() const { return error_; }

 private:
  void run() noexcept;
  void open_segment();
  void header();
  void close_segment();
  std::filesystem::path path_;
  std::uint64_t segment_limit_, segment_frames_ = 0;
  unsigned segment_ = 1;
  FILE* file_ = nullptr;
  Ring<StereoFrame> queue_{sample_rate * 4};
  std::atomic<bool> running_{true}, failed_{false};
  std::atomic<std::uint64_t> dropped_{0}, written_{0};
  std::thread thread_;
  std::string error_;
};
}  // namespace music
