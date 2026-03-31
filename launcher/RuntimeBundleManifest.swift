import Foundation

struct LoadedRuntimeBundleManifest {
    let snapshot: RuntimeBundleManifestSnapshot
    let sourceDescription: String
}

struct RuntimeBundleManifestSnapshot: Decodable {
    struct Files: Decodable {
        let launchPlanJson: String
        let launchPlanReport: String
        let setupChecklist: String
        let bashSetupScript: String
        let powershellSetupScript: String
        let bashLaunchScript: String
        let powershellLaunchScript: String
        let compatibilityCatalogJson: String
        let compatibilityCatalogReport: String
        let compatibilityCatalogMarkdown: String
        let profileLintReport: String
    }

    let generatedAt: String
    let executable: String
    let launcher: String
    let store: String
    let prefixPath: String
    let profilesDir: String
    let files: Files

    var targetSummary: String {
        let executableName = URL(fileURLWithPath: executable).lastPathComponent
        if launcher.isEmpty {
            return executableName
        }
        return "\(executableName) via \(launcher)"
    }

    var bundleSummary: String {
        var parts: [String] = []
        if !launcher.isEmpty {
            parts.append("Launcher: \(launcher)")
        }
        if !store.isEmpty {
            parts.append("Store: \(store)")
        }
        if !prefixPath.isEmpty {
            parts.append("Prefix: \(prefixPath)")
        }
        return parts.joined(separator: " | ")
    }

    var assetSummary: String {
        "Launch plan, checklist, 2 setup scripts, 2 launch scripts, catalog, and lint report"
    }

    static func loadFirstAvailable(
        fileManager: FileManager = .default,
        bundle: Bundle = .main
    ) -> LoadedRuntimeBundleManifest? {
        let candidateUrls = runtimeBundleManifestCandidateUrls(fileManager: fileManager, bundle: bundle)

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
    ) throws -> LoadedRuntimeBundleManifest {
        let standardizedUrl = url.standardizedFileURL
        let data = try Data(contentsOf: standardizedUrl)
        let snapshot = try JSONDecoder().decode(RuntimeBundleManifestSnapshot.self, from: data)
        let sourceDescription = runtimeBundleSourceDescription(
            for: standardizedUrl,
            fileManager: fileManager
        )
        return LoadedRuntimeBundleManifest(snapshot: snapshot, sourceDescription: sourceDescription)
    }

    private static func runtimeBundleManifestCandidateUrls(
        fileManager: FileManager,
        bundle: Bundle
    ) -> [URL] {
        var urls: [URL] = []

        if let bundledUrl = bundle.url(forResource: "bundle-manifest", withExtension: "json") {
            urls.append(bundledUrl)
        }

        if let resourceUrl = bundle.resourceURL {
            urls.append(resourceUrl.appendingPathComponent("bundle-manifest.json"))
        }

        let cwdUrl = URL(fileURLWithPath: fileManager.currentDirectoryPath, isDirectory: true)
        urls.append(cwdUrl.appendingPathComponent("bundle-manifest.json"))
        urls.append(cwdUrl.appendingPathComponent("../bundle-manifest.json"))

        return urls.map(\.standardizedFileURL)
    }

    private static func runtimeBundleSourceDescription(
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
