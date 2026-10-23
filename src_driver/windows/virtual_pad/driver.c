/*
 * Streamlight virtual Switch Pro controller: KMDF driver with a VHF virtual HID device.
 * See driver.h.
 *
 * Lifecycle follows Microsoft's VHF guidance: the VHF device is created and started in
 * EvtDriverDeviceAdd and deleted (synchronously) in the device's cleanup callback.
 * The input timer runs only while the device is in D0.
 *
 * Output reports from the host are answered by switch_pro_protocol.c. Replies are queued and
 * sent ahead of the regular 0x30 input reports, which are streamed like the real controller.
 */
#include "driver.h"
#include "switch_pro_descriptor.h"

// Interval for input reports, in milliseconds. The real controller sends about every 8 ms over
// USB; Windows timer resolution may stretch this. SDL re-sends "force USB" if nothing arrives
// for 100 ms, so it must stay well below that.
#define STREAMLIGHT_PAD_POLL_MS 8

// Container ID shared by the device's HID child. Must stay stable across reboots.
static const GUID g_streamlight_pad_container_id = {
    0x5e0d7c2a, 0x7b3e, 0x4d2a, {0x9f, 0x3c, 0x7a, 0x1e, 0x2b, 0x3c, 0x4d, 0x5e}};

// VHF calls this when the OS can take another input report. Because this callback is
// registered, VHF does not buffer reports for us: we submit at most one per callback.
static VOID StreamlightPadReadyForNextRead(PVOID VhfClientContext) {
  WDFDEVICE device = (WDFDEVICE) VhfClientContext;
  PDEVICE_CONTEXT ctx = GetDeviceContext(device);
  KIRQL irql;

  KeAcquireSpinLock(&ctx->Lock, &irql);
  ctx->ReadyForNextRead = TRUE;
  KeReleaseSpinLock(&ctx->Lock, irql);
}

// Output reports from the host: USB commands, sub-commands and rumble.
static VOID StreamlightPadWriteReport(PVOID VhfClientContext, VHFOPERATIONHANDLE VhfOperationHandle, PVOID VhfOperationContext, PHID_XFER_PACKET HidTransferPacket) {
  UNREFERENCED_PARAMETER(VhfOperationContext);

  WDFDEVICE device = (WDFDEVICE) VhfClientContext;
  PDEVICE_CONTEXT ctx = GetDeviceContext(device);
  const UCHAR reportId = HidTransferPacket->reportId;
  const UCHAR *payload = HidTransferPacket->reportBuffer;
  ULONG length = HidTransferPacket->reportBufferLen;
  KIRQL irql;

  // A full-size report includes the report ID as its first byte. Decide by length rather than
  // by comparing the first byte: a sub-command's packet counter can equal its report ID.
  if (length == SP_REPORT_SIZE) {
    payload++;
    length--;
  }

  KeAcquireSpinLock(&ctx->Lock, &irql);
  if (ctx->ReplyCount < STREAMLIGHT_PAD_REPLY_SLOTS) {
    ULONG slot = (ctx->ReplyHead + ctx->ReplyCount) % STREAMLIGHT_PAD_REPLY_SLOTS;
    if (sp_handle_output(reportId, payload, length, &ctx->State, ctx->ReportTimer, ctx->Replies[slot])) {
      ctx->ReplyCount++;
    }
  } else {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad: reply queue full, dropping reply to 0x%02x\n", reportId);
  }
  KeReleaseSpinLock(&ctx->Lock, irql);

  VhfAsyncOperationComplete(VhfOperationHandle, STATUS_SUCCESS);
}

VOID StreamlightPadTimer(WDFTIMER Timer) {
  WDFDEVICE device = (WDFDEVICE) WdfTimerGetParentObject(Timer);
  PDEVICE_CONTEXT ctx = GetDeviceContext(device);
  UCHAR report[SP_REPORT_SIZE];
  VHFHANDLE vhf = NULL;
  KIRQL irql;

  KeAcquireSpinLock(&ctx->Lock, &irql);
  if (ctx->ReadyForNextRead && ctx->VhfHandle != NULL) {
    ctx->ReadyForNextRead = FALSE;
    vhf = ctx->VhfHandle;

    if (ctx->ReplyCount > 0) {
      RtlCopyMemory(report, ctx->Replies[ctx->ReplyHead], SP_REPORT_SIZE);
      ctx->ReplyHead = (ctx->ReplyHead + 1) % STREAMLIGHT_PAD_REPLY_SLOTS;
      ctx->ReplyCount--;
    } else {
      sp_build_full_report(&ctx->State, ctx->ReportTimer, report);
    }
    ctx->ReportTimer++;
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
      DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad: VhfReadReportSubmit failed 0x%08x\n", status);
    }
  }
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
  VHFHANDLE vhf;
  KIRQL irql;

  // The timer is already stopped: the device always leaves D0 (StreamlightPadD0Exit) before
  // cleanup, and it never started if device add failed. Child WDF objects such as the timer
  // may already be cleaned up here, so only the context's own fields are used.
  KeAcquireSpinLock(&ctx->Lock, &irql);
  vhf = ctx->VhfHandle;
  ctx->VhfHandle = NULL;
  KeReleaseSpinLock(&ctx->Lock, irql);

  if (vhf != NULL) {
    VhfDelete(vhf, TRUE);
  }
}

static NTSTATUS StreamlightPadCreateVhf(WDFDEVICE device, PDEVICE_CONTEXT ctx) {
  VHF_CONFIG config;
  VHFHANDLE vhf;
  NTSTATUS status;

  VHF_CONFIG_INIT(&config, WdfDeviceWdmGetDeviceObject(device), sizeof(g_streamlight_pad_report_descriptor), (PUCHAR) g_streamlight_pad_report_descriptor);
  config.VhfClientContext = device;
  config.VendorID = STREAMLIGHT_PAD_VID;
  config.ProductID = STREAMLIGHT_PAD_PID;
  config.VersionNumber = STREAMLIGHT_PAD_VERSION;
  config.ContainerID = g_streamlight_pad_container_id;
  config.EvtVhfReadyForNextReadReport = StreamlightPadReadyForNextRead;
  config.EvtVhfAsyncOperationWriteReport = StreamlightPadWriteReport;

  status = VhfCreate(&config, &vhf);
  if (!NT_SUCCESS(status)) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad: VhfCreate failed 0x%08x\n", status);
    return status;
  }

  // Publish the handle before starting, so the cleanup callback deletes it if VhfStart fails.
  ctx->VhfHandle = vhf;

  status = VhfStart(vhf);
  if (!NT_SUCCESS(status)) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad: VhfStart failed 0x%08x\n", status);
  }
  return status;
}

NTSTATUS StreamlightPadDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit) {
  UNREFERENCED_PARAMETER(Driver);

  WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
  WDF_OBJECT_ATTRIBUTES attributes;
  WDF_TIMER_CONFIG timerConfig;
  WDFDEVICE device;
  PDEVICE_CONTEXT ctx;
  NTSTATUS status;

  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
  pnpCallbacks.EvtDeviceD0Entry = StreamlightPadD0Entry;
  pnpCallbacks.EvtDeviceD0Exit = StreamlightPadD0Exit;
  WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
  attributes.EvtCleanupCallback = StreamlightPadDeviceCleanup;
  status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  // The context is zero-initialised by WDF; cleanup relies on VhfHandle being NULL until set.
  ctx = GetDeviceContext(device);
  KeInitializeSpinLock(&ctx->Lock);
  sp_state_init(&ctx->State);

  WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, StreamlightPadTimer, STREAMLIGHT_PAD_POLL_MS);
  timerConfig.AutomaticSerialization = FALSE;
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ParentObject = device;
  status = WdfTimerCreate(&timerConfig, &attributes, &ctx->Timer);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  return StreamlightPadCreateVhf(device, ctx);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
  WDF_DRIVER_CONFIG config;

  WDF_DRIVER_CONFIG_INIT(&config, StreamlightPadDeviceAdd);
  return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}
