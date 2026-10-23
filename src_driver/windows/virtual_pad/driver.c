/*
 * Streamlight virtual Switch Pro controller: KMDF driver with VHF virtual HID devices.
 * See driver.h.
 *
 * Each virtual controller is its own VHF device, created and removed on request through the
 * control interface. Output reports from games are answered by switch_pro_protocol.c; replies
 * are queued and sent ahead of the regular 0x30 input reports, which are streamed like the real
 * controller's. Rumble is passed back to the handle that created the controller.
 *
 * The VHF lifecycle follows Microsoft's guidance: VhfDelete(Wait = TRUE) at PASSIVE_LEVEL, and
 * no report submission in flight while a device is deleted (rundown protection per controller).
 */
#include "driver.h"
#include "switch_pro_descriptor.h"

// Interval for input reports, in milliseconds. The real controller sends about every 8 ms over
// USB; Windows timer resolution may stretch this. SDL re-sends "force USB" if nothing arrives
// for 100 ms, so it must stay well below that.
#define STREAMLIGHT_PAD_POLL_MS 8

// Container ID of the first controller; controller n adds n to the last byte. Must stay stable
// across reboots.
static const GUID g_streamlight_pad_container_id = {
    0x5e0d7c2a, 0x7b3e, 0x4d2a, {0x9f, 0x3c, 0x7a, 0x1e, 0x2b, 0x3c, 0x4d, 0x50}};

static VOID StreamlightPadCompleteRumble(WDFREQUEST request, UCHAR index, USHORT low, USHORT high) {
  SLPAD_RUMBLE *out;
  NTSTATUS status = WdfRequestRetrieveOutputBuffer(request, sizeof(*out), (PVOID *) &out, NULL);

  if (NT_SUCCESS(status)) {
    out->index = index;
    out->low_frequency = low;
    out->high_frequency = high;
    WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, sizeof(*out));
  } else {
    WdfRequestComplete(request, status);
  }
}

// Hands pending rumble for a controller to a waiting IOCTL_SLPAD_GET_RUMBLE, if there is one.
static VOID StreamlightPadDeliverRumble(PDEVICE_CONTEXT ctx, PPAD pad) {
  WDFFILEOBJECT owner;
  WDFREQUEST request;
  BOOLEAN pending;
  USHORT low = 0, high = 0;
  KIRQL irql;

  KeAcquireSpinLock(&ctx->Lock, &irql);
  owner = pad->Owner;
  pending = pad->RumblePending;
  KeReleaseSpinLock(&ctx->Lock, irql);

  if (!pending || owner == NULL) {
    return;
  }
  if (!NT_SUCCESS(WdfIoQueueRetrieveRequestByFileObject(ctx->RumbleQueue, owner, &request))) {
    return;  // nobody is waiting; the rumble stays pending for the next request
  }

  KeAcquireSpinLock(&ctx->Lock, &irql);
  pending = pad->RumblePending && pad->Owner == owner;
  if (pending) {
    low = pad->RumbleLow;
    high = pad->RumbleHigh;
    pad->RumblePending = FALSE;
  }
  KeReleaseSpinLock(&ctx->Lock, irql);

  if (pending) {
    StreamlightPadCompleteRumble(request, pad->Index, low, high);
  } else {
    // Another path took the rumble first; put the request back for the next one.
    WdfRequestRequeue(request);
  }
}

// VHF calls this when the OS can take another input report. Because this callback is
// registered, VHF does not buffer reports for us: we submit at most one per callback.
static VOID StreamlightPadReadyForNextRead(PVOID VhfClientContext) {
  PPAD pad = (PPAD) VhfClientContext;
  PDEVICE_CONTEXT ctx = GetDeviceContext(pad->Device);
  KIRQL irql;

  KeAcquireSpinLock(&ctx->Lock, &irql);
  pad->ReadyForNextRead = TRUE;
  KeReleaseSpinLock(&ctx->Lock, irql);
}

// Output reports from games: USB commands, sub-commands and rumble.
static VOID StreamlightPadWriteReport(PVOID VhfClientContext, VHFOPERATIONHANDLE VhfOperationHandle, PVOID VhfOperationContext, PHID_XFER_PACKET HidTransferPacket) {
  UNREFERENCED_PARAMETER(VhfOperationContext);

  PPAD pad = (PPAD) VhfClientContext;
  PDEVICE_CONTEXT ctx = GetDeviceContext(pad->Device);
  const UCHAR reportId = HidTransferPacket->reportId;
  const UCHAR *payload = HidTransferPacket->reportBuffer;
  ULONG length = HidTransferPacket->reportBufferLen;
  USHORT low, high;
  BOOLEAN rumbleChanged = FALSE;
  KIRQL irql;

  // A full-size report includes the report ID as its first byte. Decide by length rather than
  // by comparing the first byte: a sub-command's packet counter can equal its report ID.
  if (length == SP_REPORT_SIZE) {
    payload++;
    length--;
  }

  KeAcquireSpinLock(&ctx->Lock, &irql);
  if (pad->ReplyCount < STREAMLIGHT_PAD_REPLY_SLOTS) {
    ULONG slot = (pad->ReplyHead + pad->ReplyCount) % STREAMLIGHT_PAD_REPLY_SLOTS;
    if (sp_handle_output(reportId, payload, length, &pad->State, pad->ReportTimer, pad->Replies[slot])) {
      pad->ReplyCount++;
    }
  } else {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad %u: reply queue full, dropping reply to 0x%02x\n", pad->Index, reportId);
  }

  if (sp_parse_rumble(reportId, payload, length, &low, &high) && (low != pad->RumbleLow || high != pad->RumbleHigh)) {
    pad->RumbleLow = low;
    pad->RumbleHigh = high;
    pad->RumblePending = TRUE;
    rumbleChanged = TRUE;
  }
  KeReleaseSpinLock(&ctx->Lock, irql);

  if (rumbleChanged) {
    StreamlightPadDeliverRumble(ctx, pad);
  }
  VhfAsyncOperationComplete(VhfOperationHandle, STATUS_SUCCESS);
}

// Submits the next input report for a controller if the OS is ready for one: a queued reply
// first, otherwise a 0x30 report with the current state.
static VOID StreamlightPadPump(PPAD pad) {
  PDEVICE_CONTEXT ctx = GetDeviceContext(pad->Device);
  UCHAR report[SP_REPORT_SIZE];
  VHFHANDLE vhf = NULL;
  KIRQL irql;

  if (!ExAcquireRundownProtection(&pad->Rundown)) {
    return;  // the controller is being removed
  }

  KeAcquireSpinLock(&ctx->Lock, &irql);
  if (pad->Vhf != NULL && pad->ReadyForNextRead) {
    pad->ReadyForNextRead = FALSE;
    vhf = pad->Vhf;

    if (pad->ReplyCount > 0) {
      RtlCopyMemory(report, pad->Replies[pad->ReplyHead], SP_REPORT_SIZE);
      pad->ReplyHead = (pad->ReplyHead + 1) % STREAMLIGHT_PAD_REPLY_SLOTS;
      pad->ReplyCount--;
    } else {
      sp_build_full_report(&pad->State, pad->ReportTimer, report);
    }
    pad->ReportTimer++;
  }
  KeReleaseSpinLock(&ctx->Lock, irql);

  if (vhf != NULL) {
    HID_XFER_PACKET packet;
    NTSTATUS status;

    packet.reportBuffer = report;
    packet.reportBufferLen = SP_REPORT_SIZE;
    packet.reportId = report[0];

    status = VhfReadReportSubmit(vhf, &packet);
    if (!NT_SUCCESS(status)) {
      DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad %u: VhfReadReportSubmit failed 0x%08x\n", pad->Index, status);
    }
  }

  ExReleaseRundownProtection(&pad->Rundown);
}

VOID StreamlightPadTimer(WDFTIMER Timer) {
  PDEVICE_CONTEXT ctx = GetDeviceContext((WDFDEVICE) WdfTimerGetParentObject(Timer));

  for (int i = 0; i < SLPAD_MAX_PADS; i++) {
    StreamlightPadPump(&ctx->Pads[i]);
  }
}

// Removes a controller. PASSIVE_LEVEL; callers serialise with ControlLock (except device cleanup).
static VOID StreamlightPadDestroy(PDEVICE_CONTEXT ctx, PPAD pad) {
  VHFHANDLE vhf;
  KIRQL irql;

  KeAcquireSpinLock(&ctx->Lock, &irql);
  vhf = pad->Vhf;
  pad->Vhf = NULL;
  pad->Owner = NULL;
  pad->RumblePending = FALSE;
  KeReleaseSpinLock(&ctx->Lock, irql);

  if (vhf == NULL) {
    return;
  }

  // Wait for any report submission to finish, then delete the device and reopen the slot.
  ExWaitForRundownProtectionRelease(&pad->Rundown);
  VhfDelete(vhf, TRUE);
  ExReInitializeRundownProtection(&pad->Rundown);
}

// Creates the controller for a slot. PASSIVE_LEVEL, called with ControlLock held.
static NTSTATUS StreamlightPadCreate(PDEVICE_CONTEXT ctx, PPAD pad, WDFFILEOBJECT owner) {
  GUID container = g_streamlight_pad_container_id;
  VHF_CONFIG config;
  VHFHANDLE vhf = NULL;
  NTSTATUS status;
  KIRQL irql;

  // Only this function and StreamlightPadDestroy change Vhf, both under ControlLock.
  if (pad->Vhf != NULL) {
    return pad->Owner == owner ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
  }

  // Reset the slot before the new device can call back.
  KeAcquireSpinLock(&ctx->Lock, &irql);
  sp_state_init(&pad->State);
  pad->State.id = pad->Index;  // gives each controller its own MAC address
  pad->ReadyForNextRead = FALSE;
  pad->ReportTimer = 0;
  pad->ReplyHead = 0;
  pad->ReplyCount = 0;
  pad->RumbleLow = 0;
  pad->RumbleHigh = 0;
  pad->RumblePending = FALSE;
  KeReleaseSpinLock(&ctx->Lock, irql);

  container.Data4[7] = (UCHAR) (container.Data4[7] + pad->Index);

  VHF_CONFIG_INIT(&config, WdfDeviceWdmGetDeviceObject(pad->Device), sizeof(g_streamlight_pad_report_descriptor), (PUCHAR) g_streamlight_pad_report_descriptor);
  config.VhfClientContext = pad;
  config.VendorID = STREAMLIGHT_PAD_VID;
  config.ProductID = STREAMLIGHT_PAD_PID;
  config.VersionNumber = STREAMLIGHT_PAD_VERSION;
  config.ContainerID = container;
  config.EvtVhfReadyForNextReadReport = StreamlightPadReadyForNextRead;
  config.EvtVhfAsyncOperationWriteReport = StreamlightPadWriteReport;

  // Judge VhfCreate by the handle, not its return value. When called after the source device has
  // started (as here, on request), it has been seen returning arbitrary values - with success and
  // with error severity - while the device was created and the handle valid. Treating such a
  // value as failure would leak a controller that nothing can delete.
  status = VhfCreate(&config, &vhf);
  pad->LastCreateStatus = status;
  pad->LastStartStatus = 0;
  pad->LastHandle = (ULONG) (ULONG_PTR) vhf;
  if (vhf == NULL) {
    if (NT_SUCCESS(status)) {
      status = STATUS_UNSUCCESSFUL;
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad %u: VhfCreate failed 0x%08x\n", pad->Index, status);
    return status;
  }

  KeAcquireSpinLock(&ctx->Lock, &irql);
  pad->Vhf = vhf;
  pad->Owner = owner;
  KeReleaseSpinLock(&ctx->Lock, irql);

  status = VhfStart(vhf);
  pad->LastStartStatus = status;
  if (!NT_SUCCESS(status)) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad %u: VhfStart failed 0x%08x\n", pad->Index, status);
    StreamlightPadDestroy(ctx, pad);
  }
  return status;
}

// Completes a rumble request at once if a controller of this handle has rumble waiting.
static BOOLEAN StreamlightPadTryCompleteRumble(PDEVICE_CONTEXT ctx, WDFFILEOBJECT file, WDFREQUEST request) {
  for (int i = 0; i < SLPAD_MAX_PADS; i++) {
    PPAD pad = &ctx->Pads[i];
    BOOLEAN pending;
    USHORT low = 0, high = 0;
    KIRQL irql;

    KeAcquireSpinLock(&ctx->Lock, &irql);
    pending = pad->Owner == file && pad->RumblePending;
    if (pending) {
      low = pad->RumbleLow;
      high = pad->RumbleHigh;
      pad->RumblePending = FALSE;
    }
    KeReleaseSpinLock(&ctx->Lock, irql);

    if (pending) {
      StreamlightPadCompleteRumble(request, pad->Index, low, high);
      return TRUE;
    }
  }
  return FALSE;
}

VOID StreamlightPadIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength, ULONG IoControlCode) {
  UNREFERENCED_PARAMETER(InputBufferLength);

  PDEVICE_CONTEXT ctx = GetDeviceContext(WdfIoQueueGetDevice(Queue));
  WDFFILEOBJECT file = WdfRequestGetFileObject(Request);
  NTSTATUS status;

  switch (IoControlCode) {
    case IOCTL_SLPAD_CREATE:
    case IOCTL_SLPAD_DESTROY: {
      SLPAD_INDEX *in;
      status = WdfRequestRetrieveInputBuffer(Request, sizeof(*in), (PVOID *) &in, NULL);
      if (!NT_SUCCESS(status)) {
        break;
      }
      if (in->index >= SLPAD_MAX_PADS) {
        status = STATUS_INVALID_PARAMETER;
        break;
      }

      PPAD pad = &ctx->Pads[in->index];
      WdfWaitLockAcquire(ctx->ControlLock, NULL);
      if (IoControlCode == IOCTL_SLPAD_CREATE) {
        status = StreamlightPadCreate(ctx, pad, file);
      } else if (pad->Vhf != NULL && pad->Owner != file) {
        status = STATUS_ACCESS_DENIED;  // only the creating handle may remove a controller
      } else {
        StreamlightPadDestroy(ctx, pad);
        status = STATUS_SUCCESS;
      }
      WdfWaitLockRelease(ctx->ControlLock);

      // Diagnostics: optionally return the raw VhfCreate/VhfStart results of the last create.
      if (IoControlCode == IOCTL_SLPAD_CREATE && OutputBufferLength >= 3 * sizeof(LONG)) {
        LONG *diag;
        if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, 3 * sizeof(LONG), (PVOID *) &diag, NULL))) {
          diag[0] = pad->LastCreateStatus;
          diag[1] = pad->LastStartStatus;
          diag[2] = (LONG) pad->LastHandle;
          WdfRequestCompleteWithInformation(Request, status, 3 * sizeof(LONG));
          return;
        }
      }
      break;
    }

    case IOCTL_SLPAD_SET_STATE: {
      SLPAD_STATE *in;
      KIRQL irql;

      status = WdfRequestRetrieveInputBuffer(Request, sizeof(*in), (PVOID *) &in, NULL);
      if (!NT_SUCCESS(status)) {
        break;
      }
      if (in->index >= SLPAD_MAX_PADS) {
        status = STATUS_INVALID_PARAMETER;
        break;
      }

      PPAD pad = &ctx->Pads[in->index];
      KeAcquireSpinLock(&ctx->Lock, &irql);
      if (pad->Vhf != NULL && pad->Owner == file) {
        RtlCopyMemory(pad->State.buttons, in->buttons, sizeof(pad->State.buttons));
        pad->State.lx = in->lx & 0x0FFF;
        pad->State.ly = in->ly & 0x0FFF;
        pad->State.rx = in->rx & 0x0FFF;
        pad->State.ry = in->ry & 0x0FFF;
        status = STATUS_SUCCESS;
      } else {
        status = STATUS_DEVICE_NOT_CONNECTED;
      }
      KeReleaseSpinLock(&ctx->Lock, irql);

      // Send the change now rather than waiting for the next timer tick.
      if (NT_SUCCESS(status)) {
        StreamlightPadPump(pad);
      }
      break;
    }

    case IOCTL_SLPAD_SET_MOTION: {
      SLPAD_MOTION *in;
      KIRQL irql;

      status = WdfRequestRetrieveInputBuffer(Request, sizeof(*in), (PVOID *) &in, NULL);
      if (!NT_SUCCESS(status)) {
        break;
      }
      if (in->index >= SLPAD_MAX_PADS) {
        status = STATUS_INVALID_PARAMETER;
        break;
      }

      PPAD pad = &ctx->Pads[in->index];
      KeAcquireSpinLock(&ctx->Lock, &irql);
      if (pad->Vhf != NULL && pad->Owner == file) {
        RtlCopyMemory(pad->State.accel, in->accel, sizeof(pad->State.accel));
        RtlCopyMemory(pad->State.gyro, in->gyro, sizeof(pad->State.gyro));
        status = STATUS_SUCCESS;
      } else {
        status = STATUS_DEVICE_NOT_CONNECTED;
      }
      KeReleaseSpinLock(&ctx->Lock, irql);
      break;
    }

    case IOCTL_SLPAD_GET_RUMBLE:
      if (OutputBufferLength < sizeof(SLPAD_RUMBLE)) {
        status = STATUS_BUFFER_TOO_SMALL;
        break;
      }
      if (StreamlightPadTryCompleteRumble(ctx, file, Request)) {
        return;
      }
      status = WdfRequestForwardToIoQueue(Request, ctx->RumbleQueue);
      if (NT_SUCCESS(status)) {
        // Rumble may have arrived between the check above and queueing; deliver it now.
        for (int i = 0; i < SLPAD_MAX_PADS; i++) {
          StreamlightPadDeliverRumble(ctx, &ctx->Pads[i]);
        }
        return;
      }
      break;

    default:
      status = STATUS_INVALID_DEVICE_REQUEST;
      break;
  }

  WdfRequestComplete(Request, status);
}

// The last handle to the control interface is closing: remove its controllers.
VOID StreamlightPadFileCleanup(WDFFILEOBJECT FileObject) {
  PDEVICE_CONTEXT ctx = GetDeviceContext(WdfFileObjectGetDevice(FileObject));

  WdfWaitLockAcquire(ctx->ControlLock, NULL);
  for (int i = 0; i < SLPAD_MAX_PADS; i++) {
    PPAD pad = &ctx->Pads[i];
    if (pad->Vhf != NULL && pad->Owner == FileObject) {
      StreamlightPadDestroy(ctx, pad);
    }
  }
  WdfWaitLockRelease(ctx->ControlLock);
}

NTSTATUS StreamlightPadD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState) {
  UNREFERENCED_PARAMETER(PreviousState);

  WdfTimerStart(GetDeviceContext(Device)->Timer, WDF_REL_TIMEOUT_IN_MS(STREAMLIGHT_PAD_POLL_MS));
  return STATUS_SUCCESS;
}

NTSTATUS StreamlightPadD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState) {
  UNREFERENCED_PARAMETER(TargetState);

  WdfTimerStop(GetDeviceContext(Device)->Timer, TRUE);
  return STATUS_SUCCESS;
}

VOID StreamlightPadDeviceCleanup(WDFOBJECT DeviceObject) {
  PDEVICE_CONTEXT ctx = GetDeviceContext((WDFDEVICE) DeviceObject);

  // All handles are closed by now, and the timer is stopped: the device always leaves D0
  // before cleanup. Child WDF objects (timer, queues, ControlLock) may already be cleaned up,
  // so only the context's own fields are used.
  for (int i = 0; i < SLPAD_MAX_PADS; i++) {
    StreamlightPadDestroy(ctx, &ctx->Pads[i]);
  }
}

NTSTATUS StreamlightPadDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit) {
  UNREFERENCED_PARAMETER(Driver);

  WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
  WDF_FILEOBJECT_CONFIG fileConfig;
  WDF_OBJECT_ATTRIBUTES attributes;
  WDF_IO_QUEUE_CONFIG queueConfig;
  WDF_TIMER_CONFIG timerConfig;
  WDFDEVICE device;
  PDEVICE_CONTEXT ctx;
  NTSTATUS status;

  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
  pnpCallbacks.EvtDeviceD0Entry = StreamlightPadD0Entry;
  pnpCallbacks.EvtDeviceD0Exit = StreamlightPadD0Exit;
  WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

  WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK, StreamlightPadFileCleanup);
  WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
  attributes.EvtCleanupCallback = StreamlightPadDeviceCleanup;
  status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  // The context is zero-initialised by WDF: every slot starts free (Vhf == NULL).
  ctx = GetDeviceContext(device);
  KeInitializeSpinLock(&ctx->Lock);
  for (int i = 0; i < SLPAD_MAX_PADS; i++) {
    ctx->Pads[i].Device = device;
    ctx->Pads[i].Index = (UCHAR) i;
    ExInitializeRundownProtection(&ctx->Pads[i].Rundown);
    sp_state_init(&ctx->Pads[i].State);
  }

  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ParentObject = device;
  status = WdfWaitLockCreate(&attributes, &ctx->ControlLock);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  // Control requests: sequential and at PASSIVE_LEVEL, since creating and deleting VHF devices
  // must happen at PASSIVE_LEVEL. They don't need the device in D0.
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
  queueConfig.EvtIoDeviceControl = StreamlightPadIoDeviceControl;
  queueConfig.PowerManaged = WdfFalse;
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ExecutionLevel = WdfExecutionLevelPassive;
  status = WdfIoQueueCreate(device, &queueConfig, &attributes, WDF_NO_HANDLE);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
  queueConfig.PowerManaged = WdfFalse;
  status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &ctx->RumbleQueue);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, StreamlightPadTimer, STREAMLIGHT_PAD_POLL_MS);
  timerConfig.AutomaticSerialization = FALSE;
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ParentObject = device;
  status = WdfTimerCreate(&timerConfig, &attributes, &ctx->Timer);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  return WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_STREAMLIGHT_VIRTUAL_PAD, NULL);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
  WDF_DRIVER_CONFIG config;

  WDF_DRIVER_CONFIG_INIT(&config, StreamlightPadDeviceAdd);
  return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}
