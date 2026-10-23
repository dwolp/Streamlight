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

// Raw IMU units with the factory calibration this controller reports (see switch_pro_protocol.c):
// accelerometer 4096 per G, gyro 13371 / 936 (about 14.29) per degree per second.
#define SP_ACCEL_PER_G 4096.0f
#define SP_GYRO_PER_DPS (13371.0f / 936.0f)

typedef struct sp_state {
  unsigned char buttons[3];
  unsigned short lx, ly, rx, ry;  // 12-bit; Y is "up = larger", as on the real controller

  unsigned char id;           // controller slot; makes each controller's MAC address unique
  unsigned char imu_enabled;  // set by the host with sub-command 0x40
  short accel[3];             // raw accelerometer X, Y, Z in the controller's own axes
  short gyro[3];              // raw gyro X, Y, Z in the controller's own axes
} sp_state;

// Sets all buttons released, both sticks centred, IMU off with no data, and slot 0.
void sp_state_init(sp_state *state);

// Converts a signed axis value (-32768..32767, up/right positive) to the 12-bit stick range.
unsigned short sp_axis_to_stick(short value);

// Builds a 0x30 full input report.
void sp_build_full_report(const sp_state *state, unsigned char timer, unsigned char out[SP_REPORT_SIZE]);

// Handles an output report. `payload` excludes the report ID. Updates `state` where the host
// changes it (IMU on/off). Returns 1 and fills `reply` if the controller must answer with an
// input report, otherwise returns 0.
int sp_handle_output(unsigned char report_id, const unsigned char *payload, unsigned long length,
                     sp_state *state, unsigned char timer, unsigned char reply[SP_REPORT_SIZE]);

// Reads the rumble strength carried by output report 0x01 or 0x10 (`payload` excludes the report
// ID). Reports the strongest low-band and high-band amplitude of the two actuators as 0..0xFFFF.
// Returns 1 if the report carries rumble data, otherwise 0.
int sp_parse_rumble(unsigned char report_id, const unsigned char *payload, unsigned long length,
                    unsigned short *low_frequency, unsigned short *high_frequency);
