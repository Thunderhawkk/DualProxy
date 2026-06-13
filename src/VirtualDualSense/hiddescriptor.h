#pragma once

#include <wdm.h>

#define DUALSENSE_VID               0x054C
#define DUALSENSE_PID               0x0CE6
#define DUALSENSE_VERSION           0x8111

#define DUALSENSE_INPUT_REPORT_ID   0x01
#define DUALSENSE_OUTPUT_REPORT_ID  0x02
#define DUALSENSE_INPUT_REPORT_SIZE 64
#define DUALSENSE_OUTPUT_REPORT_SIZE 48
#define DUALSENSE_FEATURE_REPORT_SIZE 64

// D-pad values (bits 0-3 of DpadAndButtons1)
#define DUALSENSE_DPAD_NORTH      0
#define DUALSENSE_DPAD_NORTHEAST  1
#define DUALSENSE_DPAD_EAST       2
#define DUALSENSE_DPAD_SOUTHEAST  3
#define DUALSENSE_DPAD_SOUTH      4
#define DUALSENSE_DPAD_SOUTHWEST  5
#define DUALSENSE_DPAD_WEST       6
#define DUALSENSE_DPAD_NORTHWEST  7
#define DUALSENSE_DPAD_NONE       8

// Button bit masks (DpadAndButtons1 byte)
#define DUALSENSE_BTN_SQUARE     (1 << 4)
#define DUALSENSE_BTN_CROSS      (1 << 5)
#define DUALSENSE_BTN_CIRCLE     (1 << 6)
#define DUALSENSE_BTN_TRIANGLE   (1 << 7)

// Button bit masks (Buttons2 byte)
#define DUALSENSE_BTN_L1        (1 << 0)
#define DUALSENSE_BTN_R1        (1 << 1)
#define DUALSENSE_BTN_L2        (1 << 2)
#define DUALSENSE_BTN_R2        (1 << 3)
#define DUALSENSE_BTN_CREATE    (1 << 4)
#define DUALSENSE_BTN_OPTIONS   (1 << 5)
#define DUALSENSE_BTN_L3        (1 << 6)
#define DUALSENSE_BTN_R3        (1 << 7)

// Button bit masks (Buttons3 byte)
#define DUALSENSE_BTN_PS        (1 << 0)
#define DUALSENSE_BTN_TOUCHPAD  (1 << 1)
#define DUALSENSE_BTN_MIC       (1 << 2)

#pragma pack(push, 1)
typedef struct _DUALSENSE_INPUT_REPORT {
    UCHAR ReportId;           // 0x01
    UCHAR X;                  // Left stick X  (0-255, center 128)
    UCHAR Y;                  // Left stick Y  (0-255, center 128)
    UCHAR Z;                  // Right stick X  (0-255, center 128)
    UCHAR Rz;                 // Right stick Y  (0-255, center 128)
    UCHAR Rx;                 // L2 trigger analog (0-255)
    UCHAR Ry;                 // R2 trigger analog (0-255)
    UCHAR Sequence;           // Incremented each report
    UCHAR DpadAndButtons1;    // Bits: D-pad(0-3), Square(4), Cross(5), Circle(6), Triangle(7)
    UCHAR Buttons2;           // Bits: L1(0), R1(1), L2-click(2), R2-click(3), Create(4), Options(5), L3(6), R3(7)
    UCHAR Buttons3;           // Bits: PS(0), Touchpad(1), Mic(2), padding(3-7)
    UCHAR VendorExtended[40]; // Touchpad, IMU, battery, CRC
} DUALSENSE_INPUT_REPORT, *PDUALSENSE_INPUT_REPORT;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _DUALSENSE_OUTPUT_REPORT {
    UCHAR ReportId;          // 0x02
    UCHAR Flags0;            // bit0: rumble, bit2: trigger_r, bit3: trigger_l
    UCHAR Flags1;            // bit4: mic_led, bit6: lightbar, bit7: player_led
    UCHAR RumbleRight;       // Right haptic motor (0-255)
    UCHAR RumbleLeft;        // Left haptic motor (0-255)
    UCHAR Reserved1[4];
    UCHAR MicLed;            // 0=off, 1=on, 2=pulse
    UCHAR Reserved2;
    UCHAR TriggerRight[11];  // Adaptive trigger right effect data
    UCHAR TriggerLeft[11];   // Adaptive trigger left effect data
    UCHAR Reserved3[11];
    UCHAR PlayerLed;         // 5-bit player LED indicator
    UCHAR LightbarR;         // Lightbar red
    UCHAR LightbarG;         // Lightbar green
    UCHAR LightbarB;         // Lightbar blue
} DUALSENSE_OUTPUT_REPORT, *PDUALSENSE_OUTPUT_REPORT;
#pragma pack(pop)

static const UCHAR DualSenseHidDescriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)

    // Report ID 1: Input
    0x85, 0x01,        //   Report ID (1)
    // Analog sticks and triggers: X, Y, Z, RZ, RX, RY (6 axes x 8 bits)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x09, 0x33,        //   Usage (Rx)
    0x09, 0x34,        //   Usage (Ry)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Vendor-defined: Report Sequence ID
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x20,        //   Usage (0x20)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // D-Pad (Hat Switch)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x39,        //   Usage (Hat Switch)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x07,        //   Logical Maximum (7)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Degrees, English Rotation)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)

    0x65, 0x00,        //   Unit (None)

    // Buttons: Square, Cross, Circle, Triangle, L1, R1, L2, R2, Share/Create, Options, L3, R3, PS, Touchpad, Mic
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (0x01)
    0x29, 0x0F,        //   Usage Maximum (0x0F)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0F,        //   Report Count (15)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Vendor padding (13 bits)
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x21,        //   Usage (0x21)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0D,        //   Report Count (13)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Vendor extended: touchpad, IMU, battery, etc. (52 bytes)
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x22,        //   Usage (0x22)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x34,        //   Report Count (52)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Report ID 2: Output (47 bytes vendor)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x23,        //   Usage (0x23)
    0x95, 0x2F,        //   Report Count (47)
    0x81, 0x02,        //   Output (Data,Var,Abs)

    // Report ID 5: Feature GET - Sensor Calibration (40 bytes)
    0x85, 0x05,        //   Report ID (5)
    0x09, 0x33,        //   Usage (0x33)
    0x95, 0x28,        //   Report Count (40)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    // Report ID 8: Feature SET (47 bytes)
    0x85, 0x08,        //   Report ID (8)
    0x09, 0x34,        //   Usage (0x34)
    0x95, 0x2F,        //   Report Count (47)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    // Report ID 9: Feature GET - Pair Info (19 bytes)
    0x85, 0x09,        //   Report ID (9)
    0x09, 0x24,        //   Usage (0x24)
    0x95, 0x13,        //   Report Count (19)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    // Report ID 10: Feature SET - Easy Pair (26 bytes)
    0x85, 0x0A,        //   Report ID (10)
    0x09, 0x25,        //   Usage (0x25)
    0x95, 0x1A,        //   Report Count (26)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    // Report ID 32: Feature GET - Controller Revision (63 bytes)
    0x85, 0x20,        //   Report ID (32)
    0x09, 0x26,        //   Usage (0x26)
    0x95, 0x3F,        //   Report Count (63)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    // Report ID 33: Feature SET (4 bytes)
    0x85, 0x21,        //   Report ID (33)
    0x09, 0x27,        //   Usage (0x27)
    0x95, 0x04,        //   Report Count (4)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    // Report ID 34: Feature GET (63 bytes)
    0x85, 0x22,        //   Report ID (34)
    0x09, 0x28,        //   Usage (0x28)
    0x95, 0x3F,        //   Report Count (63)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    0xC0               // End Collection
};

static const USHORT DUALSENSE_HID_DESCRIPTOR_SIZE = sizeof(DualSenseHidDescriptor);
