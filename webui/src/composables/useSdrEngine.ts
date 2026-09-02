import { ref, reactive, computed, watch } from 'vue'
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

// C++ Backend Connection State & Device Management
export const isBackendConnected = ref(false)
export const backendStatusText = ref('未连接 C++ 后端 (启动: ./sdrpp.exe -s -p 5259)')
export const availableSources = ref<string[]>(['HackRF', 'File Source', 'RTL-SDR', 'Simulator'])
export const availableDevices = ref<BackendDeviceInfo[]>([])
export const isScanningDevices = ref(false)

let backendWs: WebSocket | null = null
let reconnectTimer: number | null = null

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
 * Connect to C++ Backend Server via WebSocket (ws://127.0.0.1:5259/ws)
 */
export function connectBackendWs(url = 'ws://127.0.0.1:5259/ws') {
  if (typeof window === 'undefined') return
  if (backendWs && (backendWs.readyState === WebSocket.OPEN || backendWs.readyState === WebSocket.CONNECTING)) {
    return
  }

  try {
    backendWs = new WebSocket(url)
    backendWs.binaryType = 'arraybuffer'

    backendWs.onopen = () => {
      isBackendConnected.value = true
      backendStatusText.value = '⚡ C++ 后端已连接 (高性能 DSP 引擎)'
      console.log('Connected to SDR++ C++ Backend Engine over WebSocket')
      isPlaying.value = true
      
      // Initialize parameter sync on connect
      const sname = sourceConfig.type === 'hackrf' ? 'HackRF' : (sourceConfig.type === 'file' ? 'File Source' : 'RTL-SDR')
      sendBackendCommand('set_source', { source: sname })
      sendBackendCommand('set_freq', { freq: sourceConfig.centerFreqHz })
      sendBackendCommand('set_samplerate', { sampleRate: sourceConfig.sampleRateHz })
      sendBackendCommand('set_gain', {
        lna: sourceConfig.lnaGain,
        vga: sourceConfig.vgaGain,
        amp: sourceConfig.ampEnable,
        biasT: sourceConfig.biasT
      })
      sendBackendCommand('start')
      sendBackendCommand('get_devices')
      sendBackendCommand('get_sources')
      sendBackendCommand('get_status')
    }

    backendWs.onmessage = (event) => {
      if (event.data instanceof ArrayBuffer) {
        // Binary FFT frame: Float32Array calculated in C++ by FFTW3 / Volk
        const floats = new Float32Array(event.data)
        if (floats.length === 1024) {
          liveHackRfFft.set(floats)
          if (!hasLiveHackRfData.value) {
            hasLiveHackRfData.value = true
          }
        }
      } else if (typeof event.data === 'string') {
        try {
          const msg = JSON.parse(event.data)
          if (msg.type === 'packet') {
            // Real C++ decoded packet frame from flrc_decoder
            packetHistory.value.unshift(msg as DecodedPacket)
            if (packetHistory.value.length > 200) {
              packetHistory.value.pop()
            }
            totalPacketsCount.value++
            if (msg.crcValid) {
              validCrcCount.value++
            }
          } else if (msg.type === 'devices' || msg.devices) {
            // Update device list from backend
            if (Array.isArray(msg.devices)) {
              availableDevices.value = msg.devices
              if (availableDevices.value.length > 0 && !sourceConfig.deviceSerial) {
                sourceConfig.deviceSerial = availableDevices.value[0].serial
              }
            }
            isScanningDevices.value = false
          } else if (msg.sources) {
            if (Array.isArray(msg.sources)) {
              availableSources.value = msg.sources
            }
          } else if (msg.type === 'status' || msg.status === 'ok') {
            if (msg.running !== undefined) {
              isPlaying.value = msg.running
            }
            if (msg.devices && Array.isArray(msg.devices)) {
              availableDevices.value = msg.devices
            }
          }
        } catch (e) {
          console.warn('JSON parsing error:', e)
        }
      }
    }

    backendWs.onclose = () => {
      isBackendConnected.value = false
      backendStatusText.value = '未连接 C++ 后端 (启动: ./sdrpp.exe -s -p 5259)'
      backendWs = null
      scheduleReconnect()
    }

    backendWs.onerror = () => {
      isBackendConnected.value = false
    }
  } catch (err) {
    isBackendConnected.value = false
    scheduleReconnect()
  }
}

function scheduleReconnect() {
  if (reconnectTimer) clearTimeout(reconnectTimer)
  reconnectTimer = window.setTimeout(() => {
    connectBackendWs()
  }, 2500)
}

export function sendBackendCommand(cmd: string, params: Record<string, any> = {}) {
  if (backendWs && backendWs.readyState === WebSocket.OPEN) {
    backendWs.send(JSON.stringify({ cmd, ...params }))
  }
}

export function refreshBackendDevices() {
  isScanningDevices.value = true
  sendBackendCommand('get_devices')
  setTimeout(() => { isScanningDevices.value = false }, 1500)
}

// Auto-connect on startup
if (typeof window !== 'undefined') {
  connectBackendWs()
}

// Debounced Parameter Sync to prevent USB/socket congestion
let freqDebounceTimer: number | null = null
watch(() => sourceConfig.centerFreqHz, (hz) => {
  if (freqDebounceTimer) clearTimeout(freqDebounceTimer)
  freqDebounceTimer = window.setTimeout(() => {
    if (isBackendConnected.value) {
      sendBackendCommand('set_freq', { freq: hz })
    }
  }, 40)
})

watch(() => sourceConfig.sampleRateHz, (hz) => {
  if (isBackendConnected.value) {
    sendBackendCommand('set_samplerate', { sampleRate: hz })
  }
})

let gainDebounceTimer: number | null = null
function pushGainsToBackend() {
  if (gainDebounceTimer) clearTimeout(gainDebounceTimer)
  gainDebounceTimer = window.setTimeout(() => {
    if (isBackendConnected.value) {
      sendBackendCommand('set_gain', {
        lna: sourceConfig.lnaGain,
        vga: sourceConfig.vgaGain,
        amp: sourceConfig.ampEnable,
        biasT: sourceConfig.biasT
      })
    }
  }, 30)
}

watch(() => sourceConfig.lnaGain, pushGainsToBackend)
watch(() => sourceConfig.vgaGain, pushGainsToBackend)
watch(() => sourceConfig.ampEnable, pushGainsToBackend)
watch(() => sourceConfig.biasT, pushGainsToBackend)

watch(() => sourceConfig.deviceSerial, (serial) => {
  if (isBackendConnected.value && serial) {
    sendBackendCommand('set_device', { serial })
  }
})

// Switch source type handler
watch(() => sourceConfig.type, (newType) => {
  if (isBackendConnected.value) {
    const sname = newType === 'hackrf' ? 'HackRF' : (newType === 'file' ? 'File Source' : 'RTL-SDR')
    sendBackendCommand('set_source', { source: sname })
  }
  if (isPlaying.value) {
    stopEngine()
    startEngine()
  }
})

/**
 * Calculate VFO signal lock and SNR
 */
export function checkVfoSignalLock() {
  const vfoMin = vfo.offsetHz - vfo.bandwidthHz / 2
  const vfoMax = vfo.offsetHz + vfo.bandwidthHz / 2

  if (isBackendConnected.value || (sourceConfig.type === 'hackrf' && hasLiveHackRfData.value)) {
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
  } else if (sourceConfig.type === 'file') {
    // Channel 1: +1.40 MHz, Channel 2: -1.60 MHz
    const ch1Center = 1400000, ch1Min = 800000, ch1Max = 2000000
    const ch2Center = -1600000, ch2Min = -2200000, ch2Max = -1000000

    const overlap1 = Math.max(0, Math.min(vfoMax, ch1Max) - Math.max(vfoMin, ch1Min))
    const overlap2 = Math.max(0, Math.min(vfoMax, ch2Max) - Math.max(vfoMin, ch2Min))

    if (overlap1 > 350000) {
      const ratio = overlap1 / 1200000
      const devKhz = (vfo.offsetHz - ch1Center) / 1000
      return { locked: true, snr: ratio * 28.0, centerOffsetKhz: devKhz, isChannel1: true }
    }

    if (overlap2 > 350000) {
      const ratio = overlap2 / 1200000
      const devKhz = (vfo.offsetHz - ch2Center) / 1000
      return { locked: true, snr: ratio * 24.0, centerOffsetKhz: devKhz, isChannel1: false }
    }

    return { locked: false, snr: 0, centerOffsetKhz: 0, isChannel1: false }
  } else {
    // Offline / Standby mode
    const ch1Min = -600000, ch1Max = 600000
    const overlap = Math.max(0, Math.min(vfoMax, ch1Max) - Math.max(vfoMin, ch1Min))
    if (overlap > 300000) {
      const ratio = overlap / 1200000
      const devKhz = vfo.offsetHz / 1000
      return { locked: true, snr: Math.min(30.0, ratio * 22.0), centerOffsetKhz: devKhz, isChannel1: true }
    }
    return { locked: false, snr: 0, centerOffsetKhz: 0, isChannel1: false }
  }
}

export const isVfoLockedOnSignal = computed(() => {
  return checkVfoSignalLock().locked
})

let simTimer: number | null = null
let packetGenTimer: number | null = null
let playbackProgressTimer: number | null = null
let playDurationSec = 0

export function togglePlay() {
  isPlaying.value = !isPlaying.value
  if (isBackendConnected.value) {
    sendBackendCommand(isPlaying.value ? 'start' : 'stop')
  }
  if (isPlaying.value) {
    startEngine()
  } else {
    stopEngine()
  }
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

function startEngine() {
  if (simTimer) clearInterval(simTimer)
  if (packetGenTimer) clearInterval(packetGenTimer)
  if (playbackProgressTimer) clearInterval(playbackProgressTimer)

  playDurationSec = 0

  if (isBackendConnected.value) {
    return
  }

  if (sourceConfig.type === 'file') {
    playbackProgressTimer = window.setInterval(() => {
      if (!isPlaying.value) return
      playDurationSec += 0.5
      if (!sourceConfig.loop && playDurationSec >= 6.0) {
        stopEngine()
        isPlaying.value = false
      }
    }, 500)
  }

  // Fallback simulator loop when C++ backend is not connected
  packetGenTimer = window.setInterval(() => {
    if (!isPlaying.value || isBackendConnected.value) return

    const lock = checkVfoSignalLock()
    if (!lock.locked) return

    const now = new Date()
    const timeStr = now.toTimeString().split(' ')[0] + '.' + String(now.getMilliseconds()).padStart(3, '0')
    const isValid = Math.random() < Math.min(0.98, lock.snr / 25.0)
    const id = ++totalPacketsCount.value
    if (isValid) validCrcCount.value++

    const isPairing = id % 25 === 0
    let payload = ''
    let ascii = ''
    let sync = isPairing ? '54313253' : (lock.isChannel1 ? '1400701E' : '5BDEB350')
    let mask = isPairing ? '0x66' : '0x99'

    if (isPairing) {
      payload = '11 22 33 44 55 9A 8B 7C 6D ' + Array.from({ length: 15 }, () => Math.floor(Math.random() * 255).toString(16).padStart(2, '0').toUpperCase()).join(' ')
      ascii = '..3DU.....'
    } else {
      payload = `B1 78 1E 07 81 E0 78 78 D2 D2 1E 1E 24 D2 0A 54 2B 41 4E 47 4C 45 20 2D 50 30 0D 00 00 00 00 3B`
      ascii = '\nT+ANGLE -P0\r'
    }

    const packet: DecodedPacket = {
      id,
      timestamp: timeStr,
      freqOffsetKhz: +(vfo.offsetHz / 1000 + lock.centerOffsetKhz + (Math.random() * 4 - 2)).toFixed(1),
      syncWord: '0x' + sync,
      mask,
      payloadHex: payload,
      payloadAscii: ascii,
      hwCrc: '0x' + Math.floor(Math.random() * 0xFFFFFFFF).toString(16).padStart(8, '0').toUpperCase(),
      crcValid: isValid,
      score: +(Math.max(4.0, lock.snr / 2.0 + Math.random() * 4)).toFixed(1),
      length: isPairing ? 42 : 32
    }

    packetHistory.value.unshift(packet)
    if (packetHistory.value.length > 200) {
      packetHistory.value.pop()
    }
  }, 120)
}

function stopEngine() {
  if (simTimer) { clearInterval(simTimer); simTimer = null }
  if (packetGenTimer) { clearInterval(packetGenTimer); packetGenTimer = null }
  if (playbackProgressTimer) { clearInterval(playbackProgressTimer); playbackProgressTimer = null }
}
