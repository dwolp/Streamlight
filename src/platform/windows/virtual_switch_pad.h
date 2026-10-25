/**
 * @file src/platform/windows/virtual_switch_pad.h
 * @brief Declarations for virtual Nintendo Switch Pro Controllers on Windows.
 *
 * Talks to Streamlight's virtual Switch Pro driver (src_driver/windows/virtual_pad), which
 * creates controllers that SDL and Steam recognise as real Pro Controllers.
 */
#pragma once

// standard includes
#include <array>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>

// platform includes
#include <Windows.h>
#include <cfgmgr32.h>

// local includes
#include "src/platform/common.h"

namespace platf {
  class virtual_switch_pad_t {
  public:
    ~virtual_switch_pad_t();

    /**
     * @brief Opens the driver's control interface.
     * @return `true` if the driver is installed and usable.
     */
    bool init();

    /**
     * @brief Creates the virtual controller for a gamepad slot.
     * @param nr The global gamepad index.
     * @param client_relative_index The index the client uses for this gamepad (for rumble).
     * @param feedback_queue The queue for posting rumble back to the client.
     * @param has_motion Whether the client gamepad has motion sensors; if so, the client is asked to send motion.
     * @return 0 on success.
     */
    int alloc(int nr, std::uint8_t client_relative_index, feedback_queue_t feedback_queue, bool has_motion);

    /**
     * @brief Removes the virtual controller for a gamepad slot, if this class created it.
     * @param nr The global gamepad index.
     */
    void free(int nr);

    /**
     * @brief Checks whether a gamepad slot is a virtual Switch Pro Controller.
     * @param nr The global gamepad index.
     */
    bool owns(int nr);

    /**
     * @brief Sends the client's gamepad state to the virtual controller.
     * @param nr The global gamepad index.
     * @param gamepad_state The gamepad button/axis state sent from the client.
     */
    void update(int nr, const gamepad_state_t &gamepad_state);

    /**
     * @brief Sends a motion sample from the client to the virtual controller.
     * @param nr The global gamepad index.
     * @param motion_type `LI_MOTION_TYPE_ACCEL` (m/s^2) or `LI_MOTION_TYPE_GYRO` (deg/s).
     * @param x X axis, in SDL's sensor axes.
     * @param y Y axis, in SDL's sensor axes.
     * @param z Z axis, in SDL's sensor axes.
     */
    void motion(int nr, std::uint8_t motion_type, float x, float y, float z);

  private:
    struct pad_t {
      bool active = false;
      std::uint8_t client_relative_index = 0;
      feedback_queue_t feedback_queue;
      gamepad_feedback_msg_t last_rumble;

      // Latest raw samples in the controller's own axes; accel and gyro arrive separately.
      std::array<std::int16_t, 3> accel {};
      std::array<std::int16_t, 3> gyro {};
    };

    // What the device notifications ask the worker thread to do.
    enum class request_e {
      none,
      close,  ///< the driver is going away: close the handle so Windows can remove it
      open,  ///< the driver is (back) available: open it if it isn't open
    };

    bool control(DWORD code, const void *in, DWORD in_size);
    bool open_device();
    void close_device();
    void recreate_pads();
    void worker_main();
    void post_request(request_e request);

    static DWORD CALLBACK on_interface_event(HCMNOTIFICATION notification, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA data, DWORD size);
    static DWORD CALLBACK on_device_event(HCMNOTIFICATION notification, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA data, DWORD size);

    // The control handle. Requests hold device_mutex shared while they use it; only the worker
    // thread opens and closes it, holding the mutex exclusively.
    std::shared_mutex device_mutex;
    HANDLE device = INVALID_HANDLE_VALUE;
    HCMNOTIFICATION device_notification = nullptr;  // removal of the open driver device
    HCMNOTIFICATION interface_notification = nullptr;  // the driver's interface (re)appearing

    std::atomic<request_e> pending_request {request_e::none};
    HANDLE stop_event = nullptr;
    HANDLE wake_event = nullptr;  // a request is pending
    HANDLE closed_event = nullptr;  // the handle has been closed after a removal request
    std::thread worker;

    std::mutex mutex;  // guards pads
    std::array<pad_t, MAX_GAMEPADS> pads;
  };
}  // namespace platf
