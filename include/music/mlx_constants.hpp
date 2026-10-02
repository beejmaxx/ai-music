#pragma once
#include <mlx/array.h>
#include <mlx/transforms.h>
#include <functional>
#include <unordered_set>
#include <vector>

namespace music {
// Call while tracing, before MLX compiles the graph. Evaluate only subgraphs
// independent of every function argument (including recurrent state). MLX then
// retains their buffers instead of repeating fixed weight transformations.
inline std::size_t materialize_constants(const std::vector<mlx::core::array>& inputs,
                                         const std::vector<mlx::core::array>& outputs) {
  using mlx::core::array;
  std::unordered_set<std::uintptr_t> dynamic, seen, selected;
  for (const auto& input : inputs) dynamic.insert(input.id());
  std::vector<array> constants;
  std::function<void(const array&)> visit = [&](const array& a) {
    if (!seen.insert(a.id()).second) return;
    for (const auto& sibling : a.siblings()) seen.insert(sibling.id());
    if (!dynamic.contains(a.id())) {
      for (const auto& input : a.inputs()) visit(input);
      for (const auto& input : a.inputs())
        if (dynamic.contains(input.id())) { dynamic.insert(a.id()); break; }
    }
    if (dynamic.contains(a.id())) {
      for (const auto& sibling : a.siblings()) dynamic.insert(sibling.id());
      for (const auto& input : a.inputs())
        if (!dynamic.contains(input.id()) && input.has_primitive() && selected.insert(input.id()).second)
          constants.push_back(input);
    }
  };
  for (const auto& output : outputs) {
    visit(output);
    if (!dynamic.contains(output.id()) && output.has_primitive() && selected.insert(output.id()).second)
      constants.push_back(output);
  }
  if (!constants.empty()) mlx::core::eval(constants);
  return constants.size();
}
}  // namespace music
