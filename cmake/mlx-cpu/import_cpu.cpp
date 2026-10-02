// Lower the fused operations in MRT2's GPU export using MLX's CPU operations.
// This file belongs only to the separate CPU library; no model weights change.
#include <mlx/fast.h>
#include <mlx/fast_primitives.h>
#include <mlx/ops.h>
#include <mlx/transforms.h>
#include <map>
#include <unordered_set>

namespace mlx::core {
std::vector<array> import_cpu_primitive(
    const std::vector<Shape>& shapes, const std::vector<Dtype>& types,
    const std::shared_ptr<Primitive>& prim, const std::vector<array>& inputs,
    std::map<std::vector<uint64_t>, array>& weight_cache) {
  const auto cpu = default_stream(Device::cpu);
  std::vector<array> result;
  // The fused GPU norms encode absent weight/bias as scalar 1/0 placeholders.
  auto affine = [&](size_t index) -> std::optional<array> {
    return inputs.at(index).ndim() == 0 ? std::nullopt
                                      : std::optional<array>(inputs[index]);
  };
  if (const auto* p = dynamic_cast<const fast::RMSNorm*>(prim.get())) {
    result = {fast::rms_norm(inputs.at(0), affine(1), p->state().second, cpu)};
  } else if (const auto* p = dynamic_cast<const fast::LayerNorm*>(prim.get())) {
    result = {fast::layer_norm(inputs.at(0), affine(1), affine(2), p->state().second, cpu)};
  } else if (const auto* p = dynamic_cast<const fast::ScaledDotProductAttention*>(prim.get())) {
    const auto [unused, scale, causal, has_sinks, logsumexp] = p->state();
    (void)unused;
    if (logsumexp)
      throw std::runtime_error("CPU import does not support training attention outputs");
    std::optional<array> mask, sinks;
    if (has_sinks) sinks = inputs.back();
    if (inputs.size() == 4 + size_t(has_sinks)) mask = inputs.at(3);
    else if (inputs.size() != 3 + size_t(has_sinks))
      throw std::runtime_error("Unexpected attention inputs in CPU import");
    result = {fast::scaled_dot_product_attention(
      inputs.at(0), inputs.at(1), inputs.at(2), scale,
      causal ? "causal" : mask ? "array" : "", mask, sinks, cpu)};
  } else if (const auto* p = dynamic_cast<const QuantizedMatmul*>(prim.get())) {
    const auto [group, bits, mode, transpose] = p->state();
    if (mode != QuantizationMode::Affine || inputs.size() != 4)
      throw std::runtime_error("CPU import supports affine matrix weights only");
    // Reuse each expanded weight across the model's unrolled decoder steps.
    // float32 matmul uses Accelerate; the packed CPU kernel is much slower.
    std::vector<uint64_t> key{inputs[1].id(), inputs[2].id(), inputs[3].id(),
                              uint64_t(group), uint64_t(bits)};
    auto found = weight_cache.find(key);
    if (found == weight_cache.end()) {
      auto w = dequantize(inputs[1], astype(inputs[2], float32, cpu),
        astype(inputs[3], float32, cpu), group, bits, "affine",
        std::nullopt, float32, cpu);
      found = weight_cache.emplace(std::move(key), std::move(w)).first;
    }
    auto w = transpose ? swapaxes(found->second, -1, -2, cpu) : found->second;
    result = {astype(matmul(astype(inputs[0], float32, cpu), w, cpu), types.at(0), cpu)};
  } else if (const auto* p = dynamic_cast<const fast::Quantize*>(prim.get());
             p && std::get<4>(p->state())) {
    const auto [unused, group, bits, mode, dequantizing] = p->state();
    (void)unused;
    (void)dequantizing;
    if (mode != QuantizationMode::Affine || inputs.size() != 3)
      throw std::runtime_error("CPU import supports affine dequantization only");
    result = {dequantize(inputs[0], inputs[1], inputs[2], group, bits,
                        "affine", std::nullopt, types.at(0), cpu)};
  } else {
    return array::make_arrays(shapes, types, prim, inputs);
  }
  if (result.size() != shapes.size())
    throw std::runtime_error("CPU import changed the operation's output count");
  for (size_t i = 0; i < result.size(); ++i)
    if (result[i].shape() != shapes[i] || result[i].dtype() != types[i])
      throw std::runtime_error(std::string("CPU import changed output shape/type: ") + prim->name());
  return result;
}

void rebuild_cpu_import_tape(const std::vector<array>& inputs,
                             const std::vector<array>& outputs,
                             std::vector<array>& tape) {
  // Lowering introduces intermediate nodes. Include every dependency exactly
  // once, in topological order, for ImportedFunction's argument replacement.
  std::unordered_set<uintptr_t> seen, input_ids;
  for (const auto& input : inputs) input_ids.insert(input.id());
  tape.clear();
  std::function<void(const array&)> visit = [&](const array& a) {
    if (!seen.insert(a.id()).second) return;
    for (const auto& sibling : a.siblings()) seen.insert(sibling.id());
    if (!input_ids.contains(a.id()))
      for (const auto& input : a.inputs()) visit(input);
    tape.push_back(a);
  };
  for (const auto& output : outputs) visit(output);

  // Materialize only constant inputs to operations that depend on function
  // arguments. This expands fixed weights once, never recurrent model state.
  std::unordered_set<uintptr_t> dynamic = input_ids;
  for (const auto& a : tape) {
    for (const auto& input : a.inputs())
      if (dynamic.contains(input.id())) dynamic.insert(a.id());
    if (dynamic.contains(a.id())) {
      for (const auto& sibling : a.siblings()) dynamic.insert(sibling.id());
      for (const auto& input : a.inputs())
        if (!dynamic.contains(input.id())) eval(input);
    }
  }
  // eval detaches materialized constants. Drop their former intermediates.
  seen.clear();
  tape.clear();
  for (const auto& output : outputs) visit(output);
}
}  // namespace mlx::core
