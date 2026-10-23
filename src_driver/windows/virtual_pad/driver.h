/*
 * Streamlight virtual Switch Pro controller (kernel-mode VHF source driver).
 *
 * Step 1: creates a HID gamepad with a Switch Pro-shaped report descriptor and
 * identity. Input is generated internally for now, so the device can be checked
 * in joy.cpl before any Streamlight integration.
 */
#pragma once

#define KMDF_VERSION_MAJOR 1
#define KMDF_VERSION_MINOR 33

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

// Nintendo's USB vendor ID and the Switch Pro Controller product ID.
#define STREAMLIGHT_PAD_VID 0x057E
#define STREAMLIGHT_PAD_PID 0x2009
#define STREAMLIGHT_PAD_VERSION 0x0001

// Input report ID (matches the Switch Pro's standard full report).
#define STREAMLIGHT_PAD_REPORT_ID 0x30

// Report ID byte + 2 button bytes + 1 hat byte + 4 x 16-bit stick axes.
#define STREAMLIGHT_PAD_REPORT_SIZE 12

// Button bits in the input report, 0-based. Order follows the Switch Pro layout.
enum streamlight_pad_button {
  PAD_BUTTON_Y = 0,
  PAD_BUTTON_X,
  PAD_BUTTON_B,
  PAD_BUTTON_A,
  PAD_BUTTON_RIGHT_SR,
  PAD_BUTTON_RIGHT_SL,
  PAD_BUTTON_R,
  PAD_BUTTON_ZR,
  PAD_BUTTON_MINUS,
  PAD_BUTTON_PLUS,
  PAD_BUTTON_RIGHT_STICK,
  PAD_BUTTON_LEFT_STICK,
  PAD_BUTTON_HOME,
  PAD_BUTTON_CAPTURE,
};

typedef struct _PAD_STATE {
  UCHAR report[STREAMLIGHT_PAD_REPORT_SIZE];
} PAD_STATE;

typedef struct _DEVICE_CONTEXT {
  VHFHANDLE VhfHandle;
  WDFTIMER Timer;
  // Plain spinlock in the context (not a WDF child object) so it stays valid during device cleanup.
  KSPIN_LOCK Lock;
  PAD_STATE State;
  BOOLEAN ReadyForNextRead;
  ULONG TickCount;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD StreamlightPadDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP StreamlightPadDeviceCleanup;
EVT_WDF_DEVICE_D0_ENTRY StreamlightPadD0Entry;
EVT_WDF_DEVICE_D0_EXIT StreamlightPadD0Exit;
EVT_WDF_TIMER StreamlightPadTimer;
