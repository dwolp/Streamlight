/*
 * Exercises the driver's control interface without Streamlight: creates a virtual controller,
 * alternates button A and the left stick every half second, sets a fixed motion sample, and
 * prints rumble sent by games. The controller is removed when the program exits (closing the
 * handle).
 *
 * The motion sample is what Streamlight sends for gravity along SDL's +Y axis (9.80665 m/s^2)
 * and a rotation of 90 deg/s around SDL's +X axis. SDL should read it back as accel (0, 9.81, 0)
 * and gyro (1.571 rad/s, 0, 0).
 *
 *   build\tests\pad_control.exe [seconds] [slot]   (defaults 15 and 0)
 */
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <initguid.h>
#include <stdio.h>
#include <stdlib.h>

#include "../switch_pro_protocol.h"
#include "../virtual_pad_ioctl.h"

static HANDLE open_control_interface(void) {
  HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_STREAMLIGHT_VIRTUAL_PAD, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  SP_DEVICE_INTERFACE_DATA data = {sizeof(data)};
  HANDLE handle = INVALID_HANDLE_VALUE;
  DWORD size = 0;

  if (set == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
  if (SetupDiEnumDeviceInterfaces(set, NULL, &GUID_DEVINTERFACE_STREAMLIGHT_VIRTUAL_PAD, 0, &data)) {
    SetupDiGetDeviceInterfaceDetailW(set, &data, NULL, 0, &size, NULL);
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = malloc(size);
    detail->cbSize = sizeof(*detail);
    if (SetupDiGetDeviceInterfaceDetailW(set, &data, detail, size, NULL, NULL)) {
      handle = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    }
    free(detail);
  }
  SetupDiDestroyDeviceInfoList(set);
  return handle;
}

static BOOL control(HANDLE device, DWORD code, const void *in, DWORD in_size) {
  OVERLAPPED overlapped = {0};
  DWORD bytes = 0;
  BOOL ok;

  overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
  ok = DeviceIoControl(device, code, (void *) in, in_size, NULL, 0, &bytes, &overlapped);
  if (!ok && GetLastError() == ERROR_IO_PENDING) ok = GetOverlappedResult(device, &overlapped, &bytes, TRUE);
  // OVERLAPPED.Internal holds the driver's NTSTATUS, which is more precise than the Win32 error.
  if (!ok) printf("request 0x%08lx failed: error %lu (NTSTATUS 0x%08lx)\n", code, GetLastError(), (unsigned long) overlapped.Internal);
  CloseHandle(overlapped.hEvent);
  return ok;
}

int main(int argc, char **argv) {
  int seconds = argc > 1 ? atoi(argv[1]) : 15;
  unsigned char slot = (unsigned char) (argc > 2 ? atoi(argv[2]) : 0);
  SLPAD_INDEX index = {slot};
  SLPAD_STATE state = {0};
  // Raw axes = (-z, -x, y) of SDL's axes: gravity on SDL +Y is raw Z = 4096 (1 G);
  // 90 deg/s around SDL +X is raw Y = -90 * 13371 / 936 = -1286.
  SLPAD_MOTION motion = {slot, {0, 0, 4096}, {0, -1286, 0}};
  SLPAD_RUMBLE rumble;
  OVERLAPPED rumble_io = {0};
  DWORD bytes;

  HANDLE device = open_control_interface();
  if (device == INVALID_HANDLE_VALUE) {
    printf("Control interface not found or not accessible (error %lu). Is the driver installed?\n", GetLastError());
    return 1;
  }
  printf("Control interface opened\n");

  {
    // The driver can return the raw VhfCreate/VhfStart results and the handle for diagnostics.
    LONG diag[3] = {0};
    OVERLAPPED overlapped = {0};
    BOOL ok;

    overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok = DeviceIoControl(device, IOCTL_SLPAD_CREATE, &index, sizeof(index), diag, sizeof(diag), &bytes, &overlapped);
    if (!ok && GetLastError() == ERROR_IO_PENDING) ok = GetOverlappedResult(device, &overlapped, &bytes, TRUE);
    printf("create: %s, NTSTATUS 0x%08lx | VhfCreate 0x%08lx, VhfStart 0x%08lx, handle 0x%08lx\n",
           ok ? "ok" : "FAILED", (unsigned long) overlapped.Internal,
           (unsigned long) diag[0], (unsigned long) diag[1], (unsigned long) diag[2]);
    CloseHandle(overlapped.hEvent);
    if (!ok) return 1;
  }
  printf("Created virtual controller in slot %u; running for %d s\n", slot, seconds);
  control(device, IOCTL_SLPAD_SET_MOTION, &motion, sizeof(motion));

  // Wait for rumble in the background.
  rumble_io.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (!DeviceIoControl(device, IOCTL_SLPAD_GET_RUMBLE, NULL, 0, &rumble, sizeof(rumble), &bytes, &rumble_io) && GetLastError() != ERROR_IO_PENDING) {
    printf("rumble request failed: error %lu\n", GetLastError());
  }

  for (int tick = 0; tick < seconds * 2; tick++) {
    BOOL pressed = tick % 2 == 0;

    state.index = slot;
    state.buttons[0] = pressed ? SP_BTN0_A : 0;
    state.buttons[1] = 0;
    state.buttons[2] = 0;
    state.lx = pressed ? SP_STICK_CENTER + SP_STICK_RANGE : SP_STICK_CENTER;
    state.ly = SP_STICK_CENTER;
    state.rx = SP_STICK_CENTER;
    state.ry = SP_STICK_CENTER;
    control(device, IOCTL_SLPAD_SET_STATE, &state, sizeof(state));

    if (WaitForSingleObject(rumble_io.hEvent, 500) == WAIT_OBJECT_0) {
      if (GetOverlappedResult(device, &rumble_io, &bytes, FALSE)) {
        printf("rumble: slot %u low %u high %u\n", rumble.index, rumble.low_frequency, rumble.high_frequency);
      }
      ResetEvent(rumble_io.hEvent);
      DeviceIoControl(device, IOCTL_SLPAD_GET_RUMBLE, NULL, 0, &rumble, sizeof(rumble), &bytes, &rumble_io);
    }
  }

  CancelIoEx(device, &rumble_io);
  GetOverlappedResult(device, &rumble_io, &bytes, TRUE);
  CloseHandle(device);
  printf("Closed; the driver removes the controller\n");
  return 0;
}
