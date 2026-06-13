#pragma once

#include <windows.h>

#define BT_INPUT_REPORT_SIZE   78
#define BT_OUTPUT_REPORT_SIZE  78
#define USB_INPUT_REPORT_SIZE  64
#define USB_OUTPUT_REPORT_SIZE 48

#define BT_HIDP_HEADER_INPUT   0xA1
#define BT_HIDP_HEADER_OUTPUT  0xA2

BOOL BtToUsbInputReport(
    const BYTE* btReport,
    DWORD btSize,
    BYTE* usbReport,
    DWORD usbSize
);

BOOL UsbToBtOutputReport(
    const BYTE* usbReport,
    DWORD usbSize,
    BYTE* btReport,
    DWORD btSize,
    DWORD* bytesWritten
);

BOOL IsBtFramedReport(
    const BYTE* report,
    DWORD size
);
