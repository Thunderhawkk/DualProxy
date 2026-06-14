#Requires -RunAsAdministrator

<#
.SYNOPSIS
    Installs VirtualDualSense driver and DualProxySvc service.
.DESCRIPTION
    Checks for existing installation, removes it, installs fresh,
    and verifies everything is working.
#>

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$SolutionDir = "$PSScriptRoot\..\src"
)

$DriverInfPath = "$SolutionDir\VirtualDualSense\VirtualDualSense.inf"
$DriverSysPath = "$SolutionDir\$Platform\$Configuration\VirtualDualSense.sys"
$ServiceExePath = "$SolutionDir\$Platform\$Configuration\DualProxySvc.exe"
$TrayExePath = "$SolutionDir\$Platform\$Configuration\DualProxyTray.exe"

$DevConPath = Resolve-Path (Join-Path $PSScriptRoot "..\Drivers\devcon.exe")
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

function Force-DeleteSystemFile {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return }
    try {
        Remove-Item -Path $Path -Force -ErrorAction Stop
    } catch {
        Write-Warn "Remove-Item failed, trying takeown/icacls..."
        cmd /c "takeown /f `"$Path`" >nul 2>&1 && icacls `"$Path`" /grant Administrators:F >nul 2>&1 && del /f /q /a `"$Path`" >nul 2>&1"
    }
}

function Ensure-TestCertificate {
    Write-Step "Ensuring test certificate for driver signing..."
    
    $certSubject = "CN=DualProxy Test Certificate"
    $certStore = "Cert:\LocalMachine\My"
    
    # Find existing certificate
    $existingCert = Get-ChildItem $certStore | Where-Object { $_.Subject -eq $certSubject } | Select-Object -First 1
    
    if (-not $existingCert) {
        Write-Warn "Creating new test certificate..."
        $newCert = New-SelfSignedCertificate `
            -Subject $certSubject `
            -CertStoreLocation $certStore `
            -KeyUsage DigitalSignature `
            -Type CodeSigningCert `
            -NotAfter (Get-Date).AddYears(10) `
            -KeyAlgorithm RSA `
            -KeyLength 2048 `
            -HashAlgorithm SHA256 `
            -ErrorAction Stop
        Write-Success "Created certificate: $($newCert.Thumbprint)"
        $existingCert = $newCert
    } else {
        Write-Success "Found existing certificate: $($existingCert.Thumbprint)"
    }
    
    $thumbprint = $existingCert.Thumbprint
    
    # Export cert to temp file for certutil
    $tempCertFile = "$env:TEMP\DualProxyTestCert.cer"
    $null = $existingCert | Export-Certificate -FilePath $tempCertFile -ErrorAction Stop
    
    # Add to Trusted Root
    Write-Warn "Adding certificate to Trusted Root store..."
    $origEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $result = certutil -addstore "Root" $tempCertFile 2>&1
    $ErrorActionPreference = $origEAP
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Added to Trusted Root"
    } else {
        Write-Warn "Trusted Root add: $($result -join '`n')"
    }
    
    # Add to Trusted Publishers
    Write-Warn "Adding certificate to Trusted Publishers store..."
    $origEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $result = certutil -addstore "TrustedPublisher" $tempCertFile 2>&1
    $ErrorActionPreference = $origEAP
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Added to Trusted Publishers"
    } else {
        Write-Warn "Trusted Publishers add: $($result -join '`n')"
    }
    
    Remove-Item $tempCertFile -ErrorAction SilentlyContinue
    
    # Return ONLY the thumbprint - suppress all other output
    return $thumbprint
}

function Get-SdkToolPath {
    param([string]$ToolName)
    
    # Search common SDK locations - look for version directories then arch subdirs
    $basePaths = @(
        "C:\Program Files (x86)\Windows Kits\10\bin",
        "C:\Program Files\Windows Kits\10\bin",
        "$env:ProgramFiles (x86)\Windows Kits\10\bin",
        "$env:ProgramFiles\Windows Kits\10\bin"
    )
    
    # Inf2Cat is 32-bit only; others are 64-bit
    $archs = if ($ToolName -eq "Inf2Cat") { @("x86") } else { @("x64", "x86") }
    
    foreach ($base in $basePaths) {
        if (-not (Test-Path $base)) { continue }
        # Find version directories (10.0.xxxxx.x)
        $versionDirs = Get-ChildItem -Path $base -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^10\.\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending
        foreach ($verDir in $versionDirs) {
            foreach ($arch in $archs) {
                $archDir = Join-Path $verDir.FullName $arch
                if (Test-Path $archDir) {
                    $toolPath = Join-Path $archDir "$ToolName.exe"
                    if (Test-Path $toolPath) {
                        return $toolPath
                    }
                }
            }
        }
    }
    
    return $null
}

function Sign-DriverFiles {
    param([string]$Thumbprint, [string]$InfPath, [string]$SysPath)
    
    Write-Step "Signing driver files with test certificate..."
    
    $signtoolPath = Get-SdkToolPath "signtool"
    if (-not $signtoolPath) {
        throw "signtool.exe not found. Install Windows 10 SDK."
    }
    Write-Warn "Using signtool: $signtoolPath"
    
    $inf2catPath = Get-SdkToolPath "Inf2Cat"
    if (-not $inf2catPath) {
        throw "Inf2Cat.exe not found. Install Windows 10 SDK with driver tools."
    }
    Write-Warn "Using Inf2Cat: $inf2catPath"
    
    $infDir = Split-Path $InfPath -Parent
    $catPath = Join-Path $infDir "VirtualDualSense.cat"
    $infDirSysPath = Join-Path $infDir "VirtualDualSense.sys"
    
    # Step 1: Create catalog file from INF using Inf2Cat
    Write-Warn "Creating catalog file with Inf2Cat..."
    $origEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $result = & $inf2catPath /driver:$infDir /os:10_X64 /verbose 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $origEAP
    
    if ($exitCode -ne 0) {
        throw "Inf2Cat failed. Exit code: $exitCode`nOutput: $($result -join '`n')"
    }
    Write-Success "Catalog file created: $catPath"
    
    # Step 2: Sign the .sys file in INF directory (used by pnputil)
    Write-Warn "Signing $infDirSysPath..."
    $origEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $result = & $signtoolPath sign /fd SHA256 /sha1 $Thumbprint /s My /sm /tr http://timestamp.digicert.com /td SHA256 /v $infDirSysPath 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $origEAP
    
    if ($exitCode -ne 0) {
        throw "signtool failed on INF directory .sys. Exit code: $exitCode`nOutput: $($result -join '`n')"
    }
    Write-Success "Signed INF directory .sys file"
    
    # Step 3: Sign the .sys file in system32/drivers
    Write-Warn "Signing $SysPath..."
    $origEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $result = & $signtoolPath sign /fd SHA256 /sha1 $Thumbprint /s My /sm /tr http://timestamp.digicert.com /td SHA256 /v $SysPath 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $origEAP
    
    if ($exitCode -ne 0) {
        throw "signtool failed on system32 .sys. Exit code: $exitCode`nOutput: $($result -join '`n')"
    }
    Write-Success "Signed system32 .sys file"
    
    # Step 4: Sign the .cat file
    Write-Warn "Signing $catPath..."
    $origEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $result = & $signtoolPath sign /fd SHA256 /sha1 $Thumbprint /s My /sm /tr http://timestamp.digicert.com /td SHA256 /v $catPath 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $origEAP
    
    if ($exitCode -ne 0) {
        throw "signtool failed on .cat. Exit code: $exitCode`nOutput: $($result -join '`n')"
    }
    Write-Success "Signed .cat file"
    
    return $catPath
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

    # Try to unload driver first
    cmd /c "fltmc unload $DriverName >nul 2>&1"

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
    Force-DeleteSystemFile -Path $DriverDest
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

    if (-not (Test-Path $DriverSysPath)) {
        throw "Driver sys not found at: $DriverSysPath"
    }

    # Properly unload driver and delete driver package from store BEFORE copying
    Write-Warn "Unloading existing driver and cleaning driver store..."
    $origEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    
    # 1. Remove device node (devcon remove)
    & $DevConPath remove $DevNodeId 2>&1 | Out-Null
    Start-Sleep -Seconds 2
    
    # 2. Try fltmc unload
    cmd /c "fltmc unload $DriverName >nul 2>&1"
    
    # 3. Stop service using sc.exe (works where Stop-Service fails)
    $drvSvc = Get-Service -Name $DriverName -ErrorAction SilentlyContinue
    if ($drvSvc -and $drvSvc.Status -eq 'Running') {
        Write-Warn "Stopping driver service '$DriverName' via sc.exe..."
        cmd /c "sc stop $DriverName 2>&1" | Out-Null
        Start-Sleep -Seconds 3
    }
    
    # 4. Delete driver packages from store (prevents file restoration)
    Get-ChildItem "$env:SystemRoot\INF\oem*.inf" -ErrorAction SilentlyContinue | ForEach-Object {
        $hit = Select-String -Path $_.FullName -Pattern "VirtualDualSense" -Quiet -ErrorAction SilentlyContinue
        if ($hit) {
            Write-Warn "Removing driver package '$($_.Name)' from store..."
            pnputil /delete-driver $_.Name /uninstall /force 2>&1 | Out-Null
        }
    }
    
    # 5. Disable service so it won't auto-load after reboot
    cmd /c "sc config $DriverName start= disabled 2>&1" | Out-Null
    
    $ErrorActionPreference = $origEAP

    # Ensure destination is writable (may be protected if left over from prior install)
    if (Test-Path $DriverDest) {
        Force-DeleteSystemFile -Path $DriverDest
    }

    # Copy with verification
    Copy-Item -Path $DriverSysPath -Destination $DriverDest -Force
    Start-Sleep -Seconds 1
    
    # Verify copy succeeded (check file size matches)
    $srcSize = (Get-Item $DriverSysPath).Length
    $dstSize = (Get-Item $DriverDest).Length
    if ($srcSize -ne $dstSize) {
        throw "Driver copy verification failed: source=$srcSize dest=$dstSize"
    }
    Write-Success "Copied driver to '$DriverDest' (verified $dstSize bytes)"

    # devcon install needs the .sys alongside the INF as a source file
    $InfDir = Split-Path $DriverInfPath -Parent
    $SysInInfDir = Join-Path $InfDir "VirtualDualSense.sys"
    Write-Warn "Copying .sys alongside INF for devcon source resolution..."
    Copy-Item -Path $DriverSysPath -Destination $SysInInfDir -Force

    # Clean up stale driver packages from store
    Write-Warn "Checking for stale driver packages in store..."
    $origEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    Get-ChildItem "$env:SystemRoot\INF\oem*.inf" -ErrorAction SilentlyContinue | ForEach-Object {
        $hit = Select-String -Path $_.FullName -Pattern "VirtualDualSense" -Quiet -ErrorAction SilentlyContinue
        if ($hit) {
            Write-Warn "Removing stale driver package '$($_.Name)'..."
            pnputil /delete-driver $_.Name /force 2>&1 | Out-Null
        }
    }
    $ErrorActionPreference = $origEAP

    # Check test signing mode
    $testSigning = (bcdedit /enum | Select-String "testsigning\s+Yes") -ne $null
    if (-not $testSigning) {
        Write-Warn "Test signing mode is NOT enabled! Driver will not load without it."
        Write-Warn "Run: bcdedit /set testsigning on (then reboot)"
        throw "Test signing required but not enabled. Enable it and re-run."
    }

    # Create/ensure test certificate and sign driver files
    $thumbprint = Ensure-TestCertificate
    Sign-DriverFiles -Thumbprint $thumbprint -InfPath $DriverInfPath -SysPath $DriverDest

    # Step 1: Use pnputil to add driver package to store (processes AddService)
    Write-Warn "Adding driver package to store via pnputil..."
    $origEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $pnputilResult = pnputil /add-driver $DriverInfPath /install 2>&1
    $pnputilExit = $LASTEXITCODE
    $ErrorActionPreference = $origEAP

    if ($pnputilExit -ne 0) {
        Write-Warn "pnputil add-driver exit code: $pnputilExit"
        Write-Warn "Output: $($pnputilResult -join '`n')"
        throw "pnputil failed to add driver package."
    } else {
        Write-Success "Driver package added to store"
    }

    # Step 2: Create device node via devcon
    Write-Warn "Creating device node via devcon..."
    $origEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $result = & $DevConPath install $DriverInfPath $DevNodeId 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $origEAP

    $output = $result -join "`n"
    Write-Warn "devcon install exit code: $exitCode"

    # Wait for PnP to finish
    Write-Warn "Waiting for driver to initialize..."
    Start-Sleep -Seconds 5

    # Check/start driver service
    $drvSvc = Get-Service -Name $DriverName -ErrorAction SilentlyContinue
    if ($drvSvc) {
        Write-Warn "Driver service '$DriverName' status: $($drvSvc.Status)"
        if ($drvSvc.Status -ne 'Running') {
            Write-Warn "Attempting to start driver service..."
            try {
                Start-Service -Name $DriverName -ErrorAction Stop
                Write-Success "Driver service started"
            } catch {
                Write-Warn "Could not start driver service: $($_.Exception.Message)"
            }
        }
    } else {
        Write-Warn "Driver service '$DriverName' not found - attempting manual service creation..."
        # Manual service creation as fallback
        try {
            sc.exe create $DriverName binPath= "$DriverDest" type= kernel start= demand 2>&1 | Out-Null
            Write-Warn "Created driver service '$DriverName' manually"
            Start-Service -Name $DriverName -ErrorAction Stop
            Write-Success "Manually created driver service started"
        } catch {
            Write-Warn "Manual service creation failed: $($_.Exception.Message)"
        }
    }

    # Try accessing sideband device up to 3 times
    $deviceReady = $false
    for ($i = 1; $i -le 3; $i++) {
        try {
            $handle = [System.IO.File]::Open($SidebandDevice, 'Open', 'Read', 'Write')
            $handle.Close()
            $deviceReady = $true
            Write-Success "Sideband device accessible on attempt $i"
            break
        } catch {
            Write-Warn "Sideband device not ready (attempt $i/3), waiting..."
            Start-Sleep -Seconds 2
        }
    }

    if (-not $deviceReady) {
        throw "Device never became accessible. Check Event Viewer -> System and Setup logs for 'VirtualDualSense' or 'WDF' errors."
    }
    Write-Success "Driver installation completed"

    Start-Sleep -Seconds 3
}

function Install-Service {
    Write-Step "Installing service..."

    if (-not (Test-Path $ServiceExePath)) {
        throw "Service executable not found at: $ServiceExePath"
    }

    # Create service
    New-Service -Name $ServiceName `
        -BinaryPathName "`"$ServiceExePath`" -s" `
        -DisplayName "DualProxy DualSense Bridge Service" `
        -StartupType Automatic `
        -Description "Forwards input/output between a real Bluetooth DualSense controller and a virtual USB DualSense device" `
        -ErrorAction Stop

    Write-Success "Service '$ServiceName' created"

    # Start service
    try {
        Start-Service -Name $ServiceName -ErrorAction Stop
        Write-Success "Service '$ServiceName' started"
    } catch {
        Write-Fail "Start-Service failed: $($_.Exception.Message)"
        # Get more details from event log
        $eventLogs = Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Service Control Manager'; Level=1,2,3} -MaxEvents 5 -ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'DualProxySvc' } | Select-Object -First 3
        if ($eventLogs) {
            Write-Warn "Recent event log entries for DualProxySvc:"
            $eventLogs | ForEach-Object { Write-Warn "  $($_.TimeCreated): $($_.Message)" }
        }
        throw $_
    }
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
    $msg = "Error: " + $_.Exception.Message
    Write-Host $msg
    exit 2
}
