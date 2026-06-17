# DualProxy — Virtual DualSense over Bluetooth

Bridges a real Bluetooth DualSense controller to a virtual USB DualSense device on Windows, enabling native DualSense support in games without a wired connection.

## Architecture

```
┌──────────────┐    BT HID     ┌──────────────┐   IOCTL    ┌──────────────────┐
│ DualSense    │──────────────▶│ DualProxySvc │───────────▶│ VirtualDualSense │
│ (real, BT)   │◀──────────────│ (service)     │◀───────────│ (VHF kernel drv) │
└──────────────┘               └──────────────┘            └──────────────────┘
                                       │                           │
                                  ┌────┴────┐               Windows sees it as
                                  │  Tray   │               USB DualSense (054C:0CE6)
                                  │  (GUI)  │
                                  └─────────┘
```

- **VirtualDualSense.sys** — VHF (Virtual HID Framework) kernel driver. Creates a virtual USB DualSense controller.
- **DualProxySvc.exe** — SYSTEM-level service. Connects to the real BT DualSense, forwards input to the virtual device, and forwards output (rumble/LED) back to the real controller.
- **DualProxyTray.exe** — System tray UI. Start/stop the bridge, toggle BT passthrough.

## Prerequisites

- Windows 10/11 x64
- Visual Studio 2022 with **Desktop development with C++** workload
- Windows 10/11 WDK (10.0.28000.0+)
- Windows 10/11 SDK
- A DualSense Controller
- [ViGEmBus](https://github.com/ViGEm/ViGEmBus/releases) — required for HidHide compatibility layer
- [HidHide](https://github.com/nefarius/HidHide/releases) — hides the real controller from games so they only see the virtual one
- Test signing enabled: `bcdedit /set testsigning on` (then reboot)

## Build

Open `src\VirtualDualSense.sln` in Visual Studio 2022 and build for x64 (Debug or Release).

Or from command line:

```powershell
MSBuild src\VirtualDualSense.sln /p:Configuration=Debug /p:Platform=x64 /t:Build
```

## Install

1. Install ViGEmBus and HidHide (see Prerequisites), then reboot
2. Enable test signing: `bcdedit /set testsigning on` (requires reboot)
3. Run `scripts\install.ps1` as Administrator
4. Connect your DualSense controller via Bluetooth
5. The tray app `DualProxyTray.exe` will start automatically

## Usage

- **Tray icon** — right-click to start/stop the bridge, toggle BT read
- **Service** — runs as `DualProxySvc`, starts automatically on boot
- **Logs** — `%ProgramData%\DualProxy\logs\DualProxySvc.log`

## Project Structure

```
src/
├── VirtualDualSense/     # VHF kernel driver (WDF/KMDF)
│   ├── VirtualDualSense.inf
│   └── ioctl.c, hiddescriptor.h, ...
├── DualProxySvc/         # Bridge service (C++)
│   ├── bridge.cpp/.h     # Core bridge logic
│   ├── main.cpp          # Service entry, named pipe IPC
│   ├── hidapi.cpp/.h     # HID device enumeration/communication
│   └── report.cpp/.h     # Report format conversion (USB ↔ BT)
└── DualProxyTray/        # System tray GUI (C++)
scripts/
├── install.ps1           # Driver + service install
└── uninstall.ps1         # Driver + service removal
```

## How It Works

The service opens a HID connection to the real BT DualSense and creates a virtual USB DualSense via the VHF driver. It continuously:

1. Reads 78-byte BT HID input reports from the real controller
2. Converts them to 64-byte USB HID input reports and submits to the virtual device
3. Reads 48-byte USB output reports from the virtual device (rumble/LED)
4. Converts them to 78-byte BT output reports and sends to the real controller

The tray app communicates with the service over a named pipe for start/stop/status commands.

## License

BSD 3-Clause

## Not Yet Implemented

- **Audio haptics** — BT audio-to-haptics (speaker/headphone jack → haptic feedback via HID report 0x32)
- **Mic LED** — the microphone mute/LED state is not forwarded to the virtual device
- **Firmware/battery passthrough** — calibration data and battery status from the real controller are exposed but not fully interpreted by the virtual device
