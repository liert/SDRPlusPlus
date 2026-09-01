import { ref, reactive, computed } from 'vue'
import type {
  SourceConfig,
  DemodConfig,
  VfoState,
  DecodedPacket,
  SpectrumSettings
} from '@/types/sdr'

export const isPlaying = ref(false)
export const fps = ref(60)
export const totalPacketsCount = ref(0)
export const validCrcCount = ref(0)
export const currentLang = ref<'zh' | 'en'>('zh')

// Source Configuration
export const sourceConfig = reactive<SourceConfig>({
  type: 'file',
  centerFreqHz: 2400000000,
  sampleRateHz: 8000000,
  lnaGain: 32,
  vgaGain: 20,
  ampEnable: false,
  biasT: false,
  filePath: 'hackrf/fresh_pairing_2400_8.iq',
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
  offsetHz: 1400000, // +1.40 MHz for local sample
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

let simTimer: number | null = null
let packetGenTimer: number | null = null
let playbackProgressTimer: number | null = null
let playDurationSec = 0

export function togglePlay() {
  isPlaying.value = !isPlaying.value
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

  // If not looping, stop after ~6 seconds (simulated file length)
  playbackProgressTimer = window.setInterval(() => {
    if (!isPlaying.value) return
    playDurationSec += 0.5
    if (!sourceConfig.loop && playDurationSec >= 6.0) {
      // File finished, stop playing
      stopEngine()
      isPlaying.value = false
    }
  }, 500)

  // Packet generation stream (simulated live bursts from 1.4MHz / -1.6MHz)
  packetGenTimer = window.setInterval(() => {
    if (!isPlaying.value) return
    const now = new Date()
    const timeStr = now.toTimeString().split(' ')[0] + '.' + String(now.getMilliseconds()).padStart(3, '0')
    const isValid = Math.random() > 0.05
    const id = ++totalPacketsCount.value
    if (isValid) validCrcCount.value++

    const isPairing = id % 25 === 0

    let payload = ''
    let ascii = ''
    let sync = isPairing ? '54313253' : (Math.random() > 0.5 ? '1400701E' : '5BDEB350')
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
      freqOffsetKhz: +(vfo.offsetHz / 1000 + (Math.random() * 10 - 5)).toFixed(1),
      syncWord: '0x' + sync,
      mask,
      payloadHex: payload,
      payloadAscii: ascii,
      hwCrc: '0x' + Math.floor(Math.random() * 0xFFFFFFFF).toString(16).padStart(8, '0').toUpperCase(),
      crcValid: isValid,
      score: +(7.0 + Math.random() * 8).toFixed(1),
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
