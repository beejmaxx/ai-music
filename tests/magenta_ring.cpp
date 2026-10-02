#include <magentart/ring_buffer.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using magentart::core::RingBuffer;
using magentart::core::priming_frame_count;
using magentart::core::write_stereo;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void interrupted_stereo_read(std::size_t capacity) {
  constexpr std::size_t frame = 1920;
  RingBuffer left, right;
  left.set_virtual_capacity(capacity);
  right.set_virtual_capacity(capacity);
  check(left.get_virtual_capacity() == capacity && right.get_virtual_capacity() == capacity,
        "Requested buffer capacity was clamped by the physical ring");
  std::array<float, frame> input_left{}, input_right{};
  for (std::size_t offset = 0; offset < capacity;) {
    const auto count = std::min(frame, capacity - offset);
    for (std::size_t i = 0; i < count; ++i) {
      input_left[i] = float(offset + i + 1);
      input_right[i] = -input_left[i];
    }
    check(write_stereo(left, right, input_left.data(), input_right.data(), count),
          "Could not fill stereo buffers");
    offset += count;
  }
  check(left.available() == capacity && right.available() == capacity &&
        left.free_space() == 0 && right.free_space() == 0, "Full ring capacity was not enforced");
  for (std::size_t i = 0; i < frame; ++i) {
    input_left[i] = float(capacity + i + 1);
    input_right[i] = -input_left[i];
  }
  check(!write_stereo(left, right, input_left.data(), input_right.data(), frame),
        "Full buffers must reject a frame");

  std::array<float, 1536> discarded{};
  check(left.read(discarded.data(), discarded.size()) &&
        right.read(discarded.data(), discarded.size()), "Could not drain initial audio");
  std::array<float, 512> callback_left{}, callback_right{};
  // The audio callback is interrupted between publishing its L and R reads.
  // Only L has enough space for the next 1920-sample model frame at this point.
  check(left.read(callback_left.data(), callback_left.size()), "Left callback read failed");
  const auto left_before = left.available(), right_before = right.available();
  check(left.free_space() >= frame && right.free_space() < frame,
        "Regression must interrupt the callback at the asymmetric capacity boundary");
  check(!write_stereo(left, right, input_left.data(), input_right.data(), frame),
        "Producer must wait for both channels to have space");
  check(left.available() == left_before && right.available() == right_before,
        "Deferred stereo write must not publish either channel");

  check(right.read(callback_right.data(), callback_right.size()), "Right callback read failed");
  for (std::size_t i = 0; i < callback_left.size(); ++i)
    check(callback_left[i] == float(1536 + i + 1) && callback_right[i] == -callback_left[i],
          "Interrupted callback changed channel alignment");
  check(write_stereo(left, right, input_left.data(), input_right.data(), frame),
        "Producer must resume after the paired read completes");
  check(left.available() == right.available(), "Stereo queues diverged");

  // Drain everything and verify every sample and stereo partner survived the
  // interrupted callback, including non-model-frame-aligned 500/1500 ms capacities.
  std::vector<float> output_left(capacity + frame - 2048), output_right(output_left.size());
  check(left.read(output_left.data(), output_left.size()) &&
        right.read(output_right.data(), output_right.size()), "Final stereo drain failed");
  for (std::size_t i = 0; i < output_left.size(); ++i)
    check(output_left[i] == float(2048 + i + 1) && output_right[i] == -output_left[i],
          "Stereo audio was dropped, duplicated, or reordered");
  check(left.available() == 0 && right.available() == 0, "Final drain left unexpected samples");
}

void physical_wrap_roundtrip() {
  constexpr std::size_t frame = 1920, callback = 512, capacity = 240000;
  const auto total = 3 * RingBuffer::kCapacity + 1234;
  RingBuffer left, right;
  left.set_virtual_capacity(capacity);
  right.set_virtual_capacity(capacity);
  std::array<float, frame> input_left{}, input_right{};
  std::array<float, callback> output_left{}, output_right{};
  std::size_t produced = 0, consumed = 0;
  bool backpressure = false;
  while (consumed < total) {
    if (produced < total) {
      const auto count = std::min(frame, total - produced);
      for (std::size_t i = 0; i < count; ++i) {
        input_left[i] = float(produced + i + 1);
        input_right[i] = -input_left[i];
      }
      if (write_stereo(left, right, input_left.data(), input_right.data(), count)) produced += count;
      else backpressure = true;
    }
    check(left.available() == right.available() && left.available() <= capacity,
          "Streaming writes exceeded the configured stereo capacity");
    const auto count = std::min(callback, produced - consumed);
    check(count > 0, "Streaming test made no progress");
    check(left.read(output_left.data(), count) && right.read(output_right.data(), count),
          "Streaming read unexpectedly exhausted the ring");
    for (std::size_t i = 0; i < count; ++i)
      check(output_left[i] == float(consumed + i + 1) && output_right[i] == -output_left[i],
            "Physical ring wrap dropped, duplicated, reordered, or mispaired samples");
    consumed += count;
  }
  check(produced == total && consumed > 2 * RingBuffer::kCapacity && backpressure,
        "Regression must cross the physical boundary repeatedly under backpressure");
  check(left.available() == 0 && right.available() == 0, "Streaming roundtrip did not drain both channels");
}

void bounded_priming() {
  check(priming_frame_count(120 * 48, 1920) == 2, "Minimum buffer priming changed");
  check(priming_frame_count(160 * 48, 1920) == 3, "CPU 160 ms buffer must retain three-frame priming");
  check(priming_frame_count(500 * 48, 1920) == 12, "500 ms buffer must prime twelve actual model frames");
  check(priming_frame_count(640 * 48, 1920) == 15, "640 ms buffer priming must remain below capacity");
  check(priming_frame_count(1500 * 48, 1920) == 37, "1500 ms buffer must prime thirty-seven actual model frames");
  check(priming_frame_count(2000 * 48, 1920) == 49, "2000 ms buffer priming must remain below capacity");
  check(priming_frame_count(5000 * 48, 1920) == 124, "Default 5000 ms buffer must prime 124 actual model frames");
}
}  // namespace

int main() {
  try {
    interrupted_stereo_read(160 * 48);
    interrupted_stereo_read(500 * 48);
    interrupted_stereo_read(1500 * 48);
    interrupted_stereo_read(5000 * 48);
    physical_wrap_roundtrip();
    bounded_priming();
    std::cout << "PASS: 160/500/1500/5000 ms stereo capacity, bounded priming, repeated physical wraps, and interrupted consumption.\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
