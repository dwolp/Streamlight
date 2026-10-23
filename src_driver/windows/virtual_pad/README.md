# Streamlight virtual Switch Pro controller (Windows driver)

A kernel-mode KMDF driver that uses Microsoft's Virtual HID Framework (VHF) to create a virtual Nintendo Switch Pro Controller on Windows. It has the Pro's USB identity (VID `057E`, PID `2009`), declares the same HID reports, and answers the controller protocol, so SDL and Steam should treat it as a Pro Controller.

Streamlight does not feed it input yet, so the controller reports neutral input (no buttons pressed, sticks centred). Connecting it to Streamlight's input path is step 3.

## Files

| File | Purpose |
|---|---|
| `driver.c`, `driver.h` | KMDF device setup, VHF lifecycle, input report timer, output report handling |
| `switch_pro_protocol.c`, `.h` | The controller protocol: USB commands, sub-commands, emulated SPI flash, report building. Plain C, shared with the tests. |
| `switch_pro_descriptor.h` | HID report descriptor |
| `security_cookie.c` | Stack-protection symbols the linker needs (this WDK install doesn't provide them) |
| `streamlight_virtual_pad.inf` | Installs the driver on the `Root\StreamlightVirtualPad` device |
| `build.cmd` | Builds `build\streamlight_virtual_pad.sys` with the installed WDK and MSVC |
| `install.ps1` | Stages, test-signs and installs or updates the driver. Run as administrator. |
| `uninstall.ps1` | Removes the device and driver package. Run as administrator. |
| `tests\run_tests.cmd` | Builds and runs the user-mode protocol tests |
| `tests\test_device.ps1` | Runs SDL's start-up sequence against the installed controller |

## Protocol

The behaviour follows what SDL's HIDAPI Switch driver sends and checks (`src/joystick/hidapi/SDL_hidapi_switch.c` in libsdl-org/SDL), which Steam also relies on.

- **USB commands (output `0x80`):** status (`0x01`) replies with controller type 3 (Pro) and a MAC address. Handshake (`0x02`) and high-speed (`0x03`) reply with `0x81`. Force USB (`0x04`) and the rest have no reply.
- **Sub-commands (output `0x01`):** replied to with input report `0x21`, ACK byte at 13 and the echoed ID at 14. Device info (`0x02`) and SPI flash reads (`0x10`) return data. Everything else gets a plain ACK.
- **SPI flash:** emulated with factory stick calibration (centre 2048, range ±1536), factory IMU calibration and colours. Unwritten areas read `0xFF`, so the host treats user calibration as absent.
- **Input:** `0x30` reports are streamed about every 8 ms. Pending replies are sent ahead of them. SDL gives up on USB if nothing arrives for 100 ms.
- **Not implemented:** rumble (accepted and ignored), motion (IMU data is zero), NFC/IR.

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

If the controller is already installed, the script updates it instead of adding a second one. If it says a restart is needed, restart Windows.

Then, from a normal shell:

```
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_device.ps1
```

To remove it, run `uninstall.ps1` as administrator. The test certificate stays in the machine's trusted stores.

## Status

- Step 1 (verified on Windows 11): the driver loads, VHF creates the HID child, and input reports reach user mode.
- Step 2: the protocol unit tests pass. The device test and Steam recognition have not been run yet.

A kernel-mode driver can crash the PC if it's wrong. Save your work before installing.
