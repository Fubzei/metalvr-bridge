# Milestone Tracker

## Phase 2: Windows Games on Mac (Current)

### ✅ Milestone 0: Format Table
- **Files:** `src/vulkan_layer/format_table/format_table.cpp`, `format_table.h`
- O(1) flat array lookup, 120+ format mappings
- D24_UNORM_S8_UINT remapped for Apple Silicon
- Vertex format table included

### ✅ Milestone 1: SPIR-V → MSL Shader Translator
- **Files:** `src/shader_translator/spirv/`, `msl_emitter/`, `cache/`
- 1A: SPIR-V binary parser with full IR
- 1B: MSL 2.4+ emitter with all stage types
- 1C: Thread-safe LRU shader cache with disk persistence

### ✅ Milestone 2: Resource Creation
- **Files:** `src/vulkan_layer/resources/vk_resources.h`, `vk_resources.mm`
- 33 entry points, all Metal object wrappers

### ✅ Milestone 3: Memory Management
- **Files:** `src/vulkan_layer/memory/vk_memory.h`, `vk_memory.mm`
- 2 memory types (Shared + Private)

### ✅ Milestone 4: Command Buffers
- **Files:** `src/vulkan_layer/commands/vk_commands.h`, `vk_commands.mm`
- Deferred encoding, 1715 lines, ~40 command types

### ✅ Milestone 5: Pipeline State
- **Files:** `src/vulkan_layer/pipeline/vk_pipeline.h`, `vk_pipeline.mm`
- Graphics + compute pipelines, shader cache integration

### ✅ Milestone 6: Descriptor Sets
- **Files:** `src/vulkan_layer/descriptors/vk_descriptors.h`, `vk_descriptors.mm`
- Direct binding approach, per-bind-point state tracking

### ✅ Milestone 7: Synchronization
- **Files:** `src/vulkan_layer/sync/vk_sync.h`, `vk_sync.mm`
- Fences, binary semaphores, timeline semaphores, barriers

### ✅ Milestone 8: Swapchain & Presentation
- **Files:** `src/vulkan_layer/swapchain/vk_swapchain.h`, `vk_swapchain.mm`
- 14 functions, CAMetalLayer, Wine surface support

### ✅ Milestone 9: Transfer Operations
- **Files:** `src/vulkan_layer/transfers/` (integrated in vk_commands.mm)
- All transfer commands, blit scaling, compute-based fill

### 🔲 Milestone 10: Wine + DXVK Integration
- DLL overrides, surface creation from Wine HWND
- IOSurface zero-copy bridge

### 🔲 Milestone 11: Common Utilities
- Logging, threading pools, memory pools, profiler

### 🔲 Milestone 12: Game Testing
- vkcube → DOTA 2 → AAA titles

## Phase 3: VR Support (Deferred)
- OpenVR shim, async timewarp, Vision Pro backend, Quest streaming
- Code exists from Phase 1 but is not wired into game path
