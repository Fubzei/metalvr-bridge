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
    $OutputPath = Join-Path $repoRoot "docs\GAME_COMPATIBILITY_CATALOG.json"
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

& $toolPath "--profiles-dir" $ProfilesDir "--json" "--out" $OutputPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "JSON profile-catalog generation failed with exit code $LASTEXITCODE"
}

$formattedJson = Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json
$formattedJson | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $OutputPath

Write-Host "Updated compatibility catalog JSON: $OutputPath"
