#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace music {
struct StereoFrame { float left, right; };

// A bounded single-producer / single-consumer queue. Its allocation happens
// before playback; only the audio thread writes and only the disk thread reads.
template <typename T> class Ring {
 public:
  explicit Ring(std::size_t capacity) : data_(capacity + 1) {}
  Ring(const Ring&) = delete;
  Ring& operator=(const Ring&) = delete;

  std::size_t write(const T* src, std::size_t count) noexcept {
    const auto write = write_.load(std::memory_order_relaxed);
    const auto read = read_.load(std::memory_order_acquire);
    const auto free = (read + data_.size() - write - 1) % data_.size();
    const auto n = std::min(count, free);
    const auto first = std::min(n, data_.size() - write);
    std::copy_n(src, first, data_.data() + write);
    std::copy_n(src + first, n - first, data_.data());
    write_.store((write + n) % data_.size(), std::memory_order_release);
    return n;
  }
  std::size_t read(T* dst, std::size_t count) noexcept {
    const auto read = read_.load(std::memory_order_relaxed);
    const auto write = write_.load(std::memory_order_acquire);
    const auto ready = (write + data_.size() - read) % data_.size();
    const auto n = std::min(count, ready);
    const auto first = std::min(n, data_.size() - read);
    std::copy_n(data_.data() + read, first, dst);
    std::copy_n(data_.data(), n - first, dst + first);
    read_.store((read + n) % data_.size(), std::memory_order_release);
    return n;
  }

 private:
  std::vector<T> data_;
  alignas(64) std::atomic<std::size_t> read_{0};
  alignas(64) std::atomic<std::size_t> write_{0};
};
}  // namespace music
