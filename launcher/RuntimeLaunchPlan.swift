import Foundation

struct LoadedRuntimeLaunchPlan {
    let snapshot: RuntimeLaunchPlanSnapshot
    let sourceDescription: String
}

struct RuntimeLaunchPlanSnapshot: Decodable {
    struct RuntimeSettings: Decodable {
        let windowsVersion: String
        let syncMode: String
        let highResolutionMode: Bool
        let metalFxUpscaling: Bool
    }

    struct InstallSettings: Decodable {
        let prefixPreset: String
        let packages: [String]
        let winetricks: [String]
        let requiresLauncher: Bool
        let notes: String
    }

    let schemaVersion: String
    let appliedPrefixPresetId: String
    let appliedPrefixPresetDisplayName: String
    let selectedProfileId: String
    let selectedDisplayName: String
    let appliedProfileIds: [String]
    let matchScore: Int
    let backend: String
    let fallbackBackends: [String]
    let runtime: RuntimeSettings
    let install: InstallSettings
    let latencySensitive: Bool
    let competitive: Bool
    let antiCheatRisk: String
    let launchArgs: [String]
    let environment: [String: String]
    let dllOverrides: [String: String]

    var backendSummary: String {
        if fallbackBackends.isEmpty {
            return backend
        }
        return "\(backend) -> \(fallbackBackends.joined(separator: ", "))"
    }

    var installSummary: String {
        var parts: [String] = []
        parts.append("Prefix: \(appliedPrefixPresetDisplayName)")

        if !install.packages.isEmpty {
            parts.append("Packages: \(install.packages.joined(separator: ", "))")
        }
        if !install.winetricks.isEmpty {
            parts.append("Winetricks: \(install.winetricks.joined(separator: ", "))")
        }
        if install.requiresLauncher {
            parts.append("Launcher required")
        }

        return parts.joined(separator: " | ")
    }

    var runtimeSummary: String {
        let resolutionMode = runtime.highResolutionMode ? "high-res" : "standard-res"
        let upscalingMode = runtime.metalFxUpscaling ? "MetalFX on" : "MetalFX off"
        return "\(runtime.windowsVersion), \(runtime.syncMode), \(resolutionMode), \(upscalingMode)"
    }

    var launchSummary: String {
        "\(launchArgs.count) args, \(environment.count) env, \(dllOverrides.count) DLL overrides"
    }

    var antiCheatRiskLabel: String {
        antiCheatRisk.replacingOccurrences(of: "-", with: " ")
    }

    static func loadFirstAvailable(
        fileManager: FileManager = .default,
        bundle: Bundle = .main
    ) -> LoadedRuntimeLaunchPlan? {
        let candidateUrls = runtimeLaunchPlanCandidateUrls(fileManager: fileManager, bundle: bundle)

        for url in candidateUrls {
            guard fileManager.fileExists(atPath: url.path) else { continue }
            if let loaded = try? load(from: url, fileManager: fileManager) {
                return loaded
            }
        }

        return nil
    }

    static func load(
        from url: URL,
        fileManager: FileManager = .default
    ) throws -> LoadedRuntimeLaunchPlan {
        let standardizedUrl = url.standardizedFileURL
        let data = try Data(contentsOf: standardizedUrl)
        let snapshot = try JSONDecoder().decode(RuntimeLaunchPlanSnapshot.self, from: data)
        let sourceDescription = runtimeLaunchPlanSourceDescription(
            for: standardizedUrl,
            fileManager: fileManager
        )
        return LoadedRuntimeLaunchPlan(snapshot: snapshot, sourceDescription: sourceDescription)
    }

    private static func runtimeLaunchPlanCandidateUrls(
        fileManager: FileManager,
        bundle: Bundle
    ) -> [URL] {
        var urls: [URL] = []

        if let bundledUrl = bundle.url(forResource: "launch-plan", withExtension: "json") {
            urls.append(bundledUrl)
        }

        if let bundledPreviewUrl = bundle.url(forResource: "RUNTIME_PLAN_PREVIEW", withExtension: "json") {
            urls.append(bundledPreviewUrl)
        }

        if let resourceUrl = bundle.resourceURL {
            urls.append(resourceUrl.appendingPathComponent("launch-plan.json"))
            urls.append(resourceUrl.appendingPathComponent("RUNTIME_PLAN_PREVIEW.json"))
        }

        let cwdUrl = URL(fileURLWithPath: fileManager.currentDirectoryPath, isDirectory: true)
        urls.append(cwdUrl.appendingPathComponent("launch-plan.json"))
        urls.append(cwdUrl.appendingPathComponent("RUNTIME_PLAN_PREVIEW.json"))
        urls.append(cwdUrl.appendingPathComponent("../launch-plan.json"))
        urls.append(cwdUrl.appendingPathComponent("../RUNTIME_PLAN_PREVIEW.json"))

        return urls.map(\.standardizedFileURL)
    }

    private static func runtimeLaunchPlanSourceDescription(
        for url: URL,
        fileManager: FileManager
    ) -> String {
        let currentDirectoryUrl = URL(
            fileURLWithPath: fileManager.currentDirectoryPath,
            isDirectory: true
        ).standardizedFileURL

        if let bundleResourceUrl = Bundle.main.resourceURL?.standardizedFileURL,
           url.path.hasPrefix(bundleResourceUrl.path) {
            return "Bundled: \(url.lastPathComponent)"
        }

        if url.path.hasPrefix(currentDirectoryUrl.path) {
            let relativePath = url.path.replacingOccurrences(
                of: currentDirectoryUrl.path + "/",
                with: ""
            )
            return "Working directory: \(relativePath)"
        }

        return url.path
    }
}
