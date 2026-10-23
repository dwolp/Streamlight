/*
 * Streamlight virtual Switch Pro controller: KMDF driver with a VHF virtual HID device.
 * Step 1 - see driver.h.
 */
#include "driver.h"
#include "switch_pro_descriptor.h"

// Poll interval for submitting input reports, in milliseconds.
#define STREAMLIGHT_PAD_POLL_MS 4

// Test pattern: toggle button A once a second (250 ticks x 4 ms) so joy.cpl shows activity.
#define STREAMLIGHT_PAD_TEST_TOGGLE_TICKS 250

// Container ID shared by the device's HID child. Must stay stable across reboots.
static const GUID g_streamlight_pad_container_id = {
    0x5e0d7c2a, 0x7b3e, 0x4d2a, {0x9f, 0x3c, 0x7a, 0x1e, 0x2b, 0x3c, 0x4d, 0x5e}};

static VOID StreamlightPadResetState(PAD_STATE *state) {
  RtlZeroMemory(state, sizeof(*state));
  state->report[0] = STREAMLIGHT_PAD_REPORT_ID;

  // Centre both sticks (0x8000 little endian).
  for (int i = 4; i < STREAMLIGHT_PAD_REPORT_SIZE; i += 2) {
    state->report[i] = 0x00;
    state->report[i + 1] = 0x80;
  }
}

// VHF calls this when the OS can take another input report.
static VOID StreamlightPadReadyForNextRead(PVOID VhfClientContext) {
  WDFDEVICE device = (WDFDEVICE) VhfClientContext;
  PDEVICE_CONTEXT ctx = GetDeviceContext(device);

  WdfSpinLockAcquire(ctx->Lock);
  ctx->ReadyForNextRead = TRUE;
  WdfSpinLockRelease(ctx->Lock);
}

// Output reports (rumble, LEDs, subcommands) from the OS. Step 1 accepts and ignores them.
static VOID StreamlightPadWriteReport(PVOID VhfClientContext, VHFOPERATIONHANDLE VhfOperationHandle, PVOID VhfOperationContext, PHID_XFER_PACKET HidTransferPacket) {
  UNREFERENCED_PARAMETER(VhfClientContext);
  UNREFERENCED_PARAMETER(VhfOperationContext);

  DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "Streamlight pad: output report 0x%02x (%lu bytes) ignored\n",
             HidTransferPacket->reportId, HidTransferPacket->reportBufferLen);
  VhfAsyncOperationComplete(VhfOperationHandle, STATUS_SUCCESS);
}

VOID StreamlightPadTimer(WDFTIMER Timer) {
  WDFDEVICE device = (WDFDEVICE) WdfTimerGetParentObject(Timer);
  PDEVICE_CONTEXT ctx = GetDeviceContext(device);
  PAD_STATE snapshot;
  BOOLEAN send = FALSE;

  WdfSpinLockAcquire(ctx->Lock);
  ctx->TickCount++;
  if (ctx->TickCount % STREAMLIGHT_PAD_TEST_TOGGLE_TICKS == 0) {
    ctx->State.report[1] ^= (1 << PAD_BUTTON_A);
  }
  if (ctx->ReadyForNextRead && ctx->VhfHandle != NULL) {
    ctx->ReadyForNextRead = FALSE;
    snapshot = ctx->State;
    send = TRUE;
  }
  WdfSpinLockRelease(ctx->Lock);

  if (send) {
    HID_XFER_PACKET packet;
    NTSTATUS status;

    packet.reportBuffer = snapshot.report;
    packet.reportBufferLen = sizeof(snapshot.report);
    packet.reportId = STREAMLIGHT_PAD_REPORT_ID;

    status = VhfReadReportSubmit(ctx->VhfHandle, &packet);
    if (!NT_SUCCESS(status)) {
      DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad: VhfReadReportSubmit failed 0x%08x\n", status);
    }
  }
}

NTSTATUS StreamlightPadD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState) {
  UNREFERENCED_PARAMETER(PreviousState);

  PDEVICE_CONTEXT ctx = GetDeviceContext(Device);
  VHF_CONFIG config;
  NTSTATUS status;

  VHF_CONFIG_INIT(&config, WdfDeviceWdmGetDeviceObject(Device), sizeof(g_streamlight_pad_report_descriptor), (PUCHAR) g_streamlight_pad_report_descriptor);
  config.VhfClientContext = Device;
  config.VendorID = STREAMLIGHT_PAD_VID;
  config.ProductID = STREAMLIGHT_PAD_PID;
  config.VersionNumber = STREAMLIGHT_PAD_VERSION;
  config.ContainerID = g_streamlight_pad_container_id;
  config.EvtVhfReadyForNextReadReport = StreamlightPadReadyForNextRead;
  config.EvtVhfAsyncOperationWriteReport = StreamlightPadWriteReport;

  status = VhfCreate(&config, &ctx->VhfHandle);
  if (!NT_SUCCESS(status)) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad: VhfCreate failed 0x%08x\n", status);
    return status;
  }

  status = VhfStart(ctx->VhfHandle);
  if (!NT_SUCCESS(status)) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Streamlight pad: VhfStart failed 0x%08x\n", status);
    VhfDelete(ctx->VhfHandle, TRUE);
    ctx->VhfHandle = NULL;
    return status;
  }

  WdfSpinLockAcquire(ctx->Lock);
  ctx->ReadyForNextRead = FALSE;
  WdfSpinLockRelease(ctx->Lock);

  WdfTimerStart(ctx->Timer, WDF_REL_TIMEOUT_IN_MS(STREAMLIGHT_PAD_POLL_MS));
  return STATUS_SUCCESS;
}

NTSTATUS StreamlightPadD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState) {
  UNREFERENCED_PARAMETER(TargetState);

  PDEVICE_CONTEXT ctx = GetDeviceContext(Device);

  // Stop the timer first so it can't submit a report while the VHF device is being torn down.
  WdfTimerStop(ctx->Timer, TRUE);

  if (ctx->VhfHandle != NULL) {
    VhfDelete(ctx->VhfHandle, TRUE);
    ctx->VhfHandle = NULL;
  }
  return STATUS_SUCCESS;
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
  status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  ctx = GetDeviceContext(device);
  ctx->VhfHandle = NULL;
  ctx->ReadyForNextRead = FALSE;
  ctx->TickCount = 0;
  StreamlightPadResetState(&ctx->State);

  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ParentObject = device;
  status = WdfSpinLockCreate(&attributes, &ctx->Lock);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, StreamlightPadTimer, STREAMLIGHT_PAD_POLL_MS);
  timerConfig.AutomaticSerialization = FALSE;
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ParentObject = device;
  status = WdfTimerCreate(&timerConfig, &attributes, &ctx->Timer);
  return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
  WDF_DRIVER_CONFIG config;
  NTSTATUS status;

  WDF_DRIVER_CONFIG_INIT(&config, StreamlightPadDeviceAdd);
  status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
  return status;
}
