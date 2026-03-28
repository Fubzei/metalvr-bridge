# MetalVR Bridge

**Run Windows Steam games on Mac by translating Vulkan/DirectX to Apple Metal.**

MetalVR Bridge is a Vulkan Installable Client Driver (ICD) that intercepts Vulkan API calls and translates them to Apple Metal equivalents. Combined with Wine and DXVK, it enables Windows games to run on macOS.

```
Windows Game (.exe)
    → Wine/CrossOver (Win32 → macOS)
    → DXVK (DirectX → Vulkan)
    → MetalVR Bridge ICD (Vulkan → Metal)  ← this project
    → Apple Metal GPU → Screen
```

## Project Status

| Milestone | Module | Status |
|-----------|--------|--------|
| 0 | Format Table (VkFormat ↔ MTLPixelFormat) | ✅ Complete |
| 1 | SPIR-V → MSL Shader Translator | ✅ Complete |
| 2 | Resource Creation (Buffers, Textures, Samplers) | ✅ Complete |
| 3 | Memory Management | ✅ Complete |
| 4 | Command Buffers & Draw Calls | ✅ Complete |
| 5 | Pipeline State | ✅ Complete |
| 6 | Descriptor Sets | ✅ Complete |
| 7 | Synchronization (Fences, Semaphores) | ✅ Complete |
| 8 | Swapchain & Presentation | ✅ Complete |
| 9 | Transfer Operations | ✅ Complete |
| 10 | Wine + DXVK Integration | 🔲 Not started |
| 11 | Common Utilities | 🔲 Not started |
| 12 | Game Testing | 🔲 Not started |

**Current goal:** First successful compile on macOS → vkcube rendering → real game

## Requirements

- macOS 13+ (Sonoma recommended)
- Apple Silicon Mac (M1/M2/M3/M4) or Intel Mac with Metal support
- Xcode Command Line Tools (`xcode-select --install`)
- CMake 3.20+

## Building

```bash
# Clone
git clone https://github.com/YOUR_USERNAME/metalvr-bridge.git
cd metalvr-bridge

# Build the ICD
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)

# The output is libMetalVRBridge.dylib + vulkan_icd.json
```

## Running the Launcher App

The launcher provides a GUI for testing and launching games.

```bash
cd launcher
bash setup.sh
# Creates "MetalVR Bridge.app" — drag to /Applications
```

## Running Games

```bash
# Set the Vulkan ICD
export VK_ICD_FILENAMES=/path/to/vulkan_icd.json

# Vulkan games through Wine
wine Game.exe

# DirectX games through Wine + DXVK
# (install DXVK into Wine prefix first)
export WINEDLLOVERRIDES="d3d11=n;dxgi=n"
wine Game.exe
```

## Architecture

```
src/
├── shader_translator/     # SPIR-V → MSL compilation
│   ├── spirv/             # SPIR-V binary parser
│   ├── msl_emitter/       # MSL code generator
│   ├── cache/             # Shader compilation cache
│   └── geometry_emu/      # Geometry shader emulation (compute-based)
├── vulkan_layer/          # Vulkan ICD implementation
│   ├── icd/               # Dispatch table (200+ entry points)
│   ├── format_table/      # VkFormat ↔ MTLPixelFormat mapping
│   ├── device/            # Physical/logical device
│   ├── resources/         # Buffers, images, samplers, views
│   ├── memory/            # Memory allocation and binding
│   ├── commands/          # Deferred command recording + Metal replay
│   ├── pipeline/          # Graphics/compute pipeline state
│   ├── descriptors/       # Descriptor set management
│   ├── sync/              # Fences, semaphores, barriers
│   ├── swapchain/         # CAMetalLayer presentation
│   └── transfers/         # Copy, blit, fill operations
└── vr_runtime/            # VR support (deferred, not active)
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development guidelines.

## License

MIT License — see [LICENSE](LICENSE)
