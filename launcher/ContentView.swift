import SwiftUI

// MARK: - Main Content View

struct ContentView: View {
    @StateObject private var vm = BridgeViewModel()
    @State private var showingPreview = false

    var body: some View {
        ZStack {
            // Background
            LinearGradient(
                colors: [Color(hex: "0a0a0f"), Color(hex: "0f1019"), Color(hex: "0a0a0f")],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()

            VStack(spacing: 0) {
                // Title bar area
                titleBar

                Divider().background(Color(hex: "1e2030"))

                // Main content
                HStack(spacing: 0) {
                    // Left panel — controls + system info
                    leftPanel
                        .frame(width: 280)

                    Divider().background(Color(hex: "1e2030"))

                    // Right panel — log viewer
                    rightPanel
                }
            }
        }
        .preferredColorScheme(.dark)
    }

    // MARK: - Title Bar

    private var titleBar: some View {
        HStack(spacing: 12) {
            // Icon
            ZStack {
                RoundedRectangle(cornerRadius: 8)
                    .fill(
                        LinearGradient(
                            colors: [Color(hex: "6366f1"), Color(hex: "8b5cf6")],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                    )
                    .frame(width: 32, height: 32)

                Image(systemName: "cube.transparent")
                    .font(.system(size: 16, weight: .semibold))
                    .foregroundColor(.white)
            }

            VStack(alignment: .leading, spacing: 1) {
                Text("MetalVR Bridge")
                    .font(.system(size: 15, weight: .bold, design: .monospaced))
                    .foregroundColor(.white)

                Text("Vulkan-to-Metal Translation Layer")
                    .font(.system(size: 10, weight: .medium))
                    .foregroundColor(Color(hex: "64748b"))
            }

            Spacer()

            // Status badge
            statusBadge
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 12)
        .background(Color(hex: "0d0d14"))
    }

    private var statusBadge: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(bridgeStatusColor)
                .frame(width: 8, height: 8)

            Text(vm.bridgeStatus.rawValue)
                .font(.system(size: 11, weight: .semibold, design: .monospaced))
                .foregroundColor(bridgeStatusColor)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 5)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(bridgeStatusColor.opacity(0.1))
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(bridgeStatusColor.opacity(0.3), lineWidth: 1)
                )
        )
    }

    private var bridgeStatusColor: Color {
        switch vm.bridgeStatus {
        case .ready:        return Color(hex: "22c55e")
        case .installed:    return Color(hex: "22c55e")
        case .notInstalled: return Color(hex: "f59e0b")
        case .error:        return Color(hex: "ef4444")
        }
    }

    // MARK: - Left Panel

    private var leftPanel: some View {
        ScrollView {
            VStack(spacing: 20) {
                // Triangle Test Card
                testCard

                // Project Status Card
                projectStatusCard

                // System Info Card
                systemInfoCard

                // Steam Launch Card
                steamCard

                Spacer()
            }
            .padding(16)
        }
        .background(Color(hex: "0b0b12"))
    }

    // MARK: - Triangle Test Card

    private var testCard: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Image(systemName: "triangle")
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(Color(hex: "6366f1"))
                Text("Pipeline Test")
                    .font(.system(size: 13, weight: .bold, design: .monospaced))
                    .foregroundColor(.white)
            }

            Text("Renders a colored triangle through the full Vulkan-to-Metal stack to verify the translation layer works.")
                .font(.system(size: 11))
                .foregroundColor(Color(hex: "94a3b8"))
                .fixedSize(horizontal: false, vertical: true)

            // Test result indicator
            HStack(spacing: 8) {
                Circle()
                    .fill(testStatusColor)
                    .frame(width: 10, height: 10)
                    .overlay(
                        Circle()
                            .fill(testStatusColor.opacity(0.4))
                            .frame(width: 18, height: 18)
                            .opacity(vm.testRunning ? 1 : 0)
                            .animation(.easeInOut(duration: 0.8).repeatForever(autoreverses: true), value: vm.testRunning)
                    )

                Text(vm.testStatus.rawValue)
                    .font(.system(size: 12, weight: .semibold, design: .monospaced))
                    .foregroundColor(testStatusColor)
            }

            // Run Test Button
            Button(action: { vm.runTriangleTest() }) {
                HStack(spacing: 8) {
                    if vm.testRunning {
                        ProgressView()
                            .scaleEffect(0.7)
                            .progressViewStyle(CircularProgressViewStyle(tint: .white))
                    } else {
                        Image(systemName: "play.fill")
                            .font(.system(size: 11))
                    }
                    Text(vm.testRunning ? "Running..." : "Run Triangle Test")
                        .font(.system(size: 12, weight: .bold, design: .monospaced))
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)
                .background(
                    RoundedRectangle(cornerRadius: 8)
                        .fill(
                            vm.testRunning
                                ? LinearGradient(
                                    colors: [Color(hex: "374151"), Color(hex: "374151")],
                                    startPoint: .leading,
                                    endPoint: .trailing
                                  )
                                : LinearGradient(
                                    colors: [Color(hex: "6366f1"), Color(hex: "7c3aed")],
                                    startPoint: .leading,
                                    endPoint: .trailing
                                  )
                        )
                )
                .foregroundColor(.white)
            }
            .buttonStyle(.plain)
            .disabled(vm.testRunning)

            // Show triangle preview if available
            if let imageData = vm.triangleImageData,
               let nsImage = NSImage(data: imageData) {
                VStack(spacing: 6) {
                    Image(nsImage: nsImage)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .frame(height: 120)
                        .cornerRadius(6)
                        .overlay(
                            RoundedRectangle(cornerRadius: 6)
                                .stroke(Color(hex: "1e2030"), lineWidth: 1)
                        )

                    Text("Rendered output (saved to Desktop)")
                        .font(.system(size: 9))
                        .foregroundColor(Color(hex: "64748b"))
                }
            }
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(hex: "111118"))
                .overlay(
                    RoundedRectangle(cornerRadius: 10)
                        .stroke(Color(hex: "1e2030"), lineWidth: 1)
                )
        )
    }

    private var testStatusColor: Color {
        switch vm.testStatus {
        case .idle:    return Color(hex: "64748b")
        case .running: return Color(hex: "f59e0b")
        case .passed:  return Color(hex: "22c55e")
        case .failed:  return Color(hex: "ef4444")
        }
    }

    // MARK: - Project Status Card

    private var projectStatusCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Image(systemName: "list.clipboard")
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(Color(hex: "f59e0b"))
                Text("Project Status")
                    .font(.system(size: 13, weight: .bold, design: .monospaced))
                    .foregroundColor(.white)
            }

            if let projectStatus = vm.projectStatus {
                VStack(alignment: .leading, spacing: 7) {
                    statusRow(label: "Phase", value: projectStatus.currentPhase)
                    statusRow(label: "Phase 2", value: projectStatus.phase2Summary)
                    statusRow(label: "Readiness", value: projectStatus.readinessSummary)
                }

                statusCallout(title: "Next Gate", message: projectStatus.nextGate)

                if let nextStep = projectStatus.firstNextNonMacStep {
                    statusCallout(title: "Next Non-Mac", message: nextStep)
                }
            } else {
                Text("No bundled project status snapshot found. Rebuild the launcher from the repo root to package PROJECT_STATUS.json into the app.")
                    .font(.system(size: 11))
                    .foregroundColor(Color(hex: "94a3b8"))
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(hex: "111118"))
                .overlay(
                    RoundedRectangle(cornerRadius: 10)
                        .stroke(Color(hex: "1e2030"), lineWidth: 1)
                )
        )
    }

    // MARK: - System Info Card

    private var systemInfoCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Image(systemName: "cpu")
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(Color(hex: "06b6d4"))
                Text("System")
                    .font(.system(size: 13, weight: .bold, design: .monospaced))
                    .foregroundColor(.white)
            }

            VStack(spacing: 6) {
                infoRow(label: "GPU", value: vm.systemInfo.gpuName)
                infoRow(label: "Metal", value: vm.systemInfo.metalVersion)
                infoRow(label: "Chip", value: vm.systemInfo.chipType)
                infoRow(label: "OS", value: vm.systemInfo.osVersion)
                infoRow(label: "RAM", value: vm.systemInfo.totalMemory)
            }
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(hex: "111118"))
                .overlay(
                    RoundedRectangle(cornerRadius: 10)
                        .stroke(Color(hex: "1e2030"), lineWidth: 1)
                )
        )
    }

    private func infoRow(label: String, value: String) -> some View {
        HStack {
            Text(label)
                .font(.system(size: 10, weight: .semibold, design: .monospaced))
                .foregroundColor(Color(hex: "64748b"))
                .frame(width: 44, alignment: .leading)

            Text(value)
                .font(.system(size: 10, weight: .medium, design: .monospaced))
                .foregroundColor(Color(hex: "cbd5e1"))
                .lineLimit(1)
                .truncationMode(.middle)
        }
    }

    private func statusRow(label: String, value: String) -> some View {
        HStack(alignment: .top) {
            Text(label)
                .font(.system(size: 10, weight: .semibold, design: .monospaced))
                .foregroundColor(Color(hex: "64748b"))
                .frame(width: 58, alignment: .leading)

            Text(value)
                .font(.system(size: 10, weight: .medium, design: .monospaced))
                .foregroundColor(Color(hex: "cbd5e1"))
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private func statusCallout(title: String, message: String) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(title)
                .font(.system(size: 9, weight: .bold, design: .monospaced))
                .foregroundColor(Color(hex: "f59e0b"))

            Text(message)
                .font(.system(size: 10))
                .foregroundColor(Color(hex: "cbd5e1"))
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(Color(hex: "0d0d14"))
                .overlay(
                    RoundedRectangle(cornerRadius: 8)
                        .stroke(Color(hex: "1e2030"), lineWidth: 1)
                )
        )
    }

    // MARK: - Steam Launch Card

    private var steamCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Image(systemName: "gamecontroller")
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(Color(hex: "22c55e"))
                Text("Launch")
                    .font(.system(size: 13, weight: .bold, design: .monospaced))
                    .foregroundColor(.white)
            }

            Text("Opens Steam with the MetalVR Bridge ICD active. Windows games using Vulkan or DXVK will render through Metal.")
                .font(.system(size: 11))
                .foregroundColor(Color(hex: "94a3b8"))
                .fixedSize(horizontal: false, vertical: true)

            Button(action: { vm.launchSteam() }) {
                HStack(spacing: 8) {
                    Image(systemName: "play.circle.fill")
                        .font(.system(size: 13))
                    Text("Launch Steam")
                        .font(.system(size: 12, weight: .bold, design: .monospaced))
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)
                .background(
                    RoundedRectangle(cornerRadius: 8)
                        .fill(
                            LinearGradient(
                                colors: [Color(hex: "059669"), Color(hex: "10b981")],
                                startPoint: .leading,
                                endPoint: .trailing
                            )
                        )
                )
                .foregroundColor(.white)
            }
            .buttonStyle(.plain)
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(hex: "111118"))
                .overlay(
                    RoundedRectangle(cornerRadius: 10)
                        .stroke(Color(hex: "1e2030"), lineWidth: 1)
                )
        )
    }

    // MARK: - Right Panel (Log Viewer)

    private var rightPanel: some View {
        VStack(spacing: 0) {
            // Log header
            HStack {
                Image(systemName: "terminal")
                    .font(.system(size: 12, weight: .bold))
                    .foregroundColor(Color(hex: "22c55e"))

                Text("Diagnostic Log")
                    .font(.system(size: 12, weight: .bold, design: .monospaced))
                    .foregroundColor(Color(hex: "94a3b8"))

                Spacer()

                Text("\(vm.logs.count) entries")
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundColor(Color(hex: "475569"))

                // Export button
                Button(action: { vm.saveLogToFile() }) {
                    HStack(spacing: 4) {
                        Image(systemName: "square.and.arrow.up")
                            .font(.system(size: 10))
                        Text("Export")
                            .font(.system(size: 10, weight: .medium, design: .monospaced))
                    }
                    .foregroundColor(Color(hex: "64748b"))
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(
                        RoundedRectangle(cornerRadius: 4)
                            .stroke(Color(hex: "1e2030"), lineWidth: 1)
                    )
                }
                .buttonStyle(.plain)

                // Clear button
                Button(action: { vm.logs.removeAll() }) {
                    Image(systemName: "trash")
                        .font(.system(size: 10))
                        .foregroundColor(Color(hex: "475569"))
                        .padding(4)
                }
                .buttonStyle(.plain)
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .background(Color(hex: "0d0d14"))

            Divider().background(Color(hex: "1e2030"))

            // Log entries
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        ForEach(vm.logs) { entry in
                            logEntryRow(entry)
                                .id(entry.id)
                        }
                    }
                    .padding(.vertical, 4)
                }
                .onChange(of: vm.logs.count) { _ in
                    if let last = vm.logs.last {
                        withAnimation(.easeOut(duration: 0.15)) {
                            proxy.scrollTo(last.id, anchor: .bottom)
                        }
                    }
                }
            }
            .background(Color(hex: "0a0a0f"))
        }
    }

    private func logEntryRow(_ entry: LogEntry) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Text(entry.formattedTime)
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(Color(hex: "475569"))
                .frame(width: 75, alignment: .leading)

            Text(entry.level.rawValue)
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundColor(logLevelColor(entry.level))
                .frame(width: 40, alignment: .leading)

            Text(entry.message)
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(logMessageColor(entry.level))
                .textSelection(.enabled)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 3)
        .background(logRowBackground(entry.level))
    }

    private func logLevelColor(_ level: LogEntry.LogLevel) -> Color {
        switch level {
        case .info:  return Color(hex: "38bdf8")
        case .warn:  return Color(hex: "fbbf24")
        case .error: return Color(hex: "f87171")
        case .pass:  return Color(hex: "4ade80")
        case .fail:  return Color(hex: "f87171")
        case .debug: return Color(hex: "64748b")
        }
    }

    private func logMessageColor(_ level: LogEntry.LogLevel) -> Color {
        switch level {
        case .pass:  return Color(hex: "86efac")
        case .fail:  return Color(hex: "fca5a5")
        case .error: return Color(hex: "fca5a5")
        case .warn:  return Color(hex: "fde68a")
        default:     return Color(hex: "cbd5e1")
        }
    }

    private func logRowBackground(_ level: LogEntry.LogLevel) -> Color {
        switch level {
        case .pass:  return Color(hex: "22c55e").opacity(0.04)
        case .fail:  return Color(hex: "ef4444").opacity(0.06)
        case .error: return Color(hex: "ef4444").opacity(0.04)
        case .warn:  return Color(hex: "f59e0b").opacity(0.03)
        default:     return .clear
        }
    }
}

// MARK: - Color Extension

extension Color {
    init(hex: String) {
        let hex = hex.trimmingCharacters(in: CharacterSet.alphanumerics.inverted)
        var int: UInt64 = 0
        Scanner(string: hex).scanHexInt64(&int)
        let r, g, b: Double
        switch hex.count {
        case 6:
            r = Double((int >> 16) & 0xFF) / 255.0
            g = Double((int >> 8) & 0xFF) / 255.0
            b = Double(int & 0xFF) / 255.0
        default:
            r = 0; g = 0; b = 0
        }
        self.init(red: r, green: g, blue: b)
    }
}

// MARK: - Preview

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
            .frame(width: 800, height: 620)
    }
}
