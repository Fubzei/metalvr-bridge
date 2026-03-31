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
        case openChecklist
        case openSetupScript
        case openLaunchScript
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
        compatibilityCatalogEntry: CompatibilityCatalogSnapshot.Entry?
    ) -> RuntimeGuidedActionPlan {
        guard let runtimeLaunchPlan else {
            var steps = [
                Step(
                    id: "import-json",
                    title: "Import Runtime JSON",
                    detail: "Import a launch-plan.json or bundle-manifest.json file to load backend, setup, and launch policy into the launcher.",
                    tone: .ready,
                    action: .importJson,
                    actionLabel: "Import JSON"
                )
            ]

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
                headline: "Import runtime policy first so the launcher can generate a guided path.",
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
                        id: "review-setup-script",
                        title: "Inspect Setup Automation",
                        detail: runtimeBundleArtifactPreview?.setupScriptSummary
                            ?? "Open the generated setup script before touching the prefix.",
                        tone: .ready,
                        action: .openSetupScript,
                        actionLabel: "Open Setup"
                    )
                )
            }

            steps.append(
                Step(
                    id: "review-launch-script",
                    title: "Inspect Launch Automation",
                    detail: runtimeBundleArtifactPreview?.launchScriptSummary
                        ?? "Open the generated launch script to see the working directory, environment, and command surface.",
                    tone: .ready,
                    action: .openLaunchScript,
                    actionLabel: "Open Launch"
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
