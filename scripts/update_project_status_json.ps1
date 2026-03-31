param()

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputPath = Join-Path $repoRoot "docs\PROJECT_STATUS.json"

$status = [ordered]@{
    schemaVersion = "1"
    currentPhase = "Between Milestone 9 and Milestone 10; real Mac runtime validation is still pending."
    phase2 = [ordered]@{
        completedMilestones = @(0, 1, 2, 3, 4, 5, 6, 7, 8, 9)
        currentMilestone = 10
        currentStatus = "not-started"
    }
    phase4 = [ordered]@{
        startedMilestones = @(13, 14, 16)
        plannedMilestones = @(15, 17, 18)
    }
    provenSurfaces = @(
        "macos-ci-build-icd",
        "macos-ci-build-launcher",
        "host-side-unit-tests",
        "compatibility-profile-validation",
        "prefix-preset-validation",
        "compatibility-catalog-export",
        "compatibility-policy-lint",
        "runtime-launch-plan-generation",
        "runtime-launch-command-generation",
        "runtime-setup-command-generation",
        "runtime-bundle-export",
        "ai-handoff-doc-and-bundle",
        "launcher-project-status-snapshot",
        "launcher-compatibility-catalog-snapshot",
        "launcher-runtime-plan-preview",
        "launcher-runtime-bundle-manifest-preview",
        "launcher-runtime-bundle-artifact-preview",
        "launcher-guided-runtime-actions",
        "launcher-runtime-bundle-asset-actions",
        "launcher-runtime-copy-helpers"
    )
    notYetProven = @(
        "launcher-triangle-on-real-mac",
        "vulkaninfo-on-real-mac",
        "vkcube-on-real-mac",
        "wine-dxvk-game-launch-on-real-mac"
    )
    nextGate = "Launcher triangle, vulkaninfo, and vkcube on Mac hardware."
    nextNonMacSteps = @(
        "Promote the shared runtime-plan contract into richer launcher-side execution-prep and handoff flows now that import, preview, guided actions, and copy helpers are in place.",
        "Keep compatibility profiles and prefix presets governed through lint and docs.",
        "Keep repo and wiki handoff docs aligned with the checked-in source of truth."
    )
    sourceOfTruthDocs = @(
        "README.md",
        "docs/PROJECT_STATUS.json",
        "docs/AI_HANDOFF.md",
        "docs/GAME_COMPATIBILITY_CATALOG.json",
        "docs/MILESTONES.md",
        "docs/EXECUTION_PLAN.md",
        "docs/REPO_MAP.md",
        "docs/PHASE4_CROSSOVER_COMPETITOR_ROADMAP.md",
        "docs/GAME_COMPATIBILITY_CATALOG.md",
        "docs/MAC_RUNTIME_SMOKE_TEST.md",
        "profiles/README.md",
        "CONTRIBUTING.md"
    )
}

$status | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $outputPath
Write-Host "Updated project status JSON: $outputPath"
