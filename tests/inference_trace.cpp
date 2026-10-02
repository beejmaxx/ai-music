#include "music/inference_trace.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace

int main() {
  try {
    const auto path = std::filesystem::temp_directory_path() /
        ("music-inference-trace-" + std::to_string(getpid()) + "-" +
         std::to_string(music::InferenceTrace::Clock::now().time_since_epoch().count()) + ".csv");
    music::InferenceTrace trace(path.string());
    std::thread producer([&] {
      trace.begin_run();
      music::InferenceTraceRow row;
      row.generation_ms = 34;
      row.prepare_ms = 1;
      row.dispatch_ms = 2;
      row.evaluate_ms = 30;
      row.copy_ms = 1;
      row.generated = row.published = true;
      row.keepalive_enabled = false;
      row.underruns = 3;
      trace.record(row);
      trace.begin_run();
      for (std::size_t i = 1; i < music::InferenceTrace::capacity + 7; ++i) trace.record(row);
    });
    producer.join();
    check(!std::filesystem::exists(path), "Tracing performed filesystem IO before the worker joined");
    check(trace.size() == music::InferenceTrace::capacity && trace.dropped() == 7,
          "Trace storage exceeded its bound or omitted overflow evidence");
    check(trace.flush(), "Could not flush trace after the worker joined");
    std::ifstream input(path);
    std::string line;
    std::getline(input, line);
    check(line.ends_with(",keepalive_enabled,underruns,trace_dropped_rows"), "CSV is missing diagnostic columns");
    std::size_t rows = 0;
    while (std::getline(input, line)) {
      if (!rows) check(line.starts_with("1,0,"), "Initial worker trace lost its run/frame identity");
      if (rows == 1) check(line.starts_with("2,0,"), "Restart replaced the preceding worker trace");
      check(line.ends_with(",1,1,0,3,7"), "Trace did not preserve publication, settings, underruns, or overflow");
      ++rows;
    }
    check(rows == music::InferenceTrace::capacity, "CSV rows do not match the bounded buffer");
    std::filesystem::remove(path);
    music::InferenceTrace unwritable(path.string() + "/missing.csv");
    check(!unwritable.flush(), "Failed trace writes must be reported");
    std::cout << "PASS: bounded producer tracing, deferred CSV output, restarts, and write failures.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
