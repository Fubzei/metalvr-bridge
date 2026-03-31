param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [string]$Launcher = "",
    [string]$Store = "",
    [string]$ProfilesDir = "",
    [string]$BuildDir = "",
    [string]$OutputPath = "",
    [string]$WineBinary = "wine",
    [string]$PrefixPath = "",
    [string]$WorkingDirectory = ""
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

$safeStem = [System.IO.Path]::GetFileNameWithoutExtension($Executable)
if ([string]::IsNullOrWhiteSpace($safeStem)) {
    $safeStem = "runtime-launch-plan"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $BuildDir "$safeStem.launch-plan.json"
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)

$toolPath = Join-Path $BuildDir "tools\mvrvb_runtime_plan_preview.exe"
if (-not (Test-Path $toolPath)) {
    Write-Host "Preview tool not found; building host checks first..."
    & (Join-Path $repoRoot "scripts\run_host_checks.ps1") -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "Host-check build failed with exit code $LASTEXITCODE"
    }
}

$directory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($directory)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

$baseName = [System.IO.Path]::GetFileNameWithoutExtension($OutputPath)
$baseDirectory = Split-Path -Parent $OutputPath
$reportPath = [System.IO.Path]::GetFullPath(
    (Join-Path $baseDirectory ($baseName + ".txt"))
)
$bashPath = [System.IO.Path]::GetFullPath(
    (Join-Path $baseDirectory ($baseName + ".sh"))
)
$powerShellPath = [System.IO.Path]::GetFullPath(
    (Join-Path $baseDirectory ($baseName + ".ps1"))
)
$checklistPath = [System.IO.Path]::GetFullPath(
    (Join-Path $baseDirectory ($baseName + ".md"))
)

$commonArgs = @(
    "--profiles-dir", $ProfilesDir,
    "--exe", $Executable
)
if (-not [string]::IsNullOrWhiteSpace($Launcher)) {
    $commonArgs += @("--launcher", $Launcher)
}
if (-not [string]::IsNullOrWhiteSpace($Store)) {
    $commonArgs += @("--store", $Store)
}

& $toolPath @commonArgs "--json" "--out" $OutputPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "JSON launch-plan export failed with exit code $LASTEXITCODE"
}

& $toolPath @commonArgs "--out" $reportPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Report launch-plan export failed with exit code $LASTEXITCODE"
}

& $toolPath @commonArgs "--checklist" "--out" $checklistPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Checklist launch-plan export failed with exit code $LASTEXITCODE"
}

if ([string]::IsNullOrWhiteSpace($WineBinary)) {
    throw "WineBinary cannot be empty"
}

$scriptArgs = @(
    "--input", $OutputPath,
    "--exe", $Executable,
    "--wine-binary", $WineBinary
)
if (-not [string]::IsNullOrWhiteSpace($PrefixPath)) {
    $scriptArgs += @("--prefix", $PrefixPath)
}
if (-not [string]::IsNullOrWhiteSpace($WorkingDirectory)) {
    $scriptArgs += @("--working-dir", $WorkingDirectory)
}

& $toolPath @scriptArgs "--bash" "--out" $bashPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Bash launch-script export failed with exit code $LASTEXITCODE"
}

& $toolPath @scriptArgs "--powershell" "--out" $powerShellPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "PowerShell launch-script export failed with exit code $LASTEXITCODE"
}

Write-Host "JSON launch plan:  $OutputPath"
Write-Host "Report launch plan: $reportPath"
Write-Host "Setup checklist:   $checklistPath"
Write-Host "Bash launch script: $bashPath"
Write-Host "PowerShell script:  $powerShellPath"
