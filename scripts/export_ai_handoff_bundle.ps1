param(
    [string]$BuildDir = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build-host"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $BuildDir "exports\ai-handoff"
}

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

& (Join-Path $repoRoot "scripts\update_ai_handoff_doc.ps1")
if (-not $?) {
    throw "AI handoff doc update failed with exit code $LASTEXITCODE"
}
& (Join-Path $repoRoot "scripts\update_project_status_json.ps1")
if (-not $?) {
    throw "Project status JSON update failed with exit code $LASTEXITCODE"
}

$docTargets = @(
    "README.md",
    "CONTRIBUTING.md",
    "SECURITY.md",
    "docs\AI_HANDOFF.md",
    "docs\PROJECT_STATUS.json",
    "docs\REPO_MAP.md",
    "docs\MILESTONES.md",
    "docs\EXECUTION_PLAN.md",
    "docs\PHASE4_CROSSOVER_COMPETITOR_ROADMAP.md",
    "docs\GAME_COMPATIBILITY_CATALOG.md",
    "docs\MAC_RUNTIME_SMOKE_TEST.md",
    "profiles\README.md"
)

foreach ($relativePath in $docTargets) {
    $sourcePath = Join-Path $repoRoot $relativePath
    $targetPath = Join-Path $OutputDir $relativePath
    $targetDirectory = Split-Path -Parent $targetPath
    if (-not [string]::IsNullOrWhiteSpace($targetDirectory)) {
        New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
    }
    Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
}

$lintReport = Join-Path $OutputDir "profile-lint.txt"
$lintOutput = & powershell -ExecutionPolicy Bypass -File (Join-Path $repoRoot "scripts\run_profile_lint.ps1") 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "AI handoff lint export failed with exit code $LASTEXITCODE"
}
$lintOutput | Set-Content -LiteralPath $lintReport

$catalogOutput = Join-Path $OutputDir "compatibility-catalog.json"
& (Join-Path $repoRoot "scripts\export_profile_catalog.ps1") -BuildDir $BuildDir -OutputPath $catalogOutput | Out-Null
if (-not $?) {
    throw "AI handoff catalog export failed with exit code $LASTEXITCODE"
}

$manifestPath = Join-Path $OutputDir "handoff-manifest.json"
$manifest = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    currentPhase = "Between Milestone 9 and Milestone 10; real Mac runtime validation is still pending."
    nextGate = "Launcher triangle, vulkaninfo, and vkcube on Mac hardware."
    recommendedNextNonMacSteps = @(
        "Wire the Swift launcher to consume shared runtime-plan and bundle surfaces.",
        "Keep compatibility profiles and prefix presets governed through lint and docs.",
        "Keep repo and wiki handoff docs aligned with the checked-in source of truth."
    )
    includedFiles = @(
        "README.md",
        "CONTRIBUTING.md",
        "SECURITY.md",
        "docs/AI_HANDOFF.md",
        "docs/PROJECT_STATUS.json",
        "docs/REPO_MAP.md",
        "docs/MILESTONES.md",
        "docs/EXECUTION_PLAN.md",
        "docs/PHASE4_CROSSOVER_COMPETITOR_ROADMAP.md",
        "docs/GAME_COMPATIBILITY_CATALOG.md",
        "docs/MAC_RUNTIME_SMOKE_TEST.md",
        "profiles/README.md",
        "profile-lint.txt",
        "compatibility-catalog.json"
    )
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath

Write-Host "AI handoff bundle:  $OutputDir"
Write-Host "Handoff manifest:   $manifestPath"
