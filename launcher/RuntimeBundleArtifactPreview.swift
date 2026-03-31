import Foundation

struct RuntimeBundleArtifactPreview {
    let checklistSummary: String
    let setupScriptSummary: String
    let launchScriptSummary: String
    let lintSummary: String
    let compatibilityCatalogSummary: String

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
        let bashLaunchText = loadText(
            at: loadedRuntimeBundle.resolvedFileUrl(
                for: loadedRuntimeBundle.snapshot.files.bashLaunchScript
            ),
            fileManager: fileManager
        )
        let powerShellLaunchText = loadText(
            at: loadedRuntimeBundle.resolvedFileUrl(
                for: loadedRuntimeBundle.snapshot.files.powershellLaunchScript
            ),
            fileManager: fileManager
        )
        let importedCompatibilityCatalog = loadCompatibilityCatalog(
            at: loadedRuntimeBundle.resolvedFileUrl(
                for: loadedRuntimeBundle.snapshot.files.compatibilityCatalogJson
            ),
            fileManager: fileManager
        )

        guard checklistText != nil
            || bashSetupText != nil
            || powerShellSetupText != nil
            || bashLaunchText != nil
            || powerShellLaunchText != nil
            || lintText != nil
            || importedCompatibilityCatalog != nil else {
            return nil
        }

        return RuntimeBundleArtifactPreview(
            checklistSummary: summarizeChecklist(checklistText),
            setupScriptSummary: summarizeSetupScripts(
                bashSetupText: bashSetupText,
                powerShellSetupText: powerShellSetupText
            ),
            launchScriptSummary: summarizeLaunchScripts(
                bashLaunchText: bashLaunchText,
                powerShellLaunchText: powerShellLaunchText
            ),
            lintSummary: summarizeLint(lintText),
            compatibilityCatalogSummary: summarizeCompatibilityCatalog(importedCompatibilityCatalog)
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

    private static func loadCompatibilityCatalog(
        at url: URL,
        fileManager: FileManager
    ) -> CompatibilityCatalogSnapshot? {
        guard fileManager.fileExists(atPath: url.path) else {
            return nil
        }

        return try? CompatibilityCatalogSnapshot.load(from: url)
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
            bashScriptText: bashSetupText,
            powerShellScriptText: powerShellSetupText
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

    private static func summarizeLaunchScripts(
        bashLaunchText: String?,
        powerShellLaunchText: String?
    ) -> String {
        let availableScripts = availableScriptSurfaces(
            bashScriptText: bashLaunchText,
            powerShellScriptText: powerShellLaunchText
        )
        guard !availableScripts.isEmpty else {
            return "No exported launch scripts were found in the runtime bundle."
        }

        var parts = ["Available: \(availableScripts.joined(separator: ", "))"]
        let scriptSources = [bashLaunchText, powerShellLaunchText].compactMap { $0 }

        if let workingDirectory = extractWorkingDirectory(from: scriptSources) {
            parts.append("Working dir: \(workingDirectory)")
        }

        if let launchCommand = extractLaunchCommand(from: scriptSources) {
            parts.append("Command: \(launchCommand)")
        }

        return parts.joined(separator: " | ")
    }

    private static func summarizeCompatibilityCatalog(
        _ importedCompatibilityCatalog: CompatibilityCatalogSnapshot?
    ) -> String {
        guard let importedCompatibilityCatalog else {
            return "Compatibility catalog missing from the imported runtime bundle."
        }

        var parts = [importedCompatibilityCatalog.summaryLine]
        if importedCompatibilityCatalog.planningProfileCount > 0 {
            parts.append("\(importedCompatibilityCatalog.planningProfileCount) planning-only")
        }
        if let preview = importedCompatibilityCatalog.knownTitlePreview {
            parts.append("Preview: \(preview)")
        }

        return parts.joined(separator: " | ")
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
        bashScriptText: String?,
        powerShellScriptText: String?
    ) -> [String] {
        var surfaces: [String] = []
        if let bashScriptText,
           !bashScriptText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            surfaces.append("bash")
        }
        if let powerShellScriptText,
           !powerShellScriptText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            surfaces.append("PowerShell")
        }
        return surfaces
    }

    private static func extractWorkingDirectory(from scripts: [String]) -> String? {
        let bashPrefix = "cd '"
        let powerShellPrefix = "Set-Location -LiteralPath '"

        for script in scripts {
            for rawLine in script.components(separatedBy: .newlines) {
                let line = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
                if let extractedValue = extractQuotedValue(from: line, prefix: bashPrefix) {
                    return cleanScriptLine(extractedValue)
                }
                if let extractedValue = extractQuotedValue(from: line, prefix: powerShellPrefix) {
                    return cleanScriptLine(extractedValue)
                }
            }
        }

        return nil
    }

    private static func extractLaunchCommand(from scripts: [String]) -> String? {
        for script in scripts {
            for rawLine in script.components(separatedBy: .newlines) {
                let line = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
                if line.hasPrefix("exec ") {
                    return cleanScriptLine(String(line.dropFirst(5)))
                }
                if line.hasPrefix("& ") {
                    return cleanScriptLine(String(line.dropFirst(2)))
                }
            }
        }

        return nil
    }

    private static func cleanBullet(_ text: String) -> String {
        text
            .replacingOccurrences(of: "`", with: "")
            .replacingOccurrences(of: "  ", with: " ")
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func cleanScriptLine(_ text: String) -> String {
        text
            .replacingOccurrences(of: "'", with: "")
            .replacingOccurrences(of: "  ", with: " ")
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func extractQuotedValue(
        from line: String,
        prefix: String
    ) -> String? {
        guard line.hasPrefix(prefix), line.hasSuffix("'") else {
            return nil
        }

        return String(line.dropFirst(prefix.count).dropLast())
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
