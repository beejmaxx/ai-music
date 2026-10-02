// A standalone Metal computation check, independent of MLX and model weights.
// https://developer.apple.com/documentation/metal/performing-calculations-on-a-gpu
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cmath>
#include <iostream>

int main() {
  @autoreleasepool {
    NSMutableDictionary* report = [@{@"ok": @NO, @"stage": @"device"} mutableCopy];
    auto finish = [&](int code, NSString* error = nil) {
      if (error) report[@"error"] = error;
      NSData* json = [NSJSONSerialization dataWithJSONObject:report
        options:NSJSONWritingPrettyPrinted error:nil];
      std::cout.write(static_cast<const char*>(json.bytes), json.length);
      std::cout << '\n';
      return code;
    };
    @try {
      NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
      report[@"enumerated_devices"] = @(devices.count);
      if (devices.count == 0)
        return finish(2, @"This process cannot see a Metal GPU.");
      // Use the same enumerated device as the pinned MLX backend.
      id<MTLDevice> device = devices[0];
      report[@"device"] = device.name;
      report[@"stage"] = @"command_queue";
      id<MTLCommandQueue> queue = [device newCommandQueue];
      if (!queue) return finish(3, @"Metal could not create a command queue.");

      report[@"stage"] = @"kernel_compile";
      NSString* source = @"#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "kernel void verify_gpu(device float* values [[buffer(0)]], "
        "uint i [[thread_position_in_grid]]) { if (i < 4) values[i] = values[i] * 2.0f + 1.0f; }";
      NSError* error = nil;
      id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
      if (!library) return finish(3, error.localizedDescription ?: @"Metal kernel compilation failed.");
      id<MTLFunction> function = [library newFunctionWithName:@"verify_gpu"];
      if (!function) return finish(3, @"Metal kernel was not found.");
      id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
      if (!pipeline) return finish(3, error.localizedDescription ?: @"Metal pipeline creation failed.");

      report[@"stage"] = @"gpu_calculation";
      const float initial[] = {0, 1, 2, 3};
      id<MTLBuffer> buffer = [device newBufferWithBytes:initial length:sizeof(initial)
        options:MTLResourceStorageModeShared];
      id<MTLCommandBuffer> command = [queue commandBuffer];
      id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
      if (!buffer || !command || !encoder) return finish(3, @"Metal could not allocate the computation resources.");
      [encoder setComputePipelineState:pipeline];
      [encoder setBuffer:buffer offset:0 atIndex:0];
      [encoder dispatchThreads:MTLSizeMake(4, 1, 1) threadsPerThreadgroup:MTLSizeMake(4, 1, 1)];
      [encoder endEncoding];
      [command commit];
      [command waitUntilCompleted];
      if (command.status != MTLCommandBufferStatusCompleted)
        return finish(3, command.error.localizedDescription ?: @"The GPU command did not complete.");
      const auto* actual = static_cast<const float*>(buffer.contents);
      for (int i = 0; i < 4; ++i)
        if (!std::isfinite(actual[i]) || actual[i] != initial[i] * 2 + 1)
          return finish(3, @"The GPU returned an incorrect calculation.");
      report[@"ok"] = @YES;
      report[@"stage"] = @"complete";
      report[@"calculation"] = @"[0,1,2,3] * 2 + 1 = [1,3,5,7]";
      return finish(0);
    } @catch (NSException* error) {
      return finish(3, error.reason ?: @"Metal raised an Objective-C exception.");
    }
  }
}
