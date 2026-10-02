#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace music {
// The silent sink must finish each block by its playback deadline. A stalled
// consumer can leave the inference ring full, so underruns alone miss stalls.
// One render thread records timings; the controller reads atomic snapshots.
class PlaybackTiming {
 public:
  using Clock = std::chrono::steady_clock;

  void record(Clock::time_point completed, Clock::time_point deadline) noexcept {
    if (completed <= deadline) return;
    const auto late = std::chrono::duration_cast<std::chrono::nanoseconds>(completed - deadline).count();
    misses_.fetch_add(1, std::memory_order_relaxed);
    if (std::uint64_t(late) > max_late_ns_.load(std::memory_order_relaxed))
      max_late_ns_.store(late, std::memory_order_relaxed);
  }
  std::uint64_t misses() const noexcept { return misses_.load(std::memory_order_relaxed); }
  double max_late_ms() const noexcept { return double(max_late_ns_.load(std::memory_order_relaxed)) / 1e6; }

 private:
  std::atomic<std::uint64_t> misses_{0}, max_late_ns_{0};
};
}  // namespace music
