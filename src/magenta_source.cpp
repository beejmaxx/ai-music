#include "music/source.hpp"
#include "music/log.hpp"
#include <magentart/detail/autorelease_pool.h>
#include <magentart/realtime_runner.h>
#include <chrono>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#ifdef AI_MUSIC_CPU_ONLY
#include <mlx/compile.h>
#else
#include <mlx/memory.h>
#endif

namespace music {
#ifndef AI_MUSIC_CPU_ONLY
void require_metal_device();
#endif
namespace {
class MagentaSource final : public Source {
 public:
  MagentaSource(const std::string& root, const std::string& prompt, unsigned buffer_ms) {
    magentart::detail::AutoreleasePool pool;
    const auto base = std::filesystem::absolute(root);
    const auto model = base / "models/mrt2_small/mrt2_small.mlxfn";
    const auto state = base / "models/mrt2_small/mrt2_small_state.safetensors";
    const auto resources = base / "resources";
    if (!std::filesystem::is_regular_file(model) || !std::filesystem::is_regular_file(state))
      throw std::runtime_error("Model files are missing. Run python3 scripts/download_models.py (or set --model-dir).");
    for (const auto* asset : {"spm.model", "text_encoder.tflite", "pretrained_vector_quantizer.tflite"})
      if (!std::filesystem::is_regular_file(resources / "musiccoca" / asset))
        throw std::runtime_error(std::string("Missing model asset: ") + asset);
#ifdef AI_MUSIC_CPU_ONLY
    std::cout << "Loading Magenta RealTime 2 small for CPU-only offline generation.\n" << std::flush;
#else
    std::cout << "Loading Magenta RealTime 2 small; the first start compiles GPU kernels.\n" << std::flush;
#endif
    if (!runner_.init_assets(resources.c_str()) || !runner_.load_model(model.c_str(), false))
      throw std::runtime_error("Magenta model initialization failed");
    runner_.set_buffer_size(std::size_t(buffer_ms) * sample_rate / 1000);
    {
      const auto actual = runner_.get_buffer_size();
      music::log_line("[buffer] requested_ms=" + std::to_string(buffer_ms) +
          " actual_ms=" + std::to_string(actual * 1000 / sample_rate) +
          " actual_frames=" + std::to_string(actual));
    }
    runner_.set_volume_db(0);
    const float weights[] = {1};
    runner_.set_blend_weights(weights, 1);
    runner_.set_text_prompt(prompt);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (runner_.get_quantizer_status() != 2) {
      if (runner_.get_text_encoder_status() == 3 || runner_.get_quantizer_status() == 3)
        throw std::runtime_error("The style prompt could not be encoded");
      if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Style encoding timed out");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#ifndef AI_MUSIC_CPU_ONLY
    {
      // Autostart is disabled: no MLX generation worker is running yet.
      music::log_line("[mlx] before_start_active_bytes=" + std::to_string(mlx::core::get_active_memory()) +
          " cache_bytes=" + std::to_string(mlx::core::get_cache_memory()) +
          " peak_bytes=" + std::to_string(mlx::core::get_peak_memory()));
    }
#endif
  }
  void start() override { runner_.start(); }
  void stop() override {
    runner_.stop();
#ifndef AI_MUSIC_CPU_ONLY
    {
      // The MLX producer has joined; the separate prompt encoder uses TFLite.
      music::log_line("[mlx] after_stop_active_bytes=" + std::to_string(mlx::core::get_active_memory()) +
          " cache_bytes=" + std::to_string(mlx::core::get_cache_memory()) +
          " peak_bytes=" + std::to_string(mlx::core::get_peak_memory()));
    }
#endif
  }
  bool read(float* left, float* right, std::size_t frames) noexcept override {
    return runner_.read_audio_stereo(left, right, frames, false);
  }
  bool read_offline(float* left, float* right, std::size_t frames) noexcept override {
    return runner_.read_audio_stereo(left, right, frames, true);
  }
  void style(const std::string& text) override { runner_.set_text_prompt(text); }
  void drums(bool enabled) override { runner_.set_drumless(!enabled); }
  void tempo(float) override { throw std::runtime_error("AI tempo is controlled through the style prompt"); }
  void temperature(float value) override { runner_.set_temperature(value); }
  SourceMetrics metrics() override {
    // Upstream retains diagnostic lines until the host consumes them.
    for (const auto& line : runner_.get_logs()) {
      if (line.find("failed") != std::string::npos || line.find("Failed") != std::string::npos)
        std::cerr << "[model] " << line << '\n';
    }
    const auto m = runner_.get_metrics();
    const auto text_status = runner_.get_text_encoder_status();
    const auto quantizer_status = runner_.get_quantizer_status();
    const auto prompt_status = text_status == 3 || quantizer_status == 3 ? 3
      : text_status == 2 && quantizer_status == 2 ? 2 : 1;
    return {m.total_ms, m.buffer_available, prompt_status};
  }
  const char* name() const override {
#ifdef AI_MUSIC_CPU_ONLY
    return "magenta-rt2-small (AI, CPU offline)";
#else
    return "magenta-rt2-small (AI, Metal)";
#endif
  }
  bool is_ai() const override { return true; }

 private:
  magentart::core::RealtimeRunner runner_;
};
}  // namespace
std::unique_ptr<Source> make_magenta_source(const std::string& root, const std::string& prompt, unsigned buffer_ms) {
  if (const auto* setting = std::getenv("AI_MUSIC_BUFFER_MS")) {
    const std::string_view text(setting);
    unsigned requested = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), requested);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        requested < 120 || requested > 5000)
      throw std::runtime_error("AI_MUSIC_BUFFER_MS must be an integer from 120 to 5000");
    buffer_ms = requested;
  }
#ifndef AI_MUSIC_CPU_ONLY
  require_metal_device();
  unsigned megabytes = 1024;
  if (const auto* setting = std::getenv("AI_MUSIC_WIRED_MB")) {
    const std::string_view text(setting);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), megabytes);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || megabytes > 2048)
      throw std::runtime_error("AI_MUSIC_WIRED_MB must be an integer from 0 to 2048");
  }
  const auto requested = std::size_t(megabytes) * 1024 * 1024;
  // Reserve residency capacity before loading this model's GPU working set.
  // This process-local request may be ignored without residency-set support;
  // AI_MUSIC_WIRED_MB=0 opts out. It does not reserve other apps' GPU memory.
  const auto previous = mlx::core::set_wired_limit(requested);
  music::log_line("[mlx] requested_wired_limit_bytes=" + std::to_string(requested) +
      " previous_wired_limit_bytes=" + std::to_string(previous));
#else
  // Use the CPU kernels already compiled into MLX, with graph simplification.
  // Runtime compilation of fused CPU kernels has a large startup cost here.
  mlx::core::set_compile_mode(mlx::core::CompileMode::no_fuse);
#endif
  return std::make_unique<MagentaSource>(root, prompt, buffer_ms);
}
}  // namespace music
