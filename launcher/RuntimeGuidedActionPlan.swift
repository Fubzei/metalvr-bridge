import Foundation

struct RuntimeGuidedActionPlan {
    enum StepTone {
        case ready
        case attention
        case blocked
        case info
    }

    enum Action {
        case importJson
        case generateStarterBundle
        case copyStarterCommand
        case openChecklist
        case runSetupScript
        case runLaunchScript
        case copyEnvironment
        case copyLaunchCommand
        case savePrepSheet
        case revealBundle
        case saveReport
        case runTriangleTest
    }

    struct Step: Identifiable {
        let id: String
        let title: String
        let detail: String
        let tone: StepTone
        let action: Action?
        let actionLabel: String?
    }

    let headline: String
    let steps: [Step]

    static func build(
        projectStatus: ProjectStatusSnapshot?,
        runtimeLaunchPlan: RuntimeLaunchPlanSnapshot?,
        runtimeBundleManifest: RuntimeBundleManifestSnapshot?,
        runtimeBundleArtifactPreview: RuntimeBundleArtifactPreview?,
        canGenerateStarterBundleInApp: Bool,
        selectedCatalogEntry: CompatibilityCatalogSnapshot.Entry?,
        compatibilityCatalogEntry: CompatibilityCatalogSnapshot.Entry?
    ) -> RuntimeGuidedActionPlan {
        guard let runtimeLaunchPlan else {
            var steps: [Step] = []

            if let selectedCatalogEntry {
                steps.append(
                    Step(
                        id: "known-title-onboarding",
                        title: "Start From A Known Title",
                        detail: "\(selectedCatalogEntry.displayName) is selected. \(selectedCatalogEntry.installSummary)",
                        tone: .ready,
                        action: nil,
                        actionLabel: nil
                    )
                )
                steps.append(
                    Step(
                        id: canGenerateStarterBundleInApp ? "generate-starter-bundle" : "copy-starter-command",
                        title: canGenerateStarterBundleInApp ? "Generate A Starter Bundle" : "Copy The Starter Bundle Command",
                        detail: canGenerateStarterBundleInApp
                            ? "Use the bundled runtime-bundle builder to generate and import a portable starter bundle for the selected title directly from the app."
                            : "Use the launcher-generated starter command to build a portable runtime bundle for the selected title before importing anything back into the app.",
                        tone: .ready,
                        action: canGenerateStarterBundleInApp ? .generateStarterBundle : .copyStarterCommand,
                        actionLabel: canGenerateStarterBundleInApp ? "Generate Bundle" : "Copy Starter Cmd"
                    )
                )
                steps.append(
                    Step(
                        id: "import-json",
                        title: "Import The Generated Bundle",
                        detail: "After the builder runs, import the resulting launch-plan.json or bundle-manifest.json so the launcher can unlock guided setup and launch actions.",
                        tone: .ready,
                        action: .importJson,
                        actionLabel: "Import JSON"
                    )
                )
            } else {
                steps.append(
                    Step(
                        id: "import-json",
                        title: "Import Runtime JSON",
                        detail: "Import a launch-plan.json or bundle-manifest.json file to load backend, setup, and launch policy into the launcher.",
                        tone: .ready,
                        action: .importJson,
                        actionLabel: "Import JSON"
                    )
                )
            }

            if let nextGate = projectStatus?.nextGate {
                steps.append(
                    Step(
                        id: "next-gate",
                        title: "Track The Next Hardware Gate",
                        detail: nextGate,
                        tone: .info,
                        action: nil,
                        actionLabel: nil
                    )
                )
            }

            return RuntimeGuidedActionPlan(
                headline: selectedCatalogEntry == nil
                    ? "Import runtime policy first so the launcher can generate a guided path."
                    : "Start from the selected known title, build a starter bundle, then import it back into the launcher.",
                steps: steps
            )
        }

        var steps: [Step] = []

        if compatibilityCatalogEntry?.status == "planning" {
            let notes = compatibilityCatalogEntry?.notes ?? "This title is still planning-only."
            steps.append(
                Step(
                    id: "planning-only",
                    title: "Treat This As Planning Guidance",
                    detail: notes,
                    tone: .attention,
                    action: nil,
                    actionLabel: nil
                )
            )
        }

        switch runtimeLaunchPlan.antiCheatRisk {
        case "blocking":
            steps.append(
                Step(
                    id: "anti-cheat-blocking",
                    title: "Anti-Cheat Risk Is Blocking",
                    detail: "Do not treat this runtime plan as playable yet. Keep the workflow in validation mode until real Mac hardware proves otherwise.",
                    tone: .blocked,
                    action: nil,
                    actionLabel: nil
                )
            )
        case "high":
            steps.append(
                Step(
                    id: "anti-cheat-high",
                    title: "Anti-Cheat Risk Is High",
                    detail: "Expect title-specific service or anti-cheat issues even if rendering eventually works. Keep logs and bundle exports attached to every test run.",
                    tone: .attention,
                    action: nil,
                    actionLabel: nil
                )
            )
        default:
            break
        }

        if runtimeBundleManifest == nil {
            steps.append(
                Step(
                    id: "import-bundle",
                    title: "Import A Runtime Bundle",
                    detail: "Import bundle-manifest.json to unlock the checklist, scripts, compatibility snapshot, and report actions generated from the shared runtime-plan contract.",
                    tone: .ready,
                    action: .importJson,
                    actionLabel: "Import Bundle"
                )
            )
        } else {
            steps.append(
                Step(
                    id: "review-checklist",
                    title: "Review The Setup Checklist",
                    detail: runtimeBundleArtifactPreview?.checklistSummary
                        ?? "The bundle is imported; open the checklist to review setup intent.",
                    tone: .ready,
                    action: .openChecklist,
                    actionLabel: "Open Checklist"
                )
            )

            if runtimeLaunchPlan.install.requiresLauncher
                || !runtimeLaunchPlan.install.packages.isEmpty
                || !runtimeLaunchPlan.install.winetricks.isEmpty {
                steps.append(
                    Step(
                        id: "run-setup-script",
                        title: "Prepare The Prefix",
                        detail: runtimeBundleArtifactPreview?.setupScriptSummary
                            ?? "Run the generated setup script to automate prefix/bootstrap work before launching the game.",
                        tone: .ready,
                        action: .runSetupScript,
                        actionLabel: "Run Setup"
                    )
                )
            }

            steps.append(
                Step(
                    id: "run-launch-script",
                    title: "Run The Launch Flow",
                    detail: runtimeBundleArtifactPreview?.launchScriptSummary
                        ?? "Run the generated launch script to execute the imported working directory, environment, and command surface.",
                    tone: .ready,
                    action: .runLaunchScript,
                    actionLabel: "Run Launch"
                )
            )

            steps.append(
                Step(
                    id: "copy-environment",
                    title: "Copy Environment Snippet",
                    detail: "Copy the exported environment block from the imported launch script so the tester can stage runtime variables in Terminal without opening the script first.",
                    tone: .ready,
                    action: .copyEnvironment,
                    actionLabel: "Copy Env"
                )
            )

            steps.append(
                Step(
                    id: "copy-launch-command",
                    title: "Copy Launch Command Snippet",
                    detail: "Copy the working directory and launch command extracted from the imported launch script so execution prep can move from the launcher straight into Terminal or issue reports.",
                    tone: .ready,
                    action: .copyLaunchCommand,
                    actionLabel: "Copy Command"
                )
            )

            steps.append(
                Step(
                    id: "save-prep-sheet",
                    title: "Export A Prep Sheet",
                    detail: "Save one concise execution-prep file that combines the imported setup summary, environment surface, launch command, and current risk markers for handoff or Terminal prep.",
                    tone: .info,
                    action: .savePrepSheet,
                    actionLabel: "Save Prep"
                )
            )

            steps.append(
                Step(
                    id: "reveal-bundle",
                    title: "Reveal The Imported Bundle",
                    detail: "Open the bundle folder in Finder so the tester can inspect or share the full exported asset set.",
                    tone: .info,
                    action: .revealBundle,
                    actionLabel: "Reveal Bundle"
                )
            )

            steps.append(
                Step(
                    id: "save-report",
                    title: "Export A Combined Bundle Report",
                    detail: "Save one text report containing the launch plan, checklist, lint, and script bodies for easier handoff and issue filing.",
                    tone: .info,
                    action: .saveReport,
                    actionLabel: "Save Report"
                )
            )
        }

        if let nextGate = projectStatus?.nextGate {
            steps.append(
                Step(
                    id: "run-mac-gate",
                    title: "Run The First Mac Validation Gate",
                    detail: nextGate,
                    tone: .info,
                    action: .runTriangleTest,
                    actionLabel: "Run Triangle"
                )
            )
        }

        return RuntimeGuidedActionPlan(
            headline: headline(
                runtimeLaunchPlan: runtimeLaunchPlan,
                hasImportedBundle: runtimeBundleManifest != nil
            ),
            steps: steps
        )
    }

    private static func headline(
        runtimeLaunchPlan: RuntimeLaunchPlanSnapshot,
        hasImportedBundle: Bool
    ) -> String {
        let bundleState = hasImportedBundle
            ? "The imported bundle unlocks guided setup and launch actions."
            : "Importing a runtime bundle will unlock checklist and script actions."
        return "\(runtimeLaunchPlan.selectedDisplayName) is loaded via \(runtimeLaunchPlan.backendSummary). \(bundleState)"
    }
}
