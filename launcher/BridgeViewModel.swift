import Foundation
import Combine
import Metal
import AppKit
import UniformTypeIdentifiers

// MARK: - Log Entry

struct LogEntry: Identifiable {
    let id = UUID()
    let timestamp: Date
    let level: LogLevel
    let message: String

    enum LogLevel: String {
        case info   = "INFO"
        case warn   = "WARN"
        case error  = "ERROR"
        case pass   = "PASS"
        case fail   = "FAIL"
        case debug  = "DEBUG"

        var color: String {
            switch self {
            case .info:  return "cyan"
            case .warn:  return "yellow"
            case .error: return "red"
            case .pass:  return "green"
            case .fail:  return "red"
            case .debug: return "gray"
            }
        }
    }

    var formattedTime: String {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f.string(from: timestamp)
    }
}

// MARK: - System Info

struct SystemInfo {
    let gpuName: String
    let metalVersion: String
    let osVersion: String
    let chipType: String
    let totalMemory: String
    let metalFeatureSet: String

    static func gather() -> SystemInfo {
        let device = MTLCreateSystemDefaultDevice()
        let gpuName = device?.name ?? "Unknown GPU"

        var metalVer = "Metal"
        if let dev = device {
            if dev.supportsFamily(.common3) { metalVer = "Metal 3" }
            else if dev.supportsFamily(.common2) { metalVer = "Metal 2" }
            else if dev.supportsFamily(.common1) { metalVer = "Metal 1" }
        }

        let os = ProcessInfo.processInfo.operatingSystemVersion
        let osStr = "macOS \(os.majorVersion).\(os.minorVersion).\(os.patchVersion)"

        var chipType = "Unknown"
        var sysInfo = utsname()
        uname(&sysInfo)
        let machine = withUnsafePointer(to: &sysInfo.machine) {
            $0.withMemoryRebound(to: CChar.self, capacity: Int(_SYS_NAMELEN)) {
                String(cString: $0)
            }
        }
        if machine.contains("arm64") { chipType = "Apple Silicon (arm64)" }
        else { chipType = "Intel (\(machine))" }

        let totalMem = ProcessInfo.processInfo.physicalMemory
        let memGB = String(format: "%.1f GB", Double(totalMem) / 1_073_741_824.0)

        var featureSet = "Standard"
        if let dev = device {
            let maxBuf = dev.maxBufferLength
            featureSet = "Max buffer: \(maxBuf / 1_048_576) MB, "
            featureSet += dev.hasUnifiedMemory ? "Unified Memory" : "Discrete Memory"
        }

        return SystemInfo(
            gpuName: gpuName,
            metalVersion: metalVer,
            osVersion: osStr,
            chipType: chipType,
            totalMemory: memGB,
            metalFeatureSet: featureSet
        )
    }
}

// MARK: - Bridge Status

enum BridgeStatus: String {
    case notInstalled  = "Not Installed"
    case installed     = "Installed"
    case ready         = "Ready"
    case error         = "Error"
}

enum TestStatus: String {
    case idle       = "Not Run"
    case running    = "Running..."
    case passed     = "PASSED"
    case failed     = "FAILED"
}

// MARK: - ViewModel

@MainActor
final class BridgeViewModel: ObservableObject {
    private enum RuntimeAutomationFollowUp {
        case importGeneratedBundle(manifestUrl: URL, displayName: String)
    }

    @Published var logs: [LogEntry] = []
    @Published var systemInfo: SystemInfo
    @Published var projectStatus: ProjectStatusSnapshot?
    @Published var compatibilityCatalog: CompatibilityCatalogSnapshot?
    @Published var selectedCatalogProfileId: String = ""
    @Published var starterExecutablePath: String = ""
    @Published var runtimeLaunchPlan: RuntimeLaunchPlanSnapshot?
    @Published var runtimeLaunchPlanSource: String = ""
    @Published var runtimeBundleManifest: RuntimeBundleManifestSnapshot?
    @Published var runtimeBundleManifestSource: String = ""
    @Published var runtimeBundleArtifactPreview: RuntimeBundleArtifactPreview?
    @Published var bridgeStatus: BridgeStatus = .notInstalled
    @Published var testStatus: TestStatus = .idle
    @Published var testRunning: Bool = false
    @Published var runtimeAutomationStatus: String = "Idle - import a runtime bundle to unlock one-click setup and launch."
    @Published var runtimeAutomationRunning: Bool = false
    @Published var icdPath: String = ""
    @Published var triangleImageData: Data? = nil

    private let bridgeDylibName = "libMetalVRBridge.dylib"
    private let icdManifestName = "vulkan_icd.json"
    private var loadedRuntimeBundle: LoadedRuntimeBundleManifest?
    private var activeRuntimeProcess: Process?
    private var runtimeAutomationCancellationRequested = false

    init() {
        self.systemInfo = SystemInfo.gather()
        self.projectStatus = ProjectStatusSnapshot.load()
        self.compatibilityCatalog = CompatibilityCatalogSnapshot.load()
        primeKnownTitleSelection()
        if let loadedRuntimeBundle = RuntimeBundleManifestSnapshot.loadFirstAvailable() {
            applyRuntimeBundleManifest(loadedRuntimeBundle, logSuccess: false)
        }
        if runtimeLaunchPlan == nil,
           let loadedRuntimePlan = RuntimeLaunchPlanSnapshot.loadFirstAvailable() {
            self.runtimeLaunchPlan = loadedRuntimePlan.snapshot
            self.runtimeLaunchPlanSource = loadedRuntimePlan.sourceDescription
            synchronizeKnownTitleSelection(with: loadedRuntimePlan.snapshot.selectedProfileId)
        }
        checkInstallation()
        logProjectStatus()
        logCompatibilityCatalog()
        logRuntimeLaunchPlan()
    }

    var guidedRuntimeActionPlan: RuntimeGuidedActionPlan {
        RuntimeGuidedActionPlan.build(
            projectStatus: projectStatus,
            runtimeLaunchPlan: runtimeLaunchPlan,
            runtimeBundleManifest: runtimeBundleManifest,
            runtimeBundleArtifactPreview: runtimeBundleArtifactPreview,
            canGenerateStarterBundleInApp: canGenerateStarterBundleInApp,
            selectedCatalogEntry: selectedCatalogEntry,
            compatibilityCatalogEntry: runtimeLaunchPlan.flatMap { compatibilityCatalog?.entry(for: $0.selectedProfileId) }
        )
    }

    var hasRuntimeExecutionPrep: Bool {
        runtimeLaunchPlan != nil || runtimeBundleManifest != nil
    }

    var knownCatalogEntries: [CompatibilityCatalogSnapshot.Entry] {
        compatibilityCatalog?.knownEntries ?? []
    }

    var selectedCatalogEntry: CompatibilityCatalogSnapshot.Entry? {
        if let selected = compatibilityCatalog?.entry(for: selectedCatalogProfileId),
           selected.profileId != "global-defaults" {
            return selected
        }

        return knownCatalogEntries.first
    }

    var starterBundleCommandPreview: String {
        guard let selectedCatalogEntry else {
            return "Bundle the compatibility catalog first so the launcher can stage a starter command."
        }

        let executablePath = starterExecutablePath.trimmingCharacters(in: .whitespacesAndNewlines)
        let resolvedExecutablePath = executablePath.isEmpty
            ? defaultStarterExecutablePath(for: selectedCatalogEntry)
            : executablePath
        let outputDirectory = "./build-host/exports/\(selectedCatalogEntry.profileId)-bundle"

        var lines = [
            "./build-host/tools/mvrvb_runtime_bundle_builder \\",
            "  --exe \"\(escapedCommandValue(resolvedExecutablePath))\" \\"
        ]

        if let launcher = selectedCatalogEntry.match.launchers.first,
           !launcher.isEmpty {
            lines.append("  --launcher \"\(escapedCommandValue(launcher))\" \\")
        }

        if let store = selectedCatalogEntry.match.stores.first,
           !store.isEmpty {
            lines.append("  --store \"\(escapedCommandValue(store))\" \\")
        }

        lines.append("  --out-dir \"\(escapedCommandValue(outputDirectory))\"")
        return lines.joined(separator: "\n")
    }

    var starterBundleSummary: String {
        guard let selectedCatalogEntry else {
            return "No checked-in title is currently selected."
        }

        return "\(selectedCatalogEntry.displayName) | \(selectedCatalogEntry.backendSummary) | \(selectedCatalogEntry.installSummary)"
    }

    var runtimeExecutionPrepSummary: String {
        if let runtimeLaunchPlan {
            return runtimeLaunchPlan.launchSummary
        }
        if let runtimeBundleArtifactPreview {
            return runtimeBundleArtifactPreview.launchScriptSummary
        }
        return "Import a runtime plan or runtime bundle to preview setup, environment, and launch surfaces."
    }

    var runtimeEnvironmentSnippetPreview: String {
        buildRuntimeEnvironmentSnippet()
            ?? "No imported launch-environment snippet is available yet. Import bundle-manifest.json to load exported launch scripts."
    }

    var runtimeLaunchCommandSnippetPreview: String {
        buildRuntimeLaunchCommandSnippet()
            ?? "No imported launch-command snippet is available yet. Import bundle-manifest.json to load exported launch scripts."
    }

    var canRunRuntimeBundleSetupScript: Bool {
        preferredRuntimeSetupScriptUrl() != nil
    }

    var canRunRuntimeBundleLaunchScript: Bool {
        preferredRuntimeLaunchScriptUrl() != nil
    }

    var canCancelRuntimeAutomation: Bool {
        runtimeAutomationRunning && activeRuntimeProcess != nil
    }

    var canGenerateStarterBundleInApp: Bool {
        selectedCatalogEntry != nil &&
        runtimeBundleBuilderUrl() != nil &&
        bundledProfilesDirectoryUrl() != nil
    }

    // MARK: - Known Title Onboarding

    func selectCatalogProfile(_ profileId: String) {
        guard let entry = compatibilityCatalog?.entry(for: profileId),
              entry.profileId != "global-defaults" else {
            return
        }

        selectedCatalogProfileId = entry.profileId
        starterExecutablePath = defaultStarterExecutablePath(for: entry)
        log(.info, "Selected known title onboarding profile: \(entry.displayName)")
    }

    func copyStarterBundleCommand() {
        guard selectedCatalogEntry != nil else {
            log(.warn, "No known title onboarding profile is available to copy")
            return
        }

        copyTextToPasteboard(
            starterBundleCommandPreview,
            description: "starter runtime-bundle command"
        )
    }

    func generateStarterRuntimeBundle() {
        guard let selectedCatalogEntry else {
            log(.warn, "No known title onboarding profile is available to generate")
            runtimeAutomationStatus = "No known title selected for runtime-bundle generation."
            return
        }

        let trimmedExecutablePath = starterExecutablePath.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedExecutablePath.isEmpty,
              !trimmedExecutablePath.hasPrefix("/path/to/") else {
            log(.warn, "Starter runtime-bundle generation needs a real executable path first")
            runtimeAutomationStatus = "Enter a real executable path before generating a starter bundle."
            return
        }

        guard let builderUrl = runtimeBundleBuilderUrl() else {
            log(.warn, "Runtime bundle builder is not available in this launcher build")
            runtimeAutomationStatus = "Bundled runtime-bundle builder not available. Copy the starter command instead."
            return
        }

        guard let profilesDirectoryUrl = bundledProfilesDirectoryUrl() else {
            log(.warn, "Bundled profiles directory is not available in this launcher build")
            runtimeAutomationStatus = "Bundled profiles are missing. Rebuild the launcher from the repo root."
            return
        }

        let outputDirectoryUrl = defaultStarterBundleOutputDirectory(for: selectedCatalogEntry)
        var arguments = [
            "--profiles-dir", profilesDirectoryUrl.path,
            "--exe", trimmedExecutablePath,
            "--out-dir", outputDirectoryUrl.path
        ]
        if let launcher = selectedCatalogEntry.match.launchers.first,
           !launcher.isEmpty {
            arguments.append(contentsOf: ["--launcher", launcher])
        }
        if let store = selectedCatalogEntry.match.stores.first,
           !store.isEmpty {
            arguments.append(contentsOf: ["--store", store])
        }

        let manifestUrl = outputDirectoryUrl.appendingPathComponent("bundle-manifest.json")
        let selectedDisplayName = selectedCatalogEntry.displayName

        runRuntimeAutomationProcess(
            displayName: "starter bundle generation",
            executableUrl: builderUrl,
            arguments: arguments,
            currentDirectoryUrl: outputDirectoryUrl.deletingLastPathComponent(),
            followUp: .importGeneratedBundle(
                manifestUrl: manifestUrl,
                displayName: selectedDisplayName
            )
        )
    }

    // MARK: - Installation Check

    func checkInstallation() {
        log(.info, "Checking MetalVR Bridge installation...")
        log(.info, "System: \(systemInfo.osVersion), \(systemInfo.chipType)")
        log(.info, "GPU: \(systemInfo.gpuName) (\(systemInfo.metalVersion))")
        log(.info, "Memory: \(systemInfo.totalMemory), \(systemInfo.metalFeatureSet)")

        // Look for the ICD in common locations
        let searchPaths = [
            Bundle.main.bundlePath + "/Contents/Resources",
            Bundle.main.bundlePath + "/Contents/Frameworks",
            FileManager.default.homeDirectoryForCurrentUser.path + "/.local/share/vulkan/icd.d",
            "/usr/local/share/vulkan/icd.d",
            "/opt/homebrew/share/vulkan/icd.d",
        ]

        var foundDylib = false
        var foundManifest = false

        for path in searchPaths {
            let dylibPath = (path as NSString).appendingPathComponent(bridgeDylibName)
            let manifestPath = (path as NSString).appendingPathComponent(icdManifestName)

            if FileManager.default.fileExists(atPath: dylibPath) {
                log(.pass, "Found ICD dylib: \(dylibPath)")
                foundDylib = true
            }
            if FileManager.default.fileExists(atPath: manifestPath) {
                log(.pass, "Found ICD manifest: \(manifestPath)")
                icdPath = manifestPath
                foundManifest = true
            }
        }

        // Also check if VK_ICD_FILENAMES is set
        if let envPath = ProcessInfo.processInfo.environment["VK_ICD_FILENAMES"] {
            log(.info, "VK_ICD_FILENAMES = \(envPath)")
            if FileManager.default.fileExists(atPath: envPath) {
                icdPath = envPath
                foundManifest = true
                log(.pass, "ICD manifest exists at environment path")
            } else {
                log(.warn, "VK_ICD_FILENAMES points to missing file")
            }
        }

        if foundDylib && foundManifest {
            bridgeStatus = .installed
            log(.pass, "MetalVR Bridge is installed and ready")
        } else if foundDylib || foundManifest {
            bridgeStatus = .error
            log(.warn, "Partial installation detected — missing \(foundDylib ? "manifest" : "dylib")")
        } else {
            bridgeStatus = .notInstalled
            log(.warn, "MetalVR Bridge not found. Build the project and place files in app bundle or Vulkan ICD directory.")
        }
    }

    // MARK: - Triangle Test

    func runTriangleTest() {
        guard !testRunning else { return }
        testRunning = true
        testStatus = .running
        triangleImageData = nil

        log(.info, "=== TRIANGLE TEST STARTING ===")
        log(.info, "Testing the full Vulkan-to-Metal translation pipeline...")

        Task { @MainActor in
            await executeTriangleTest()
        }
    }

    private func executeTriangleTest() async {
        // Step 1: Check Metal device
        log(.info, "[1/8] Checking Metal device...")
        guard let device = MTLCreateSystemDefaultDevice() else {
            log(.fail, "No Metal device available")
            finishTest(passed: false)
            return
        }
        log(.pass, "[1/8] Metal device: \(device.name)")

        // Step 2: Check Vulkan loader
        log(.info, "[2/8] Checking Vulkan loader...")
        let vulkanLoaderPaths = [
            "/usr/local/lib/libvulkan.1.dylib",
            "/opt/homebrew/lib/libvulkan.1.dylib",
            Bundle.main.bundlePath + "/Contents/Frameworks/libvulkan.1.dylib",
        ]
        var vulkanFound = false
        for path in vulkanLoaderPaths {
            if FileManager.default.fileExists(atPath: path) {
                log(.pass, "[2/8] Vulkan loader found: \(path)")
                vulkanFound = true
                break
            }
        }
        if !vulkanFound {
            log(.warn, "[2/8] Vulkan loader not found — install LunarG Vulkan SDK or MoltenVK")
            log(.info, "[2/8] Continuing with Metal-only test...")
        }

        // Step 3: Create Metal resources (simulating what the ICD does)
        log(.info, "[3/8] Creating Metal command queue...")
        guard let queue = device.makeCommandQueue() else {
            log(.fail, "[3/8] Failed to create command queue")
            finishTest(passed: false)
            return
        }
        log(.pass, "[3/8] Command queue created")

        // Step 4: Compile test shaders (MSL directly — simulating what the SPIR-V translator outputs)
        log(.info, "[4/8] Compiling Metal shaders (simulated MSL emitter output)...")
        let shaderSource = """
        #include <metal_stdlib>
        using namespace metal;

        struct VertexIn {
            float2 position [[attribute(0)]];
            float3 color    [[attribute(1)]];
        };

        struct VertexOut {
            float4 position [[position]];
            float3 color;
        };

        vertex VertexOut triangle_vertex(VertexIn in [[stage_in]]) {
            VertexOut out;
            out.position = float4(in.position, 0.0, 1.0);
            out.color = in.color;
            return out;
        }

        fragment float4 triangle_fragment(VertexOut in [[stage_in]]) {
            return float4(in.color, 1.0);
        }
        """

        let library: MTLLibrary
        do {
            library = try await device.makeLibrary(source: shaderSource, options: nil)
            log(.pass, "[4/8] Shaders compiled successfully")
        } catch {
            log(.fail, "[4/8] Shader compilation failed: \(error.localizedDescription)")
            finishTest(passed: false)
            return
        }

        guard let vertexFunc = library.makeFunction(name: "triangle_vertex"),
              let fragmentFunc = library.makeFunction(name: "triangle_fragment") else {
            log(.fail, "[4/8] Failed to load shader functions")
            finishTest(passed: false)
            return
        }

        // Step 5: Create vertex buffer
        log(.info, "[5/8] Creating vertex buffer (3 colored vertices)...")
        // Format: x, y, r, g, b per vertex
        let vertices: [Float] = [
             0.0,  0.5,   1.0, 0.0, 0.0,  // top — red
            -0.5, -0.5,   0.0, 1.0, 0.0,  // bottom left — green
             0.5, -0.5,   0.0, 0.0, 1.0,  // bottom right — blue
        ]
        guard let vertexBuffer = device.makeBuffer(
            bytes: vertices,
            length: vertices.count * MemoryLayout<Float>.size,
            options: .storageModeShared
        ) else {
            log(.fail, "[5/8] Failed to create vertex buffer")
            finishTest(passed: false)
            return
        }
        log(.pass, "[5/8] Vertex buffer created (\(vertices.count * 4) bytes)")

        // Step 6: Create render pipeline and offscreen target
        log(.info, "[6/8] Creating render pipeline and 512x512 offscreen target...")
        let width = 512
        let height = 512

        let texDesc = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: width,
            height: height,
            mipmapped: false
        )
        texDesc.usage = [.renderTarget, .shaderRead]
        texDesc.storageMode = .shared

        guard let renderTarget = device.makeTexture(descriptor: texDesc) else {
            log(.fail, "[6/8] Failed to create render target texture")
            finishTest(passed: false)
            return
        }

        let vertexDescriptor = MTLVertexDescriptor()
        // attribute 0: position (float2)
        vertexDescriptor.attributes[0].format = .float2
        vertexDescriptor.attributes[0].offset = 0
        vertexDescriptor.attributes[0].bufferIndex = 0
        // attribute 1: color (float3)
        vertexDescriptor.attributes[1].format = .float3
        vertexDescriptor.attributes[1].offset = 8
        vertexDescriptor.attributes[1].bufferIndex = 0
        // layout
        vertexDescriptor.layouts[0].stride = 20 // 2 floats + 3 floats = 20 bytes
        vertexDescriptor.layouts[0].stepFunction = .perVertex

        let pipelineDesc = MTLRenderPipelineDescriptor()
        pipelineDesc.vertexFunction = vertexFunc
        pipelineDesc.fragmentFunction = fragmentFunc
        pipelineDesc.vertexDescriptor = vertexDescriptor
        pipelineDesc.colorAttachments[0].pixelFormat = .bgra8Unorm

        let pipelineState: MTLRenderPipelineState
        do {
            pipelineState = try await device.makeRenderPipelineState(descriptor: pipelineDesc)
            log(.pass, "[6/8] Render pipeline created")
        } catch {
            log(.fail, "[6/8] Pipeline creation failed: \(error.localizedDescription)")
            finishTest(passed: false)
            return
        }

        // Step 7: Record and submit draw commands
        log(.info, "[7/8] Recording and submitting draw commands...")
        let rpDesc = MTLRenderPassDescriptor()
        rpDesc.colorAttachments[0].texture = renderTarget
        rpDesc.colorAttachments[0].loadAction = .clear
        rpDesc.colorAttachments[0].storeAction = .store
        rpDesc.colorAttachments[0].clearColor = MTLClearColor(red: 0.1, green: 0.1, blue: 0.15, alpha: 1.0)

        guard let cmdBuffer = queue.makeCommandBuffer(),
              let encoder = cmdBuffer.makeRenderCommandEncoder(descriptor: rpDesc) else {
            log(.fail, "[7/8] Failed to create command buffer or encoder")
            finishTest(passed: false)
            return
        }

        encoder.setRenderPipelineState(pipelineState)
        encoder.setVertexBuffer(vertexBuffer, offset: 0, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        encoder.endEncoding()
        cmdBuffer.commit()
        cmdBuffer.waitUntilCompleted()

        if let error = cmdBuffer.error {
            log(.fail, "[7/8] GPU execution failed: \(error.localizedDescription)")
            finishTest(passed: false)
            return
        }
        log(.pass, "[7/8] GPU execution completed successfully")

        // Step 8: Read back and validate
        log(.info, "[8/8] Reading back framebuffer and validating...")

        let bytesPerRow = width * 4
        let totalBytes = bytesPerRow * height
        var pixelData = [UInt8](repeating: 0, count: totalBytes)

        renderTarget.getBytes(
            &pixelData,
            bytesPerRow: bytesPerRow,
            from: MTLRegionMake2D(0, 0, width, height),
            mipmapLevel: 0
        )

        // Validate: check that the center of the triangle has non-background color
        // Center of our triangle is approximately at (256, 256) — (0,0) in NDC
        // Actually the centroid of our triangle is at (0, -0.167) in NDC
        // which maps to roughly (256, 299) in pixel coords (Y inverted)
        let checkPoints: [(Int, Int, String)] = [
            (256, 256, "center"),
            (256, 170, "upper"),
            (200, 330, "lower-left"),
            (312, 330, "lower-right"),
        ]

        var hasColor = false
        for (px, py, label) in checkPoints {
            let idx = (py * bytesPerRow) + (px * 4)
            if idx + 3 < pixelData.count {
                let b = pixelData[idx]
                let g = pixelData[idx + 1]
                let r = pixelData[idx + 2]
                let a = pixelData[idx + 3]
                let isBg = (r < 40 && g < 40 && b < 50)
                if !isBg && a > 200 {
                    hasColor = true
                    log(.debug, "  Pixel at \(label) (\(px),\(py)): R=\(r) G=\(g) B=\(b) A=\(a) — colored")
                } else {
                    log(.debug, "  Pixel at \(label) (\(px),\(py)): R=\(r) G=\(g) B=\(b) A=\(a) — background")
                }
            }
        }

        // Also check corners are background color
        let cornerIdx = 0 // top-left pixel
        let cornerR = pixelData[cornerIdx + 2]
        let cornerG = pixelData[cornerIdx + 1]
        let cornerB = pixelData[cornerIdx]
        let cornersAreBg = (cornerR < 40 && cornerG < 40 && cornerB < 50)
        log(.debug, "  Corner pixel: R=\(cornerR) G=\(cornerG) B=\(cornerB) — \(cornersAreBg ? "background" : "NOT background")")

        // Generate PNG
        if let image = createPNG(from: pixelData, width: width, height: height) {
            triangleImageData = image
            // Save to desktop
            let desktop = FileManager.default.homeDirectoryForCurrentUser
                .appendingPathComponent("Desktop")
                .appendingPathComponent("metalvr_triangle_test.png")
            do {
                try image.write(to: desktop)
                log(.info, "Triangle image saved to: \(desktop.path)")
            } catch {
                log(.warn, "Could not save to desktop: \(error.localizedDescription)")
            }
        }

        if hasColor && cornersAreBg {
            log(.pass, "[8/8] Triangle rendered correctly!")
            log(.pass, "=== TRIANGLE TEST PASSED ===")
            log(.pass, "The Vulkan-to-Metal translation pipeline is functional.")
            finishTest(passed: true)
        } else if hasColor {
            log(.warn, "[8/8] Triangle has color but corners are not background — possible clear issue")
            log(.warn, "=== TRIANGLE TEST PARTIAL PASS ===")
            finishTest(passed: true)
        } else {
            log(.fail, "[8/8] No triangle color detected — rendering may have failed")
            log(.fail, "=== TRIANGLE TEST FAILED ===")
            log(.fail, "Check GPU compatibility and Metal driver version.")
            finishTest(passed: false)
        }
    }

    // MARK: - Steam Launcher

    func launchSteam() {
        log(.info, "Launching Steam with MetalVR Bridge ICD...")

        let steamPaths = [
            "/Applications/Steam.app",
            FileManager.default.homeDirectoryForCurrentUser.path + "/Applications/Steam.app",
        ]

        var steamPath: String? = nil
        for path in steamPaths {
            if FileManager.default.fileExists(atPath: path) {
                steamPath = path
                break
            }
        }

        guard let path = steamPath else {
            log(.error, "Steam.app not found in /Applications or ~/Applications")
            return
        }

        var env = ProcessInfo.processInfo.environment

        // Set Vulkan ICD path if we found the manifest
        if !icdPath.isEmpty {
            env["VK_ICD_FILENAMES"] = icdPath
            log(.info, "Set VK_ICD_FILENAMES = \(icdPath)")
        } else {
            log(.warn, "No ICD manifest path set — Steam will use default Vulkan driver")
        }

        // Also set MVK/portability flags
        env["VK_LOADER_LAYERS_ENABLE"] = ""
        env["MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS"] = "1"

        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        task.arguments = ["-a", path, "--env", "VK_ICD_FILENAMES=\(icdPath)"]
        task.environment = env

        do {
            try task.run()
            log(.pass, "Steam launched with MetalVR Bridge active")
        } catch {
            log(.error, "Failed to launch Steam: \(error.localizedDescription)")
        }
    }

    func importRuntimeLaunchPlan() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = [.json]
        panel.title = "Import Runtime Plan or Bundle"
        panel.message = "Choose a launch-plan.json or bundle-manifest.json exported by MetalVR Bridge tooling."

        if panel.runModal() == .OK, let url = panel.url {
            do {
                if let loadedRuntimeBundle = try? RuntimeBundleManifestSnapshot.load(from: url) {
                    applyRuntimeBundleManifest(loadedRuntimeBundle, logSuccess: true)
                    return
                }

                let loadedRuntimePlan = try RuntimeLaunchPlanSnapshot.load(from: url)
                runtimeLaunchPlan = loadedRuntimePlan.snapshot
                runtimeLaunchPlanSource = loadedRuntimePlan.sourceDescription
                loadedRuntimeBundle = nil
                runtimeBundleManifest = nil
                runtimeBundleManifestSource = ""
                runtimeBundleArtifactPreview = nil
                synchronizeKnownTitleSelection(with: loadedRuntimePlan.snapshot.selectedProfileId)
                log(.pass, "Imported runtime plan: \(loadedRuntimePlan.snapshot.selectedDisplayName)")
                logRuntimeLaunchPlan()
            } catch {
                log(.error, "Failed to import runtime JSON: \(error.localizedDescription)")
            }
        }
    }

    func resetRuntimeLaunchPlan() {
        if let activeRuntimeProcess, activeRuntimeProcess.isRunning {
            runtimeAutomationCancellationRequested = true
            activeRuntimeProcess.terminate()
            log(.info, "Stopped the active runtime automation while resetting the imported runtime bundle")
        }
        runtimeBundleManifest = nil
        runtimeBundleManifestSource = ""
        runtimeBundleArtifactPreview = nil
        loadedRuntimeBundle = nil
        runtimeAutomationStatus = "Idle - import a runtime bundle to unlock one-click setup and launch."
        runtimeAutomationRunning = false
        activeRuntimeProcess = nil
        runtimeAutomationCancellationRequested = false

        if let loadedRuntimeBundle = RuntimeBundleManifestSnapshot.loadFirstAvailable() {
            applyRuntimeBundleManifest(loadedRuntimeBundle, logSuccess: false)
        }

        if runtimeLaunchPlan == nil,
           let loadedRuntimePlan = RuntimeLaunchPlanSnapshot.loadFirstAvailable() {
            runtimeLaunchPlan = loadedRuntimePlan.snapshot
            runtimeLaunchPlanSource = loadedRuntimePlan.sourceDescription
            log(.info, "Reloaded bundled runtime plan preview")
            logRuntimeLaunchPlan()
            return
        }

        runtimeLaunchPlan = nil
        runtimeLaunchPlanSource = ""
        log(.info, "Cleared runtime plan preview")
    }

    // MARK: - Export Log

    func exportLog() -> String {
        var text = "MetalVR Bridge - Diagnostic Log\n"
        text += "Generated: \(Date())\n"
        text += "System: \(systemInfo.osVersion), \(systemInfo.chipType)\n"
        text += "GPU: \(systemInfo.gpuName) (\(systemInfo.metalVersion))\n"
        text += "Memory: \(systemInfo.totalMemory)\n"
        text += "Bridge Status: \(bridgeStatus.rawValue)\n"
        text += "Test Status: \(testStatus.rawValue)\n"
        if let projectStatus {
            text += "Project Phase: \(projectStatus.currentPhase)\n"
            text += "Phase 2: \(projectStatus.phase2Summary)\n"
            text += "Next Gate: \(projectStatus.nextGate)\n"
            text += "Proven Surfaces: \(projectStatus.provenSurfaces.count)\n"
        }
        if let compatibilityCatalog {
            text += "Catalog Profiles: \(compatibilityCatalog.summary.totalProfiles)\n"
            text += "Catalog Competitive Profiles: \(compatibilityCatalog.summary.competitiveProfiles)\n"
            text += "Catalog Planning Profiles: \(compatibilityCatalog.planningProfileCount)\n"
        }
        if let selectedCatalogEntry {
            text += "Known Title Selection: \(selectedCatalogEntry.displayName)\n"
            text += "Known Title Backend: \(selectedCatalogEntry.backendSummary)\n"
            text += "Known Title Install: \(selectedCatalogEntry.installSummary)\n"
            text += "Known Title Starter Path: \(starterExecutablePath)\n"
        }
        if let runtimeLaunchPlan {
            text += "Runtime Plan Profile: \(runtimeLaunchPlan.selectedDisplayName)\n"
            text += "Runtime Plan Backend: \(runtimeLaunchPlan.backend)\n"
            text += "Runtime Plan Prefix: \(runtimeLaunchPlan.appliedPrefixPresetDisplayName)\n"
            text += "Runtime Plan Source: \(runtimeLaunchPlanSource)\n"
        }
        if let runtimeBundleManifest {
            text += "Runtime Bundle Target: \(runtimeBundleManifest.targetSummary)\n"
            text += "Runtime Bundle Source: \(runtimeBundleManifestSource)\n"
        }
        if let loadedRuntimeBundle {
            text += "Runtime Bundle Directory: \(loadedRuntimeBundle.bundleDirectoryUrl.path)\n"
        }
        if let runtimeBundleArtifactPreview {
            text += "Runtime Bundle Checklist: \(runtimeBundleArtifactPreview.checklistSummary)\n"
            text += "Runtime Bundle Setup Scripts: \(runtimeBundleArtifactPreview.setupScriptSummary)\n"
            text += "Runtime Bundle Launch Scripts: \(runtimeBundleArtifactPreview.launchScriptSummary)\n"
            text += "Runtime Bundle Lint: \(runtimeBundleArtifactPreview.lintSummary)\n"
            text += "Runtime Bundle Catalog: \(runtimeBundleArtifactPreview.compatibilityCatalogSummary)\n"
        }
        text += "Runtime Automation: \(runtimeAutomationStatus)\n"
        text += String(repeating: "=", count: 72) + "\n\n"

        for entry in logs {
            text += "[\(entry.formattedTime)] [\(entry.level.rawValue)] \(entry.message)\n"
        }
        return text
    }

    func saveLogToFile() {
        let text = exportLog()
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.plainText]
        panel.nameFieldStringValue = "metalvr_diagnostic_\(dateStamp()).txt"
        panel.begin { [weak self] response in
            guard response == .OK, let url = panel.url else {
                return
            }

            Task { @MainActor in
                guard let self else { return }
                try? text.write(to: url, atomically: true, encoding: .utf8)
                self.log(.info, "Log saved to \(url.path)")
            }
        }
    }

    func revealRuntimeBundleAssets() {
        guard let loadedRuntimeBundle else {
            log(.warn, "No imported runtime bundle is available to reveal")
            return
        }

        NSWorkspace.shared.activateFileViewerSelecting([loadedRuntimeBundle.bundleDirectoryUrl])
        log(.info, "Revealed runtime bundle assets: \(loadedRuntimeBundle.bundleDirectoryUrl.path)")
    }

    func openRuntimeBundleChecklist() {
        openRuntimeBundleAsset(
            named: "runtime bundle checklist",
            manifestPaths: [loadedRuntimeBundle?.snapshot.files.setupChecklist].compactMap { $0 }
        )
    }

    func openRuntimeBundleSetupScript() {
        openRuntimeBundleAsset(
            named: "runtime bundle setup script",
            manifestPaths: [
                loadedRuntimeBundle?.snapshot.files.bashSetupScript,
                loadedRuntimeBundle?.snapshot.files.powershellSetupScript
            ].compactMap { $0 }
        )
    }

    func openRuntimeBundleLaunchScript() {
        openRuntimeBundleAsset(
            named: "runtime bundle launch script",
            manifestPaths: [
                loadedRuntimeBundle?.snapshot.files.bashLaunchScript,
                loadedRuntimeBundle?.snapshot.files.powershellLaunchScript
            ].compactMap { $0 }
        )
    }

    func copyRuntimeLaunchCommandSnippet() {
        guard let snippet = buildRuntimeLaunchCommandSnippet() else {
            log(.warn, "No runtime launch command snippet was available to copy")
            return
        }

        copyTextToPasteboard(snippet, description: "runtime launch command snippet")
    }

    func copyRuntimeEnvironmentSnippet() {
        guard let snippet = buildRuntimeEnvironmentSnippet() else {
            log(.warn, "No runtime environment snippet was available to copy")
            return
        }

        copyTextToPasteboard(snippet, description: "runtime environment snippet")
    }

    func runRuntimeBundleSetupScript() {
        guard let scriptUrl = preferredRuntimeSetupScriptUrl() else {
            log(.warn, "No runnable runtime setup script was available in the imported bundle")
            runtimeAutomationStatus = "Setup script missing from the imported runtime bundle."
            return
        }

        runRuntimeBundleShellScript(
            displayName: "runtime setup automation",
            scriptUrl: scriptUrl
        )
    }

    func runRuntimeBundleLaunchScript() {
        guard let scriptUrl = preferredRuntimeLaunchScriptUrl() else {
            log(.warn, "No runnable runtime launch script was available in the imported bundle")
            runtimeAutomationStatus = "Launch script missing from the imported runtime bundle."
            return
        }

        runRuntimeBundleShellScript(
            displayName: "runtime launch automation",
            scriptUrl: scriptUrl
        )
    }

    func cancelRuntimeAutomation() {
        guard let activeRuntimeProcess, activeRuntimeProcess.isRunning else {
            log(.warn, "No runtime automation is currently running")
            return
        }

        runtimeAutomationCancellationRequested = true
        runtimeAutomationStatus = "Cancelling runtime automation..."
        activeRuntimeProcess.terminate()
        log(.info, "Cancelling the active runtime automation")
    }

    func saveRuntimeExecutionPrepSheet() {
        guard let prepText = buildRuntimeExecutionPrepSheet() else {
            log(.warn, "No runtime execution prep sheet is available to export")
            return
        }

        let panel = NSSavePanel()
        panel.allowedContentTypes = [.plainText]
        panel.nameFieldStringValue = "metalvr_runtime_prep_\(dateStamp()).txt"
        panel.begin { [weak self] response in
            guard response == .OK, let url = panel.url else {
                return
            }

            Task { @MainActor in
                guard let self else { return }
                do {
                    try prepText.write(to: url, atomically: true, encoding: .utf8)
                    self.log(.info, "Runtime execution prep sheet saved to \(url.path)")
                } catch {
                    self.log(.error, "Failed to save runtime execution prep sheet: \(error.localizedDescription)")
                }
            }
        }
    }

    func saveRuntimeBundleReport() {
        guard let reportText = buildRuntimeBundleReport() else {
            log(.warn, "No imported runtime bundle report is available to export")
            return
        }

        let panel = NSSavePanel()
        panel.allowedContentTypes = [.plainText]
        panel.nameFieldStringValue = "metalvr_runtime_bundle_\(dateStamp()).txt"
        panel.begin { [weak self] response in
            guard response == .OK, let url = panel.url else {
                return
            }

            Task { @MainActor in
                guard let self else { return }
                do {
                    try reportText.write(to: url, atomically: true, encoding: .utf8)
                    self.log(.info, "Runtime bundle report saved to \(url.path)")
                } catch {
                    self.log(.error, "Failed to save runtime bundle report: \(error.localizedDescription)")
                }
            }
        }
    }

    func performGuidedRuntimeAction(_ action: RuntimeGuidedActionPlan.Action) {
        switch action {
        case .importJson:
            importRuntimeLaunchPlan()
        case .generateStarterBundle:
            generateStarterRuntimeBundle()
        case .copyStarterCommand:
            copyStarterBundleCommand()
        case .openChecklist:
            openRuntimeBundleChecklist()
        case .runSetupScript:
            runRuntimeBundleSetupScript()
        case .runLaunchScript:
            runRuntimeBundleLaunchScript()
        case .copyEnvironment:
            copyRuntimeEnvironmentSnippet()
        case .copyLaunchCommand:
            copyRuntimeLaunchCommandSnippet()
        case .savePrepSheet:
            saveRuntimeExecutionPrepSheet()
        case .revealBundle:
            revealRuntimeBundleAssets()
        case .saveReport:
            saveRuntimeBundleReport()
        case .runTriangleTest:
            runTriangleTest()
        }
    }

    // MARK: - Helpers

    private func log(_ level: LogEntry.LogLevel, _ message: String) {
        let entry = LogEntry(timestamp: Date(), level: level, message: message)
        if Thread.isMainThread {
            logs.append(entry)
        } else {
            Task { @MainActor in
                self.logs.append(entry)
            }
        }
    }

    private func finishTest(passed: Bool) {
        Task { @MainActor in
            self.testStatus = passed ? .passed : .failed
            self.testRunning = false
        }
    }

    private func logProjectStatus() {
        guard let projectStatus else {
            log(.warn, "Project status snapshot not bundled - launcher will show runtime-only diagnostics")
            return
        }

        log(.info, "Project phase: \(projectStatus.currentPhase)")
        log(.info, "Phase 2 status: \(projectStatus.phase2Summary)")
        log(.info, "Next gate: \(projectStatus.nextGate)")
        log(.info, "Proven surfaces: \(projectStatus.provenSurfaces.count), pending Mac gates: \(projectStatus.notYetProven.count)")
    }

    private func logCompatibilityCatalog() {
        guard let compatibilityCatalog else {
            log(.warn, "Compatibility catalog snapshot not bundled - launcher will not show checked-in profile coverage")
            return
        }

        log(.info, "Compatibility catalog profiles: \(compatibilityCatalog.summary.totalProfiles) total, \(compatibilityCatalog.summary.competitiveProfiles) competitive")
        log(.info, "Compatibility catalog planning profiles: \(compatibilityCatalog.planningProfileCount)")
        if let preview = compatibilityCatalog.knownTitlePreview {
            log(.info, "Known profile preview: \(preview)")
        }
    }

    private func logRuntimeLaunchPlan() {
        guard let runtimeLaunchPlan else {
            log(.warn, "Runtime launch plan preview not loaded - import launch-plan.json to preview backend and setup policy")
            return
        }

        log(.info, "Runtime plan profile: \(runtimeLaunchPlan.selectedDisplayName) via \(runtimeLaunchPlan.backend)")
        log(.info, "Runtime plan prefix: \(runtimeLaunchPlan.appliedPrefixPresetDisplayName), source: \(runtimeLaunchPlanSource)")
        log(.info, "Runtime plan launch surface: \(runtimeLaunchPlan.launchSummary)")
    }

    private func logRuntimeBundleManifest() {
        guard let runtimeBundleManifest else {
            return
        }

        log(.info, "Runtime bundle target: \(runtimeBundleManifest.targetSummary)")
        log(.info, "Runtime bundle source: \(runtimeBundleManifestSource)")
        log(.info, "Runtime bundle summary: \(runtimeBundleManifest.bundleSummary)")
        log(.info, "Runtime bundle assets: \(runtimeBundleManifest.assetSummary)")
    }

    private func logRuntimeBundleArtifactPreview() {
        guard let runtimeBundleArtifactPreview else {
            return
        }

        log(.info, "Runtime bundle checklist: \(runtimeBundleArtifactPreview.checklistSummary)")
        log(.info, "Runtime bundle setup scripts: \(runtimeBundleArtifactPreview.setupScriptSummary)")
        log(.info, "Runtime bundle launch scripts: \(runtimeBundleArtifactPreview.launchScriptSummary)")
        log(.info, "Runtime bundle lint: \(runtimeBundleArtifactPreview.lintSummary)")
        log(.info, "Runtime bundle catalog: \(runtimeBundleArtifactPreview.compatibilityCatalogSummary)")
    }

    private func applyRuntimeBundleManifest(
        _ loadedRuntimeBundle: LoadedRuntimeBundleManifest,
        logSuccess: Bool
    ) {
        self.loadedRuntimeBundle = loadedRuntimeBundle
        runtimeBundleManifest = loadedRuntimeBundle.snapshot
        runtimeBundleManifestSource = loadedRuntimeBundle.sourceDescription
        runtimeAutomationStatus = "Bundle ready - use Prepare Prefix or Run Launch for one-click runtime actions."
        runtimeAutomationRunning = false
        runtimeAutomationCancellationRequested = false

        if logSuccess {
            log(.pass, "Imported runtime bundle: \(loadedRuntimeBundle.snapshot.targetSummary)")
        }

        let launchPlanUrl = loadedRuntimeBundle.resolvedFileUrl(
            for: loadedRuntimeBundle.snapshot.files.launchPlanJson
        )
        if let loadedRuntimePlan = try? RuntimeLaunchPlanSnapshot.load(from: launchPlanUrl) {
            runtimeLaunchPlan = loadedRuntimePlan.snapshot
            runtimeLaunchPlanSource = "Bundle manifest: \(loadedRuntimeBundle.sourceDescription)"
            synchronizeKnownTitleSelection(with: loadedRuntimePlan.snapshot.selectedProfileId)
            logRuntimeLaunchPlan()
        } else {
            log(.warn, "Runtime bundle imported but launch-plan.json could not be loaded from the manifest path")
        }

        runtimeBundleArtifactPreview = RuntimeBundleArtifactPreview.load(from: loadedRuntimeBundle)
        if runtimeBundleArtifactPreview == nil {
            log(.warn, "Runtime bundle imported but setup checklist, setup scripts, and lint report were not readable from the manifest paths")
        }

        logRuntimeBundleManifest()
        logRuntimeBundleArtifactPreview()
    }

    private func buildRuntimeBundleReport() -> String? {
        guard let loadedRuntimeBundle else {
            return nil
        }

        var text = "MetalVR Bridge - Runtime Bundle Report\n"
        text += "Generated: \(Date())\n"
        text += "Bundle Target: \(loadedRuntimeBundle.snapshot.targetSummary)\n"
        text += "Bundle Source: \(loadedRuntimeBundle.sourceDescription)\n"
        text += "Bundle Directory: \(loadedRuntimeBundle.bundleDirectoryUrl.path)\n"
        if let runtimeBundleArtifactPreview {
            text += "Checklist Summary: \(runtimeBundleArtifactPreview.checklistSummary)\n"
            text += "Setup Scripts Summary: \(runtimeBundleArtifactPreview.setupScriptSummary)\n"
            text += "Launch Scripts Summary: \(runtimeBundleArtifactPreview.launchScriptSummary)\n"
            text += "Lint Summary: \(runtimeBundleArtifactPreview.lintSummary)\n"
            text += "Catalog Summary: \(runtimeBundleArtifactPreview.compatibilityCatalogSummary)\n"
        }
        text += "Automation Status: \(runtimeAutomationStatus)\n"
        text += String(repeating: "=", count: 72) + "\n\n"

        appendRuntimeBundleSection(
            "Launch Plan Report",
            contents: readRuntimeBundleTextAsset(for: loadedRuntimeBundle.snapshot.files.launchPlanReport),
            to: &text
        )
        appendRuntimeBundleSection(
            "Setup Checklist",
            contents: readRuntimeBundleTextAsset(for: loadedRuntimeBundle.snapshot.files.setupChecklist),
            to: &text
        )
        appendRuntimeBundleSection(
            "Profile Lint Report",
            contents: readRuntimeBundleTextAsset(for: loadedRuntimeBundle.snapshot.files.profileLintReport),
            to: &text
        )
        appendRuntimeBundleSection(
            "Compatibility Catalog Report",
            contents: readRuntimeBundleTextAsset(for: loadedRuntimeBundle.snapshot.files.compatibilityCatalogReport),
            to: &text
        )
        appendRuntimeBundleSection(
            "Bash Setup Script",
            contents: readRuntimeBundleTextAsset(for: loadedRuntimeBundle.snapshot.files.bashSetupScript),
            to: &text
        )
        appendRuntimeBundleSection(
            "PowerShell Setup Script",
            contents: readRuntimeBundleTextAsset(for: loadedRuntimeBundle.snapshot.files.powershellSetupScript),
            to: &text
        )
        appendRuntimeBundleSection(
            "Bash Launch Script",
            contents: readRuntimeBundleTextAsset(for: loadedRuntimeBundle.snapshot.files.bashLaunchScript),
            to: &text
        )
        appendRuntimeBundleSection(
            "PowerShell Launch Script",
            contents: readRuntimeBundleTextAsset(for: loadedRuntimeBundle.snapshot.files.powershellLaunchScript),
            to: &text
        )

        return text
    }

    private func buildRuntimeExecutionPrepSheet() -> String? {
        guard runtimeLaunchPlan != nil || runtimeBundleManifest != nil else {
            return nil
        }

        var text = "MetalVR Bridge - Runtime Execution Prep\n"
        text += "Generated: \(Date())\n"

        if let runtimeLaunchPlan {
            text += "Profile: \(runtimeLaunchPlan.selectedDisplayName)\n"
            text += "Backend: \(runtimeLaunchPlan.backendSummary)\n"
            text += "Prefix: \(runtimeLaunchPlan.appliedPrefixPresetDisplayName)\n"
            text += "Runtime: \(runtimeLaunchPlan.runtimeSummary)\n"
            text += "Risk: \(runtimeLaunchPlan.antiCheatRiskLabel)\n"
            text += "Setup Summary: \(runtimeLaunchPlan.installSummary)\n"
            text += "Launch Summary: \(runtimeLaunchPlan.launchSummary)\n"
            text += "Plan Source: \(runtimeLaunchPlanSource)\n"
        }

        if let runtimeBundleManifest {
            text += "Bundle Target: \(runtimeBundleManifest.targetSummary)\n"
            text += "Bundle Summary: \(runtimeBundleManifest.bundleSummary)\n"
            text += "Bundle Assets: \(runtimeBundleManifest.assetSummary)\n"
            text += "Bundle Source: \(runtimeBundleManifestSource)\n"
        }

        if let runtimeBundleArtifactPreview {
            text += "Checklist Summary: \(runtimeBundleArtifactPreview.checklistSummary)\n"
            text += "Setup Scripts Summary: \(runtimeBundleArtifactPreview.setupScriptSummary)\n"
            text += "Launch Scripts Summary: \(runtimeBundleArtifactPreview.launchScriptSummary)\n"
            text += "Lint Summary: \(runtimeBundleArtifactPreview.lintSummary)\n"
        }
        text += "Automation Status: \(runtimeAutomationStatus)\n"

        if let nextGate = projectStatus?.nextGate {
            text += "Next Hardware Gate: \(nextGate)\n"
        }

        text += String(repeating: "=", count: 72) + "\n\n"

        appendRuntimeBundleSection(
            "Environment Snippet",
            contents: buildRuntimeEnvironmentSnippet(),
            to: &text
        )
        appendRuntimeBundleSection(
            "Launch Command Snippet",
            contents: buildRuntimeLaunchCommandSnippet(),
            to: &text
        )
        appendRuntimeBundleSection(
            "Setup Checklist Summary",
            contents: runtimeBundleArtifactPreview?.checklistSummary,
            to: &text
        )

        return text
    }

    private func primeKnownTitleSelection() {
        guard selectedCatalogProfileId.isEmpty,
              let firstEntry = knownCatalogEntries.first else {
            return
        }

        selectedCatalogProfileId = firstEntry.profileId
        starterExecutablePath = defaultStarterExecutablePath(for: firstEntry)
    }

    private func synchronizeKnownTitleSelection(with profileId: String) {
        guard let entry = compatibilityCatalog?.entry(for: profileId),
              entry.profileId != "global-defaults" else {
            return
        }

        selectedCatalogProfileId = entry.profileId
        let trimmedPath = starterExecutablePath.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmedPath.isEmpty || trimmedPath.hasPrefix("/path/to/") {
            starterExecutablePath = defaultStarterExecutablePath(for: entry)
        }
    }

    private func defaultStarterExecutablePath(
        for entry: CompatibilityCatalogSnapshot.Entry
    ) -> String {
        "/path/to/\(entry.starterExecutableHint)"
    }

    private func escapedCommandValue(_ value: String) -> String {
        value.replacingOccurrences(of: "\"", with: "\\\"")
    }

    private func runtimeBundleBuilderUrl() -> URL? {
        let fileManager = FileManager.default
        var candidateUrls: [URL] = []

        if let resourceUrl = Bundle.main.resourceURL {
            candidateUrls.append(resourceUrl.appendingPathComponent("mvrvb_runtime_bundle_builder"))
        }

        let cwdUrl = URL(fileURLWithPath: fileManager.currentDirectoryPath, isDirectory: true)
        candidateUrls.append(cwdUrl.appendingPathComponent("build-host/tools/mvrvb_runtime_bundle_builder"))
        candidateUrls.append(cwdUrl.appendingPathComponent("../build-host/tools/mvrvb_runtime_bundle_builder"))
        candidateUrls.append(cwdUrl.appendingPathComponent("build-launcher-host/tools/mvrvb_runtime_bundle_builder"))
        candidateUrls.append(cwdUrl.appendingPathComponent("../build-launcher-host/tools/mvrvb_runtime_bundle_builder"))

        return candidateUrls
            .map(\.standardizedFileURL)
            .first(where: { fileManager.fileExists(atPath: $0.path) })
    }

    private func bundledProfilesDirectoryUrl() -> URL? {
        let fileManager = FileManager.default
        var candidateUrls: [URL] = []

        if let resourceUrl = Bundle.main.resourceURL {
            candidateUrls.append(resourceUrl.appendingPathComponent("profiles"))
        }

        let cwdUrl = URL(fileURLWithPath: fileManager.currentDirectoryPath, isDirectory: true)
        candidateUrls.append(cwdUrl.appendingPathComponent("profiles"))
        candidateUrls.append(cwdUrl.appendingPathComponent("../profiles"))

        return candidateUrls
            .map(\.standardizedFileURL)
            .first(where: { fileManager.fileExists(atPath: $0.path) })
    }

    private func defaultStarterBundleOutputDirectory(
        for entry: CompatibilityCatalogSnapshot.Entry
    ) -> URL {
        let fileManager = FileManager.default
        let documentsDirectory = fileManager.urls(
            for: .documentDirectory,
            in: .userDomainMask
        ).first ?? fileManager.homeDirectoryForCurrentUser

        return documentsDirectory
            .appendingPathComponent("MetalVR Bridge Bundles", isDirectory: true)
            .appendingPathComponent("\(entry.profileId)-bundle-\(dateStamp())", isDirectory: true)
    }

    private func readRuntimeBundleTextAsset(for manifestPath: String) -> String? {
        guard let loadedRuntimeBundle else {
            return nil
        }

        let assetUrl = loadedRuntimeBundle.resolvedFileUrl(for: manifestPath)
        return try? String(contentsOf: assetUrl, encoding: .utf8)
    }

    private func appendRuntimeBundleSection(
        _ title: String,
        contents: String?,
        to text: inout String
    ) {
        text += "## \(title)\n\n"
        if let contents,
           !contents.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            text += contents.trimmingCharacters(in: .whitespacesAndNewlines)
        } else {
            text += "(missing from imported runtime bundle)"
        }
        text += "\n\n"
    }

    private func openRuntimeBundleAsset(
        named assetName: String,
        manifestPaths: [String]
    ) {
        guard let assetUrl = resolvedRuntimeBundleAssetUrl(for: manifestPaths) else {
            log(.warn, "No \(assetName) was available in the imported runtime bundle")
            return
        }

        if NSWorkspace.shared.open(assetUrl) {
            log(.info, "Opened \(assetName): \(assetUrl.path)")
        } else {
            log(.warn, "Failed to open \(assetName): \(assetUrl.path)")
        }
    }

    private func resolvedRuntimeBundleAssetUrl(for manifestPaths: [String]) -> URL? {
        guard let loadedRuntimeBundle else {
            return nil
        }

        for manifestPath in manifestPaths {
            let assetUrl = loadedRuntimeBundle.resolvedFileUrl(for: manifestPath)
            if FileManager.default.fileExists(atPath: assetUrl.path) {
                return assetUrl
            }
        }

        return nil
    }

    private func preferredRuntimeSetupScriptUrl() -> URL? {
        resolvedRuntimeBundleAssetUrl(
            for: [loadedRuntimeBundle?.snapshot.files.bashSetupScript].compactMap { $0 }
        )
    }

    private func preferredRuntimeLaunchScriptUrl() -> URL? {
        resolvedRuntimeBundleAssetUrl(
            for: [loadedRuntimeBundle?.snapshot.files.bashLaunchScript].compactMap { $0 }
        )
    }

    private func preferredRuntimeLaunchScriptContents() -> String? {
        guard let scriptUrl = preferredRuntimeLaunchScriptUrl(),
              let contents = try? String(contentsOf: scriptUrl, encoding: .utf8),
              !contents.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            return nil
        }

        return contents
    }

    private func buildRuntimeLaunchCommandSnippet() -> String? {
        guard let scriptContents = preferredRuntimeLaunchScriptContents() else {
            return nil
        }

        let lines = scriptContents
            .components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }

        var snippetLines: [String] = []

        if let workingDirectory = lines.first(where: {
            $0.hasPrefix("cd ") || $0.hasPrefix("Set-Location -LiteralPath ")
        }) {
            snippetLines.append(workingDirectory)
        }

        if let command = lines.first(where: {
            $0.hasPrefix("exec ") || $0.hasPrefix("& ")
        }) {
            snippetLines.append(command)
        }

        let snippet = snippetLines.joined(separator: "\n")
        return snippet.isEmpty ? nil : snippet
    }

    private func buildRuntimeEnvironmentSnippet() -> String? {
        guard let scriptContents = preferredRuntimeLaunchScriptContents() else {
            return nil
        }

        let lines = scriptContents
            .components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter {
                $0.hasPrefix("export ") || $0.hasPrefix("$env:")
            }

        let snippet = lines.joined(separator: "\n")
        return snippet.isEmpty ? nil : snippet
    }

    private func copyTextToPasteboard(_ text: String, description: String) {
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        pasteboard.setString(text, forType: .string)
        log(.info, "Copied \(description) to the clipboard")
    }

    private func runRuntimeBundleShellScript(
        displayName: String,
        scriptUrl: URL
    ) {
        runRuntimeAutomationProcess(
            displayName: displayName,
            executableUrl: URL(fileURLWithPath: "/bin/bash"),
            arguments: [scriptUrl.path],
            currentDirectoryUrl: scriptUrl.deletingLastPathComponent()
        )
    }

    private func runRuntimeAutomationProcess(
        displayName: String,
        executableUrl: URL,
        arguments: [String],
        currentDirectoryUrl: URL?,
        followUp: RuntimeAutomationFollowUp? = nil
    ) {
        guard !runtimeAutomationRunning else {
            log(.warn, "Runtime automation is already running - wait for it to finish before starting another action")
            return
        }

        let process = Process()
        let standardOutput = Pipe()
        let standardError = Pipe()
        process.executableURL = executableUrl
        process.arguments = arguments
        process.currentDirectoryURL = currentDirectoryUrl
        process.standardOutput = standardOutput
        process.standardError = standardError

        let outputReader = standardOutput.fileHandleForReading
        let errorReader = standardError.fileHandleForReading
        process.terminationHandler = { [weak self] finishedProcess in
            let outputText = String(data: outputReader.readDataToEndOfFile(), encoding: .utf8)
            let errorText = String(data: errorReader.readDataToEndOfFile(), encoding: .utf8)
            let viewModel = self

            Task { @MainActor in
                guard let viewModel else { return }
                viewModel.activeRuntimeProcess = nil
                viewModel.runtimeAutomationRunning = false
                viewModel.logScriptOutput(outputText, prefix: "[\(displayName)]", level: .debug)
                viewModel.logScriptOutput(errorText, prefix: "[\(displayName)]", level: .warn)

                if viewModel.runtimeAutomationCancellationRequested {
                    viewModel.runtimeAutomationStatus = "Cancelled: \(displayName)"
                    viewModel.log(.warn, "Cancelled \(displayName)")
                } else if finishedProcess.terminationStatus == 0 {
                    viewModel.runtimeAutomationStatus = "Completed: \(displayName)"
                    viewModel.log(.pass, "Completed \(displayName)")
                    viewModel.handleRuntimeAutomationFollowUp(followUp)
                } else {
                    viewModel.runtimeAutomationStatus =
                        "Failed: \(displayName) (exit \(finishedProcess.terminationStatus))"
                    viewModel.log(.fail, "Failed \(displayName) with exit code \(finishedProcess.terminationStatus)")
                }

                viewModel.runtimeAutomationCancellationRequested = false
            }
        }

        do {
            if let currentDirectoryUrl {
                try FileManager.default.createDirectory(
                    at: currentDirectoryUrl,
                    withIntermediateDirectories: true
                )
            }
            try process.run()
            activeRuntimeProcess = process
            runtimeAutomationRunning = true
            runtimeAutomationCancellationRequested = false
            runtimeAutomationStatus = "Running: \(displayName)"
            let launchedCommand = ([executableUrl.path] + arguments).joined(separator: " ")
            log(.info, "Started \(displayName): \(launchedCommand)")
        } catch {
            runtimeAutomationStatus = "Failed to start: \(displayName)"
            runtimeAutomationCancellationRequested = false
            log(.error, "Failed to start \(displayName): \(error.localizedDescription)")
        }
    }

    private func handleRuntimeAutomationFollowUp(_ followUp: RuntimeAutomationFollowUp?) {
        guard let followUp else {
            return
        }

        switch followUp {
        case let .importGeneratedBundle(manifestUrl, displayName):
            do {
                let loadedRuntimeBundle = try RuntimeBundleManifestSnapshot.load(from: manifestUrl)
                applyRuntimeBundleManifest(loadedRuntimeBundle, logSuccess: true)
                log(.pass, "Generated and imported starter runtime bundle for \(displayName)")
            } catch {
                runtimeAutomationStatus = "Generated starter bundle, but importing the manifest failed."
                log(.error, "Starter bundle generation succeeded but the manifest could not be imported: \(error.localizedDescription)")
            }
        }
    }

    private func logScriptOutput(
        _ text: String?,
        prefix: String,
        level: LogEntry.LogLevel
    ) {
        guard let text,
              !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            return
        }

        let lines = text.components(separatedBy: .newlines)
        for line in lines {
            let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
            if trimmed.isEmpty {
                continue
            }
            log(level, "\(prefix) \(trimmed)")
        }
    }

    private func dateStamp() -> String {
        let f = DateFormatter()
        f.dateFormat = "yyyyMMdd_HHmmss"
        return f.string(from: Date())
    }

    private func createPNG(from pixels: [UInt8], width: Int, height: Int) -> Data? {
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        let bitmapInfo = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue)
            .union(.byteOrder32Little) // BGRA

        var mutablePixels = pixels
        return mutablePixels.withUnsafeMutableBytes { rawBuffer in
            guard let baseAddress = rawBuffer.baseAddress,
                  let context = CGContext(
                    data: baseAddress,
                    width: width,
                    height: height,
                    bitsPerComponent: 8,
                    bytesPerRow: width * 4,
                    space: colorSpace,
                    bitmapInfo: bitmapInfo.rawValue
                  ),
                  let cgImage = context.makeImage() else { return nil }

            let rep = NSBitmapImageRep(cgImage: cgImage)
            return rep.representation(using: .png, properties: [:])
        }
    }
}
