param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [string]$Launcher = "",
    [string]$Store = "",
    [string]$PrefixPath = "",
    [string]$ProfilesDir = "",
    [string]$BuildDir = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build-host"
}
if ([string]::IsNullOrWhiteSpace($ProfilesDir)) {
    $ProfilesDir = Join-Path $repoRoot "profiles"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $safeName = [System.IO.Path]::GetFileNameWithoutExtension($Executable)
    if ([string]::IsNullOrWhiteSpace($safeName)) {
        $safeName = "runtime-bundle"
    }
    $OutputDir = Join-Path $BuildDir ("exports\" + $safeName + "-bundle")
}

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$ProfilesDir = [System.IO.Path]::GetFullPath($ProfilesDir)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$launchPlanJsonName = "launch-plan.json"
$launchPlanReportName = "launch-plan.txt"
$setupChecklistName = "launch-plan.md"
$bashSetupScriptName = "launch-plan.setup.sh"
$powerShellSetupScriptName = "launch-plan.setup.ps1"
$bashLaunchScriptName = "launch-plan.sh"
$powerShellLaunchScriptName = "launch-plan.ps1"
$catalogJsonName = "compatibility-catalog.json"
$catalogReportName = "compatibility-catalog.txt"
$catalogMarkdownName = "compatibility-catalog.md"
$lintReportName = "profile-lint.txt"

$launchPlanJson = Join-Path $OutputDir $launchPlanJsonName
$catalogJson = Join-Path $OutputDir $catalogJsonName
$lintReport = Join-Path $OutputDir $lintReportName
$manifestPath = Join-Path $OutputDir "bundle-manifest.json"

& (Join-Path $repoRoot "scripts\export_runtime_plan.ps1") `
    -Executable $Executable `
    -Launcher $Launcher `
    -Store $Store `
    -PrefixPath $PrefixPath `
    -ProfilesDir $ProfilesDir `
    -BuildDir $BuildDir `
    -OutputPath $launchPlanJson
if ($LASTEXITCODE -ne 0) {
    throw "Runtime-plan bundle export failed with exit code $LASTEXITCODE"
}

& (Join-Path $repoRoot "scripts\export_profile_catalog.ps1") `
    -ProfilesDir $ProfilesDir `
    -BuildDir $BuildDir `
    -OutputPath $catalogJson
if ($LASTEXITCODE -ne 0) {
    throw "Compatibility-catalog bundle export failed with exit code $LASTEXITCODE"
}

$lintArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $repoRoot "scripts\run_profile_lint.ps1"),
    "-ProfilesDir", $ProfilesDir,
    "-BuildDir", $BuildDir
)
$lintOutput = & powershell @lintArgs 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Profile lint bundle export failed with exit code $LASTEXITCODE"
}
$lintOutput | Set-Content -LiteralPath $lintReport

$manifest = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    executable = $Executable
    launcher = $Launcher
    store = $Store
    prefixPath = $PrefixPath
    profilesDir = $ProfilesDir
    files = [ordered]@{
        launchPlanJson = $launchPlanJsonName
        launchPlanReport = $launchPlanReportName
        setupChecklist = $setupChecklistName
        bashSetupScript = $bashSetupScriptName
        powershellSetupScript = $powerShellSetupScriptName
        bashLaunchScript = $bashLaunchScriptName
        powershellLaunchScript = $powerShellLaunchScriptName
        compatibilityCatalogJson = $catalogJsonName
        compatibilityCatalogReport = $catalogReportName
        compatibilityCatalogMarkdown = $catalogMarkdownName
        profileLintReport = $lintReportName
    }
}

$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath

Write-Host "Runtime bundle directory: $OutputDir"
Write-Host "Bundle manifest:         $manifestPath"
