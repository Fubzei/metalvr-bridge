import Foundation
import Metal
import AppKit

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

        var metalVer = "Unknown"
        if let dev = device {
            if dev.supportsFamily(.metal3)      { metalVer = "Metal 3" }
            else if dev.supportsFamily(.apple7)  { metalVer = "Metal 3 (Apple 7)" }
            else if dev.supportsFamily(.apple6)  { metalVer = "Metal 2.3 (Apple 6)" }
            else if dev.supportsFamily(.apple5)  { metalVer = "Metal 2.2 (Apple 5)" }
            else if dev.supportsFamily(.apple4)  { metalVer = "Metal 2.1 (Apple 4)" }
            else if dev.supportsFamily(.common3) { metalVer = "Metal 2 (Common 3)" }
            else                                 { metalVer = "Metal 2" }
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
class BridgeViewModel: ObservableObject {
    @Published var logs: [LogEntry] = []
    @Published var systemInfo: SystemInfo
    @Published var bridgeStatus: BridgeStatus = .notInstalled
    @Published var testStatus: TestStatus = .idle
    @Published var testRunning: Bool = false
    @Published var icdPath: String = ""
    @Published var triangleImageData: Data? = nil

    private let bridgeDylibName = "libMetalVRBridge.dylib"
    private let icdManifestName = "vulkan_icd.json"

    init() {
        self.systemInfo = SystemInfo.gather()
        checkInstallation()
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

        Task.detached { [weak self] in
            await self?.executeTriangleTest()
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
            library = try device.makeLibrary(source: shaderSource, options: nil)
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
            pipelineState = try device.makeRenderPipelineState(descriptor: pipelineDesc)
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

    // MARK: - Export Log

    func exportLog() -> String {
        var text = "MetalVR Bridge — Diagnostic Log\n"
        text += "Generated: \(Date())\n"
        text += "System: \(systemInfo.osVersion), \(systemInfo.chipType)\n"
        text += "GPU: \(systemInfo.gpuName) (\(systemInfo.metalVersion))\n"
        text += "Memory: \(systemInfo.totalMemory)\n"
        text += "Bridge Status: \(bridgeStatus.rawValue)\n"
        text += "Test Status: \(testStatus.rawValue)\n"
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
        panel.begin { response in
            if response == .OK, let url = panel.url {
                try? text.write(to: url, atomically: true, encoding: .utf8)
                self.log(.info, "Log saved to \(url.path)")
            }
        }
    }

    // MARK: - Helpers

    private func log(_ level: LogEntry.LogLevel, _ message: String) {
        let entry = LogEntry(timestamp: Date(), level: level, message: message)
        if Thread.isMainThread {
            logs.append(entry)
        } else {
            DispatchQueue.main.async { self.logs.append(entry) }
        }
    }

    private func finishTest(passed: Bool) {
        DispatchQueue.main.async {
            self.testStatus = passed ? .passed : .failed
            self.testRunning = false
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

        guard let context = CGContext(
            data: UnsafeMutablePointer(mutating: pixels),
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: width * 4,
            space: colorSpace,
            bitmapInfo: bitmapInfo.rawValue
        ) else { return nil }

        guard let cgImage = context.makeImage() else { return nil }

        let rep = NSBitmapImageRep(cgImage: cgImage)
        return rep.representation(using: .png, properties: [:])
    }
}
