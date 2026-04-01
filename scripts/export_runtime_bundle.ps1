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

function Resolve-ToolPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir,
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    $normalizedRelativePath = $RelativePath -replace '[\\/]+', [System.IO.Path]::DirectorySeparatorChar
    $candidates = @(
        (Join-Path $BuildDir ($normalizedRelativePath + ".exe")),
        (Join-Path $BuildDir $normalizedRelativePath)
    ) | Select-Object -Unique

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    return [System.IO.Path]::GetFullPath($candidates[0])
}

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
    $OutputDir = Join-Path (Join-Path $BuildDir "exports") ($safeName + "-bundle")
}

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$ProfilesDir = [System.IO.Path]::GetFullPath($ProfilesDir)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$toolRelativePath = Join-Path "tools" "mvrvb_runtime_bundle_builder"
$toolPath = Resolve-ToolPath -BuildDir $BuildDir -RelativePath $toolRelativePath
if (-not (Test-Path $toolPath)) {
    Write-Host "Runtime bundle builder not found; building host checks first..."
    $hostChecksScript = Join-Path (Join-Path $repoRoot "scripts") "run_host_checks.ps1"
    & $hostChecksScript -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "Host-check build failed with exit code $LASTEXITCODE"
    }

    $toolPath = Resolve-ToolPath -BuildDir $BuildDir -RelativePath $toolRelativePath
    if (-not (Test-Path $toolPath)) {
        throw "Runtime bundle builder was still not found after building host checks"
    }
}

$toolArgs = @(
    "--profiles-dir", $ProfilesDir,
    "--exe", $Executable,
    "--out-dir", $OutputDir
)
if (-not [string]::IsNullOrWhiteSpace($Launcher)) {
    $toolArgs += @("--launcher", $Launcher)
}
if (-not [string]::IsNullOrWhiteSpace($Store)) {
    $toolArgs += @("--store", $Store)
}
if (-not [string]::IsNullOrWhiteSpace($PrefixPath)) {
    $toolArgs += @("--prefix", $PrefixPath)
}

& $toolPath @toolArgs
if ($LASTEXITCODE -ne 0) {
    throw "Runtime bundle export failed with exit code $LASTEXITCODE"
}

$manifestPath = Join-Path $OutputDir "bundle-manifest.json"

Write-Host "Runtime bundle directory: $OutputDir"
Write-Host "Bundle manifest:         $manifestPath"
