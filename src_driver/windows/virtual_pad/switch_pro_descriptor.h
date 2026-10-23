/*
 * HID report descriptor for the virtual Switch Pro-shaped gamepad.
 *
 * Input report layout (report ID 0x30, 12 bytes total):
 *   byte 0      report ID (0x30)
 *   bytes 1-2   14 buttons, bit n = button n+1; top 2 bits of byte 2 are padding
 *   byte 3      hat switch (low nibble, 0 = neutral, 1-8 = directions); high nibble padding
 *   bytes 4-5   left stick X   (uint16, little endian)
 *   bytes 6-7   left stick Y
 *   bytes 8-9   right stick X
 *   bytes 10-11 right stick Y
 *
 * This is a generic gamepad layout with the Switch Pro's report ID and button count.
 * It is not yet the Pro's exact report format, which the Switch-specific driver in
 * Steam/SDL also checks. That comes in step 2.
 */
#pragma once

static const UCHAR g_streamlight_pad_report_descriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Gamepad)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x30,       //   Report ID (0x30)

    // 14 buttons
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (1)
    0x29, 0x0E,       //   Usage Maximum (14)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x0E,       //   Report Count (14)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x03,       //   Input (Constant, Variable, Absolute) - padding

    // Hat switch (4 bits) + padding
    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x39,       //   Usage (Hat switch)
    0x15, 0x01,       //   Logical Minimum (1)
    0x25, 0x08,       //   Logical Maximum (8)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3B, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (Degrees)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x42,       //   Input (Data, Variable, Absolute, Null State)
    0x65, 0x00,       //   Unit (None)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x03,       //   Input (Constant, Variable, Absolute) - padding

    // Two sticks: X, Y, Rx, Ry as unsigned 16-bit values
    0x09, 0x30,       //   Usage (X)
    0x09, 0x31,       //   Usage (Y)
    0x09, 0x33,       //   Usage (Rx)
    0x09, 0x34,       //   Usage (Ry)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0xFF, //   Logical Maximum (65535)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0xFF, 0xFF, //   Physical Maximum (65535)
    0x75, 0x10,       //   Report Size (16)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)

    0xC0,             // End Collection
};
