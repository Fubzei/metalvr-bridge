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

$launchPlanJson = Join-Path $OutputDir "launch-plan.json"
$catalogJson = Join-Path $OutputDir "compatibility-catalog.json"
$lintReport = Join-Path $OutputDir "profile-lint.txt"
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
        launchPlanJson = $launchPlanJson
        launchPlanReport = [System.IO.Path]::GetFullPath((Join-Path $OutputDir "launch-plan.txt"))
        setupChecklist = [System.IO.Path]::GetFullPath((Join-Path $OutputDir "launch-plan.md"))
        bashSetupScript = [System.IO.Path]::GetFullPath((Join-Path $OutputDir "launch-plan.setup.sh"))
        powershellSetupScript = [System.IO.Path]::GetFullPath((Join-Path $OutputDir "launch-plan.setup.ps1"))
        bashLaunchScript = [System.IO.Path]::GetFullPath((Join-Path $OutputDir "launch-plan.sh"))
        powershellLaunchScript = [System.IO.Path]::GetFullPath((Join-Path $OutputDir "launch-plan.ps1"))
        compatibilityCatalogJson = $catalogJson
        compatibilityCatalogReport = [System.IO.Path]::GetFullPath((Join-Path $OutputDir "compatibility-catalog.txt"))
        compatibilityCatalogMarkdown = [System.IO.Path]::GetFullPath((Join-Path $OutputDir "compatibility-catalog.md"))
        profileLintReport = $lintReport
    }
}

$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath

Write-Host "Runtime bundle directory: $OutputDir"
Write-Host "Bundle manifest:         $manifestPath"
