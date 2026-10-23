/*
 * Streamlight virtual Switch Pro controller (kernel-mode VHF source driver).
 *
 * The root device exposes a control interface (virtual_pad_ioctl.h). Through it, Streamlight
 * creates up to SLPAD_MAX_PADS virtual controllers, each a separate VHF HID device with the
 * Switch Pro Controller's USB identity and protocol (switch_pro_protocol.c), sets their input
 * state and receives rumble from games.
 */
#pragma once

#define KMDF_VERSION_MAJOR 1
#define KMDF_VERSION_MINOR 33

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

// Allocates the control interface GUID in this translation unit. Kept after the system headers
// so their GUIDs are not defined again here.
#include <initguid.h>

#include "switch_pro_protocol.h"
#include "virtual_pad_ioctl.h"

// Nintendo's USB vendor ID and the Switch Pro Controller product ID.
#define STREAMLIGHT_PAD_VID 0x057E
#define STREAMLIGHT_PAD_PID 0x2009
#define STREAMLIGHT_PAD_VERSION 0x0200

// Replies waiting to be sent as input reports. The host sends one request and waits for its
// reply, so a few slots are plenty; if the queue is full the newest reply is dropped.
#define STREAMLIGHT_PAD_REPLY_SLOTS 8

// One virtual controller slot.
typedef struct _PAD {
  WDFDEVICE Device;
  UCHAR Index;

  // Held while a report is being submitted, so the VHF device is not deleted under it.
  EX_RUNDOWN_REF Rundown;

  // The fields below are protected by DEVICE_CONTEXT.Lock.
  VHFHANDLE Vhf;        // NULL when the slot is free
  WDFFILEOBJECT Owner;  // handle that created the controller; it is removed when that handle closes
  BOOLEAN ReadyForNextRead;

  sp_state State;
  UCHAR ReportTimer;  // counter placed in every input report

  UCHAR Replies[STREAMLIGHT_PAD_REPLY_SLOTS][SP_REPORT_SIZE];
  ULONG ReplyHead;
  ULONG ReplyCount;

  // Latest rumble from games, waiting for IOCTL_SLPAD_GET_RUMBLE.
  USHORT RumbleLow;
  USHORT RumbleHigh;
  BOOLEAN RumblePending;

  // Results of the last VhfCreate/VhfStart, returned by IOCTL_SLPAD_CREATE for diagnostics.
  NTSTATUS LastCreateStatus;
  NTSTATUS LastStartStatus;
  ULONG LastHandle;  // low 32 bits of the VHF handle
} PAD, *PPAD;

typedef struct _DEVICE_CONTEXT {
  // Plain spinlock in the context (not a WDF child object) so it stays valid during device cleanup.
  KSPIN_LOCK Lock;
  WDFWAITLOCK ControlLock;  // serialises creating and destroying controllers
  WDFTIMER Timer;
  WDFQUEUE RumbleQueue;     // pending IOCTL_SLPAD_GET_RUMBLE requests
  PAD Pads[SLPAD_MAX_PADS];
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD StreamlightPadDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP StreamlightPadDeviceCleanup;
EVT_WDF_DEVICE_D0_ENTRY StreamlightPadD0Entry;
EVT_WDF_DEVICE_D0_EXIT StreamlightPadD0Exit;
EVT_WDF_FILE_CLEANUP StreamlightPadFileCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL StreamlightPadIoDeviceControl;
EVT_WDF_TIMER StreamlightPadTimer;
