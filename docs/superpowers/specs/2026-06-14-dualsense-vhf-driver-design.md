# DualSense VHF Emulator — Design Spec

## Overview
A Windows kernel driver + userspace service that emulates a USB-connected DualSense controller, forwarding all input/output between a real Bluetooth DualSense controller and games/applications. Includes systray toggle UI and install/uninstall scripts with verification.

## Architecture

```
┌─────────────────────────────────────────────┐
│  DualProxyTray.exe                          │
│  (System Tray UI)                           │
│  - Toggle on/off (icon + hotkey Ctrl+Win+D) │
│  - Status: green/red/yellow icon            │
│  - Settings in %LOCALAPPDATA%\DualProxy\    │
└───────────────────┬─────────────────────────┘
                    │ Named Pipe IPC
┌───────────────────▼─────────────────────────┐
│  DualProxySvc.exe                           │
│  (SYSTEM-level service)                     │
│  - Enumerates BT DualSense via HID API      │
│  - BT→USB report translation                │
│  - Continuous I/O forwarding loop           │
│  - Auto-pause on BT disconnect             │
└──────────┬────────────────────┬─────────────┘
           │ IOCTL              │ HID API
┌──────────▼──────────┐ ┌──────▼──────────────┐
│ VirtualDualSense.sys│ │ Real BT DualSense    │
│ (KMDF + VHF)        │ │ (VID 0x054C)         │
│ - HID: VID 0x054C   │ │                       │
│        PID 0x0CE6   │ │                       │
│ - Full USB HID desc │ │                       │
└─────────────────────┘ └──────────────────────┘
```

## Data Flow
- **Input:** BT controller → HID API → Service (BT→USB conversion) → IOCTL → VHF → Windows/Games
- **Output:** Games → VHF → IOCTL → Service (USB→BT conversion) → WriteFile → BT controller

## Component 1: Kernel Driver (VirtualDualSense.sys)

- **Framework**: WDF KMDF + Virtual HID Framework (VHF), part of WDK
- **Language**: C/C++ with WDK
- **Device**: `\\.\VirtualDualSenseX` (numbered instances)
- **VHF HID device**: VID 0x054C, PID 0x0CE6, version 0x8111

**IOCTL Interface:**
| IOCTL | Code | In | Out | Description |
|---|---|---|---|---|
| `IOCTL_VDS_ACTIVATE` | `CTL_CODE(0x8601, 0x800, ...)` | — | — | Start VHF, enumerate HID child |
| `IOCTL_VDS_DEACTIVATE` | `CTL_CODE(0x8601, 0x801, ...)` | — | — | Remove VHF child (toggle off) |
| `IOCTL_VDS_SUBMIT_INPUT` | `CTL_CODE(0x8601, 0x802, ...)` | 64 bytes | — | Submit USB input report |
| `IOCTL_VDS_READ_OUTPUT` | `CTL_CODE(0x8601, 0x803, ...)` | — | 48 bytes | Read output report (non-blocking) |
| `IOCTL_VDS_GET_OUTPUT_CNT` | `CTL_CODE(0x8601, 0x804, ...)` | — | 4 bytes | Pending output report count |

**HID Report Descriptors:** Matches real DualSense USB descriptor:
- Input Report 0x01: 64 bytes (buttons, sticks, triggers, touchpad, IMU, battery)
- Output Report 0x02: 48 bytes (haptics, adaptive triggers, LED)
- Feature Report 0x05/0x06: Calibration data

**Toggle:** `IOCTL_VDS_DEACTIVATE` → game sees device disconnect. `IOCTL_VDS_ACTIVATE` → device reappears.

## Component 2: Userspace Service (DualProxySvc.exe)

- **Language**: C++ (Win32)
- **Type**: Windows SERVICE (SYSTEM)

**Flow:**
1. **Startup**: Find BT DualSense via HID (`SetupDiGetClassDevs`), open sideband device, call `IOCTL_VDS_ACTIVATE`
2. **Loop**: 
   - Read BT input report (78 bytes) → strip BT framing → submit 64-byte USB report via IOCTL
   - Poll for output reports via IOCTL → wrap in BT framing → write to BT HID device
3. **BT→USB conversion**: BT DualSense sends 78-byte reports with 2-byte length header + report ID + 64-byte payload + CRC/trailer. Extract bytes 2–65 → submit as USB report.
4. **USB→BT conversion**: Receive 48-byte USB output → prepend 2-byte BT HIDP header + append BT trailer → write via `WriteFile` to BT device.
5. **Feature requests**: Games may request calibration data. The service caches initial calibration from the real controller and serves it via feature report IOCTLs.

## Component 3: Systray Controller (DualProxyTray.exe)

- **Language**: C++ (Win32, no console)
- **Icon states**: Green (active), Red (inactive), Yellow (no BT controller)
- **Menu**: Enable/Disable, Settings, Exit
- **Hotkey**: `Ctrl+Win+D` (configurable)
- **IPC**: Named pipe to service

## Component 4: Logging System

A unified logging layer across all components.

**Log Location:** `%PROGRAMDATA%\DualProxy\logs\` (one file per component)
- `DualProxySvc.log` (service logs)
- `DualProxyTray.log` (UI logs)
- `VirtualDualSense.log` (kernel driver via ETW)

**Log Format (each entry fully unique):**
```
[2026-06-14 10:30:45.123] [SVC-001] [INFO]  [BT_ENUM] Bluetooth DualSense found: VID=0x054C PID=0x0CE6 Serial=XX:XX:XX
```

Each log entry has:
- **Timestamp** (ms precision)
- **Component prefix + sequential ID** (`SVC-001`, `DRV-001`, `TRAY-001`) — unique across the entire run
- **Severity**: `DEBUG`, `INFO`, `WARN`, `ERROR`, `CRITICAL`
- **Category tag** (e.g., `BT_ENUM`, `VHF_ACTIVATE`, `BT_READ`, `IOCTL_SEND`, `REPORT_CONVERT`)
- **Descriptive message** with specific values (error codes, byte counts, device info)
- **Call stack** included for all ERROR/CRITICAL entries

**Log rotation:** 5 files × 10MB max, auto-delete oldest

## Component 5: Install/Uninstall Scripts

**install.ps1:**
1. Check elevation
2. If VirtualDualSense exists: stop service, devcon remove, sc delete, verify removal
3. Copy .sys to %SYSTEMROOT%\System32\drivers\
4. devcon install VirtualDualSense.inf root\VirtualDualSense
5. sc create DualProxySvc binPath=...
6. sc start DualProxySvc
7. Verify: devcon status, sc query, test sideband open

**uninstall.ps1:**
1. Check elevation
2. sc stop DualProxySvc
3. devcon remove root\VirtualDualSense
4. sc delete DualProxySvc
5. Delete driver file
6. Verify: devcon find empty, sc query fails, file gone, sideband fails

## Developer Setup

- Install Visual Studio 2022 with "Desktop development with C++" workload
- Install WDK (Windows Driver Kit) for WDF/VHF headers and tools
- Enable test signing: `bcdedit /set testsigning on`
- Build: MSBuild from VS developer prompt
- Test: install.ps1 → test with joy.cpl / game
