#include "music/stream.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>

namespace music {
StreamOutput::StreamOutput(int fd) : fd_(fd) {
  const auto flags = fcntl(fd_, F_GETFL);
  if (flags == -1 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == -1)
    throw std::runtime_error("Cannot open live PCM pipe");
  writer_ = std::jthread([this](std::stop_token stop) { run(stop); });
}
StreamOutput::~StreamOutput() {
  writer_.request_stop();
  writer_.join();
  close(fd_);
}
void StreamOutput::run(std::stop_token stop) {
  std::array<StereoFrame, 512> frames{};
  std::array<unsigned char, 2048> bytes{};
  while (!stop.stop_requested()) {
    const auto count = queue_.read(frames.data(), frames.size());
    if (!count) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
    for (std::size_t i = 0; i < count; ++i) {
      const float channels[] = {frames[i].left, frames[i].right};
      for (unsigned c = 0; c < 2; ++c) {
        const auto value = std::uint16_t(std::int16_t(std::lrint(std::clamp(channels[c], -1.f, 1.f) * 32767)));
        bytes[i * 4 + c * 2] = value & 255;
        bytes[i * 4 + c * 2 + 1] = value >> 8;
      }
    }
    for (std::size_t sent = 0; sent < count * 4 && !stop.stop_requested();) {
      const auto n = write(fd_, bytes.data() + sent, count * 4 - sent);
      if (n > 0) { sent += std::size_t(n); continue; }
      if (n < 0 && errno == EINTR) continue;
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        pollfd out{fd_, POLLOUT, 0};
        poll(&out, 1, 50);
        continue;
      }
      failed_.store(true);
      return;
    }
  }
}
}  // namespace music
