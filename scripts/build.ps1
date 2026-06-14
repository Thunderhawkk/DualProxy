param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$SolutionPath = Resolve-Path (Join-Path $PSScriptRoot "..\src\VirtualDualSense.sln")

# Find VS installation
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found"
    exit 1
}
$vsPath = & $vswhere -latest -property installationPath 2>$null
if (-not $vsPath) {
    Write-Error "Visual Studio not found"
    exit 1
}

$vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsDevCmd)) {
    Write-Error "VsDevCmd.bat not found at $vsDevCmd"
    exit 1
}

Write-Host "Building DualProxy ($Configuration|$Platform)..." -ForegroundColor Cyan

$buildCmd = "`"$vsDevCmd`" -arch=$Platform -host_arch=$Platform 2>`$null"
$buildCmd += " && msbuild `"$SolutionPath`""
$buildCmd += " /t:Rebuild"
$buildCmd += " /p:Configuration=$Configuration"
$buildCmd += " /p:Platform=$Platform"
$buildCmd += " /m"
$buildCmd += " /v:minimal"

$result = cmd /c "$buildCmd"
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "Build succeeded" -ForegroundColor Green
} else {
    Write-Host "Build failed (exit code $exitCode)" -ForegroundColor Red
}

exit $exitCode