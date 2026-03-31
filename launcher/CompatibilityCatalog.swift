import Foundation

struct CompatibilityCatalogSnapshot: Decodable {
    struct Summary: Decodable {
        let totalProfiles: Int
        let autoMatchProfiles: Int
        let templateProfiles: Int
        let competitiveProfiles: Int
        let latencySensitiveProfiles: Int
        let statusCounts: [String: Int]
        let categoryCounts: [String: Int]
        let backendCounts: [String: Int]
    }

    struct Entry: Decodable, Identifiable {
        struct RuntimeSettings: Decodable {
            let windowsVersion: String
            let syncMode: String
            let highResolutionMode: Bool
            let metalFxUpscaling: Bool
        }

        struct MatchSettings: Decodable {
            let executables: [String]
            let launchers: [String]
            let stores: [String]
        }

        struct InstallSettings: Decodable {
            let prefixPreset: String
            let packages: [String]
            let winetricks: [String]
            let requiresLauncher: Bool
            let notes: String
        }

        let profileId: String
        let displayName: String
        let appliedPrefixPresetDisplayName: String
        let status: String
        let allowAutoMatch: Bool
        let category: String
        let notes: String
        let defaultRenderer: String
        let fallbackRenderers: [String]
        let latencySensitive: Bool
        let competitive: Bool
        let antiCheatRisk: String
        let runtime: RuntimeSettings
        let match: MatchSettings
        let install: InstallSettings
        let launchArgs: [String]
        let environmentCount: Int
        let dllOverrideCount: Int

        var id: String { profileId }

        var isPlanningOnly: Bool {
            status == "planning"
        }

        var antiCheatRiskLabel: String {
            antiCheatRisk.replacingOccurrences(of: "-", with: " ")
        }

        var backendSummary: String {
            if fallbackRenderers.isEmpty {
                return defaultRenderer
            }
            return "\(defaultRenderer) -> \(fallbackRenderers.joined(separator: ", "))"
        }

        var runtimeSummary: String {
            let resolutionMode = runtime.highResolutionMode ? "high-res" : "standard-res"
            let upscalingMode = runtime.metalFxUpscaling ? "MetalFX on" : "MetalFX off"
            return "\(runtime.windowsVersion), \(runtime.syncMode), \(resolutionMode), \(upscalingMode)"
        }

        var installSummary: String {
            var parts: [String] = ["Prefix: \(appliedPrefixPresetDisplayName)"]
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

        var matchSummary: String {
            var parts: [String] = []
            if let executable = match.executables.first {
                parts.append("Exe hint: \(executable)")
            }
            if !match.launchers.isEmpty {
                parts.append("Launchers: \(match.launchers.joined(separator: ", "))")
            }
            if !match.stores.isEmpty {
                parts.append("Stores: \(match.stores.joined(separator: ", "))")
            }
            if parts.isEmpty {
                return "No checked-in auto-match hints yet."
            }
            return parts.joined(separator: " | ")
        }

        var behaviorSummary: String {
            var parts: [String] = []
            if competitive {
                parts.append("competitive")
            }
            if latencySensitive {
                parts.append("latency-sensitive")
            }
            if allowAutoMatch {
                parts.append("auto-match")
            } else {
                parts.append("manual-template")
            }
            return parts.joined(separator: " | ")
        }

        var starterExecutableHint: String {
            match.executables.first ?? "Game.exe"
        }
    }

    let schemaVersion: String
    let summary: Summary
    let entries: [Entry]

    var planningProfileCount: Int {
        summary.statusCounts["planning"] ?? 0
    }

    var summaryLine: String {
        "\(summary.totalProfiles) checked-in profiles, \(summary.competitiveProfiles) competitive"
    }

    var knownEntries: [Entry] {
        entries.filter { $0.profileId != "global-defaults" }
    }

    var knownTitlePreview: String? {
        let titles = knownEntries.map(\.displayName)

        guard !titles.isEmpty else { return nil }
        return titles.prefix(2).joined(separator: ", ")
    }

    func entry(for profileId: String) -> Entry? {
        entries.first { $0.profileId == profileId }
    }

    static func load(
        fileManager: FileManager = .default,
        bundle: Bundle = .main
    ) -> CompatibilityCatalogSnapshot? {
        let candidateUrls = compatibilityCatalogCandidateUrls(fileManager: fileManager, bundle: bundle)

        for url in candidateUrls {
            guard fileManager.fileExists(atPath: url.path) else { continue }
            do {
                return try load(from: url)
            } catch {
                continue
            }
        }

        return nil
    }

    static func load(from url: URL) throws -> CompatibilityCatalogSnapshot {
        let data = try Data(contentsOf: url.standardizedFileURL)
        return try JSONDecoder().decode(CompatibilityCatalogSnapshot.self, from: data)
    }

    private static func compatibilityCatalogCandidateUrls(
        fileManager: FileManager,
        bundle: Bundle
    ) -> [URL] {
        var urls: [URL] = []

        if let bundledUrl = bundle.url(forResource: "GAME_COMPATIBILITY_CATALOG", withExtension: "json") {
            urls.append(bundledUrl)
        }

        if let resourceUrl = bundle.resourceURL {
            urls.append(resourceUrl.appendingPathComponent("GAME_COMPATIBILITY_CATALOG.json"))
        }

        let cwdUrl = URL(fileURLWithPath: fileManager.currentDirectoryPath, isDirectory: true)
        urls.append(cwdUrl.appendingPathComponent("docs/GAME_COMPATIBILITY_CATALOG.json"))
        urls.append(cwdUrl.appendingPathComponent("../docs/GAME_COMPATIBILITY_CATALOG.json"))

        return urls.map(\.standardizedFileURL)
    }
}
