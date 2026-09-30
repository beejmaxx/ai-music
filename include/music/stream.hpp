#pragma once
#include "music/ring.hpp"
#include <cstdint>
#include <thread>

namespace music {
// Audio callback -> bounded queue -> pipe writer. No network/file I/O in push().
class StreamOutput {
 public:
  explicit StreamOutput(int fd);
  ~StreamOutput();
  void push(const StereoFrame* frames, std::size_t count) noexcept {
    dropped_.fetch_add(count - queue_.write(frames, count), std::memory_order_relaxed);
  }
  bool failed() const { return failed_.load(); }
  std::uint64_t dropped() const { return dropped_.load(); }
 private:
  void run(std::stop_token stop);
  int fd_;
  Ring<StereoFrame> queue_{48000};
  std::atomic<bool> failed_{false};
  std::atomic<std::uint64_t> dropped_{0};
  std::jthread writer_;
};
}  // namespace music
