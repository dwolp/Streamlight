/*
 * Nintendo Switch Pro Controller USB protocol, as expected by SDL's HIDAPI Switch driver
 * (which Steam also uses). Plain C with no kernel or Win32 calls, so the same code runs in the
 * driver and in the user-mode tests (tests/protocol_test.c).
 *
 * Reports are 64 bytes including the report ID at byte 0, matching the real controller over USB.
 */
#pragma once

#define SP_REPORT_SIZE 64

// Input reports (device -> host)
#define SP_INPUT_FULL 0x30          // standard full input report, streamed continuously
#define SP_INPUT_SUBCMD_REPLY 0x21  // reply to a sub-command (output report 0x01)
#define SP_INPUT_USB_REPLY 0x81     // reply to a USB command (output report 0x80)

// Output reports (host -> device)
#define SP_OUTPUT_RUMBLE_SUBCMD 0x01  // rumble data + sub-command
#define SP_OUTPUT_RUMBLE 0x10         // rumble data only
#define SP_OUTPUT_USB 0x80            // USB command (status, handshake, ...)

// Button bits. buttons[0] = right side, buttons[1] = shared, buttons[2] = left side.
#define SP_BTN0_Y 0x01
#define SP_BTN0_X 0x02
#define SP_BTN0_B 0x04
#define SP_BTN0_A 0x08
#define SP_BTN0_R 0x40
#define SP_BTN0_ZR 0x80
#define SP_BTN1_MINUS 0x01
#define SP_BTN1_PLUS 0x02
#define SP_BTN1_RSTICK 0x04
#define SP_BTN1_LSTICK 0x08
#define SP_BTN1_HOME 0x10
#define SP_BTN1_CAPTURE 0x20
#define SP_BTN2_DOWN 0x01
#define SP_BTN2_UP 0x02
#define SP_BTN2_RIGHT 0x04
#define SP_BTN2_LEFT 0x08
#define SP_BTN2_L 0x40
#define SP_BTN2_ZL 0x80

// 12-bit stick values. The emulated factory calibration reports this centre and range,
// so values outside [centre - range, centre + range] are clamped by the host.
#define SP_STICK_CENTER 2048
#define SP_STICK_RANGE 1536

typedef struct sp_state {
  unsigned char buttons[3];
  unsigned short lx, ly, rx, ry;  // 12-bit; Y is "up = larger", as on the real controller
} sp_state;

// Sets all buttons released and both sticks centred.
void sp_state_init(sp_state *state);

// Converts a signed axis value (-32768..32767, up/right positive) to the 12-bit stick range.
unsigned short sp_axis_to_stick(short value);

// Builds a 0x30 full input report.
void sp_build_full_report(const sp_state *state, unsigned char timer, unsigned char out[SP_REPORT_SIZE]);

// Handles an output report. `payload` excludes the report ID. Returns 1 and fills `reply`
// if the controller must answer with an input report, otherwise returns 0.
int sp_handle_output(unsigned char report_id, const unsigned char *payload, unsigned long length,
                     const sp_state *state, unsigned char timer, unsigned char reply[SP_REPORT_SIZE]);
