param(
    [string]$ProfilesDir = "",
    [string]$BuildDir = "",
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build-host"
}
if ([string]::IsNullOrWhiteSpace($ProfilesDir)) {
    $ProfilesDir = Join-Path $repoRoot "profiles"
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$ProfilesDir = [System.IO.Path]::GetFullPath($ProfilesDir)

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $BuildDir "exports\profile-catalog.json"
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

$toolPath = Join-Path $BuildDir "tools\mvrvb_profile_catalog.exe"
if (-not (Test-Path $toolPath)) {
    Write-Host "Profile catalog tool not found; building host checks first..."
    & (Join-Path $repoRoot "scripts\run_host_checks.ps1") -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "Host-check build failed with exit code $LASTEXITCODE"
    }
}

$baseName = [System.IO.Path]::GetFileNameWithoutExtension($OutputPath)
$reportPath = [System.IO.Path]::GetFullPath(
    (Join-Path $outputDirectory ($baseName + ".txt"))
)

$commonArgs = @(
    "--profiles-dir", $ProfilesDir
)

& $toolPath @commonArgs "--json" "--out" $OutputPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "JSON profile-catalog export failed with exit code $LASTEXITCODE"
}

& $toolPath @commonArgs "--out" $reportPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Report profile-catalog export failed with exit code $LASTEXITCODE"
}

Write-Host "JSON profile catalog:  $OutputPath"
Write-Host "Report profile catalog: $reportPath"
