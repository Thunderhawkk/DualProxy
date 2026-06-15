#include "report.h"
#include <memory.h>
#include <stdint.h>

#define DS_OUTPUT_TAG       0x10
#define DS_OUTPUT_REPORT_BT 0x31
#define DS_CRC32_SEED       0xA2

static uint32_t s_crc32_table[256] = { 0 };
static bool s_crc32_init = false;

static void InitCrc32Table()
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
        s_crc32_table[i] = crc;
    }
    s_crc32_init = true;
}

static uint32_t Crc32Le(const uint8_t* data, size_t len, uint32_t seed)
{
    if (!s_crc32_init) InitCrc32Table();
    uint32_t crc = seed;
    for (size_t i = 0; i < len; i++)
        crc = s_crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

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
        DWORD dataLen = btSize - 1;
        if (dataLen > USB_INPUT_REPORT_SIZE)
            dataLen = USB_INPUT_REPORT_SIZE;
        memcpy(usbReport, btReport + 1, dataLen);
        if (dataLen < usbSize)
            memset(usbReport + dataLen, 0, usbSize - dataLen);
    }
    else
    {
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
    if (usbSize < USB_OUTPUT_REPORT_SIZE) return FALSE;

    static BYTE s_outputSeq = 0;

    // BT output format:
    //   [0]=report_id(0x31) [1]=seq_tag [2]=tag(0x10) [3-4]=reserved
    //   [5-73]=common data (69 bytes, USB output from byte 1)
    //   [74-77]=CRC32
    memset(btReport, 0, btSize);
    btReport[0] = DS_OUTPUT_REPORT_BT;
    btReport[1] = (s_outputSeq << 4) | 0x00;
    btReport[2] = DS_OUTPUT_TAG;
    // btReport[3-4] left as zero (reserved)
    s_outputSeq = (s_outputSeq + 1) & 0x0F;

    // Common data (USB output from byte 1) at offset 5
    DWORD commonSize = usbSize - 1;
    if (commonSize > 69) commonSize = 69;
    memcpy(btReport + 5, usbReport + 1, commonSize);

    // CRC32 over seed byte + entire report except last 4 bytes
    uint8_t seed = DS_CRC32_SEED;
    uint32_t crc = Crc32Le(NULL, 0, 0xFFFFFFFF);
    crc = Crc32Le(&seed, 1, crc);
    crc = Crc32Le(btReport, BT_OUTPUT_REPORT_SIZE - 4, crc);
    crc = ~crc;

    btReport[74] = (BYTE)(crc & 0xFF);
    btReport[75] = (BYTE)((crc >> 8) & 0xFF);
    btReport[76] = (BYTE)((crc >> 16) & 0xFF);
    btReport[77] = (BYTE)((crc >> 24) & 0xFF);

    *bytesWritten = BT_OUTPUT_REPORT_SIZE;
    return TRUE;
}
