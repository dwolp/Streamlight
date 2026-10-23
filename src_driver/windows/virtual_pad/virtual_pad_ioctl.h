/*
 * Control interface of the Streamlight virtual Switch Pro driver, shared by the driver and
 * Streamlight (src/platform/windows/input.cpp).
 *
 * Streamlight opens the device interface below and creates one virtual controller per client
 * gamepad. Controllers created through a handle are removed when that handle is closed, so a
 * crashed host never leaves controllers behind.
 */
#pragma once

// {6C1F8A4E-3B2D-4E7A-9C5F-1D8B2A7E4F30}
DEFINE_GUID(GUID_DEVINTERFACE_STREAMLIGHT_VIRTUAL_PAD,
            0x6c1f8a4e, 0x3b2d, 0x4e7a, 0x9c, 0x5f, 0x1d, 0x8b, 0x2a, 0x7e, 0x4f, 0x30);

// Maximum number of virtual controllers (matches Streamlight's MAX_GAMEPADS).
#define SLPAD_MAX_PADS 16

#define SLPAD_IOCTL(function) CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + (function), METHOD_BUFFERED, FILE_ANY_ACCESS)

// Creates the virtual controller for a slot. Input: SLPAD_INDEX.
#define IOCTL_SLPAD_CREATE SLPAD_IOCTL(0)

// Removes the virtual controller in a slot. Input: SLPAD_INDEX.
#define IOCTL_SLPAD_DESTROY SLPAD_IOCTL(1)

// Sets the controller's input state. Input: SLPAD_STATE.
#define IOCTL_SLPAD_SET_STATE SLPAD_IOCTL(2)

// Waits for rumble from a game on any controller created through this handle.
// Output: SLPAD_RUMBLE. Stays pending until rumble changes; cancel it to stop waiting.
#define IOCTL_SLPAD_GET_RUMBLE SLPAD_IOCTL(3)

// Sets the controller's latest motion sample. Input: SLPAD_MOTION. Games only see it once they
// enable the IMU, as with the real controller.
#define IOCTL_SLPAD_SET_MOTION SLPAD_IOCTL(4)

#pragma pack(push, 1)

typedef struct _SLPAD_INDEX {
  unsigned char index;  // 0 .. SLPAD_MAX_PADS - 1
} SLPAD_INDEX;

typedef struct _SLPAD_STATE {
  unsigned char index;
  unsigned char buttons[3];  // Switch layout: right, shared, left (see switch_pro_protocol.h)
  unsigned short lx, ly, rx, ry;  // 12-bit stick values, Y up = larger
} SLPAD_STATE;

typedef struct _SLPAD_MOTION {
  unsigned char index;
  short accel[3];  // raw accelerometer X, Y, Z in the controller's own axes (4096 per G)
  short gyro[3];   // raw gyro X, Y, Z in the controller's own axes (about 14.29 per degree/s)
} SLPAD_MOTION;

typedef struct _SLPAD_RUMBLE {
  unsigned char index;
  unsigned short low_frequency;   // 0 .. 0xFFFF, strongest low-band amplitude of both actuators
  unsigned short high_frequency;  // 0 .. 0xFFFF, strongest high-band amplitude of both actuators
} SLPAD_RUMBLE;

#pragma pack(pop)
