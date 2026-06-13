#include "report.h"
#include <memory.h>

BOOL IsBtFramedReport(const BYTE* report, DWORD size)
{
    if (size < 2) return FALSE;
    return (report[0] == BT_HIDP_HEADER_INPUT || report[0] == BT_HIDP_HEADER_OUTPUT);
}

BOOL BtToUsbInputReport(const BYTE* btReport, DWORD btSize, BYTE* usbReport, DWORD usbSize)
{
    if (!btReport || !usbReport) return FALSE;
    if (usbSize < USB_INPUT_REPORT_SIZE) return FALSE;

    if (IsBtFramedReport(btReport, btSize))
    {
        // BT frame: [0xA1][ReportID][data 63 bytes][CRC/padding]
        // Extract starting from ReportID (skip the 0xA1 header)
        DWORD dataLen = btSize - 1;
        if (dataLen > USB_INPUT_REPORT_SIZE)
            dataLen = USB_INPUT_REPORT_SIZE;
        memcpy(usbReport, btReport + 1, dataLen);
        if (dataLen < usbSize)
            memset(usbReport + dataLen, 0, usbSize - dataLen);
    }
    else
    {
        // Already in USB format (OS stripped BT header)
        DWORD copyLen = (btSize < usbSize) ? btSize : usbSize;
        memcpy(usbReport, btReport, copyLen);
        if (copyLen < usbSize)
            memset(usbReport + copyLen, 0, usbSize - copyLen);
    }
    return TRUE;
}

BOOL UsbToBtOutputReport(const BYTE* usbReport, DWORD usbSize, BYTE* btReport, DWORD btSize, DWORD* bytesWritten)
{
    if (!usbReport || !btReport || !bytesWritten) return FALSE;
    if (btSize < BT_OUTPUT_REPORT_SIZE) return FALSE;

    memset(btReport, 0, btSize);
    btReport[0] = BT_HIDP_HEADER_OUTPUT;

    DWORD copyLen = (usbSize < (btSize - 1)) ? usbSize : (btSize - 1);
    memcpy(btReport + 1, usbReport, copyLen);

    *bytesWritten = BT_OUTPUT_REPORT_SIZE;
    return TRUE;
}
