#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdexcept>

namespace music {
void require_metal_device() {
  @autoreleasepool {
    // MLX 0.31.1 indexes device zero without first checking the array length.
    // Detect an inaccessible GPU before entering that code or loading weights.
    NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
    if (devices.count == 0)
      throw std::runtime_error(
        "No Metal GPU is accessible to this process. Magenta's Metal backend cannot start. "
        "Use the optional ai-music-cpu build with --offline, or run the Metal build in an environment with GPU access.");
    id<MTLCommandQueue> queue = [devices[0] newCommandQueue];
    if (!queue)
      throw std::runtime_error("Metal found a GPU but could not create a command queue");
  }
}
}  // namespace music
