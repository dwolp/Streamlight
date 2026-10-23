<script setup>
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import Checkbox from "../../Checkbox.vue";

const props = defineProps([
  'platform',
  'config'
])

const config = ref(props.config)

// Live input status from the host: available gamepad drivers and connected controllers.
const STATUS_INTERVAL_MS = 3000
const status = ref(null)
const statusError = ref(false)
let statusTimer = null

async function loadStatus() {
  try {
    const response = await fetch('./api/input/status', { credentials: 'include' })
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`)
    }
    status.value = await response.json()
    statusError.value = false
  } catch (e) {
    statusError.value = true
  }
}

onMounted(() => {
  loadStatus()
  statusTimer = setInterval(loadStatus, STATUS_INTERVAL_MS)
})

onBeforeUnmount(() => clearInterval(statusTimer))

const gamepadsEnabled = computed(() => config.value.controller === 'enabled')
const keyboardEnabled = computed(() => config.value.keyboard === 'enabled')
const mouseEnabled = computed(() => config.value.mouse === 'enabled')
const isWindows = computed(() => props.platform === 'windows')
const isLinux = computed(() => props.platform === 'linux')
const gamepadType = computed(() => config.value.gamepad)

const TYPE_LABELS = {
  x360: 'config.gamepad_x360',
  ds4: 'config.gamepad_ds4',
  xone: 'config.gamepad_xone',
  ds5: 'config.gamepad_ds5',
  switch: 'config.gamepad_switch',
}

// Gamepad types this platform can emulate, in the order they are offered.
const gamepadOptions = computed(() => {
  if (isWindows.value) return ['x360', 'ds4', 'switch']
  if (isLinux.value) return ['xone', 'ds5', 'switch']
  return []
})

// Unknown until the status has loaded, so assume available rather than flag everything.
function backendAvailable(name) {
  const backend = status.value?.backends?.find(b => b.name === name)
  return backend ? backend.available : true
}

const drivers = computed(() => {
  if (!status.value) return []
  if (isWindows.value) {
    return [
      { label: 'config.input_driver_vigem', installed: backendAvailable('x360') },
      { label: 'config.input_driver_switch', installed: backendAvailable('switch') },
    ]
  }
  return (status.value.backends || [])
    .filter(b => b.name !== 'auto' && TYPE_LABELS[b.name])
    .map(b => ({ label: TYPE_LABELS[b.name], installed: b.available }))
})

const controllers = computed(() => status.value?.gamepads || [])

// Options that only apply to some gamepad types.
const showAutoOptions = computed(() => gamepadType.value === 'auto' && (isWindows.value || isLinux.value))
const showDs4Options = computed(() => gamepadType.value === 'ds4' || (gamepadType.value === 'auto' && isWindows.value))
const showDs5Options = computed(() => gamepadType.value === 'ds5' || (gamepadType.value === 'auto' && isLinux.value))
const showSwitchNote = computed(() => gamepadType.value === 'switch' || (gamepadType.value === 'auto' && backendAvailable('switch')))
</script>

<template>
  <div id="input" class="config-page">
    <!-- Status -->
    <section class="input-section">
      <div class="input-section-header">
        <h3><i class="fas fa-fw fa-signal"></i> {{ $t('config.input_status') }}</h3>
        <span class="small text-body-secondary" v-if="status && !statusError">
          <span class="live-dot"></span> {{ $t('config.input_status_live') }}
        </span>
      </div>

      <div class="alert alert-warning py-2 small" v-if="statusError">{{ $t('config.input_status_error') }}</div>

      <div class="row g-3" v-if="platform !== 'macos'">
        <div class="col-md-5">
          <div class="status-panel h-100">
            <div class="status-panel-title">{{ $t('config.input_drivers') }}</div>
            <div v-if="!status" class="placeholder-glow"><span class="placeholder col-8"></span></div>
            <ul class="list-unstyled mb-0">
              <li v-for="driver in drivers" :key="driver.label" class="status-row">
                <i class="fas fa-fw" :class="driver.installed ? 'fa-circle-check text-success' : 'fa-circle-xmark text-danger'"></i>
                <span class="flex-grow-1">{{ $t(driver.label) }}</span>
                <span class="small" :class="driver.installed ? 'text-success' : 'text-danger'">
                  {{ $t(driver.installed ? 'config.input_installed' : 'config.input_not_installed') }}
                </span>
              </li>
            </ul>
          </div>
        </div>

        <div class="col-md-7">
          <div class="status-panel h-100">
            <div class="status-panel-title">{{ $t('config.input_controllers') }}</div>
            <div v-if="!status" class="placeholder-glow"><span class="placeholder col-10"></span></div>
            <p v-else-if="controllers.length === 0" class="small text-body-secondary mb-0">
              {{ $t('config.input_controllers_none') }}
            </p>
            <ul class="list-unstyled mb-0">
              <li v-for="pad in controllers" :key="pad.index" class="controller-row">
                <div class="d-flex align-items-center gap-2 flex-wrap">
                  <i class="fas fa-fw fa-gamepad text-primary"></i>
                  <strong>{{ $t('config.input_controller_number', { number: pad.client_index + 1 }) }}</strong>
                  <span class="text-body-secondary">{{ $t(`config.input_client_${pad.client_type}`) }}</span>
                  <i class="fas fa-arrow-right small text-body-secondary"></i>
                  <span class="badge emulated-badge" v-if="pad.emulated">
                    {{ $t('config.input_emulated_as') }} {{ TYPE_LABELS[pad.emulated] ? $t(TYPE_LABELS[pad.emulated]) : pad.emulated }}
                  </span>
                  <span class="badge text-bg-secondary" v-else>{{ $t('config.input_not_emulated') }}</span>
                </div>
                <div class="d-flex gap-1 flex-wrap mt-1 ms-4">
                  <span class="cap" :class="{ off: !pad.rumble }">
                    <i class="fas fa-fw" :class="pad.rumble ? 'fa-check' : 'fa-xmark'"></i> {{ $t('config.input_cap_rumble') }}
                  </span>
                  <span class="cap" :class="{ off: !pad.motion }">
                    <i class="fas fa-fw" :class="pad.motion ? 'fa-check' : 'fa-xmark'"></i> {{ $t('config.input_cap_motion') }}
                  </span>
                  <span class="cap" :class="{ off: !pad.touchpad }">
                    <i class="fas fa-fw" :class="pad.touchpad ? 'fa-check' : 'fa-xmark'"></i> {{ $t('config.input_cap_touchpad') }}
                  </span>
                  <span class="cap" :class="{ off: !pad.analog_triggers }">
                    <i class="fas fa-fw" :class="pad.analog_triggers ? 'fa-check' : 'fa-xmark'"></i> {{ $t('config.input_cap_analog_triggers') }}
                  </span>
                </div>
              </li>
            </ul>
          </div>
        </div>
      </div>
      <p v-else class="small text-body-secondary mb-0">{{ $t('config.input_macos_no_gamepads') }}</p>
    </section>

    <!-- Gamepads -->
    <section class="input-section">
      <div class="input-section-header">
        <h3><i class="fas fa-fw fa-gamepad"></i> {{ $t('config.input_section_gamepads') }}</h3>
      </div>

      <Checkbox class="mb-3"
                id="controller"
                locale-prefix="config"
                v-model="config.controller"
                default="true"
      ></Checkbox>

      <div class="section-options" v-if="gamepadsEnabled">
        <!-- Emulated Gamepad Type -->
        <div class="mb-3" v-if="platform !== 'macos'">
          <label for="gamepad" class="form-label">{{ $t('config.gamepad') }}</label>
          <select id="gamepad" class="form-select" v-model="config.gamepad">
            <option value="auto">{{ $t('_common.auto') }}</option>
            <option v-for="type in gamepadOptions" :key="type" :value="type" :disabled="!backendAvailable(type)">
              {{ $t(TYPE_LABELS[type]) }}{{ backendAvailable(type) ? '' : ` (${$t('config.gamepad_not_installed')})` }}
            </option>
          </select>
          <div class="form-text">
            {{ gamepadType === 'auto' ? $t('config.gamepad_auto_explain') : $t('config.gamepad_desc') }}
          </div>
        </div>

        <div class="alert alert-info py-2 small" v-if="isWindows && showSwitchNote">
          <i class="fas fa-circle-info"></i> {{ $t('config.gamepad_switch_note') }}
        </div>

        <!-- Automatic selection options (Windows and Linux) -->
        <template v-if="showAutoOptions">
          <Checkbox class="mb-3"
                    id="motion_as_ds4"
                    locale-prefix="config"
                    v-model="config.motion_as_ds4"
                    default="true"
          ></Checkbox>
          <Checkbox class="mb-3"
                    id="touchpad_as_ds4"
                    locale-prefix="config"
                    v-model="config.touchpad_as_ds4"
                    default="true"
          ></Checkbox>
        </template>

        <!-- DS4: back button as touchpad click (Windows) -->
        <Checkbox v-if="showDs4Options"
                  class="mb-3"
                  id="ds4_back_as_touchpad_click"
                  locale-prefix="config"
                  v-model="config.ds4_back_as_touchpad_click"
                  default="true"
        ></Checkbox>

        <!-- DS5: controller MAC randomization (Linux) -->
        <Checkbox v-if="showDs5Options"
                  class="mb-3"
                  id="ds5_inputtino_randomize_mac"
                  locale-prefix="config"
                  v-model="config.ds5_inputtino_randomize_mac"
                  default="true"
        ></Checkbox>

        <!-- Forward rumble to clients (Windows) -->
        <Checkbox v-if="isWindows"
                  class="mb-3"
                  id="forward_rumble"
                  locale-prefix="config"
                  v-model="config.forward_rumble"
                  default="true"
        ></Checkbox>

        <!-- Home/Guide Button Emulation Timeout -->
        <div class="mb-3">
          <label for="back_button_timeout" class="form-label">{{ $t('config.back_button_timeout') }}</label>
          <input type="text" class="form-control" id="back_button_timeout" placeholder="-1"
                 v-model="config.back_button_timeout" />
          <div class="form-text">{{ $t('config.back_button_timeout_desc') }}</div>
        </div>
      </div>
    </section>

    <!-- Keyboard -->
    <section class="input-section">
      <div class="input-section-header">
        <h3><i class="fas fa-fw fa-keyboard"></i> {{ $t('config.input_section_keyboard') }}</h3>
      </div>

      <Checkbox class="mb-3"
                id="keyboard"
                locale-prefix="config"
                v-model="config.keyboard"
                default="true"
      ></Checkbox>

      <div class="section-options" v-if="keyboardEnabled">
        <template v-if="isWindows">
          <div class="row g-3 mb-3">
            <div class="col-md-6">
              <label for="key_repeat_delay" class="form-label">{{ $t('config.key_repeat_delay') }}</label>
              <input type="text" class="form-control" id="key_repeat_delay" placeholder="500"
                     v-model="config.key_repeat_delay" />
              <div class="form-text">{{ $t('config.key_repeat_delay_desc') }}</div>
            </div>
            <div class="col-md-6">
              <label for="key_repeat_frequency" class="form-label">{{ $t('config.key_repeat_frequency') }}</label>
              <input type="text" class="form-control" id="key_repeat_frequency" placeholder="24.9"
                     v-model="config.key_repeat_frequency" />
              <div class="form-text">{{ $t('config.key_repeat_frequency_desc') }}</div>
            </div>
          </div>

          <Checkbox class="mb-3"
                    id="always_send_scancodes"
                    locale-prefix="config"
                    v-model="config.always_send_scancodes"
                    default="true"
          ></Checkbox>
        </template>

        <Checkbox class="mb-3"
                  id="key_rightalt_to_key_win"
                  locale-prefix="config"
                  v-model="config.key_rightalt_to_key_win"
                  default="false"
        ></Checkbox>
      </div>
    </section>

    <!-- Mouse and touch -->
    <section class="input-section">
      <div class="input-section-header">
        <h3><i class="fas fa-fw fa-computer-mouse"></i> {{ $t('config.input_section_mouse') }}</h3>
      </div>

      <Checkbox class="mb-3"
                id="mouse"
                locale-prefix="config"
                v-model="config.mouse"
                default="true"
      ></Checkbox>

      <div class="section-options" v-if="mouseEnabled">
        <Checkbox class="mb-3"
                  id="high_resolution_scrolling"
                  locale-prefix="config"
                  v-model="config.high_resolution_scrolling"
                  default="true"
        ></Checkbox>

        <Checkbox class="mb-3"
                  id="native_pen_touch"
                  locale-prefix="config"
                  v-model="config.native_pen_touch"
                  default="true"
        ></Checkbox>
      </div>
    </section>

    <!-- Other -->
    <section class="input-section">
      <div class="input-section-header">
        <h3><i class="fas fa-fw fa-sliders"></i> {{ $t('config.input_section_other') }}</h3>
      </div>

      <Checkbox class="mb-0"
                id="enable_input_only_mode"
                locale-prefix="config"
                v-model="config.enable_input_only_mode"
                default="false"
      ></Checkbox>
    </section>
  </div>
</template>

<style scoped>
.input-section + .input-section {
  margin-top: 1.5rem;
  padding-top: 1.25rem;
  border-top: 1px solid var(--sl-border);
}

.input-section-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.75rem;
}

.input-section-header h3 {
  font-size: 1rem;
  font-weight: 650;
  margin: 0;
}

.input-section-header h3 i {
  color: var(--sl-accent);
}

/* Options that belong to the section's main toggle */
.section-options {
  padding-left: 1rem;
  border-left: 2px solid var(--sl-accent-soft);
}

.status-panel {
  padding: 0.85rem 1rem;
  border-radius: var(--sl-radius-sm);
  background: var(--sl-surface-muted);
  border: 1px solid var(--sl-border);
}

.status-panel-title {
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--bs-secondary-color);
  margin-bottom: 0.5rem;
}

.status-row {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.3rem 0;
}

.controller-row {
  padding: 0.45rem 0;
}

.controller-row + .controller-row {
  border-top: 1px solid var(--sl-border);
}

.emulated-badge {
  background: var(--sl-accent-soft);
  color: var(--sl-accent-strong);
  font-weight: 600;
}

.cap {
  font-size: 0.75rem;
  padding: 0.1rem 0.45rem;
  border-radius: 999px;
  background: rgba(16, 185, 129, 0.12);
  color: #0f9f6e;
}

.cap.off {
  background: transparent;
  color: var(--bs-secondary-color);
  border: 1px solid var(--sl-border);
}

.live-dot {
  display: inline-block;
  width: 0.5rem;
  height: 0.5rem;
  border-radius: 50%;
  background: #10b981;
  margin-right: 0.25rem;
  animation: pulse 2s ease-in-out infinite;
}

@keyframes pulse {
  50% { opacity: 0.35; }
}
</style>
