#Requires -RunAsAdministrator

<#
.SYNOPSIS
    Uninstalls VirtualDualSense driver and DualProxySvc service.
.DESCRIPTION
    Stops service, removes driver, deletes service, cleans up files,
    and verifies no leftovers remain.
#>

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

# Main
Write-Host "========================================" -ForegroundColor Yellow
Write-Host "  DualProxy Uninstallation Script" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Yellow
Write-Host ""

$removalOk = $true

try {
    # Step 1: Stop the service
    Write-Step "Stopping DualProxySvc service..."
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service) {
        if ($service.Status -eq 'Running') {
            Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
            Start-Sleep -Seconds 3
            $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
            if ($service.Status -eq 'Running') {
                Write-Fail "Could not stop service '$ServiceName'"
                $removalOk = $false
            } else {
                Write-Success "Service '$ServiceName' stopped"
            }
        } else {
            Write-Warn "Service '$ServiceName' was not running (Status: $($service.Status))"
        }
    } else {
        Write-Success "Service '$ServiceName' not found (already removed)"
    }

    # Step 2: Uninstall the driver
    Write-Step "Removing VirtualDualSense driver..."
    $devConResult = & $DevConPath remove $DevNodeId 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Driver '$DevNodeId' removed via devcon"
    } else {
        # devcon might return non-zero if device not found - that's fine
        Write-Warn "devcon remove returned exit code $LASTEXITCODE (may be OK if already removed)"
    }
    Start-Sleep -Seconds 2

    # Step 3: Delete the service
    Write-Step "Deleting DualProxySvc service..."
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service) {
        sc.exe delete $ServiceName 2>&1 | Out-Null
        Start-Sleep -Seconds 2
        $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
        if ($service) {
            Write-Fail "Could not delete service '$ServiceName'"
            $removalOk = $false
        } else {
            Write-Success "Service '$ServiceName' deleted"
        }
    } else {
        Write-Success "Service '$ServiceName' not found (already deleted)"
    }

    # Step 4: Remove driver file
    Write-Step "Removing driver file..."
    if (Test-Path $DriverDest) {
        Remove-Item -Path $DriverDest -Force -ErrorAction SilentlyContinue
        if (Test-Path $DriverDest) {
            Write-Fail "Could not remove '$DriverDest'"
            $removalOk = $false
        } else {
            Write-Success "Driver file removed from '$DriverDest'"
        }
    } else {
        Write-Success "Driver file not found at '$DriverDest' (already removed)"
    }

    # Step 5: Full verification
    Write-Step "Running full verification..."

    $allClean = $true

    # Verify driver (devcon find should return empty)
    $devConResult = & $DevConPath find $DevNodeId 2>&1
    if ($devConResult -match "VirtualDualSense") {
        Write-Fail "Driver '$DevNodeId' is still present in devcon"
        $allClean = $false
    } else {
        Write-Success "Driver '$DevNodeId' not found in devcon"
    }

    # Verify service
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service) {
        Write-Fail "Service '$ServiceName' still exists"
        $allClean = $false
    } else {
        Write-Success "Service '$ServiceName' does not exist"
    }

    # Verify file
    if (Test-Path $DriverDest) {
        Write-Fail "Driver file '$DriverDest' still exists"
        $allClean = $false
    } else {
        Write-Success "Driver file does not exist"
    }

    # Verify sideband device
    try {
        $handle = [System.IO.File]::Open($SidebandDevice, 'Open', 'Read', 'Write')
        $handle.Close()
        Write-Fail "Sideband device '$SidebandDevice' is still accessible"
        $allClean = $false
    } catch {
        Write-Success "Sideband device '$SidebandDevice' is not accessible"
    }

    Write-Host ""
    if ($allClean) {
        Write-Host "========================================" -ForegroundColor Green
        Write-Host "  UNINSTALLATION COMPLETE - NO LEFTOVERS" -ForegroundColor Green
        Write-Host "========================================" -ForegroundColor Green
        exit 0
    } else {
        Write-Host "========================================" -ForegroundColor Red
        Write-Host "  UNINSTALL WARNINGS - SOME LEFTOVERS" -ForegroundColor Red
        Write-Host "  Manual cleanup may be needed." -ForegroundColor Red
        Write-Host "========================================" -ForegroundColor Red
        exit 1
    }
}
catch {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "  UNINSTALLATION FAILED!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Error: $_"
    exit 2
}
