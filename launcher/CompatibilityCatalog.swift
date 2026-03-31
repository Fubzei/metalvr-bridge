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

    struct Entry: Decodable {
        let profileId: String
        let displayName: String
        let status: String
        let category: String
        let notes: String
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

    var knownTitlePreview: String? {
        let titles = entries
            .filter { $0.profileId != "global-defaults" }
            .map(\.displayName)

        guard !titles.isEmpty else { return nil }
        return titles.prefix(2).joined(separator: ", ")
    }

    static func load(
        fileManager: FileManager = .default,
        bundle: Bundle = .main
    ) -> CompatibilityCatalogSnapshot? {
        let candidateUrls = compatibilityCatalogCandidateUrls(fileManager: fileManager, bundle: bundle)

        for url in candidateUrls {
            guard fileManager.fileExists(atPath: url.path) else { continue }
            do {
                let data = try Data(contentsOf: url)
                return try JSONDecoder().decode(CompatibilityCatalogSnapshot.self, from: data)
            } catch {
                continue
            }
        }

        return nil
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
