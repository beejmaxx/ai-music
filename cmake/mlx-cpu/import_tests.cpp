#include <mlx/mlx.h>
#include <mlx/fast_primitives.h>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace mx = mlx::core;
using Fn = std::function<mx::Args(const mx::Args&)>;

// Construct the serialized GPU graph without executing anything on a GPU.
const mx::Stream exported_gpu(17, mx::Device::gpu);

void roundtrip(const std::filesystem::path& dir, const char* name,
               const Fn& graph, const mx::Args& inputs,
               const std::vector<float>& expected,
               const mx::Args& second_inputs = {},
               const std::vector<float>& second_expected = {}) {
  const auto path = (dir / (std::string(name) + ".mlxfn")).string();
  mx::export_function(path, graph, inputs);
  auto imported = mx::import_function(path);
  auto compiled = mx::compile([&](const mx::Args& args) { return imported(args); });
  for (bool compile : {false, true}) {
    for (int run = 0; run < (second_inputs.empty() ? 1 : 2); ++run) {
      const auto& args = run == 0 ? inputs : second_inputs;
      const auto& want = run == 0 ? expected : second_expected;
      auto outputs = compile ? compiled(args) : imported(args);
      mx::eval(outputs);
      if (outputs.size() != 1 || outputs[0].size() != want.size())
        throw std::runtime_error(std::string(name) + ": wrong output size");
      const auto* values = outputs[0].data<float>();
      for (size_t i = 0; i < want.size(); ++i)
        if (!std::isfinite(values[i]) || std::abs(values[i] - want[i]) > 1e-4f)
          throw std::runtime_error(std::string(name) + ": numerical mismatch at " + std::to_string(i));
    }
  }
  std::cout << "PASS " << name << " (imported and compiled)\n";
}

extern "C" MLX_API int ai_music_test_cpu_import(const char* directory) {
  try {
    const std::filesystem::path dir(directory);
    std::filesystem::create_directories(dir);
    if (mx::default_device() != mx::Device(mx::Device::cpu) || mx::is_available(mx::Device::gpu))
      throw std::runtime_error("This test requires the separate CPU-only library");
    const float eps = 1e-5f;
    roundtrip(dir, "rms-affine", [=](const mx::Args& a) {
      return mx::Args{mx::array(a[0].shape(), mx::float32,
        std::make_shared<mx::fast::RMSNorm>(exported_gpu, nullptr, eps), a)};
    }, {mx::array({3.f, 4.f}, {1, 2}), mx::array({2.f, .5f})},
       {6.f / std::sqrt(12.5f + eps), 2.f / std::sqrt(12.5f + eps)});
    roundtrip(dir, "layer-affine", [=](const mx::Args& a) {
      return mx::Args{mx::array(a[0].shape(), mx::float32,
        std::make_shared<mx::fast::LayerNorm>(exported_gpu, nullptr, eps), a)};
    }, {mx::array({1.f, 3.f}, {1, 2}), mx::array({2.f, .5f}), mx::array({.1f, -.2f})},
       {-2.f / std::sqrt(1.f + eps) + .1f, .5f / std::sqrt(1.f + eps) - .2f});
    roundtrip(dir, "layer-no-affine", [=](const mx::Args& a) {
      return mx::Args{mx::array(a[0].shape(), mx::float32,
        std::make_shared<mx::fast::LayerNorm>(exported_gpu, nullptr, eps), a)};
    }, {mx::array({1.f, 3.f}, {1, 2}), mx::array(1.f), mx::array(0.f)},
       {-1.f / std::sqrt(1.f + eps), 1.f / std::sqrt(1.f + eps)});

    const mx::Args qkv = {mx::array({1.f, 0.f}, {1, 1, 1, 2}),
      mx::array({1.f, 0.f, 0.f, 1.f}, {1, 1, 2, 2}),
      mx::array({2.f, 4.f, 6.f, 8.f}, {1, 1, 2, 2})};
    auto attention = [](bool sinks) -> Fn { return [=](const mx::Args& a) {
      return mx::Args{mx::array(a[0].shape(), mx::float32,
        std::make_shared<mx::fast::ScaledDotProductAttention>(
          exported_gpu, nullptr, 1.f, false, sinks, false), a)};
    }; };
    const auto e = std::exp(1.f);
    roundtrip(dir, "attention", attention(false), qkv, {(2*e+6)/(e+1), (4*e+8)/(e+1)});
    auto masked = qkv;
    masked.push_back(mx::array({false, true}, {1, 1, 1, 2}));
    roundtrip(dir, "attention-mask", attention(false), masked, {6.f, 8.f});
    auto sinks = qkv;
    sinks.push_back(mx::array({0.f}));
    roundtrip(dir, "attention-sink", attention(true), sinks, {(2*e+6)/(e+2), (4*e+8)/(e+2)});

    std::vector<float> unpacked;
    for (int i = 0; i < 32; ++i) unpacked.push_back(2.f * (i % 8) - 1.f);
    roundtrip(dir, "dequantize", [](const mx::Args& a) {
      return mx::Args{mx::array({1, 32}, mx::float32,
        std::make_shared<mx::fast::Quantize>(exported_gpu, nullptr,
          32, 4, mx::QuantizationMode::Affine, true), a)};
    }, {mx::array({0x76543210u, 0x76543210u, 0x76543210u, 0x76543210u}, {1, 4}),
        mx::array({2.f}, {1, 1}), mx::array({-1.f}, {1, 1})}, unpacked);

    const auto weights = mx::array({0x76543210u, 0x76543210u, 0x76543210u, 0x76543210u,
                                   0x11111111u, 0x11111111u, 0x11111111u, 0x11111111u}, {2, 4});
    const auto scales = mx::array({2.f, .5f}, {2, 1});
    const auto biases = mx::array({-1.f, .25f}, {2, 1});
    auto matrix = [](const mx::Args& a) {
      return mx::Args{mx::array({1, 2}, mx::float32,
        std::make_shared<mx::QuantizedMatmul>(exported_gpu, 32, 4,
          mx::QuantizationMode::Affine, true), a)};
    };
    roundtrip(dir, "quantized-matrix-dynamic", matrix,
      {mx::ones({1, 32}), weights, scales, biases}, {192.f, 24.f},
      {mx::ones({1, 32}), weights, mx::array({1.f, .5f}, {2, 1}), biases}, {80.f, 24.f});
    roundtrip(dir, "quantized-matrix-constant", [=](const mx::Args& a) {
      return matrix({a[0], weights, scales, biases});
    }, {mx::ones({1, 32})}, {192.f, 24.f}, {mx::full({1, 32}, mx::array(2.f))}, {384.f, 48.f});
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
