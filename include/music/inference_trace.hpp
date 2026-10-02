#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <string>
#include <utility>
#include <vector>

namespace music {
struct InferenceTraceRow {
  std::uint64_t run = 0, frame = 0;
  double generation_start_ms = 0, generation_end_ms = 0, generation_ms = 0;
  float prepare_ms = 0, dispatch_ms = 0, evaluate_ms = 0, copy_ms = 0;
  double publication_finished_ms = 0, publication_wait_ms = 0;
  std::uint32_t left_before = 0, right_before = 0;
  std::uint32_t left_after_generation = 0, right_after_generation = 0;
  std::uint32_t left_after_publication = 0, right_after_publication = 0;
  std::uint32_t wait_iterations = 0, keepalive_calls = 0;
  bool generated = false, published = false, keepalive_enabled = true;
  std::uint64_t underruns = 0;
};

// Opt-in producer diagnostics. Reserve once before playback; recording never
// grows the allocation. Only flush after joining the producer thread.
class InferenceTrace {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::size_t capacity = 20000;  // 13 minutes at 25 model frames/s.

  explicit InferenceTrace(std::string path) : path_(std::move(path)) { rows_.reserve(capacity); }
  void begin_run() noexcept {
    if (!run_) origin_ = Clock::now();
    ++run_;
    frame_ = 0;
  }
  double elapsed_ms(Clock::time_point point) const noexcept {
    return std::chrono::duration<double, std::milli>(point - origin_).count();
  }
  void record(InferenceTraceRow row) noexcept {
    row.run = run_;
    row.frame = frame_++;
    if (rows_.size() == capacity) { ++dropped_; return; }
    rows_.push_back(row);
  }
  std::size_t size() const noexcept { return rows_.size(); }
  std::uint64_t dropped() const noexcept { return dropped_; }
  const std::string& path() const noexcept { return path_; }

  bool flush() const noexcept {
    try {
      std::ofstream output(path_);
      if (!output) return false;
      output << "run,frame,generation_start_ms,generation_end_ms,generation_ms,prepare_ms,dispatch_ms,"
                "evaluate_ms,copy_ms,publication_finished_ms,publication_wait_ms,left_before,right_before,"
                "left_after_generation,right_after_generation,left_after_publication,right_after_publication,"
                "wait_iterations,keepalive_calls,generated,published,keepalive_enabled,underruns,trace_dropped_rows\n"
             << std::setprecision(9);
      for (const auto& row : rows_) {
        output << row.run << ',' << row.frame << ',' << row.generation_start_ms << ',' << row.generation_end_ms
               << ',' << row.generation_ms << ',' << row.prepare_ms << ',' << row.dispatch_ms << ',' << row.evaluate_ms
               << ',' << row.copy_ms << ',' << row.publication_finished_ms << ',' << row.publication_wait_ms
               << ',' << row.left_before << ',' << row.right_before << ',' << row.left_after_generation
               << ',' << row.right_after_generation << ',' << row.left_after_publication << ',' << row.right_after_publication
               << ',' << row.wait_iterations << ',' << row.keepalive_calls << ',' << row.generated << ',' << row.published
               << ',' << row.keepalive_enabled << ',' << row.underruns << ',' << dropped_ << '\n';
      }
      output.close();
      return bool(output);
    } catch (...) { return false; }
  }

 private:
  std::string path_;
  std::vector<InferenceTraceRow> rows_;
  Clock::time_point origin_{};
  std::uint64_t run_ = 0, frame_ = 0, dropped_ = 0;
};
}  // namespace music
