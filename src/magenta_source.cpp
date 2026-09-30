#include "music/source.hpp"
#include <magentart/detail/autorelease_pool.h>
#include <magentart/realtime_runner.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace music {
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
    std::cout << "Loading Magenta RealTime 2 small; the first start compiles GPU kernels.\n" << std::flush;
    if (!runner_.init_assets(resources.c_str()) || !runner_.load_model(model.c_str()))
      throw std::runtime_error("Magenta model initialization failed");
    runner_.set_buffer_size(std::size_t(buffer_ms) * sample_rate / 1000);
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
  }
  void start() override { runner_.start(); }
  void stop() override { runner_.stop(); }
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
  const char* name() const override { return "magenta-rt2-small (AI)"; }
  bool is_ai() const override { return true; }

 private:
  magentart::core::RealtimeRunner runner_;
};
}  // namespace
std::unique_ptr<Source> make_magenta_source(const std::string& root, const std::string& prompt, unsigned buffer_ms) {
  return std::make_unique<MagentaSource>(root, prompt, buffer_ms);
}
}  // namespace music
