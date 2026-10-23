/*
 * HID report descriptor for the virtual Switch Pro Controller.
 *
 * Declares the reports the real controller uses over USB, each 63 bytes after the report ID
 * (64 bytes total), so raw HID readers such as SDL/Steam can exchange them:
 *   input  0x30  standard full input report (streamed)
 *   input  0x21  sub-command reply
 *   input  0x81  USB command reply
 *   output 0x01  rumble + sub-command
 *   output 0x10  rumble only
 *   output 0x11  MCU request
 *   output 0x80  USB command
 *   output 0x82  USB pre-handshake
 *
 * Report 0x30 also describes its contents for generic HID consumers (DirectInput, joy.cpl).
 * The fields line up with the real byte layout (see switch_pro_protocol.c):
 *   bytes 1-2    timer, battery/connection (vendor)
 *   bytes 3-5    24 button bits: right, shared and left button bytes
 *   bytes 6-11   X, Y, Rx, Ry as packed 12-bit values
 *   bytes 12-63  vibrator, IMU samples, padding (vendor)
 */
#pragma once

static const UCHAR g_streamlight_pad_report_descriptor[] = {
    0x05, 0x01,              // Usage Page (Generic Desktop)
    0x09, 0x04,              // Usage (Joystick)
    0xA1, 0x01,              // Collection (Application)

    // Input 0x30: full input report
    0x85, 0x30,              //   Report ID (0x30)
    0x06, 0x00, 0xFF,        //   Usage Page (Vendor 0xFF00)
    0x09, 0x01,              //   Usage (0x01)
    0x15, 0x00,              //   Logical Minimum (0)
    0x26, 0xFF, 0x00,        //   Logical Maximum (255)
    0x75, 0x08,              //   Report Size (8)
    0x95, 0x02,              //   Report Count (2) - timer, battery/connection
    0x81, 0x02,              //   Input (Data, Variable, Absolute)
    0x05, 0x09,              //   Usage Page (Button)
    0x19, 0x01,              //   Usage Minimum (1)
    0x29, 0x18,              //   Usage Maximum (24)
    0x15, 0x00,              //   Logical Minimum (0)
    0x25, 0x01,              //   Logical Maximum (1)
    0x75, 0x01,              //   Report Size (1)
    0x95, 0x18,              //   Report Count (24)
    0x81, 0x02,              //   Input (Data, Variable, Absolute)
    0x05, 0x01,              //   Usage Page (Generic Desktop)
    0x09, 0x30,              //   Usage (X)
    0x09, 0x31,              //   Usage (Y)
    0x09, 0x33,              //   Usage (Rx)
    0x09, 0x34,              //   Usage (Ry)
    0x15, 0x00,              //   Logical Minimum (0)
    0x26, 0xFF, 0x0F,        //   Logical Maximum (4095)
    0x75, 0x0C,              //   Report Size (12)
    0x95, 0x04,              //   Report Count (4)
    0x81, 0x02,              //   Input (Data, Variable, Absolute)
    0x06, 0x00, 0xFF,        //   Usage Page (Vendor 0xFF00)
    0x09, 0x02,              //   Usage (0x02)
    0x15, 0x00,              //   Logical Minimum (0)
    0x26, 0xFF, 0x00,        //   Logical Maximum (255)
    0x75, 0x08,              //   Report Size (8)
    0x95, 0x34,              //   Report Count (52) - vibrator, IMU, padding
    0x81, 0x02,              //   Input (Data, Variable, Absolute)

    // The remaining reports are vendor data: 63 bytes each. Usage page, logical range and
    // report size carry over from the item above.
    0x85, 0x21, 0x09, 0x03, 0x95, 0x3F, 0x81, 0x02,  // Input 0x21: sub-command reply
    0x85, 0x81, 0x09, 0x04, 0x95, 0x3F, 0x81, 0x02,  // Input 0x81: USB command reply
    0x85, 0x01, 0x09, 0x05, 0x95, 0x3F, 0x91, 0x02,  // Output 0x01: rumble + sub-command
    0x85, 0x10, 0x09, 0x06, 0x95, 0x3F, 0x91, 0x02,  // Output 0x10: rumble
    0x85, 0x11, 0x09, 0x07, 0x95, 0x3F, 0x91, 0x02,  // Output 0x11: MCU request
    0x85, 0x80, 0x09, 0x08, 0x95, 0x3F, 0x91, 0x02,  // Output 0x80: USB command
    0x85, 0x82, 0x09, 0x09, 0x95, 0x3F, 0x91, 0x02,  // Output 0x82: USB pre-handshake

    0xC0,                    // End Collection
};
