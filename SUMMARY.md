# Dual (VHF HID Minidriver) — Build Summary

## Completed Work

### Build Fixes & WDK 10.0.28000.0 Migration
- Fixed include order, pragma order
- Converted HRESULT → NTSTATUS
- Updated `WDF_FILEOBJECT_CONFIG_INIT` to 3-arg signature
- Replaced `StringCbPrintfW` → `RtlStringCbPrintfW`
- Added `InitializeSListHead`

### VHF API Rewrite (WDK 10.0.28000.0)
- `VHF_CONFIG_INIT` uses 4 parameters
- Separate callbacks per operation: `WriteReport`, `GetFeature`, `SetFeature`
- `_Function_class_` annotations on callbacks
- `VhfClientContext` for instance data
- `VhfAsyncOperationComplete(VHFOPERATIONHANDLE)` API

### Output Report Queue — WdfRequest Removal
- Replaced `WdfRequestSetInputBuffer` (removed in KMDF 1.33) with lock-free `InterlockedSList`
- Replaced `WdfIoQueueRetrieveNextRequest` with `InterlockedPopEntrySList` in IOCTL handler
- Removed `WDFQUEUE OutputReportQueue` from device context; added `SLIST_HEADER`
- Added SList draining on deactivation to prevent memory leaks

### Build Configuration
- Added `vhfkm.lib` dependency
- Disabled signing, Inf2Cat, and InfVerif in vcxproj

### Build Result
- **0 warnings, 0 errors**
- **25.6 KB** `.sys` file
