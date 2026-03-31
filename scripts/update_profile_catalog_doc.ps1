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
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repoRoot "docs\GAME_COMPATIBILITY_CATALOG.md"
}

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$ProfilesDir = [System.IO.Path]::GetFullPath($ProfilesDir)
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)

$toolPath = Join-Path $BuildDir "tools\mvrvb_profile_catalog.exe"
if (-not (Test-Path $toolPath)) {
    Write-Host "Profile catalog tool not found; building host checks first..."
    & (Join-Path $repoRoot "scripts\run_host_checks.ps1") -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "Host-check build failed with exit code $LASTEXITCODE"
    }
}

$directory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($directory)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

$tempPath = Join-Path $BuildDir "profile-catalog.generated.md"
& $toolPath "--profiles-dir" $ProfilesDir "--markdown" "--out" $tempPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Markdown profile-catalog generation failed with exit code $LASTEXITCODE"
}

$generated = Get-Content -Path $tempPath -Raw
$banner = @"
<!--
  Generated from checked-in .mvrvb-profile files via scripts/update_profile_catalog_doc.ps1.
  Do not hand-edit this file without regenerating it from the profiles tree.
-->

"@
[System.IO.File]::WriteAllText($OutputPath, $banner + $generated)

Write-Host "Updated compatibility catalog doc: $OutputPath"
