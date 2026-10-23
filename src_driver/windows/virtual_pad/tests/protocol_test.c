/*
 * User-mode tests for switch_pro_protocol.c. Requests are built and replies checked the way
 * SDL's HIDAPI Switch driver does it. Build and run with tests\run_tests.cmd.
 */
#include <stdio.h>
#include <string.h>

#include "../switch_pro_protocol.h"

static int failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      failures++;                                                \
    }                                                            \
  } while (0)

// Sends a USB command (output report 0x80) and returns whether a reply was produced.
static int usb_command(unsigned char command, unsigned char reply[SP_REPORT_SIZE]) {
  unsigned char packet[SP_REPORT_SIZE] = {SP_OUTPUT_USB, command};
  sp_state state;
  sp_state_init(&state);
  return sp_handle_output(packet[0], packet + 1, SP_REPORT_SIZE - 1, &state, 0, reply);
}

// Sends a sub-command (output report 0x01) built like SDL's ConstructSubcommand.
static int subcommand(unsigned char id, const unsigned char *args, int arg_length, unsigned char reply[SP_REPORT_SIZE]) {
  static const unsigned char neutral_rumble[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
  unsigned char packet[SP_REPORT_SIZE] = {0};
  sp_state state;

  packet[0] = SP_OUTPUT_RUMBLE_SUBCMD;
  packet[1] = 0x05;  // packet counter
  memcpy(packet + 2, neutral_rumble, sizeof(neutral_rumble));
  packet[10] = id;
  memcpy(packet + 11, args, arg_length);

  sp_state_init(&state);
  return sp_handle_output(packet[0], packet + 1, SP_REPORT_SIZE - 1, &state, 7, reply);
}

// Reads SPI flash through sub-command 0x10 and checks the reply the way SDL does.
static int spi_read(unsigned long address, unsigned char length, unsigned char *data) {
  unsigned char args[5] = {
      (unsigned char) address, (unsigned char) (address >> 8), (unsigned char) (address >> 16), (unsigned char) (address >> 24), length};
  unsigned char reply[SP_REPORT_SIZE];

  if (!subcommand(0x10, args, sizeof(args), reply)) return 0;
  if (reply[0] != SP_INPUT_SUBCMD_REPLY || !(reply[13] & 0x80) || reply[14] != 0x10) return 0;
  if (memcmp(reply + 15, args, 5) != 0) return 0;
  memcpy(data, reply + 20, length);
  return 1;
}

static void test_usb_commands(void) {
  unsigned char reply[SP_REPORT_SIZE];

  CHECK(usb_command(0x01, reply));
  CHECK(reply[0] == 0x81 && reply[1] == 0x01);
  CHECK(reply[3] == 0x03);  // Pro Controller
  CHECK(reply[4] == 0x01 && reply[9] == 0x98);  // MAC, least significant byte first

  CHECK(usb_command(0x02, reply) && reply[0] == 0x81 && reply[1] == 0x02);
  CHECK(usb_command(0x03, reply) && reply[0] == 0x81 && reply[1] == 0x03);
  CHECK(!usb_command(0x04, reply));  // force USB has no reply
  CHECK(!usb_command(0x05, reply));
}

static void test_device_info(void) {
  unsigned char reply[SP_REPORT_SIZE];

  CHECK(subcommand(0x02, NULL, 0, reply));
  CHECK(reply[0] == 0x21 && (reply[13] & 0x80) && reply[14] == 0x02);
  CHECK(reply[17] == 0x03);  // controller type: Pro
  CHECK(reply[19] == 0x98 && reply[24] == 0x01);  // MAC, most significant byte first
}

static void test_plain_ack(void) {
  static const unsigned char on = 0x01;
  unsigned char reply[SP_REPORT_SIZE];

  CHECK(subcommand(0x48, &on, 1, reply) && reply[13] == 0x80 && reply[14] == 0x48);
  CHECK(subcommand(0x03, (const unsigned char *) "\x30", 1, reply) && reply[13] == 0x80 && reply[14] == 0x03);
  CHECK(reply[1] == 7);  // timer is copied into the reply
  CHECK(reply[2] == 0x91);
}

static void test_spi_calibration(void) {
  unsigned char data[0x1D];
  unsigned char *l = data, *r = data + 9;

  // User calibration absent: no 0xB2 0xA1 magic, so SDL falls back to factory calibration.
  CHECK(spi_read(0x8010, 0x16, data));
  CHECK(data[0] == 0xFF && data[1] == 0xFF);
  CHECK(spi_read(0x8026, 0x14, data));
  CHECK(data[0] == 0xFF && data[1] == 0xFF);

  // Factory stick calibration, decoded with SDL's formulas.
  CHECK(spi_read(0x603D, 0x12, data));
  CHECK((((l[1] << 8) & 0xF00) | l[0]) == SP_STICK_RANGE);   // left X max above centre
  CHECK(((l[2] << 4) | (l[1] >> 4)) == SP_STICK_RANGE);      // left Y max above centre
  CHECK((((l[4] << 8) & 0xF00) | l[3]) == SP_STICK_CENTER);  // left X centre
  CHECK(((l[5] << 4) | (l[4] >> 4)) == SP_STICK_CENTER);     // left Y centre
  CHECK((((l[7] << 8) & 0xF00) | l[6]) == SP_STICK_RANGE);   // left X min below centre
  CHECK((((r[1] << 8) & 0xF00) | r[0]) == SP_STICK_CENTER);  // right X centre
  CHECK(((r[2] << 4) | (r[1] >> 4)) == SP_STICK_CENTER);     // right Y centre
  CHECK((((r[4] << 8) & 0xF00) | r[3]) == SP_STICK_RANGE);   // right X min below centre
  CHECK((((r[7] << 8) & 0xF00) | r[6]) == SP_STICK_RANGE);   // right X max above centre

  // Factory IMU calibration: accel sensitivity 0x4000, gyro sensitivity 0x343B.
  CHECK(spi_read(0x6020, 0x18, data));
  CHECK(data[6] == 0x00 && data[7] == 0x40);
  CHECK(data[18] == 0x3B && data[19] == 0x34);

  // Colours.
  CHECK(spi_read(0x6050, 0x0C, data));
  CHECK(data[0] == 0x32 && data[3] == 0xFF);

  // Oversized reads are clamped to 0x1D bytes and the clamped length is echoed.
  {
    unsigned char args[5] = {0x00, 0x60, 0x00, 0x00, 0x40};
    unsigned char reply[SP_REPORT_SIZE];
    CHECK(subcommand(0x10, args, sizeof(args), reply) && reply[19] == 0x1D);
  }
}

static void test_full_report(void) {
  sp_state state;
  unsigned char report[SP_REPORT_SIZE];
  const unsigned char *ls = report + 6;

  sp_state_init(&state);
  state.buttons[0] = SP_BTN0_A | SP_BTN0_ZR;
  state.buttons[1] = SP_BTN1_HOME;
  state.buttons[2] = SP_BTN2_UP;
  state.lx = sp_axis_to_stick(32767);
  state.ly = sp_axis_to_stick(-32768);
  sp_build_full_report(&state, 42, report);

  CHECK(report[0] == 0x30 && report[1] == 42 && report[2] == 0x91);
  CHECK(report[3] == 0x88 && report[4] == 0x10 && report[5] == 0x02);
  CHECK((ls[0] | ((ls[1] & 0xF) << 8)) == SP_STICK_CENTER + SP_STICK_RANGE);           // SDL's X decode
  CHECK((((ls[1] & 0xF0) >> 4) | (ls[2] << 4)) == SP_STICK_CENTER - SP_STICK_RANGE);   // SDL's Y decode
  CHECK(report[13] == 0 && report[48] == 0);  // no IMU data
}

static void test_axis_conversion(void) {
  CHECK(sp_axis_to_stick(0) == SP_STICK_CENTER);
  CHECK(sp_axis_to_stick(32767) == SP_STICK_CENTER + SP_STICK_RANGE);
  CHECK(sp_axis_to_stick(-32768) == SP_STICK_CENTER - SP_STICK_RANGE);
}

static void test_no_reply(void) {
  unsigned char packet[SP_REPORT_SIZE] = {SP_OUTPUT_RUMBLE, 0x01};
  unsigned char reply[SP_REPORT_SIZE];
  sp_state state;
  sp_state_init(&state);
  CHECK(!sp_handle_output(packet[0], packet + 1, SP_REPORT_SIZE - 1, &state, 0, reply));
  CHECK(!sp_handle_output(SP_OUTPUT_RUMBLE_SUBCMD, packet + 1, 5, &state, 0, reply));  // too short
}

int main(void) {
  test_usb_commands();
  test_device_info();
  test_plain_ack();
  test_spi_calibration();
  test_full_report();
  test_axis_conversion();
  test_no_reply();

  if (failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("All protocol tests passed\n");
  return 0;
}
