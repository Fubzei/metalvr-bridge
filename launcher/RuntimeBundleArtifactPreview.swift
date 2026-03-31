import Foundation

struct RuntimeBundleArtifactPreview {
    let checklistSummary: String
    let setupScriptSummary: String
    let lintSummary: String

    static func load(
        from loadedRuntimeBundle: LoadedRuntimeBundleManifest,
        fileManager: FileManager = .default
    ) -> RuntimeBundleArtifactPreview? {
        let checklistText = loadText(
            at: loadedRuntimeBundle.resolvedFileUrl(
                for: loadedRuntimeBundle.snapshot.files.setupChecklist
            ),
            fileManager: fileManager
        )
        let bashSetupText = loadText(
            at: loadedRuntimeBundle.resolvedFileUrl(
                for: loadedRuntimeBundle.snapshot.files.bashSetupScript
            ),
            fileManager: fileManager
        )
        let powerShellSetupText = loadText(
            at: loadedRuntimeBundle.resolvedFileUrl(
                for: loadedRuntimeBundle.snapshot.files.powershellSetupScript
            ),
            fileManager: fileManager
        )
        let lintText = loadText(
            at: loadedRuntimeBundle.resolvedFileUrl(
                for: loadedRuntimeBundle.snapshot.files.profileLintReport
            ),
            fileManager: fileManager
        )

        guard checklistText != nil || bashSetupText != nil || powerShellSetupText != nil || lintText != nil else {
            return nil
        }

        return RuntimeBundleArtifactPreview(
            checklistSummary: summarizeChecklist(checklistText),
            setupScriptSummary: summarizeSetupScripts(
                bashSetupText: bashSetupText,
                powerShellSetupText: powerShellSetupText
            ),
            lintSummary: summarizeLint(lintText)
        )
    }

    private static func loadText(
        at url: URL,
        fileManager: FileManager
    ) -> String? {
        guard fileManager.fileExists(atPath: url.path) else {
            return nil
        }

        return try? String(contentsOf: url, encoding: .utf8)
    }

    private static func summarizeChecklist(_ checklistText: String?) -> String {
        guard let checklistText,
              !checklistText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            return "Checklist file missing from the imported runtime bundle."
        }

        let setupBullets = extractBullets(inSection: "Setup", from: checklistText)
        if !setupBullets.isEmpty {
            return compactPreview(setupBullets, limit: 3)
        }

        let launchBullets = extractBullets(inSection: "Launch Surface", from: checklistText)
        if !launchBullets.isEmpty {
            return compactPreview(launchBullets, limit: 2)
        }

        let genericBullets = checklistText
            .components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { $0.hasPrefix("- ") }
            .map { cleanBullet(String($0.dropFirst(2))) }

        if !genericBullets.isEmpty {
            return compactPreview(genericBullets, limit: 3)
        }

        return "Checklist imported, but no structured setup bullets were found."
    }

    private static func summarizeSetupScripts(
        bashSetupText: String?,
        powerShellSetupText: String?
    ) -> String {
        let availableScripts = availableScriptSurfaces(
            bashSetupText: bashSetupText,
            powerShellSetupText: powerShellSetupText
        )
        let combinedScriptText = [bashSetupText, powerShellSetupText]
            .compactMap { $0 }
            .joined(separator: "\n")

        var parts: [String] = []
        if availableScripts.isEmpty {
            parts.append("No exported setup scripts were found in the runtime bundle.")
        } else {
            parts.append("Available: \(availableScripts.joined(separator: ", "))")
        }

        let automatedSteps = automatedSetupSteps(in: combinedScriptText)
        if !automatedSteps.isEmpty {
            parts.append("Automated: \(automatedSteps.joined(separator: ", "))")
        }

        let manualSteps = extractManualFollowUpLines(from: combinedScriptText)
        if !manualSteps.isEmpty {
            parts.append("Manual: \(compactPreview(manualSteps, limit: 2))")
        }

        return parts.joined(separator: " | ")
    }

    private static func summarizeLint(_ lintText: String?) -> String {
        guard let lintText,
              !lintText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            return "Lint report missing from the imported runtime bundle."
        }

        let lines = lintText
            .components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }

        if lines.isEmpty {
            return "Lint report imported, but it did not contain any readable lines."
        }

        return compactPreview(lines, limit: 2)
    }

    private static func extractBullets(
        inSection title: String,
        from text: String
    ) -> [String] {
        var bullets: [String] = []
        var captureSection = false

        for rawLine in text.components(separatedBy: .newlines) {
            let line = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
            if line.hasPrefix("## ") {
                captureSection = (line == "## \(title)")
                continue
            }

            if captureSection && line.hasPrefix("- ") {
                bullets.append(cleanBullet(String(line.dropFirst(2))))
            }
        }

        return bullets
    }

    private static func extractManualFollowUpLines(from text: String) -> [String] {
        text.components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { $0.hasPrefix("# - ") }
            .map { cleanBullet(String($0.dropFirst(4))) }
    }

    private static func automatedSetupSteps(in text: String) -> [String] {
        var steps: [String] = []

        if text.contains("wineboot") {
            steps.append("wineboot")
        }
        if text.contains("winetricks") {
            steps.append("winetricks")
        }

        return steps
    }

    private static func availableScriptSurfaces(
        bashSetupText: String?,
        powerShellSetupText: String?
    ) -> [String] {
        var surfaces: [String] = []
        if let bashSetupText,
           !bashSetupText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            surfaces.append("bash")
        }
        if let powerShellSetupText,
           !powerShellSetupText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            surfaces.append("PowerShell")
        }
        return surfaces
    }

    private static func cleanBullet(_ text: String) -> String {
        text
            .replacingOccurrences(of: "`", with: "")
            .replacingOccurrences(of: "  ", with: " ")
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func compactPreview(
        _ items: [String],
        limit: Int
    ) -> String {
        guard !items.isEmpty else {
            return ""
        }

        let preview = Array(items.prefix(limit))
        let remainder = items.count - preview.count
        if remainder > 0 {
            return preview.joined(separator: " | ") + " | +\(remainder) more"
        }

        return preview.joined(separator: " | ")
    }
}
