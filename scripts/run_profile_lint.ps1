param(
    [string]$ProfilesDir = "",
    [string]$BuildDir = "",
    [switch]$Strict
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
$toolPath = Join-Path $BuildDir "tools\mvrvb_profile_lint.exe"

if (-not (Test-Path $toolPath)) {
    Write-Host "Profile lint tool not found; building host checks first..."
    & (Join-Path $repoRoot "scripts\run_host_checks.ps1") -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "Host-check build failed with exit code $LASTEXITCODE"
    }
}

$args = @(
    "--profiles-dir", $ProfilesDir
)
if ($Strict) {
    $args += "--strict"
}

& $toolPath @args
if ($LASTEXITCODE -ne 0) {
    throw "Profile lint failed with exit code $LASTEXITCODE"
}
