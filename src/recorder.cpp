#include "music/recorder.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace music {
namespace {
void u16(FILE* file, std::uint16_t value) {
  const unsigned char bytes[] = {static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8)};
  if (std::fwrite(bytes, 1, 2, file) != 2) throw std::runtime_error("Cannot write WAV header");
}
void u32(FILE* file, std::uint32_t value) {
  u16(file, value & 0xffff); u16(file, value >> 16);
}
void tag(FILE* file, const char* value) {
  if (std::fwrite(value, 1, 4, file) != 4) throw std::runtime_error("Cannot write WAV header");
}
}  // namespace

Recorder::Recorder(const std::filesystem::path& path, std::uint64_t limit)
    : path_(path), segment_limit_(limit) {
  if (!limit || limit > (UINT32_MAX - 36) / 4) throw std::runtime_error("Invalid WAV segment length");
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
  open_segment();
  try { thread_ = std::thread(&Recorder::run, this); }
  catch (...) { close_segment(); throw; }
}
Recorder::~Recorder() { finish(); }

void Recorder::open_segment() {
  auto path = path_;
  if (segment_ > 1) path = path_.parent_path() / (path_.stem().string() + "-" + std::to_string(segment_) + ".wav");
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0) throw std::runtime_error("Cannot create " + path.string() + ": " + std::strerror(errno));
  file_ = ::fdopen(fd, "wb");
  if (!file_) { ::close(fd); throw std::runtime_error("Cannot open recording stream"); }
  segment_frames_ = 0;
  try { header(); }
  catch (...) { std::fclose(file_); file_ = nullptr; throw; }
}

void Recorder::header() {
  if (std::fseek(file_, 0, SEEK_SET)) throw std::runtime_error("Cannot seek recording");
  const auto size = static_cast<std::uint32_t>(segment_frames_ * 4);
  tag(file_, "RIFF"); u32(file_, size + 36); tag(file_, "WAVE");
  tag(file_, "fmt "); u32(file_, 16); u16(file_, 1); u16(file_, 2);
  u32(file_, sample_rate); u32(file_, sample_rate * 4); u16(file_, 4); u16(file_, 16);
  tag(file_, "data"); u32(file_, size);
  if (std::fflush(file_) || std::fseek(file_, 0, SEEK_END)) throw std::runtime_error("Cannot flush recording");
}

void Recorder::close_segment() {
  if (!file_) return;
  try { header(); }
  catch (...) { std::fclose(file_); file_ = nullptr; throw; }
  const auto result = std::fclose(file_);
  file_ = nullptr;
  if (result) throw std::runtime_error("Cannot close recording");
}

void Recorder::push(const StereoFrame* frames, std::size_t count) noexcept {
  if (failed_.load(std::memory_order_relaxed)) return;
  const auto n = queue_.write(frames, count);
  dropped_.fetch_add(count - n, std::memory_order_relaxed);
}

void Recorder::run() noexcept {
  try {
    std::array<StereoFrame, block_size> frames{};
    std::array<std::int16_t, block_size * 2> pcm{};
    for (;;) {
      const auto n = queue_.read(frames.data(), frames.size());
      if (!n) {
        if (!running_.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      for (std::size_t i = 0; i < n; ++i) {
        const auto encode = [](float v) { return std::int16_t(std::lrint(std::clamp(v, -1.0f, 1.0f) * 32767)); };
        pcm[2 * i] = encode(frames[i].left);
        pcm[2 * i + 1] = encode(frames[i].right);
      }
      std::size_t offset = 0;
      while (offset < n) {
        if (segment_frames_ == segment_limit_) { close_segment(); ++segment_; open_segment(); }
        const auto count = std::min<std::uint64_t>(n - offset, segment_limit_ - segment_frames_);
        const auto before = segment_frames_ / sample_rate;
        if (std::fwrite(pcm.data() + 2 * offset, 4, count, file_) != count)
          throw std::runtime_error("Recording write failed (disk full or unavailable)");
        offset += count;
        segment_frames_ += count;
        written_.fetch_add(count, std::memory_order_relaxed);
        if (segment_frames_ / sample_rate != before) header();
      }
    }
    close_segment();
  } catch (const std::exception& e) {
    error_ = e.what();
    failed_.store(true, std::memory_order_release);
    if (file_) { std::fclose(file_); file_ = nullptr; }
  }
}

void Recorder::finish() {
  running_.store(false, std::memory_order_release);
  if (thread_.joinable()) thread_.join();
}
}  // namespace music
