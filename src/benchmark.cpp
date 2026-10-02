#include <magentart/detail/autorelease_pool.h>
#include <magentart/mlx_engine.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <pthread.h>
#include <stdexcept>
#include <vector>

namespace music { void require_metal_device(); }

int main(int argc, char** argv) {
  try {
    if (argc != 5) throw std::runtime_error("Usage: music-benchmark MODEL.mlxfn STEPS WARMUP OUTPUT.csv");
    const int steps = std::stoi(argv[2]), warmup = std::stoi(argv[3]);
    if (steps < 1 || steps > 15000 || warmup < 1 || warmup > 1000)
      throw std::runtime_error("Invalid benchmark length");
    music::require_metal_device();
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    using Clock = std::chrono::steady_clock;
    magentart::detail::AutoreleasePool pool;
    magentart::core::MLXEngine engine;
    const auto started = Clock::now();
    if (!engine.load_model(argv[1])) throw std::runtime_error("Model load failed");
    std::ofstream csv(argv[4]);
    if (!csv) throw std::runtime_error("Could not open benchmark CSV");
    csv << "step,warmup,total_ms,prepare_ms,dispatch_ms,evaluate_ms,copy_ms\n" << std::setprecision(9);
    std::array<float, magentart::core::kFrameSamples> left{}, right{};
    std::vector<double> times;
    double energy = 0, peak = 0;
    std::cout << "Warming up " << warmup << " frames...\n" << std::flush;
    auto measured_start = Clock::now();
    for (int step = 0; step < warmup + steps; ++step) {
      magentart::detail::AutoreleasePool frame_pool;
      if (step == warmup) {
        measured_start = Clock::now();
        std::cout << "Measuring " << steps << " frames (40 ms audio per frame)...\n" << std::flush;
      }
      if (!engine.generate_frame(left.data(), right.data())) throw std::runtime_error("Generation failed");
      const auto m = engine.last_metrics();
      csv << step << ',' << (step < warmup) << ',' << m.total_ms << ',' << m.prepare_ms << ','
          << m.dispatch_ms << ',' << m.evaluate_ms << ',' << m.copy_ms << '\n';
      if (step >= warmup) {
        times.push_back(m.total_ms);
        for (const auto& channel : {left, right}) for (float sample : channel) {
          if (!std::isfinite(sample)) throw std::runtime_error("Non-finite audio");
          energy += double(sample) * sample;
          peak = std::max(peak, double(std::abs(sample)));
        }
      }
      if ((step + 1) % 25 == 0) {
        csv.flush();
        std::cout << "Frame " << step + 1 << '/' << warmup + steps << ": " << m.total_ms << " ms\n" << std::flush;
      }
    }
    const double measured = std::chrono::duration<double>(Clock::now() - measured_start).count();
    const double wall = std::chrono::duration<double>(Clock::now() - started).count();
    std::sort(times.begin(), times.end());
    auto percentile = [&](double q) { return times[std::min(times.size() - 1, std::size_t(std::ceil(q * times.size()) - 1))]; };
    const double mean = std::accumulate(times.begin(), times.end(), 0.) / times.size();
    const double rms = std::sqrt(energy / (steps * 2. * left.size()));
    if (rms <= 1e-5) throw std::runtime_error("Audio is effectively silent");
    csv.close();
    auto path = std::filesystem::path(argv[4]);
    path.replace_extension(".json");
    std::ofstream report(path);
    report << std::setprecision(9) << "{\n  \"ok\": true,\n  \"steps\": " << steps
      << ",\n  \"warmup_steps\": " << warmup << ",\n  \"mean_ms\": " << mean
      << ",\n  \"p50_ms\": " << percentile(.5) << ",\n  \"p95_ms\": " << percentile(.95)
      << ",\n  \"max_ms\": " << times.back() << ",\n  \"audio_seconds\": " << steps * .04
      << ",\n  \"measured_seconds\": " << measured << ",\n  \"wall_seconds\": " << wall
      << ",\n  \"realtime_factor\": " << measured / (steps * .04)
      << ",\n  \"rms\": " << rms << ",\n  \"peak\": " << peak << "\n}\n";
    if (!csv || !report) throw std::runtime_error("Could not write benchmark results");
    std::cout << "Mean " << mean << " ms; p95 " << percentile(.95)
              << " ms; budget 40 ms. Report: " << path << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
}
