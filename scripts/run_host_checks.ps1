param(
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build-host"
}

$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
    [System.Environment]::GetEnvironmentVariable("Path", "User")

$clang = Get-Command clang -ErrorAction Stop
$clangxx = Get-Command clang++ -ErrorAction Stop
$cmake = Get-Command cmake -ErrorAction Stop
$ctest = Get-Command ctest -ErrorAction Stop
Get-Command ninja -ErrorAction Stop | Out-Null

Write-Host "Configuring host-side checks in $BuildDir"
& $cmake.Source `
    -S (Join-Path $repoRoot "host-tests") `
    -B $BuildDir `
    -G Ninja `
    -DCMAKE_C_COMPILER="$($clang.Source)" `
    -DCMAKE_CXX_COMPILER="$($clangxx.Source)" `
    -DMVRVB_REPO_ROOT="$repoRoot"
if ($LASTEXITCODE -ne 0) {
    throw "Host-check configure failed with exit code $LASTEXITCODE"
}

Write-Host "Building host-side targets"
& $cmake.Source --build $BuildDir
if ($LASTEXITCODE -ne 0) {
    throw "Host-check build failed with exit code $LASTEXITCODE"
}

Write-Host "Running host-side test suite"
& $ctest.Source --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "Host-check test run failed with exit code $LASTEXITCODE"
}
