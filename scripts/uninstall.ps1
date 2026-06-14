#Requires -RunAsAdministrator

param(
    [string]$DevConPath = "$PSScriptRoot\..\Drivers\devcon.exe"
)

$DriverDest = "$env:SystemRoot\System32\drivers\VirtualDualSense.sys"
$ServiceName = "DualProxySvc"
$DriverName = "VirtualDualSense"
$DevNodeId = "ROOT\VirtualDualSense"
$SidebandDevice = "\\.\VirtualDualSense0"
$CertSubject = "CN=DualProxy Test Certificate"
$LogPath = "$env:ProgramData\DualProxy\logs"
$CertName = "DualProxy Test Certificate"

$ErrorActionPreference = "Continue"

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

function Force-DeleteSystemFile {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return }
    Write-Warn "Removing: $Path"
    try {
        Remove-Item -Path $Path -Force -ErrorAction Stop
    } catch {
        cmd /c "takeown /f `"$Path`" >nul 2>&1 && icacls `"$Path`" /grant Administrators:F >nul 2>&1 && del /f /q /a `"$Path`" >nul 2>&1"
    }
}

Write-Host "========================================" -ForegroundColor Yellow
Write-Host "  DualProxy Uninstallation Script" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Yellow
Write-Host ""

$allClean = $true

# Step 1: Stop and delete DualProxySvc user-mode service
Write-Step "Removing DualProxySvc service..."
$service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($service) {
    if ($service.Status -eq 'Running') {
        Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
    }
    sc.exe delete $ServiceName 2>&1 | Out-Null
    Start-Sleep -Seconds 1
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service) {
        Write-Fail "Could not delete service '$ServiceName'"
        $allClean = $false
    } else {
        Write-Success "Service '$ServiceName' deleted"
    }
} else {
    Write-Success "Service '$ServiceName' not found"
}

# Step 2: Stop and remove VirtualDualSense kernel driver service
Write-Step "Removing VirtualDualSense kernel driver..."
$drvSvc = Get-Service -Name $DriverName -ErrorAction SilentlyContinue
if ($drvSvc) {
    if ($drvSvc.Status -eq 'Running') {
        cmd /c "sc stop $DriverName 2>&1" | Out-Null
        Start-Sleep -Seconds 2
    }
    $removed = $false
    for ($i = 0; $i -lt 5; $i++) {
        $output = cmd /c "sc delete $DriverName 2>&1"
        Start-Sleep -Seconds 1
        $svcCheck = Get-Service -Name $DriverName -ErrorAction SilentlyContinue
        if (-not $svcCheck) {
            $removed = $true
            break
        }
    }
    if (-not $removed) {
        # Fallback: nuke the service registry key directly
        $keyPath = "SYSTEM\CurrentControlSet\Services\$DriverName"
        cmd /c "reg delete `"HKLM\$keyPath`" /f 2>&1" | Out-Null
        $svcCheck = Get-Service -Name $DriverName -ErrorAction SilentlyContinue
        if (-not $svcCheck) {
            $removed = $true
        }
    }
    if ($removed) {
        Write-Success "Kernel driver service removed"
    } else {
        Write-Fail "Could not remove kernel driver service"
        $allClean = $false
    }
} else {
    Write-Success "Kernel driver service not found"
}

# Step 3: Remove device node
Write-Step "Removing device node..."
$result = & $DevConPath remove $DevNodeId 2>&1
if ($LASTEXITCODE -eq 0 -or $result -match "No devices") {
    Write-Success "Device node removed"
} else {
    Write-Warn "devcon remove result: $result"
}
Start-Sleep -Seconds 2

# Step 4: Remove stale driver packages from store
Write-Step "Removing driver packages from store..."
Get-ChildItem "$env:SystemRoot\INF\oem*.inf" -ErrorAction SilentlyContinue | ForEach-Object {
    $hit = Select-String -Path $_.FullName -Pattern "VirtualDualSense" -Quiet -ErrorAction SilentlyContinue
    if ($hit) {
        Write-Warn "Removing driver package '$($_.Name)'..."
        pnputil /delete-driver $_.Name /uninstall /force 2>&1 | Out-Null
    }
}

# Step 5: Delete driver .sys file
Write-Step "Removing driver file..."
Force-DeleteSystemFile -Path $DriverDest
if (Test-Path $DriverDest) {
    Write-Fail "Could not remove '$DriverDest'"
    $allClean = $false
} else {
    Write-Success "Driver file removed"
}

# Step 6: Remove service binary
Write-Step "Removing service binary..."
$svcPath = Join-Path $PSScriptRoot "..\src\x64\Release\DualProxySvc.exe"
if (Test-Path $svcPath) {
    Write-Warn "Service binary at $svcPath (leave as-is or delete manually)"
}

# Step 7: Remove catalog file from source directory
Write-Step "Removing catalog file..."
$catPath = "G:\Code\OpenCode\Dual\src\VirtualDualSense\VirtualDualSense.cat"
if (Test-Path $catPath) {
    Remove-Item -Path $catPath -Force -ErrorAction SilentlyContinue
    Write-Success "Catalog file removed"
} else {
    Write-Success "Catalog file not found"
}

# Step 8: Remove test certificate from stores
Write-Step "Removing test certificate from Trusted stores..."
$thumbprint = ""
$existingCert = Get-ChildItem "Cert:\LocalMachine\My" | Where-Object { $_.Subject -eq $CertSubject } | Select-Object -First 1
if ($existingCert) {
    $thumbprint = $existingCert.Thumbprint
    
    # Export to temp file for certutil
    $tempCertFile = "$env:TEMP\DualProxyTestCert.cer"
    $null = $existingCert | Export-Certificate -FilePath $tempCertFile -ErrorAction SilentlyContinue
    
    # Remove from Trusted Root
    $rootCert = Get-ChildItem "Cert:\LocalMachine\Root" | Where-Object { $_.Thumbprint -eq $thumbprint }
    if ($rootCert) {
        certutil -delstore "Root" $thumbprint 2>&1 | Out-Null
        Write-Success "Removed from Trusted Root"
    }
    
    # Remove from Trusted Publishers
    $pubCert = Get-ChildItem "Cert:\LocalMachine\TrustedPublisher" | Where-Object { $_.Thumbprint -eq $thumbprint }
    if ($pubCert) {
        certutil -delstore "TrustedPublisher" $thumbprint 2>&1 | Out-Null
        Write-Success "Removed from Trusted Publishers"
    }
    
    # Remove from Personal store
    Remove-Item -Path $existingCert.PSPath -Force -ErrorAction SilentlyContinue
    Write-Success "Certificate removed from Personal store"
    
    Remove-Item $tempCertFile -ErrorAction SilentlyContinue
} else {
    Write-Success "Test certificate not found"
}

# Step 9: Remove stale VHF HID device instances from previous installations
Write-Step "Removing stale VHF HID device instances..."
$staleCount = 0
$staleDevices = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object {
    $_.InstanceId -match 'HID_DEVICE_SYSTEM_VHF' -and $_.Status -eq 'Unknown'
}
foreach ($dev in $staleDevices) {
    $result = & $DevConPath remove "@$($dev.InstanceId)" 2>&1
    $staleCount++
}
if ($staleCount -gt 0) {
    Write-Success "Removed $staleCount stale VHF device(s)"
} else {
    Write-Success "No stale VHF devices found"
}

# Step 10: Remove log directory
Write-Step "Removing log files..."
if (Test-Path $LogPath) {
    try {
        Remove-Item -Path "$LogPath\*" -Force -Recurse -ErrorAction Stop
        Remove-Item -Path $LogPath -Force -ErrorAction Stop
        Write-Success "Log files removed"
    } catch {
        Write-Warn "Could not remove log directory: $($_.Exception.Message)"
    }
} else {
    Write-Success "Log directory not found"
}

# Step 10: Full verification
Write-Step "Running full verification..."

# Verify driver device
$devConResult = & $DevConPath find $DevNodeId 2>&1
if ($devConResult -match "VirtualDualSense") {
    Write-Fail "Driver '$DevNodeId' is still present"
    $allClean = $false
} else {
    Write-Success "Driver '$DevNodeId' not found"
}

# Verify user-mode service
$service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($service) {
    Write-Fail "Service '$ServiceName' still exists"
    $allClean = $false
} else {
    Write-Success "Service '$ServiceName' does not exist"
}

# Verify kernel driver service
$drvSvc = Get-Service -Name $DriverName -ErrorAction SilentlyContinue
if ($drvSvc) {
    Write-Fail "Kernel driver service '$DriverName' still exists"
    $allClean = $false
} else {
    Write-Success "Kernel driver service '$DriverName' does not exist"
}

# Verify driver file
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

# Verify no driver packages in store
$storeHit = $false
Get-ChildItem "$env:SystemRoot\INF\oem*.inf" -ErrorAction SilentlyContinue | ForEach-Object {
    $hit = Select-String -Path $_.FullName -Pattern "VirtualDualSense" -Quiet -ErrorAction SilentlyContinue
    if ($hit) {
        Write-Fail "Driver package '$($_.Name)' still in store"
        $storeHit = $true
        $allClean = $false
    }
}
if (-not $storeHit) {
    Write-Success "No driver packages in store"
}

# Verify certificate removed
$certStillExists = Get-ChildItem "Cert:\LocalMachine\My" | Where-Object { $_.Subject -eq $CertSubject } | Select-Object -First 1
if ($certStillExists) {
    Write-Fail "Test certificate still exists in Personal store"
    $allClean = $false
} else {
    Write-Success "Test certificate removed"
}

# Verify certificate not in Trusted stores
$rootCert = Get-ChildItem "Cert:\LocalMachine\Root" | Where-Object { $_.Subject -eq $CertSubject } | Select-Object -First 1
if ($rootCert) {
    Write-Fail "Certificate still in Trusted Root"
    $allClean = $false
} else {
    Write-Success "Certificate not in Trusted Root"
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
