# Contributing to MetalVR Bridge

## Development Workflow

1. **Pull latest** before starting work: `git pull origin main`
2. **Create a branch** for your work: `git checkout -b milestone-10-wine`
3. **Make changes**, commit often with clear messages
4. **Push your branch**: `git push origin milestone-10-wine`
5. **Open a Pull Request** on GitHub
6. **CI runs automatically** — checks if it compiles on macOS
7. **Merge** after CI passes and code is reviewed

## Commit Message Format

```
[Milestone N] Short description

Longer explanation if needed.
```

Examples:
```
[Milestone 7] Add VkFence implementation with MTLCommandBuffer completion handler
[Milestone 8] Fix swapchain present mode FIFO vsync mapping
[Bugfix] Fix D24_UNORM_S8_UINT format mapping on Apple Silicon
```

## Code Standards

- **Language:** C++20 with Objective-C++ (.mm) for Metal/Cocoa API calls
- **Naming:** `MvBuffer`, `MvImage` for structs. `mvb_CreateBuffer` for Vulkan implementations.
- **Memory:** No raw `new`/`delete`. Use `std::unique_ptr` or pool allocators. ARC for ObjC.
- **Threading:** `MTLDevice` and `MTLCommandQueue` are thread-safe. Encoders are NOT.
- **Errors:** Return accurate `VkResult` codes. Never silently succeed on failure.
- **Logging:** Use `MVB_LOG_DEBUG/INFO/WARN/ERROR` macros.
- **Line length:** Max 120 characters.
- **Dispatch table:** Every new Vulkan function MUST be wired into `vulkan_icd.cpp`.

## File Organization

| File type | Extension | When to use |
|-----------|-----------|-------------|
| Pure C++ | `.cpp` / `.h` | No Apple framework calls |
| Objective-C++ | `.mm` / `.h` | Calls Metal, AppKit, CoreFoundation |
| Metal shaders | `.metal` | GPU shader code |
| Swift | `.swift` | Launcher app only |

## Testing Checklist

Before submitting a PR, verify:

- [ ] No duplicate symbols with `vulkan_icd.cpp` stubs
- [ ] All new functions added to ICD dispatch table
- [ ] All `toMv()`/`toVk()` handle casts declared in header
- [ ] No compiler warnings with `-Wall -Wextra`
- [ ] Existing tests still pass (when CI is set up)

## Milestone Workflow

Work on one milestone at a time. Each milestone builds on the previous:

```
Format Table → Shader Translator → Resources → Memory → Commands
→ Pipelines → Descriptors → Sync → Swapchain → Transfers
→ [Triangle Test] → Wine → Utilities → Game Testing
```

Do not skip ahead. Verify each milestone before starting the next.
