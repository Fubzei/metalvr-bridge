# Mac Runtime Smoke Test

This is the canonical day-one runbook for the first real Mac validation pass.
Run the steps in order and do not skip ahead.

## Fastest Path

If you want the least ambiguous first pass, run the automation script from the
repo root:

```bash
bash scripts/mac_runtime_smoke_test.sh
```

The script:

- captures commit, env, and system info
- builds the ICD, tests, and launcher
- opens the launcher for the manual triangle step
- runs `vulkaninfo`
- runs `vkcube`
- writes `summary.md`
- packages the log bundle for the GitHub issue template

Use the detailed manual steps below if you need to run only part of the flow or
debug the automation itself.

## Goal

Prove the project passes these checkpoints on real Mac hardware:

1. Launcher triangle test
2. `vulkaninfo`
3. `vkcube`

If a step fails, stop there, collect logs, and report that boundary before
moving on.

## Host Requirements

- Apple Silicon Mac preferred
- macOS 13 or newer
- Xcode Command Line Tools
- Homebrew

Install prerequisites:

```bash
xcode-select --install
brew install cmake vulkan-headers vulkan-tools
```

## Build Setup

You now have two valid setup paths:

1. Preferred for code changes under active development: build locally on the Mac
2. Faster for a pure smoke test of an already-green commit: download the ICD and
   launcher artifacts from the latest green GitHub Actions run

If you use CI artifacts, download:

- `metalvr-bridge-icd`
- `metalvr-launcher`

The ICD artifact contains a manifest copy rewritten to point at the artifact-local
`libMetalVRBridge.dylib`, so it can be used directly after extraction.

For the most faithful first pass, local build is still recommended:

```bash
export REPO_ROOT="$HOME/metalvr-bridge"
git clone https://github.com/Fubzei/metalvr-bridge.git "$REPO_ROOT"
cd "$REPO_ROOT"
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DMVRVB_BUILD_TESTS=ON
cmake --build . -j"$(sysctl -n hw.ncpu)"
ctest --output-on-failure
cd "$REPO_ROOT/launcher"
bash setup.sh
```

Expected outputs:

- `$REPO_ROOT/build/libMetalVRBridge.dylib`
- `$REPO_ROOT/vulkan_icd.json`
- `$REPO_ROOT/launcher/MetalVR Bridge.app`
- GitHub Actions artifacts named `metalvr-bridge-icd` and `metalvr-launcher` on green runs

## Logging Setup

Create one folder for all smoke-test outputs if you are running the manual path:

```bash
export LOG_DIR="$HOME/metalvr-bridge-logs"
mkdir -p "$LOG_DIR"
export MVRVB_LOG_LEVEL=debug
export MVRVB_LOG_FILE="$LOG_DIR/icd.log"
export VK_ICD_FILENAMES="$REPO_ROOT/vulkan_icd.json"
```

Optional loader-side logging for failures:

```bash
export VK_LOADER_DEBUG=all
```

## Test 1: Launcher Triangle

Open the launcher:

```bash
open "$REPO_ROOT/launcher/MetalVR Bridge.app"
```

In the app:

1. Click `Run Triangle Test`
2. Wait for the test to finish
3. If it fails or partially passes, click `Export`

Expected result:

- the app reports the triangle test as passed
- a triangle preview image is produced
- the launcher can export a diagnostic log

If this fails:

- stop here
- save the launcher-exported diagnostic log
- note whether the failure was Metal device creation, shader compile, pipeline creation, or GPU execution

## Test 2: `vulkaninfo`

Run:

```bash
cd "$REPO_ROOT"
vulkaninfo > "$LOG_DIR/vulkaninfo.txt" 2>&1
echo $? > "$LOG_DIR/vulkaninfo.exit.txt"
```

Expected result:

- exit code `0`
- driver/device output is printed
- `icd.log` contains early runtime boundary logs such as:
  - `vkCreateInstance request`
  - `vkCreateDevice request`
  - `vkCreateDevice OK`

If this fails:

- stop here
- keep `icd.log`, `vulkaninfo.txt`, and `vulkaninfo.exit.txt`
- record whether failure happened before device enumeration, during device creation, or immediately after

## Test 3: `vkcube`

Run:

```bash
cd "$REPO_ROOT"
vkcube > "$LOG_DIR/vkcube.txt" 2>&1
echo $? > "$LOG_DIR/vkcube.exit.txt"
```

Expected result:

- a window opens
- the cube renders and animates
- `icd.log` contains the first-render path logs such as:
  - `CreateGraphicsPipelines`
  - `CreateSwapchainKHR request`
  - `CreateSwapchainKHR OK`
  - `AcquireNextImageKHR OK`
  - `QueueSubmit`
  - `QueuePresentKHR`
  - `Replay BindDescriptorSets`
  - `Replay flushRenderState`
  - `Replay Draw`
  - transfer or sync markers if the app uploads resources first:
    - `Replay CopyBuffer`
    - `Replay CopyBufferToImage`
    - `Replay PipelineBarrier`

If this fails:

- save a screenshot:

```bash
screencapture -x "$LOG_DIR/vkcube_failure.png"
```

- stop here
- keep `icd.log`, `vkcube.txt`, `vkcube.exit.txt`, and the screenshot

## Failure Triage

Use the first failing boundary:

- Launcher triangle fails:
  - likely Metal, shader compilation, or basic GPU execution issue outside the Vulkan ICD path
- Triangle passes but `vulkaninfo` fails:
  - likely loader, instance creation, physical-device enumeration, or logical-device creation
- `vulkaninfo` passes but `vkcube` fails before showing a window:
  - likely swapchain, shader module, or pipeline creation
- `vkcube` shows a window but renders black or corrupted output:
  - likely descriptor binding, command replay, pipeline state, or present sequencing

Useful log markers when narrowing the first break:

- `Replay flushRenderState`
  - confirms graphics state was applied before draw
- `Replay flushDescriptorsRender`
  - confirms descriptor sets were actually flushed to Metal bindings
- `Replay Draw` or `Replay DrawIndexed`
  - confirms a draw call was reached
- `Replay DrawIndirect`, `Replay DrawIndexedIndirect`, or `Replay DispatchIndirect`
  - confirms indirect work reached the replay path
- `Replay CopyBuffer`, `Replay CopyBufferToImage`, `Replay ResolveImage`, `Replay BlitImage`
  - confirms transfer activity leading into the render path
- `Replay ExecuteCommands`
  - confirms secondary command buffers were replayed
- `Replay PipelineBarrier` or `Replay PipelineBarrier2`
  - confirms synchronization breadcrumbs around the failing section

## Report Bundle

When reporting the first Mac smoke-test result, include:

- tested commit SHA
- Mac model
- macOS version
- whether the launcher triangle passed
- whether `vulkaninfo` passed
- whether `vkcube` passed
- these files from `$LOG_DIR`:
  - `icd.log`
  - `vulkaninfo.txt`
  - `vulkaninfo.exit.txt`
  - `vkcube.txt`
  - `vkcube.exit.txt`
  - `vkcube_failure.png` if applicable
- launcher diagnostic export if the launcher test failed

If you use `scripts/mac_runtime_smoke_test.sh`, attach:

- the generated `summary.md`
- the generated bundle directory or `.zip`
- any extra screenshot or launcher export not already copied into the bundle

## Stop Rule

Do not move on to Wine, DXVK, or game testing until:

- launcher triangle passes
- `vulkaninfo` passes
- `vkcube` passes
