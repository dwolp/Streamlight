# Streamlight virtual Switch Pro controller (Windows driver)

A kernel-mode KMDF driver that uses Microsoft's Virtual HID Framework (VHF) to create virtual Nintendo Switch Pro Controllers on Windows. Each controller has the Pro's USB identity (VID `057E`, PID `2009`), declares the same HID reports, and answers the controller protocol, so SDL and Steam treat it as a Pro Controller.

Streamlight uses it when the gamepad type is "Nintendo Pro (Switch)", or in "auto" mode when the client reports a Nintendo controller (`src/platform/windows/virtual_switch_pad.cpp`). Controllers exist only while Streamlight has created them, and disappear when its handle closes, including after a crash.

## Files

| File | Purpose |
|---|---|
| `driver.c`, `driver.h` | KMDF device setup, control interface, VHF lifecycle per controller, input report timer, output report and rumble handling |
| `virtual_pad_ioctl.h` | Control interface shared with Streamlight: create, destroy, set state, get rumble |
| `switch_pro_protocol.c`, `.h` | The controller protocol: USB commands, sub-commands, emulated SPI flash, report building, rumble decoding. Plain C, shared with the tests and Streamlight. |
| `switch_pro_descriptor.h` | HID report descriptor |
| `security_cookie.c` | Stack-protection symbols the linker needs (this WDK install doesn't provide them) |
| `streamlight_virtual_pad.inf` | Installs the driver on the `Root\StreamlightVirtualPad` device |
| `build.cmd` | Builds `build\streamlight_virtual_pad.sys` with the installed WDK and MSVC |
| `install.ps1` | Stages, test-signs and installs or updates the driver. Run as administrator. |
| `uninstall.ps1` | Removes the device and driver package. Run as administrator. |
| `tests\run_tests.cmd` | Builds and runs the user-mode protocol tests, and builds `pad_control.exe` |
| `tests\pad_control.c` | Creates a controller through the control interface without Streamlight, alternates button A and the left stick, and prints rumble |
| `tests\test_device.ps1` | Runs SDL's start-up sequence against an existing controller |

## Control interface

The root device exposes the interface `GUID_DEVINTERFACE_STREAMLIGHT_VIRTUAL_PAD`. Access is limited to SYSTEM, Administrators and the interactive user.

- `IOCTL_SLPAD_CREATE` / `IOCTL_SLPAD_DESTROY` add or remove the controller in one of 16 slots. Each controller is its own VHF device. Only the handle that created a controller can remove it or change its state.
- `IOCTL_SLPAD_SET_STATE` sets buttons (Switch layout) and 12-bit stick values. The change is sent to the OS right away when it is ready for a report.
- `IOCTL_SLPAD_GET_RUMBLE` stays pending until a game changes the rumble on one of the handle's controllers.
- Closing the handle removes all of its controllers.

## Protocol

The behaviour follows what SDL's HIDAPI Switch driver sends and checks (`src/joystick/hidapi/SDL_hidapi_switch.c` in libsdl-org/SDL), which Steam also relies on.

- **USB commands (output `0x80`):** status (`0x01`) replies with controller type 3 (Pro) and a MAC address. Handshake (`0x02`) and high-speed (`0x03`) reply with `0x81`. Force USB (`0x04`) and the rest have no reply.
- **Sub-commands (output `0x01`):** replied to with input report `0x21`, ACK byte at 13 and the echoed ID at 14. Device info (`0x02`) and SPI flash reads (`0x10`) return data. Everything else gets a plain ACK.
- **SPI flash:** emulated with factory stick calibration (centre 2048, range ±1536), factory IMU calibration and colours. Unwritten areas read `0xFF`, so the host treats user calibration as absent.
- **Input:** `0x30` reports are streamed about every 8 ms. Pending replies are sent ahead of them. SDL gives up on USB if nothing arrives for 100 ms.
- **Rumble:** the HD rumble in output reports `0x01` and `0x10` is reduced to the strongest low-band and high-band amplitude of the two actuators. The encoded amplitude scale is logarithmic and is mapped linearly, which is an approximation.
- **Not implemented:** motion (IMU data is zero), NFC/IR.

## Build and test

Needs the Visual Studio 2022 Build Tools (C++ workload) and WDK 10.0.26100.

```
build.cmd
tests\run_tests.cmd
```

The build uses MSVC directly, not `vcvars64.bat`, because `vcvars64.bat` fails on some PATH entries on this machine.

## Install, update and check

Test-signing must be on (`bcdedit /set testsigning on`, then reboot). From an elevated PowerShell:

```
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
```

If the driver is already installed, the script updates it instead of adding a second device. If it says a restart is needed, restart Windows.

Without Streamlight running there is no controller. To check the driver on its own, start the control tool in one window:

```
build\tests\pad_control.exe 30
```

Then, while it runs, from another normal shell:

```
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_device.ps1
```

`test_device.ps1` expects neutral input, so its last check fails while `pad_control.exe` is moving the stick. That is expected.

To remove the driver, run `uninstall.ps1` as administrator. The test certificate stays in the machine's trusted stores.

## Status

- Step 1 (verified on Windows 11): the driver loads, VHF creates the HID child, and input reports reach user mode.
- Step 2 (verified on Windows 11): `test_device.ps1` passes, SDL 3.4.18 opens it as a "Nintendo Switch Pro Controller", and Steam lists it.
- Step 3 (verified on Windows 11): Streamlight creates a virtual Pro Controller per client controller. A real Pro Controller on an NVIDIA Shield with Artemis shows up on the PC as a Pro Controller. Two controllers at once work, and mixing with ViGEm controllers works. With `pad_control.exe`, SDL reads separate serial numbers per controller and the expected accelerometer and gyro values. Rumble from Steam reaches Streamlight.

## Client limits

- Artemis on the NVIDIA Shield (Android 11) reports neither rumble nor motion for a Switch Pro Controller, so neither works there. The host side supports both; a client on Android 12 or later is needed for motion.
- Artemis sends all controllers as one unless its multiple-controller setting is enabled.
- Configuration → Input → Status in the web UI shows what the client reports for each connected controller.

A kernel-mode driver can crash the PC if it's wrong. Save your work before installing.
