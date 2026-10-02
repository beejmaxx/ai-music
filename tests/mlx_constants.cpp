#include "music/mlx_constants.hpp"
#include <mlx/mlx.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace mx = mlx::core;

int main() {
  try {
    mx::set_default_device(mx::Device::cpu);
    mx::set_compile_mode(mx::CompileMode::no_fuse);
    std::size_t cached = 0, traces = 0;
    auto fn = mx::compile([&](const std::vector<mx::array>& inputs) {
      ++traces;
      auto weights = mx::multiply(mx::array({1.f, 2.f, 3.f}), mx::array(2.f));
      // Input 0 is conditioning, input 1 is mutable recurrent state. Both must
      // remain dynamic even when their initial values happen to be zero.
      auto state = mx::add(inputs[1], inputs[0]);
      std::vector<mx::array> outputs{mx::multiply(state, weights), state, mx::sum(weights)};
      cached += music::materialize_constants(inputs, outputs);
      return outputs;
    });
    auto state = mx::array({0.f, 0.f, 0.f});
    for (int step = 1; step <= 4; ++step) {
      auto output = fn({mx::array({float(step), float(step), float(step)}), state});
      mx::eval(output);
      const float sum = float(step * (step + 1) / 2);
      for (int i = 0; i < 3; ++i)
        if (output[0].data<float>()[i] != sum * float(2 * (i + 1)))
          throw std::runtime_error("Conditioning or recurrent state was frozen");
      if (output[2].item<float>() != 12.f)
        throw std::runtime_error("Constant output changed");
      state = output[1];
    }
    if (traces != 1 || cached < 2)
      throw std::runtime_error("Expected one trace and cached weight/output calculations");

    // Materializing constants must not freeze a multi-output dynamic primitive.
    auto multi = mx::compile([](const std::vector<mx::array>& inputs) {
      auto result = mx::divmod(inputs[0], mx::array(3));
      std::vector<mx::array> output{result[1], result[0]};
      music::materialize_constants(inputs, output);
      return output;
    });
    for (int value : {5, 10}) {
      auto output = multi({mx::array(value)});
      mx::eval(output);
      if (output[0].item<int>() != value % 3 || output[1].item<int>() != value / 3)
        throw std::runtime_error("Multi-output operation was frozen");
    }
    std::cout << "PASS: fixed computations cached; conditioning, recurrent state, and multi-output operations stay dynamic.\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
