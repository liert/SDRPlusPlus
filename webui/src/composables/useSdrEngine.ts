import { ref, reactive, computed, watch } from 'vue'
import { invoke } from '@tauri-apps/api/core'
import type {
  SourceConfig,
  DemodConfig,
  VfoState,
  DecodedPacket,
  SpectrumSettings,
  FftWindowType,
  BackendDeviceInfo
} from '@/types/sdr'
import { liveHackRfFft, hasLiveHackRfData, updateLiveFftData } from './useHackRf'

export { hasLiveHackRfData }

export const isPlaying = ref(false)
export const fps = ref(60)
export const dspIpcFps = ref(0)
export const dspSeq = ref(0)
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
  type: 'file',
  centerFreqHz: 2400000000,
  sampleRateHz: 8000000,
  lnaGain: 16,
  vgaGain: 20,
  ampEnable: false,
  biasT: false,
  deviceSerial: '',
  filePath: '',
  fileFormat: 'raw_int8',
  loop: true
})

export const fileErrorNotice = ref<string>('')

export let lastUserFreqChangeTime = 0
export let lastUserSrChangeTime = 0

export function setCenterFreq(hz: number) {
  lastUserFreqChangeTime = performance.now()
  sourceConfig.centerFreqHz = Math.round(hz)
  sendBackendCommand('set_freq', { freq: sourceConfig.centerFreqHz })
}

export function adjustCenterFreq(deltaHz: number) {
  setCenterFreq(sourceConfig.centerFreqHz + deltaHz)
}

export function setSampleRate(sr: number) {
  lastUserSrChangeTime = performance.now()
  sourceConfig.sampleRateHz = Math.round(sr)
  sendBackendCommand('set_samplerate', { sampleRate: sourceConfig.sampleRateHz })
}

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

// FFT Window Mapping Constants
export const WINDOW_TYPE_MAP: Record<FftWindowType, number> = {
  blackman_harris: 0,
  hann: 1,
  hamming: 2,
  blackman: 3,
  nuttall: 4,
  flat_top: 5,
  rectangular: 6
}

export const WINDOW_NAME_MAP: Record<number, FftWindowType> = {
  0: 'blackman_harris',
  1: 'hann',
  2: 'hamming',
  3: 'blackman',
  4: 'nuttall',
  5: 'flat_top',
  6: 'rectangular'
}

// Spectrum & Waterfall Visual Settings (Configurable FFT Parameters)
export const spectrumSettings = reactive<SpectrumSettings>({
  minDb: -100,
  maxDb: 10,
  fftSize: 1024,
  fftWindow: 'blackman_harris',
  fftRate: 60,
  smoothing: 0.65,
  averagingMode: 'ema',
  peakHold: true,
  peakDecaySpeed: 'medium',
  colormap: 'turbo',
  waterfallSpeed: 1,
  fillSpectrum: true,
  lineWidth: 1.5,
  showGrid: true,
  splitRatio: 0.38,
  autoScale: false
})

// Decoded Packets History
export const packetHistory = ref<DecodedPacket[]>([])

export const crcSuccessRate = computed(() => {
  if (totalPacketsCount.value === 0) return 0
  return ((validCrcCount.value / totalPacketsCount.value) * 100).toFixed(1)
})

export function logUi(msg: string, level = 'INFO') {
  console.log(`[${level}] [UI/Canvas] ${msg}`)
  try {
    invoke('log_frontend_message', { level, msg })
  } catch (e) {}
}

let shmFftTimer: number | null = null

/**
 * Initialize High-Performance Windows Shared Memory (Zero-Copy IPC)
 */
export function initShmEngine() {
  let isPolling = false
  let frameCounter = 0
  let pollCounter = 0
  let lastFpsCalc = performance.now()

  // 1. Rock-solid 60 FPS Shared Memory polling timer (16ms)
  if (!shmFftTimer) {
    shmFftTimer = window.setInterval(async () => {
      if (isPolling) return
      isPolling = true
      try {
        const res = await invoke<number[]>('get_shm_fft')
        pollCounter++
        if (pollCounter % 120 === 1 && Array.isArray(res)) {
          logUi(`IPC pollShmFft: received ${res.length} floats (first 3: [${res.slice(0, 3).map(v => v.toFixed(1)).join(', ')}])`)
        }
        if (Array.isArray(res) && res.length >= 256) {
          updateLiveFftData(res)
          if (!isBackendConnected.value) {
            isBackendConnected.value = true
            backendStatusText.value = '⚡ Windows 原生共享内存直通 (IPC Zero-Copy)'
          }
          frameCounter++
        }
      } catch (e: any) {
        if (pollCounter % 60 === 1) {
          logUi(`IPC pollShmFft error: ${e?.message || e}`, 'ERROR')
        }
      } finally {
        isPolling = false
      }

      const now = performance.now()
      if (now - lastFpsCalc >= 1000) {
        dspIpcFps.value = Math.round((frameCounter * 1000) / (now - lastFpsCalc))
        frameCounter = 0
        lastFpsCalc = now
      }
    }, 16)
  }

  // 2. Poll Metadata and Decoded Packet Ring Buffer every 100ms
  if (!shmPollTimer) {
    shmPollTimer = window.setInterval(async () => {
      try {
        const st = await invoke<any>('get_shm_status')
        if (st && st.connected) {
          isBackendConnected.value = true
          isPlaying.value = st.running
          dspSeq.value = st.seq
          backendStatusText.value = '⚡ Windows 原生共享内存直通 (IPC Zero-Copy)'

          const now = performance.now()
          if (now - lastUserSrChangeTime > 1500) {
            if (st.sampleRate && Math.abs(sourceConfig.sampleRateHz - st.sampleRate) > 1) {
              sourceConfig.sampleRateHz = st.sampleRate
            }
          }
          if (now - lastUserFreqChangeTime > 1500) {
            if (st.centerFreq && Math.abs(sourceConfig.centerFreqHz - st.centerFreq) > 1) {
              sourceConfig.centerFreqHz = st.centerFreq
            }
          }

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
  lastUserFreqChangeTime = performance.now()
  if (freqDebounceTimer) clearTimeout(freqDebounceTimer)
  freqDebounceTimer = window.setTimeout(() => {
    sendBackendCommand('set_freq', { freq: hz })
  }, 30)
})

watch(() => sourceConfig.sampleRateHz, (hz) => {
  lastUserSrChangeTime = performance.now()
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
  sendBackendCommand('set_source', {
    source: sname,
    path: sourceConfig.filePath
  })
})

watch(() => sourceConfig.filePath, (path) => {
  if (sourceConfig.type === 'file') {
    sendBackendCommand('set_source', {
      source: 'File Source',
      path
    })
  }
})

// Debounced FFT Parameter Sync to C++ Backend
let fftDebounceTimer: number | null = null
export function pushFftParamsToBackend() {
  if (fftDebounceTimer) clearTimeout(fftDebounceTimer)
  fftDebounceTimer = window.setTimeout(() => {
    sendBackendCommand('set_fft_params', {
      fftSize: spectrumSettings.fftSize,
      fftWindow: WINDOW_TYPE_MAP[spectrumSettings.fftWindow] ?? 0,
      fftRate: spectrumSettings.fftRate
    })
    logUi(`[FFT Sync] Synced params to C++ DSP core: Size=${spectrumSettings.fftSize}, Window=${spectrumSettings.fftWindow}, Rate=${spectrumSettings.fftRate} FPS`)
  }, 40)
}

watch(() => spectrumSettings.fftSize, pushFftParamsToBackend)
watch(() => spectrumSettings.fftWindow, pushFftParamsToBackend)
watch(() => spectrumSettings.fftRate, pushFftParamsToBackend)

/**
 * Auto-scale FFT dB range based on real-time signal noise floor and peak power
 */
export function autoScaleFftRange() {
  if (!hasLiveHackRfData.value || liveHackRfFft.length === 0) {
    spectrumSettings.minDb = -100
    spectrumSettings.maxDb = 10
    return
  }

  const len = liveHackRfFft.length
  const sampled: number[] = []
  const step = Math.max(1, Math.floor(len / 256))
  for (let i = 0; i < len; i += step) {
    sampled.push(liveHackRfFft[i])
  }
  sampled.sort((a, b) => a - b)

  const noiseFloor = sampled[Math.floor(sampled.length * 0.15)] ?? -105
  const peakSignal = sampled[Math.floor(sampled.length * 0.98)] ?? -40

  const targetMin = Math.max(-140, Math.floor(noiseFloor - 10))
  const targetMax = Math.min(20, Math.ceil(peakSignal + 15))

  spectrumSettings.minDb = Math.min(targetMin, targetMax - 20)
  spectrumSettings.maxDb = Math.max(targetMax, spectrumSettings.minDb + 20)
  logUi(`⚡ [Auto-Scale] Evaluated floor=${noiseFloor.toFixed(1)} dBm, peak=${peakSignal.toFixed(1)} dBm -> Set range: [${spectrumSettings.minDb}, ${spectrumSettings.maxDb}] dBm`)
}

/**
 * Reset FFT Parameters to default
 */
export function resetFftDefaults() {
  spectrumSettings.minDb = -100
  spectrumSettings.maxDb = 10
  spectrumSettings.fftSize = 1024
  spectrumSettings.fftWindow = 'blackman_harris'
  spectrumSettings.fftRate = 60
  spectrumSettings.smoothing = 0.65
  spectrumSettings.averagingMode = 'ema'
  spectrumSettings.peakHold = true
  spectrumSettings.peakDecaySpeed = 'medium'
  spectrumSettings.colormap = 'turbo'
  spectrumSettings.waterfallSpeed = 1
  spectrumSettings.fillSpectrum = true
  spectrumSettings.lineWidth = 1.5
  spectrumSettings.showGrid = true
  spectrumSettings.splitRatio = 0.38
  pushFftParamsToBackend()
  logUi('Reset FFT & Display settings to default')
}

/**
 * Apply quick FFT preset profiles
 */
export function applyFftPreset(presetId: 'flrc' | 'weak' | 'fhss' | 'hi_res') {
  if (presetId === 'flrc') {
    // Optimized for SX1280 2.4G bursts
    spectrumSettings.fftSize = 1024
    spectrumSettings.fftWindow = 'blackman_harris'
    spectrumSettings.fftRate = 60
    spectrumSettings.smoothing = 0.5
    spectrumSettings.peakHold = true
    spectrumSettings.peakDecaySpeed = 'fast'
    spectrumSettings.minDb = -105
    spectrumSettings.maxDb = 5
  } else if (presetId === 'weak') {
    // Narrowband weak signal discovery
    spectrumSettings.fftSize = 4096
    spectrumSettings.fftWindow = 'nuttall'
    spectrumSettings.fftRate = 30
    spectrumSettings.smoothing = 0.85
    spectrumSettings.peakHold = true
    spectrumSettings.peakDecaySpeed = 'slow'
    spectrumSettings.minDb = -120
    spectrumSettings.maxDb = -20
  } else if (presetId === 'fhss') {
    // Frequency hopping spread spectrum & fast bursts
    spectrumSettings.fftSize = 1024
    spectrumSettings.fftWindow = 'hann'
    spectrumSettings.fftRate = 90
    spectrumSettings.smoothing = 0.2
    spectrumSettings.peakHold = true
    spectrumSettings.peakDecaySpeed = 'fast'
    spectrumSettings.waterfallSpeed = 2
  } else if (presetId === 'hi_res') {
    // Ultra high resolution spectrum analyzer
    spectrumSettings.fftSize = 4096
    spectrumSettings.fftWindow = 'flat_top'
    spectrumSettings.fftRate = 30
    spectrumSettings.smoothing = 0.7
    spectrumSettings.peakHold = true
    spectrumSettings.peakDecaySpeed = 'medium'
  }
  pushFftParamsToBackend()
  logUi(`Applied FFT Preset Profile: ${presetId}`)
}

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
  if (!isPlaying.value && sourceConfig.type === 'file' && (!sourceConfig.filePath || !sourceConfig.filePath.trim())) {
    fileErrorNotice.value = currentLang.value === 'zh'
      ? '❌ 未载入 IQ 录制文件！请在左侧【信号源设置】中点击选择文件后再启动。'
      : '❌ No IQ file loaded! Please select an IQ file in RF Source before starting.'
    logUi('Start rejected: No IQ file specified for File Source', 'WARN')
    return
  }
  fileErrorNotice.value = ''
  isPlaying.value = !isPlaying.value
  const targetSrc = sourceConfig.type === 'hackrf' ? 'HackRF' : (sourceConfig.type === 'file' ? 'File Source' : 'RTL-SDR')
  sendBackendCommand(isPlaying.value ? 'start' : 'stop', { source: targetSrc })
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
