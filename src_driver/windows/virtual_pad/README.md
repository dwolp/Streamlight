# Streamlight virtual Switch Pro controller (Windows driver)

A kernel-mode KMDF driver that uses Microsoft's Virtual HID Framework (VHF) to create a virtual HID gamepad on Windows. It is step 1 of the plan: a device with a Switch Pro-shaped report descriptor and identity (VID `057E`, PID `2009`), with test input so you can see it in `joy.cpl`.

Streamlight does not use this driver yet. Step 2 adds the Switch-specific protocol, and step 3 connects it to Streamlight's input path.

## Files

| File | Purpose |
|---|---|
| `driver.c`, `driver.h` | KMDF device setup, VHF lifecycle, input timer, output report handling |
| `switch_pro_descriptor.h` | HID report descriptor and report layout |
| `security_cookie.c` | Stack-protection symbols the linker needs (this WDK install doesn't provide them) |
| `streamlight_virtual_pad.inf` | Installs the driver on the `Root\StreamlightVirtualPad` device |
| `build.cmd` | Builds `build\streamlight_virtual_pad.sys` with the installed WDK and MSVC |
| `install.ps1` | Stages, test-signs and installs the driver. Run as administrator. |
| `uninstall.ps1` | Removes the device and driver package. Run as administrator. |

## Build

Needs the Visual Studio 2022 Build Tools (C++ workload) and WDK 10.0.26100. From a normal shell:

```
build.cmd
```

The build uses MSVC directly, not `vcvars64.bat`, because `vcvars64.bat` fails on some PATH entries on this machine.

## Install and test

Test-signing must be on (`bcdedit /set testsigning on`, then reboot). The desktop shows a "Test Mode" watermark while it's on.

From an elevated PowerShell:

```
.\install.ps1
```

Then open **Set up USB game controllers** (`joy.cpl`). A device named "Streamlight Virtual Nintendo Switch Pro Controller" should appear. Button A toggles about once a second, so you can see that input reaches the OS.

To remove it, run `.\uninstall.ps1` as administrator.

## Known limits (step 1)

- The input is a test pattern. Nothing is read from Streamlight yet.
- Output reports (rumble, LEDs, Switch subcommands) are accepted and ignored, so Steam and games won't see a real Switch Pro protocol yet.
- Motion and touch are not implemented.
- The device identity is the Pro's USB ID, but the report layout is a generic gamepad with the Pro's report ID. Games and Steam may not recognize it as a Pro until step 2.

## Verified so far

- `build.cmd` compiles and links the driver with WDK 10.0.26100 and KMDF 1.33.
- The INF passes the WDK's `infverif /w` (strict Windows Driver) check.
- The driver follows Microsoft's VHF guidance: `vhf` is a lower filter in the INF, the VHF device is created in `EvtDriverDeviceAdd` and deleted in the device cleanup callback, and input reports are submitted only after `EvtVhfReadyForNextReadReport`.

## Not yet verified

- Loading the driver and the VHF device appearing in `joy.cpl`. Nothing has been installed yet.
- `KmdfLibraryVersion = 1.33` assumes Windows ships KMDF 1.33 or later, which Windows 11 does. No coinstaller is included.

A kernel-mode driver can crash the PC if it's wrong. Save your work before the first install.
