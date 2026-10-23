/*
 * Streamlight virtual Switch Pro controller (kernel-mode VHF source driver).
 *
 * Creates a HID device with the Switch Pro Controller's USB identity and report layout and
 * answers the controller protocol (see switch_pro_protocol.c), so SDL/Steam treat it as a
 * Pro Controller. Input state is neutral until Streamlight feeds it (step 3).
 */
#pragma once

#define KMDF_VERSION_MAJOR 1
#define KMDF_VERSION_MINOR 33

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

#include "switch_pro_protocol.h"

// Nintendo's USB vendor ID and the Switch Pro Controller product ID.
#define STREAMLIGHT_PAD_VID 0x057E
#define STREAMLIGHT_PAD_PID 0x2009
#define STREAMLIGHT_PAD_VERSION 0x0200

// Replies waiting to be sent as input reports. The host sends one request and waits for its
// reply, so a few slots are plenty; if the queue is full the newest reply is dropped.
#define STREAMLIGHT_PAD_REPLY_SLOTS 8

typedef struct _DEVICE_CONTEXT {
  VHFHANDLE VhfHandle;
  WDFTIMER Timer;
  // Plain spinlock in the context (not a WDF child object) so it stays valid during device cleanup.
  KSPIN_LOCK Lock;
  BOOLEAN ReadyForNextRead;

  sp_state State;
  UCHAR ReportTimer;  // counter placed in every input report

  UCHAR Replies[STREAMLIGHT_PAD_REPLY_SLOTS][SP_REPORT_SIZE];
  ULONG ReplyHead;
  ULONG ReplyCount;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD StreamlightPadDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP StreamlightPadDeviceCleanup;
EVT_WDF_DEVICE_D0_ENTRY StreamlightPadD0Entry;
EVT_WDF_DEVICE_D0_EXIT StreamlightPadD0Exit;
EVT_WDF_TIMER StreamlightPadTimer;
