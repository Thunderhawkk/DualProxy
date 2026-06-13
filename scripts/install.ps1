#Requires -RunAsAdministrator

<#
.SYNOPSIS
    Installs VirtualDualSense driver and DualProxySvc service.
.DESCRIPTION
    Checks for existing installation, removes it, installs fresh,
    and verifies everything is working.
#>

param(
    [string]$SolutionDir = "$PSScriptRoot\..\src",
    [string]$DriverInfPath = "$SolutionDir\VirtualDualSense\Release\VirtualDualSense.inf",
    [string]$DriverSysPath = "$SolutionDir\VirtualDualSense\Release\VirtualDualSense.sys",
    [string]$ServiceExePath = "$SolutionDir\DualProxySvc\Release\DualProxySvc.exe",
    [string]$TrayExePath = "$SolutionDir\DualProxyTray\Release\DualProxyTray.exe"
)

$DevConPath = "$PSScriptRoot\devcon.exe"
$DriverDest = "$env:SystemRoot\System32\drivers\VirtualDualSense.sys"
$ServiceName = "DualProxySvc"
$DriverName = "VirtualDualSense"
$DevNodeId = "ROOT\VirtualDualSense"
$SidebandDevice = "\\.\VirtualDualSense0"

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ">>> $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "    PASS: $Message" -ForegroundColor Green
}

function Write-Fail {
    param([string]$Message)
    Write-Host "    FAIL: $Message" -ForegroundColor Red
}

function Write-Warn {
    param([string]$Message)
    Write-Host "    WARN: $Message" -ForegroundColor Yellow
}

function Test-ExistingInstallation {
    Write-Step "Checking for existing installation..."

    $found = $false

    # Check service
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service) {
        Write-Warn "Found existing service '$ServiceName' (State: $($service.Status))"
        $found = $true
    }

    # Check driver
    $devConResult = & $DevConPath find $DevNodeId 2>&1
    if ($LASTEXITCODE -eq 0 -and $devConResult -match "VirtualDualSense") {
        Write-Warn "Found existing driver at '$DevNodeId'"
        $found = $true
    }

    # Check driver file
    if (Test-Path $DriverDest) {
        Write-Warn "Found existing driver file at '$DriverDest'"
        $found = $true
    }

    if (-not $found) {
        Write-Success "No existing installation found"
    }

    return $found
}

function Remove-Existing {
    Write-Step "Removing existing installation..."

    # Stop service
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service -and $service.Status -eq 'Running') {
        Write-Warn "Stopping service '$ServiceName'..."
        Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
    }

    # Uninstall driver (devcon remove)
    Write-Warn "Removing driver '$DevNodeId'..."
    $result = & $DevConPath remove $DevNodeId 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Warn "devcon remove returned exit code $LASTEXITCODE (may be OK if not fully installed)"
    }
    Start-Sleep -Seconds 2

    # Delete service
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service) {
        Write-Warn "Deleting service '$ServiceName'..."
        sc.exe delete $ServiceName 2>&1 | Out-Null
        Start-Sleep -Seconds 1
    }

    # Remove driver file
    if (Test-Path $DriverDest) {
        Write-Warn "Removing driver file '$DriverDest'..."
        Remove-Item -Path $DriverDest -Force -ErrorAction SilentlyContinue
    }
}

function Verify-Removed {
    Write-Step "Verifying removal..."

    $allClean = $true

    # Check service
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service) {
        Write-Fail "Service '$ServiceName' still exists"
        $allClean = $false
    } else {
        Write-Success "Service '$ServiceName' removed"
    }

    # Check driver
    $devConResult = & $DevConPath find $DevNodeId 2>&1
    if ($devConResult -match "VirtualDualSense") {
        Write-Fail "Driver '$DevNodeId' still present"
        $allClean = $false
    } else {
        Write-Success "Driver '$DevNodeId' removed"
    }

    # Check driver file
    if (Test-Path $DriverDest) {
        Write-Fail "Driver file '$DriverDest' still exists"
        $allClean = $false
    } else {
        Write-Success "Driver file removed"
    }

    # Check sideband (optional - device node may linger briefly)
    try {
        $handle = [System.IO.File]::Open($SidebandDevice, 'Open', 'Read', 'Write')
        $handle.Close()
        Write-Fail "Sideband device '$SidebandDevice' still accessible"
        $allClean = $false
    } catch {
        Write-Success "Sideband device '$SidebandDevice' not accessible (expected)"
    }

    return $allClean
}

function Install-Driver {
    Write-Step "Installing driver..."

    # Copy .sys to system32\drivers
    if (-not (Test-Path $DriverSysPath)) {
        throw "Driver sys not found at: $DriverSysPath"
    }
    Copy-Item -Path $DriverSysPath -Destination $DriverDest -Force
    Write-Success "Copied driver to '$DriverDest'"

    # Install via devcon
    $result = & $DevConPath install $DriverInfPath $DevNodeId 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "devcon install failed. Exit code: $LASTEXITCODE`nOutput: $result"
    }
    Write-Success "devcon install completed"

    Start-Sleep -Seconds 3
}

function Install-Service {
    Write-Step "Installing service..."

    if (-not (Test-Path $ServiceExePath)) {
        throw "Service executable not found at: $ServiceExePath"
    }

    # Create service
    New-Service -Name $ServiceName `
        -BinaryPathName "`"$ServiceExePath`"" `
        -DisplayName "DualProxy DualSense Bridge Service" `
        -StartupType Automatic `
        -Description "Forwards input/output between a real Bluetooth DualSense controller and a virtual USB DualSense device" `
        -ErrorAction Stop

    Write-Success "Service '$ServiceName' created"

    # Start service
    Start-Service -Name $ServiceName -ErrorAction Stop
    Write-Success "Service '$ServiceName' started"
}

function Verify-Installed {
    Write-Step "Verifying installation..."

    $allGood = $true

    # Check service
    Start-Sleep -Seconds 2
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $service) {
        Write-Fail "Service '$ServiceName' not found"
        return $false
    }
    if ($service.Status -ne 'Running') {
        Write-Fail "Service '$ServiceName' is not running (Status: $($service.Status))"
        $allGood = $false
    } else {
        Write-Success "Service '$ServiceName' is running"
    }

    # Check driver
    $devConResult = & $DevConPath status $DevNodeId 2>&1
    if ($devConResult -match "Running") {
        Write-Success "Driver '$DevNodeId' is running"
    } elseif ($devConResult -match "Stopped") {
        Write-Warn "Driver '$DevNodeId' exists but is stopped"
    } else {
        Write-Fail "Driver '$DevNodeId' not found"
        $allGood = $false
    }

    # Check driver file
    if (Test-Path $DriverDest) {
        Write-Success "Driver file present at '$DriverDest'"
    } else {
        Write-Fail "Driver file not found at '$DriverDest'"
        $allGood = $false
    }

    return $allGood
}

# Main
Write-Host "========================================" -ForegroundColor Yellow
Write-Host "  DualProxy Installation Script" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Yellow
Write-Host ""

try {
    if (Test-ExistingInstallation) {
        Remove-Existing
        if (-not (Verify-Removed)) {
            Write-Warn "Some components could not be fully removed. Continuing anyway..."
        }
    } else {
        Write-Success "Clean installation"
    }

    Install-Driver
    Install-Service

    if (Verify-Installed) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Green
        Write-Host "  INSTALLATION COMPLETE AND VERIFIED!" -ForegroundColor Green
        Write-Host "========================================" -ForegroundColor Green
        Write-Host ""
        Write-Host "Virtual DualSense controller should now appear in joy.cpl"
        Write-Host "Connect your Bluetooth DualSense controller to start using it."
        exit 0
    } else {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Red
        Write-Host "  INSTALLATION COMPLETED WITH WARNINGS" -ForegroundColor Red
        Write-Host "========================================" -ForegroundColor Red
        exit 1
    }
}
catch {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "  INSTALLATION FAILED!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Error: $_"
    exit 2
}
