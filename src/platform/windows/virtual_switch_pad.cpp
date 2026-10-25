/**
 * @file src/platform/windows/virtual_switch_pad.cpp
 * @brief Definitions for virtual Nintendo Switch Pro Controllers on Windows.
 */
#include "virtual_switch_pad.h"

// standard includes
#include <algorithm>
#include <cmath>
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

    // ZL and ZR are digital on the Pro Controller. Analog triggers press them past XInput's own
    // trigger threshold (XINPUT_GAMEPAD_TRIGGER_THRESHOLD), so a trigger resting slightly above
    // zero doesn't hold the button down.
    constexpr std::uint8_t TRIGGER_THRESHOLD = 30;

    // How long a removal of the driver may wait for us to close our handle. An open handle
    // would make Windows refuse the removal (and ask for a restart when updating the driver).
    constexpr DWORD CLOSE_TIMEOUT_MS = 5000;

    // While the driver is unavailable, how often to try opening it again, in addition to
    // reacting to its interface reappearing.
    constexpr DWORD REOPEN_INTERVAL_MS = 5000;

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
     * @details Face buttons map by label (A to A, B to B, ...), matching Streamlight's Switch
     * emulation on Linux (inputtino).
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
      if (gamepad_state.rt > TRIGGER_THRESHOLD) state.buttons[0] |= SP_BTN0_ZR;

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
      if (gamepad_state.lt > TRIGGER_THRESHOLD) state.buttons[2] |= SP_BTN2_ZL;

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
    if (worker.joinable()) {
      SetEvent(stop_event);
      worker.join();
    }
    if (interface_notification) {
      CM_Unregister_Notification(interface_notification);
    }

    // Closing the handle makes the driver remove every controller we created.
    close_device();

    for (auto event : {stop_event, wake_event, closed_event}) {
      if (event) {
        CloseHandle(event);
      }
    }
  }

  bool virtual_switch_pad_t::init() {
    if (find_control_interface().empty()) {
      BOOST_LOG(info) << "Streamlight virtual Switch Pro driver is not installed; Switch Pro emulation is unavailable"sv;
      return false;
    }

    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wake_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    closed_event = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (!stop_event || !wake_event || !closed_event || !open_device()) {
      return false;
    }

    // Hear about the driver coming back after an update or restart.
    CM_NOTIFY_FILTER filter {};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_STREAMLIGHT_VIRTUAL_PAD;
    if (CM_Register_Notification(&filter, this, on_interface_event, &interface_notification) != CR_SUCCESS) {
      interface_notification = nullptr;
      BOOST_LOG(warning) << "Couldn't watch for the virtual Switch Pro driver being reinstalled; restart Streamlight after updating it"sv;
    }

    worker = std::thread {&virtual_switch_pad_t::worker_main, this};
    BOOST_LOG(info) << "Streamlight virtual Switch Pro driver found"sv;
    return true;
  }

  /**
   * @brief Opens the driver and watches for its removal.
   * @return `true` if the driver is open.
   */
  bool virtual_switch_pad_t::open_device() {
    auto path = find_control_interface();
    if (path.empty()) {
      return false;
    }

    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      BOOST_LOG(warning) << "Couldn't open the virtual Switch Pro driver ["sv << util::hex(GetLastError()).to_string_view() << ']';
      return false;
    }

    // Without this notification the open handle would block removing or updating the driver.
    HCMNOTIFICATION notification = nullptr;
    CM_NOTIFY_FILTER filter {};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
    filter.u.DeviceHandle.hTarget = handle;
    if (CM_Register_Notification(&filter, this, on_device_event, &notification) != CR_SUCCESS) {
      notification = nullptr;
      BOOST_LOG(warning) << "Couldn't watch the virtual Switch Pro driver for removal; updating it will need a restart"sv;
    }

    std::unique_lock lock {device_mutex};
    device = handle;
    device_notification = notification;
    return true;
  }

  /**
   * @brief Closes the driver handle. The driver removes the controllers created through it.
   * @details Called from the worker thread or the destructor, never from a notification
   * callback: unregistering a notification waits for its callbacks to finish.
   */
  void virtual_switch_pad_t::close_device() {
    HCMNOTIFICATION notification;
    bool was_open;
    {
      std::unique_lock lock {device_mutex};
      was_open = device != INVALID_HANDLE_VALUE;
      if (was_open) {
        CloseHandle(device);
        device = INVALID_HANDLE_VALUE;
      }
      notification = device_notification;
      device_notification = nullptr;
    }

    // Let a waiting removal request go ahead before unregistering its notification.
    if (closed_event) {
      SetEvent(closed_event);
    }
    if (notification) {
      CM_Unregister_Notification(notification);
    }

    // The controllers are gone; don't leave a client controller rumbling.
    if (was_open) {
      std::lock_guard lock {mutex};
      for (auto &pad : pads) {
        if (pad.active && (pad.last_rumble.data.rumble.lowfreq || pad.last_rumble.data.rumble.highfreq)) {
          pad.last_rumble = gamepad_feedback_msg_t::make_rumble(pad.client_relative_index, 0, 0);
          pad.feedback_queue->raise(pad.last_rumble);
        }
      }
    }
  }

  /**
   * @brief Creates the controllers again for client gamepads that are still connected, after
   * the driver has been reopened.
   */
  void virtual_switch_pad_t::recreate_pads() {
    std::array<bool, MAX_GAMEPADS> active {};
    {
      std::lock_guard lock {mutex};
      for (int nr = 0; nr < MAX_GAMEPADS; nr++) {
        active[nr] = pads[nr].active;
      }
    }

    for (int nr = 0; nr < MAX_GAMEPADS; nr++) {
      if (!active[nr]) {
        continue;
      }

      SLPAD_INDEX in {static_cast<unsigned char>(nr)};
      if (control(IOCTL_SLPAD_CREATE, &in, sizeof(in))) {
        BOOST_LOG(info) << "Recreated virtual Switch Pro controller for gamepad "sv << nr;
        std::lock_guard lock {mutex};
        pads[nr].last_rumble = {};
      } else {
        BOOST_LOG(warning) << "Couldn't recreate the virtual Switch Pro controller for gamepad "sv << nr;
      }
    }
  }

  void virtual_switch_pad_t::post_request(request_e request) {
    pending_request = request;
    SetEvent(wake_event);
  }

  DWORD CALLBACK virtual_switch_pad_t::on_interface_event(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA, DWORD) {
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL) {
      static_cast<virtual_switch_pad_t *>(context)->post_request(request_e::open);
    }
    return ERROR_SUCCESS;
  }

  DWORD CALLBACK virtual_switch_pad_t::on_device_event(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA, DWORD) {
    auto self = static_cast<virtual_switch_pad_t *>(context);

    switch (action) {
      case CM_NOTIFY_ACTION_DEVICEQUERYREMOVE:
        // Windows asks whether the driver may be removed (an update, or disabling it). Close our
        // handle before answering, or the open handle would refuse the removal.
        ResetEvent(self->closed_event);
        self->post_request(request_e::close);
        WaitForSingleObject(self->closed_event, CLOSE_TIMEOUT_MS);
        break;
      case CM_NOTIFY_ACTION_DEVICEREMOVEPENDING:
      case CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE:
        self->post_request(request_e::close);
        break;
      default:
        break;
    }
    return ERROR_SUCCESS;
  }

  int virtual_switch_pad_t::alloc(int nr, std::uint8_t client_relative_index, feedback_queue_t feedback_queue, bool has_motion) {
    if (nr < 0 || nr >= MAX_GAMEPADS) {
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
   * @return `true` on success; `false` if it failed or the driver isn't open.
   */
  bool virtual_switch_pad_t::control(DWORD code, const void *in, DWORD in_size) {
    std::shared_lock lock {device_mutex};
    if (device == INVALID_HANDLE_VALUE) {
      return false;
    }

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
      // State and motion updates are frequent; don't flood the log if the driver is going away.
      const auto error = GetLastError();
      if (code == IOCTL_SLPAD_SET_STATE || code == IOCTL_SLPAD_SET_MOTION) {
        BOOST_LOG(debug) << "Virtual Switch Pro request 0x"sv << util::hex(code).to_string_view() << " failed ["sv << util::hex(error).to_string_view() << ']';
      } else {
        BOOST_LOG(warning) << "Virtual Switch Pro request 0x"sv << util::hex(code).to_string_view() << " failed ["sv << util::hex(error).to_string_view() << ']';
      }
    }

    CloseHandle(overlapped.hEvent);
    return ok;
  }

  /**
   * @brief Waits for rumble from games and passes it back to the clients, and opens and closes
   * the driver as it comes and goes.
   */
  void virtual_switch_pad_t::worker_main() {
    HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!io_event) {
      return;
    }

    while (true) {
      HANDLE handle;
      {
        std::shared_lock lock {device_mutex};
        handle = device;
      }

      if (handle == INVALID_HANDLE_VALUE) {
        // The driver is unavailable: wait for its interface to reappear, and retry now and then.
        HANDLE handles[] {stop_event, wake_event};
        if (WaitForMultipleObjects(2, handles, FALSE, REOPEN_INTERVAL_MS) == WAIT_OBJECT_0) {
          break;
        }
        pending_request = request_e::none;
        SetEvent(closed_event);  // already closed: don't keep a removal request waiting

        if (open_device()) {
          BOOST_LOG(info) << "Virtual Switch Pro driver is available again"sv;
          recreate_pads();
        }
        continue;
      }

      // Only this thread closes the handle, so it can use it without holding the lock.
      SLPAD_RUMBLE rumble {};
      OVERLAPPED overlapped {};
      overlapped.hEvent = io_event;
      ResetEvent(io_event);

      DWORD bytes = 0;
      BOOL ok = DeviceIoControl(handle, IOCTL_SLPAD_GET_RUMBLE, nullptr, 0, &rumble, sizeof(rumble), &bytes, &overlapped);
      if (!ok && GetLastError() == ERROR_IO_PENDING) {
        HANDLE handles[] {stop_event, wake_event, io_event};
        auto wait = WaitForMultipleObjects(3, handles, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0 + 2) {
          CancelIoEx(handle, &overlapped);
          GetOverlappedResult(handle, &overlapped, &bytes, TRUE);  // wait for the cancellation
          if (wait == WAIT_OBJECT_0) {
            break;
          }

          // A device notification.
          if (pending_request.exchange(request_e::none) == request_e::close) {
            BOOST_LOG(info) << "Virtual Switch Pro driver is being removed or updated; closing it"sv;
            close_device();
          }
          continue;
        }
        ok = GetOverlappedResult(handle, &overlapped, &bytes, FALSE);
      }

      if (!ok || bytes < sizeof(rumble)) {
        // The driver went away without notice, or failed: close it and wait for it to return.
        BOOST_LOG(warning) << "Lost the virtual Switch Pro driver ["sv << util::hex(GetLastError()).to_string_view() << "]; waiting for it to return"sv;
        close_device();
        continue;
      }

      BOOST_LOG(debug) << "Switch Pro rumble from game for gamepad "sv << static_cast<int>(rumble.index)
                       << ": low "sv << rumble.low_frequency << ", high "sv << rumble.high_frequency
                       << (rumble.timed_out ? " (stopped by the driver: the game stopped refreshing it)"sv : ""sv)
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
