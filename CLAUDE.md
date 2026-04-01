# MetalVR Bridge — Engineering Rules

## Language & Naming
- C++20 with Objective-C++ (.mm) for Metal/Cocoa API calls
- Structs: MvBuffer, MvImage, MvPipeline, MvFence, MvSemaphore
- Functions: mvb_CreateBuffer, mvb_DestroyBuffer
- Handle casts: toMv(VkHandle) -> MvStruct*, toVk(MvStruct*) -> VkHandle
- Shader translator namespace: mvb::spirv, mvb::msl
- Launcher: Swift/SwiftUI only

## Memory & Safety
- No raw new/delete. std::unique_ptr, std::shared_ptr, or pool allocators.
- ARC for all Objective-C objects.
- Return accurate VkResult codes. Never silently succeed on failure.
- Hot paths (per-draw-call in vk_commands.mm) must be allocation-free after init.

## Threading
- MTLDevice: thread-safe
- MTLCommandQueue: thread-safe
- MTLCommandBuffer: NOT thread-safe (one thread per buffer)
- MTLRenderCommandEncoder: NOT thread-safe

## Integration
- Every new Vulkan function MUST wire into vulkan_icd.cpp dispatch table.
- Remove the stub when wiring a real implementation.
- Include KHR aliases where applicable.

## Commits
- Format: "[Milestone N] description" or "[Fix] description" or "[Verify] description"
- Push after each logical unit of work. Do not batch unrelated changes.
- Max 120 chars per line in source code.

## Files
- .cpp/.h for pure C++ (no Apple frameworks)
- .mm/.h for Objective-C++ (Metal, AppKit, CoreFoundation)
- .metal for GPU shaders
- .swift for launcher app only
