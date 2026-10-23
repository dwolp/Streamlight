/**
 * @file src/platform/windows/virtual_switch_pad.cpp
 * @brief Definitions for virtual Nintendo Switch Pro Controllers on Windows.
 */
#include "virtual_switch_pad.h"

// standard includes
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// platform includes
#include <SetupApi.h>
#include <winioctl.h>

// local includes
#include "src/config.h"
#include "src/logging.h"
#include "src/utility.h"
#include "src_driver/windows/virtual_pad/switch_pro_protocol.h"

// Defines the driver's interface GUID in this file. Kept after the other includes so their
// GUIDs are not defined again here.
#include <initguid.h>

#include "src_driver/windows/virtual_pad/virtual_pad_ioctl.h"

namespace platf {
  using namespace std::literals;

  namespace {
    constexpr float EARTH_G = 9.80665f;

    // How often the client should send each motion sensor, in Hz.
    constexpr std::uint16_t MOTION_REPORT_RATE = 100;

    /**
     * @brief Rounds and clamps a raw IMU value to the controller's 16-bit range.
     */
    std::int16_t to_raw_imu(float value) {
      return static_cast<std::int16_t>(std::clamp(std::lround(value), -32768L, 32767L));
    }

    /**
     * @brief Converts a client motion sample (SDL sensor axes) to the controller's own axes.
     * @details SDL maps a Pro Controller's raw axes to its sensor axes as
     * (x, y, z) = (-raw Y, raw Z, -raw X) for both accelerometer and gyro, so the raw axes are
     * (X, Y, Z) = (-z, -x, y). `scale` converts the client's units to raw units.
     */
    std::array<std::int16_t, 3> to_switch_axes(float x, float y, float z, float scale) {
      return {to_raw_imu(-z * scale), to_raw_imu(-x * scale), to_raw_imu(y * scale)};
    }

    /**
     * @brief Converts a client axis value (-32768..32767, up/right positive) to a 12-bit stick value.
     */
    std::uint16_t to_stick(std::int16_t value) {
      return static_cast<std::uint16_t>(SP_STICK_CENTER + static_cast<long>(value) * SP_STICK_RANGE / 32767);
    }

    /**
     * @brief Converts the client's gamepad state to the Switch Pro Controller layout.
     * @details Face buttons map by label (A to A, B to B, ...), and ZL/ZR are pressed by any
     * trigger travel, matching Streamlight's Switch emulation on Linux (inputtino).
     */
    SLPAD_STATE to_switch_state(int nr, const gamepad_state_t &gamepad_state) {
      const auto flags = gamepad_state.buttonFlags;
      SLPAD_STATE state {};

      state.index = static_cast<unsigned char>(nr);

      if (flags & Y) state.buttons[0] |= SP_BTN0_Y;
      if (flags & X) state.buttons[0] |= SP_BTN0_X;
      if (flags & B) state.buttons[0] |= SP_BTN0_B;
      if (flags & A) state.buttons[0] |= SP_BTN0_A;
      if (flags & RIGHT_BUTTON) state.buttons[0] |= SP_BTN0_R;
      if (gamepad_state.rt > 0) state.buttons[0] |= SP_BTN0_ZR;

      if (flags & BACK) state.buttons[1] |= SP_BTN1_MINUS;
      if (flags & START) state.buttons[1] |= SP_BTN1_PLUS;
      if (flags & RIGHT_STICK) state.buttons[1] |= SP_BTN1_RSTICK;
      if (flags & LEFT_STICK) state.buttons[1] |= SP_BTN1_LSTICK;
      if (flags & HOME) state.buttons[1] |= SP_BTN1_HOME;
      if (flags & MISC_BUTTON) state.buttons[1] |= SP_BTN1_CAPTURE;

      if (flags & DPAD_DOWN) state.buttons[2] |= SP_BTN2_DOWN;
      if (flags & DPAD_UP) state.buttons[2] |= SP_BTN2_UP;
      if (flags & DPAD_RIGHT) state.buttons[2] |= SP_BTN2_RIGHT;
      if (flags & DPAD_LEFT) state.buttons[2] |= SP_BTN2_LEFT;
      if (flags & LEFT_BUTTON) state.buttons[2] |= SP_BTN2_L;
      if (gamepad_state.lt > 0) state.buttons[2] |= SP_BTN2_ZL;

      state.lx = to_stick(gamepad_state.lsX);
      state.ly = to_stick(gamepad_state.lsY);
      state.rx = to_stick(gamepad_state.rsX);
      state.ry = to_stick(gamepad_state.rsY);
      return state;
    }

    /**
     * @brief Finds the driver's control interface.
     * @return The device path, or an empty string if the driver isn't installed.
     */
    std::wstring find_control_interface() {
      HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_STREAMLIGHT_VIRTUAL_PAD, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
      if (set == INVALID_HANDLE_VALUE) {
        return {};
      }

      std::wstring path;
      SP_DEVICE_INTERFACE_DATA data {};
      data.cbSize = sizeof(data);
      if (SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_STREAMLIGHT_VIRTUAL_PAD, 0, &data)) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &data, nullptr, 0, &size, nullptr);
        if (size > 0) {
          std::vector<std::uint8_t> buffer(size);
          auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
          detail->cbSize = sizeof(*detail);
          if (SetupDiGetDeviceInterfaceDetailW(set, &data, detail, size, nullptr, nullptr)) {
            path = detail->DevicePath;
          }
        }
      }

      SetupDiDestroyDeviceInfoList(set);
      return path;
    }
  }  // namespace

  virtual_switch_pad_t::~virtual_switch_pad_t() {
    if (rumble_thread.joinable()) {
      SetEvent(stop_event);
      rumble_thread.join();
    }

    // Closing the handle makes the driver remove every controller we created.
    if (device != INVALID_HANDLE_VALUE) {
      CloseHandle(device);
    }
    if (stop_event) {
      CloseHandle(stop_event);
    }
  }

  bool virtual_switch_pad_t::init() {
    auto path = find_control_interface();
    if (path.empty()) {
      BOOST_LOG(info) << "Streamlight virtual Switch Pro driver is not installed; Switch Pro emulation is unavailable"sv;
      return false;
    }

    device = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (device == INVALID_HANDLE_VALUE) {
      BOOST_LOG(warning) << "Couldn't open the virtual Switch Pro driver ["sv << util::hex(GetLastError()).to_string_view() << ']';
      return false;
    }

    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event) {
      return false;
    }

    rumble_thread = std::thread {&virtual_switch_pad_t::rumble_thread_main, this};
    BOOST_LOG(info) << "Streamlight virtual Switch Pro driver found"sv;
    return true;
  }

  int virtual_switch_pad_t::alloc(int nr, std::uint8_t client_relative_index, feedback_queue_t feedback_queue, bool has_motion) {
    if (device == INVALID_HANDLE_VALUE || nr < 0 || nr >= MAX_GAMEPADS) {
      return -1;
    }

    SLPAD_INDEX in {static_cast<unsigned char>(nr)};
    if (!control(IOCTL_SLPAD_CREATE, &in, sizeof(in))) {
      return -1;
    }

    // The client only sends motion when asked to.
    if (has_motion) {
      feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(client_relative_index, LI_MOTION_TYPE_ACCEL, MOTION_REPORT_RATE));
      feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(client_relative_index, LI_MOTION_TYPE_GYRO, MOTION_REPORT_RATE));
    }

    std::lock_guard lock {mutex};
    auto &pad = pads[nr];
    pad = {};
    pad.active = true;
    pad.client_relative_index = client_relative_index;
    pad.feedback_queue = std::move(feedback_queue);
    return 0;
  }

  void virtual_switch_pad_t::free(int nr) {
    if (!owns(nr)) {
      return;
    }

    SLPAD_INDEX in {static_cast<unsigned char>(nr)};
    control(IOCTL_SLPAD_DESTROY, &in, sizeof(in));

    std::lock_guard lock {mutex};
    pads[nr] = {};
  }

  bool virtual_switch_pad_t::owns(int nr) {
    if (nr < 0 || nr >= MAX_GAMEPADS) {
      return false;
    }

    std::lock_guard lock {mutex};
    return pads[nr].active;
  }

  void virtual_switch_pad_t::update(int nr, const gamepad_state_t &gamepad_state) {
    if (!owns(nr)) {
      return;
    }

    auto state = to_switch_state(nr, gamepad_state);
    control(IOCTL_SLPAD_SET_STATE, &state, sizeof(state));
  }

  void virtual_switch_pad_t::motion(int nr, std::uint8_t motion_type, float x, float y, float z) {
    if (nr < 0 || nr >= MAX_GAMEPADS) {
      return;
    }

    SLPAD_MOTION in {};
    {
      std::lock_guard lock {mutex};
      auto &pad = pads[nr];
      if (!pad.active) {
        return;
      }

      if (motion_type == LI_MOTION_TYPE_ACCEL) {
        pad.accel = to_switch_axes(x, y, z, SP_ACCEL_PER_G / EARTH_G);  // m/s^2 to raw
      } else if (motion_type == LI_MOTION_TYPE_GYRO) {
        pad.gyro = to_switch_axes(x, y, z, SP_GYRO_PER_DPS);  // deg/s to raw
      } else {
        return;
      }

      in.index = static_cast<unsigned char>(nr);
      std::copy(pad.accel.begin(), pad.accel.end(), in.accel);
      std::copy(pad.gyro.begin(), pad.gyro.end(), in.gyro);
    }

    control(IOCTL_SLPAD_SET_MOTION, &in, sizeof(in));
  }

  /**
   * @brief Sends a control request to the driver and waits for it to finish.
   * @return `true` on success.
   */
  bool virtual_switch_pad_t::control(DWORD code, const void *in, DWORD in_size) {
    OVERLAPPED overlapped {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
      return false;
    }

    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(device, code, const_cast<void *>(in), in_size, nullptr, 0, &bytes, &overlapped);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
      ok = GetOverlappedResult(device, &overlapped, &bytes, TRUE);
    }
    if (!ok) {
      BOOST_LOG(warning) << "Virtual Switch Pro request 0x"sv << util::hex(code).to_string_view() << " failed ["sv << util::hex(GetLastError()).to_string_view() << ']';
    }

    CloseHandle(overlapped.hEvent);
    return ok;
  }

  /**
   * @brief Waits for rumble from games and passes it back to the clients.
   */
  void virtual_switch_pad_t::rumble_thread_main() {
    HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!io_event) {
      return;
    }

    while (true) {
      SLPAD_RUMBLE rumble {};
      OVERLAPPED overlapped {};
      overlapped.hEvent = io_event;
      ResetEvent(io_event);

      DWORD bytes = 0;
      BOOL ok = DeviceIoControl(device, IOCTL_SLPAD_GET_RUMBLE, nullptr, 0, &rumble, sizeof(rumble), &bytes, &overlapped);
      if (!ok && GetLastError() == ERROR_IO_PENDING) {
        HANDLE handles[] {stop_event, io_event};
        if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) {
          CancelIoEx(device, &overlapped);
          GetOverlappedResult(device, &overlapped, &bytes, TRUE);  // wait for the cancellation
          break;
        }
        ok = GetOverlappedResult(device, &overlapped, &bytes, FALSE);
      }

      if (!ok || bytes < sizeof(rumble)) {
        // Don't spin if the driver keeps failing; retry after a pause unless we're stopping.
        if (WaitForSingleObject(stop_event, 1000) == WAIT_OBJECT_0) {
          break;
        }
        continue;
      }

      BOOST_LOG(debug) << "Switch Pro rumble from game for gamepad "sv << static_cast<int>(rumble.index)
                       << ": low "sv << rumble.low_frequency << ", high "sv << rumble.high_frequency
                       << (config::input.forward_rumble ? ""sv : " (not forwarded: forward_rumble is off)"sv);

      // config::input.forward_rumble - Default is true so ignore rumble messages when false
      if (!config::input.forward_rumble || rumble.index >= MAX_GAMEPADS) {
        continue;
      }

      std::lock_guard lock {mutex};
      auto &pad = pads[rumble.index];
      if (!pad.active) {
        continue;
      }

      auto msg = gamepad_feedback_msg_t::make_rumble(pad.client_relative_index, rumble.low_frequency, rumble.high_frequency);
      if (msg.data.rumble.lowfreq != pad.last_rumble.data.rumble.lowfreq ||
          msg.data.rumble.highfreq != pad.last_rumble.data.rumble.highfreq) {
        pad.feedback_queue->raise(msg);
        pad.last_rumble = msg;
      }
    }

    CloseHandle(io_event);
  }
}  // namespace platf
