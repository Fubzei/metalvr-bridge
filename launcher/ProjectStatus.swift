import Foundation

struct ProjectStatusSnapshot: Decodable {
    struct Phase2Status: Decodable {
        let completedMilestones: [Int]
        let currentMilestone: Int
        let currentStatus: String
    }

    struct Phase4Status: Decodable {
        let startedMilestones: [Int]
        let plannedMilestones: [Int]
    }

    let schemaVersion: String
    let currentPhase: String
    let phase2: Phase2Status
    let phase4: Phase4Status
    let provenSurfaces: [String]
    let notYetProven: [String]
    let nextGate: String
    let nextNonMacSteps: [String]
    let sourceOfTruthDocs: [String]

    var phase2Summary: String {
        let readableStatus = currentStatusLabel(phase2.currentStatus)
        return "Milestone \(phase2.currentMilestone) (\(readableStatus))"
    }

    var phase4Summary: String {
        "\(phase4.startedMilestones.count) started, \(phase4.plannedMilestones.count) planned"
    }

    var readinessSummary: String {
        "\(provenSurfaces.count) proven surfaces, \(notYetProven.count) Mac-only gates pending"
    }

    var firstNextNonMacStep: String? {
        nextNonMacSteps.first
    }

    static func load(
        fileManager: FileManager = .default,
        bundle: Bundle = .main
    ) -> ProjectStatusSnapshot? {
        let candidateUrls = projectStatusCandidateUrls(fileManager: fileManager, bundle: bundle)

        for url in candidateUrls {
            guard fileManager.fileExists(atPath: url.path) else { continue }
            do {
                let data = try Data(contentsOf: url)
                return try JSONDecoder().decode(ProjectStatusSnapshot.self, from: data)
            } catch {
                continue
            }
        }

        return nil
    }

    private static func projectStatusCandidateUrls(
        fileManager: FileManager,
        bundle: Bundle
    ) -> [URL] {
        var urls: [URL] = []

        if let bundledUrl = bundle.url(forResource: "PROJECT_STATUS", withExtension: "json") {
            urls.append(bundledUrl)
        }

        if let resourceUrl = bundle.resourceURL {
            urls.append(resourceUrl.appendingPathComponent("PROJECT_STATUS.json"))
        }

        let cwdUrl = URL(fileURLWithPath: fileManager.currentDirectoryPath, isDirectory: true)
        urls.append(cwdUrl.appendingPathComponent("docs/PROJECT_STATUS.json"))
        urls.append(cwdUrl.appendingPathComponent("../docs/PROJECT_STATUS.json"))

        return urls.map(\.standardizedFileURL)
    }

    private func currentStatusLabel(_ value: String) -> String {
        value.replacingOccurrences(of: "-", with: " ")
    }
}
