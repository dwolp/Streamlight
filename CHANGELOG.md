# Changelog

Changes in Streamlight compared to [Apollo](https://github.com/ClassicOldSong/Apollo), which it is forked from. For upstream Sunshine changes, see `docs/changelog.md`.

## 2026-10-24

### Nintendo Switch Pro Controller emulation (Windows)

Streamlight can now present a controller to games on the host as a real Nintendo Switch Pro Controller. Previously, Windows only supported Xbox 360 and DualShock 4 emulation through ViGEmBus. SDL 3 and Steam recognise the virtual controller as a "Nintendo Switch Pro Controller".

**New Windows driver** (`src_driver/windows/virtual_pad`)

- A kernel-mode KMDF driver built on Microsoft's Virtual HID Framework (VHF). Each virtual controller is a separate HID device with the Pro Controller's USB identity (VID `057E`, PID `2009`) and HID report layout.
- Emulates the controller protocol that SDL's Switch driver (and Steam) expects:
  - USB commands: status, handshake and high-speed.
  - Sub-commands, including device info, input mode, player lights, vibration and IMU on/off.
  - Emulated SPI flash with factory stick and IMU calibration and controller colours.
  - The standard `0x30` input report, streamed about every 8 ms.
- Up to 16 controllers at once, created and removed on demand through a control interface. Each controller gets its own MAC address, so SDL and Steam treat them as separate controllers.
- Controllers are removed automatically when Streamlight closes its handle, including after a crash. No phantom controllers are left behind.
- Rumble from games (Switch HD rumble) is decoded into low- and high-frequency motor strengths and passed back to Streamlight.
- Motion (gyro and accelerometer) is fed into the IMU part of the input report once a game enables the IMU, as with the real controller.
- Access to the control interface is limited to SYSTEM, Administrators and the interactive user.
- Build, install and test scripts:
  - `build.cmd` builds with MSVC and WDK 10.0.26100.
  - `install.ps1` and `uninstall.ps1` handle test-signed installs; `install.ps1` updates an existing install in place.
  - `tests\run_tests.cmd` runs the user-mode protocol unit tests.
  - `tests\pad_control.c` creates a controller without Streamlight.
  - `tests\test_device.ps1` replays SDL's start-up sequence against the device.
- The driver is test-signed, so Windows test-signing must be turned on.

**Streamlight integration** (`src/platform/windows/virtual_switch_pad.cpp`)

- New gamepad type "Nintendo Pro (Switch)" on Windows.
- In "auto" mode, Streamlight now picks the Switch Pro Controller when the client reports a Nintendo controller and the driver is installed.
- If the driver isn't installed, or a controller can't be created, Streamlight falls back to ViGEm as before.
- Buttons map by label (A to A, B to B, …), matching Streamlight's Switch emulation on Linux. ZL and ZR are pressed by any trigger travel.
- Rumble is forwarded to the client, and respects the "Forward Rumble Messages" setting.
- When the client controller has motion sensors, Streamlight asks the client for motion and converts it into the Pro Controller's axes and units, so SDL reads back the same values the client sent.
- Mixed setups work, for example one controller as a Switch Pro Controller and another as an Xbox 360 controller through ViGEm.
- The log records, for each Switch Pro controller, whether the client reports motion and rumble. At debug level it also records every rumble change from games.

### Input status and Input settings page

- New "Status" box at the top of Configuration → Input, refreshed every 3 seconds:
  - **Gamepad drivers:** whether ViGEmBus and the Switch Pro driver are installed (on Linux, which emulation types are available).
  - **Connected controllers:** each controller's number on the client, the type the client reported, what Streamlight emulates it as, and which features the client reports: rumble, motion, touchpad and analog triggers.
- New API endpoint `GET /api/input/status` (requires login) that provides this data. Streamlight records each controller when it connects and forgets it when it disconnects.
- The Input page is regrouped into Gamepads, Keyboard, Mouse and touch, and Other. Options only appear when they apply to the chosen gamepad type and platform.
- Gamepad types whose driver isn't installed are greyed out and marked "not installed". The "auto" description explains how the type is chosen. A note explains that rumble and motion for Switch Pro Controllers depend on the client.
- Checkboxes on all configuration pages are more compact: the description and default value share one line under the label.

### Web interface

- New look across all pages:
  - A purple accent colour with matching light and dark themes, and a gradient navigation bar.
  - Tighter spacing, with restyled cards, buttons, forms, tables and menus.
- Home page:
  - The changelog of new releases is replaced by an "About Streamlight" section that describes what the app does.
  - The update check is removed. The home page no longer contacts GitHub.
  - The Clients box (client downloads), the Resources box (links to Apollo's discussions and wiki) and the version display are removed. The home page now shows the About section and the Legal links. The first-run welcome page no longer shows Resources either.
- The "notify pre-releases" option is removed from the General settings, as it only applied to the update check.
- Applications page: a more compact list and toolbar.
- Configuration page: tabs are a single-row segmented control, and Save and Apply sit in a bar fixed to the bottom of the page.
- The shared stylesheet is now loaded after the bundled Bootstrap styles, so the theme applies consistently.

### Branding and documentation

- User-visible branding renamed from Apollo to Streamlight.
- Windows install renamed from Apollo to Streamlight:
  - The installer is `Streamlight.exe`, installs to `C:\Program Files\Streamlight`, and shows as "Streamlight" in the Start menu and in Apps & features.
  - The service is `StreamlightService` ("Streamlight Service"), and the tray icon tooltip says Streamlight.
  - The firewall rules are named "Streamlight".
  - The program itself is still `sunshine.exe`, with `tools\sunshinesvc.exe` as the service helper.
- Moving over from Apollo on Windows: on first install, the installer copies the configuration of an existing Apollo install. That covers the web UI login, paired clients, apps, covers and settings. It also takes over the Apollo service's start type and removes the Apollo service, so the two don't compete for the same ports. Apollo's files are left in place, and its uninstaller can be run afterwards: answer "No" to removing the Virtual Gamepad and SudoVDA drivers, since Streamlight uses them too. Its firewall rules are named "Apollo", so they don't affect Streamlight's.
- README rewritten for Streamlight as a personal fork, with a note that the fork was made with the help of Claude (Anthropic), that the changes have not been quality checked by a human, and that contributions are not accepted.

### Fixes

- The Windows build failed in `src/video.cpp`: an upstream change used a `profile` field that the video configuration has never had. The AMD AMF H.264 encoder now always uses the High profile, which is what that code fell back to.

### Known limitations

- **Motion and rumble on the NVIDIA Shield:** Artemis on the Shield (Android 11) reports neither motion sensors nor rumble for a Switch Pro Controller, so neither works there, even though the host side supports both. A client on Android 12 or later is needed for motion. Connected controllers in the Input status box show what the client reports.
- **Multiple controllers in Artemis:** Artemis sends every controller as the same controller unless its multiple-controller setting is enabled.
- **Rumble strength is approximate:** Switch HD rumble amplitudes are encoded on a logarithmic scale and mapped linearly.
- **Test-signed driver:** the Switch Pro driver is not production-signed. Windows test-signing must be on, and the desktop shows a "Test Mode" watermark.
- **Only built on Windows here:** the Linux and macOS parts of the input status (`gamepad_emulated_type`) have not been built or tested.
