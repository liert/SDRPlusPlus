import { ref, reactive, computed, watch } from 'vue'
import { invoke } from '@tauri-apps/api/core'
import type {
  SourceConfig,
  DemodConfig,
  VfoState,
  DecodedPacket,
  SpectrumSettings,
  BackendDeviceInfo
} from '@/types/sdr'
import { liveHackRfFft, hasLiveHackRfData } from './useHackRf'

export { hasLiveHackRfData }

export const isPlaying = ref(false)
export const fps = ref(60)
export const totalPacketsCount = ref(0)
export const validCrcCount = ref(0)
export const currentLang = ref<'zh' | 'en'>('zh')

export const isTauriEnv = true

// C++ Backend Connection State & Device Management
export const isBackendConnected = ref(false)
export const backendStatusText = ref('⚡ Windows 原生共享内存直通 (IPC Zero-Copy)')
export const availableSources = ref<string[]>(['HackRF', 'File Source', 'RTL-SDR', 'Simulator'])
export const availableDevices = ref<BackendDeviceInfo[]>([])
export const isScanningDevices = ref(false)

let shmPollTimer: number | null = null

// Source Configuration
export const sourceConfig = reactive<SourceConfig>({
  type: 'hackrf',
  centerFreqHz: 2400000000,
  sampleRateHz: 8000000,
  lnaGain: 32,
  vgaGain: 20,
  ampEnable: false,
  biasT: false,
  deviceSerial: '',
  filePath: 'fresh_pairing_2400_8.iq',
  fileFormat: 'raw_int8',
  loop: true
})

// Demodulation & Physical Layer Config
export const demodConfig = reactive<DemodConfig>({
  modulation: 'FLRC',
  preset: 'SX1280 FLRC (1.3 Mbps)',
  symbolRate: 1300000,
  deviation: 325000,
  filterCutoff: 750000,
  enableAgcPreamble: true,
  agcThreshold: 3.5,
  enableTimingPreamble: true,
  timingTolerance: 3,
  autoSyncWord: true,
  syncWord: '54313253',
  maskMode: 'auto',
  customMask: 0x99,
  differentialDecode: true,
  enableHwCrc: true,
  defaultPayloadLen: 32
})

// VFO Tuning & Bandwidth
export const vfo = reactive<VfoState>({
  id: 'vfo-1',
  name: 'FLRC RX',
  offsetHz: 1400000, // +1.40 MHz default (exact location of H12 signal bursts)
  bandwidthHz: 1800000,
  color: '#3b82f6',
  enabled: true
})

// Spectrum & Waterfall Visual Settings
export const spectrumSettings = reactive<SpectrumSettings>({
  minDb: -100,
  maxDb: -20,
  fftSize: 1024,
  smoothing: 0.7,
  colormap: 'turbo',
  fillSpectrum: true,
  peakHold: true
})

// Decoded Packets History
export const packetHistory = ref<DecodedPacket[]>([])

export const crcSuccessRate = computed(() => {
  if (totalPacketsCount.value === 0) return 0
  return ((validCrcCount.value / totalPacketsCount.value) * 100).toFixed(1)
})

/**
 * Initialize High-Performance Windows Shared Memory (Zero-Copy IPC)
 */
export function initShmEngine() {
  let isPolling = false

  // 1. High-speed zero-latency Shared Memory FFT polling loop (60 FPS)
  const pollShmFft = async () => {
    if (isPolling) return
    isPolling = true
    try {
      const res = await invoke<ArrayBuffer>('get_shm_fft')
      if (res && res.byteLength === 4096) {
        const floats = new Float32Array(res)
        liveHackRfFft.set(floats)
        if (!hasLiveHackRfData.value) {
          hasLiveHackRfData.value = true
        }
        if (!isBackendConnected.value) {
          isBackendConnected.value = true
          backendStatusText.value = '⚡ Windows 原生共享内存直通 (IPC Zero-Copy)'
        }
      }
    } catch (e) {
      // Backend mapping not yet ready
    } finally {
      isPolling = false
    }
    requestAnimationFrame(pollShmFft)
  }

  requestAnimationFrame(pollShmFft)

  // 2. Poll Metadata and Decoded Packet Ring Buffer every 100ms
  if (!shmPollTimer) {
    shmPollTimer = window.setInterval(async () => {
      try {
        const st = await invoke<any>('get_shm_status')
        if (st && st.connected) {
          isBackendConnected.value = true
          isPlaying.value = st.running
          backendStatusText.value = '⚡ Windows 原生共享内存直通 (IPC Zero-Copy)'

          if (Array.isArray(st.devices) && st.devices.length > 0) {
            availableDevices.value = st.devices
            if (!sourceConfig.deviceSerial) {
              sourceConfig.deviceSerial = st.devices[0].serial
            }
          } else if (st.deviceSerial && availableDevices.value.length === 0) {
            availableDevices.value = [{
              serial: st.deviceSerial,
              name: `HackRF One (${st.deviceSerial.slice(-8)})`,
              index: 0
            }]
            if (!sourceConfig.deviceSerial) {
              sourceConfig.deviceSerial = st.deviceSerial
            }
          }

          if (Array.isArray(st.packets) && st.packets.length > 0) {
            for (const pkt of st.packets) {
              packetHistory.value.unshift(pkt as DecodedPacket)
              if (packetHistory.value.length > 200) {
                packetHistory.value.pop()
              }
              totalPacketsCount.value++
              if (pkt.crcValid) {
                validCrcCount.value++
              }
            }
          }
        }
      } catch (e) {}
    }, 100)
  }

  return true
}

export function sendBackendCommand(cmd: string, params: Record<string, any> = {}) {
  try {
    invoke('send_shm_cmd', { cmd, params })
  } catch (e) {
    console.warn('IPC command send failed:', e)
  }
}

export function refreshBackendDevices() {
  isScanningDevices.value = true
  sendBackendCommand('get_devices')
  setTimeout(() => { isScanningDevices.value = false }, 1000)
}

// Auto-start Shared Memory IPC on module load
if (typeof window !== 'undefined') {
  initShmEngine()
}

// Debounced Parameter Sync to prevent USB congestion
let freqDebounceTimer: number | null = null
watch(() => sourceConfig.centerFreqHz, (hz) => {
  if (freqDebounceTimer) clearTimeout(freqDebounceTimer)
  freqDebounceTimer = window.setTimeout(() => {
    sendBackendCommand('set_freq', { freq: hz })
  }, 40)
})

watch(() => sourceConfig.sampleRateHz, (hz) => {
  sendBackendCommand('set_samplerate', { sampleRate: hz })
})

let gainDebounceTimer: number | null = null
function pushGainsToBackend() {
  if (gainDebounceTimer) clearTimeout(gainDebounceTimer)
  gainDebounceTimer = window.setTimeout(() => {
    sendBackendCommand('set_gain', {
      lna: sourceConfig.lnaGain,
      vga: sourceConfig.vgaGain,
      amp: sourceConfig.ampEnable,
      biasT: sourceConfig.biasT
    })
  }, 30)
}

watch(() => sourceConfig.lnaGain, pushGainsToBackend)
watch(() => sourceConfig.vgaGain, pushGainsToBackend)
watch(() => sourceConfig.ampEnable, pushGainsToBackend)
watch(() => sourceConfig.biasT, pushGainsToBackend)

watch(() => sourceConfig.deviceSerial, (serial) => {
  if (serial) {
    sendBackendCommand('set_device', { serial })
  }
})

// Switch source type handler
watch(() => sourceConfig.type, (newType) => {
  const sname = newType === 'hackrf' ? 'HackRF' : (newType === 'file' ? 'File Source' : 'RTL-SDR')
  sendBackendCommand('set_source', { source: sname })
  if (isPlaying.value) {
    sendBackendCommand('stop')
    sendBackendCommand('start')
  }
})

/**
 * Calculate VFO signal lock and SNR
 */
export function checkVfoSignalLock() {
  const vfoMin = vfo.offsetHz - vfo.bandwidthHz / 2
  const vfoMax = vfo.offsetHz + vfo.bandwidthHz / 2

  if (hasLiveHackRfData.value) {
    // Analyze live FFT power in the VFO frequency passband
    const fftLen = liveHackRfFft.length
    const binStart = Math.max(0, Math.min(fftLen - 1, Math.floor((0.5 + vfoMin / sourceConfig.sampleRateHz) * fftLen)))
    const binEnd = Math.max(0, Math.min(fftLen - 1, Math.floor((0.5 + vfoMax / sourceConfig.sampleRateHz) * fftLen)))

    let maxPwr = -120
    let maxIdx = binStart
    for (let b = binStart; b <= binEnd; b++) {
      if (liveHackRfFft[b] > maxPwr) {
        maxPwr = liveHackRfFft[b]
        maxIdx = b
      }
    }

    const noiseFloor = -105 + (sourceConfig.lnaGain / 40.0) * 16.0 + (sourceConfig.vgaGain / 62.0) * 10.0
    const snr = maxPwr - noiseFloor

    if (snr > 7.0) {
      const peakRelHz = ((maxIdx / fftLen) - 0.5) * sourceConfig.sampleRateHz
      const devKhz = (peakRelHz - vfo.offsetHz) / 1000
      return { locked: true, snr: Math.min(32.0, Math.max(6.0, snr)), centerOffsetKhz: devKhz, isChannel1: vfo.offsetHz >= 0 }
    }
    return { locked: false, snr: 0, centerOffsetKhz: 0, isChannel1: false }
  } else {
    // Standby mode
    return { locked: false, snr: 0, centerOffsetKhz: 0, isChannel1: false }
  }
}

export const isVfoLockedOnSignal = computed(() => {
  return checkVfoSignalLock().locked
})

export function togglePlay() {
  isPlaying.value = !isPlaying.value
  sendBackendCommand(isPlaying.value ? 'start' : 'stop')
}

export function clearPackets() {
  packetHistory.value = []
  totalPacketsCount.value = 0
  validCrcCount.value = 0
}

export function applyPreset(presetName: string) {
  demodConfig.preset = presetName
  if (presetName === 'SX1280 FLRC (1.3 Mbps)') {
    demodConfig.modulation = 'FLRC'
    demodConfig.symbolRate = 1300000
    demodConfig.deviation = 325000
    demodConfig.filterCutoff = 750000
    demodConfig.autoSyncWord = true
    demodConfig.maskMode = 'auto'
    demodConfig.enableHwCrc = true
  } else if (presetName === 'SX1280 FLRC (520 kbps)') {
    demodConfig.modulation = 'FLRC'
    demodConfig.symbolRate = 520000
    demodConfig.deviation = 130000
    demodConfig.filterCutoff = 300000
  } else if (presetName === 'SX1280 FLRC (260 kbps)') {
    demodConfig.modulation = 'FLRC'
    demodConfig.symbolRate = 260000
    demodConfig.deviation = 65000
    demodConfig.filterCutoff = 150000
  }
}
