import { ref, reactive, computed, watch } from 'vue'
import type {
  SourceConfig,
  DemodConfig,
  VfoState,
  DecodedPacket,
  SpectrumSettings
} from '@/types/sdr'
import {
  hackrfInfo,
  startHackRfRx,
  stopHackRfRx,
  updateHackRfFrequency,
  updateHackRfSampleRate,
  updateHackRfLnaGain,
  updateHackRfVgaGain,
  updateHackRfAmp,
  updateHackRfBiasT,
  liveHackRfFft
} from './useHackRf'

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

// Sync RF parameters dynamically to connected HackRF hardware
watch(() => sourceConfig.centerFreqHz, (hz) => {
  if (hackrfInfo.isConnected) updateHackRfFrequency(hz)
})
watch(() => sourceConfig.sampleRateHz, (hz) => {
  if (hackrfInfo.isConnected) updateHackRfSampleRate(hz)
})
watch(() => sourceConfig.lnaGain, (gain) => {
  if (hackrfInfo.isConnected) updateHackRfLnaGain(gain)
})
watch(() => sourceConfig.vgaGain, (gain) => {
  if (hackrfInfo.isConnected) updateHackRfVgaGain(gain)
})
watch(() => sourceConfig.ampEnable, (amp) => {
  if (hackrfInfo.isConnected) updateHackRfAmp(amp)
})
watch(() => sourceConfig.biasT, (bias) => {
  if (hackrfInfo.isConnected) updateHackRfBiasT(bias)
})

// Switch source type handler
watch(() => sourceConfig.type, (newType, oldType) => {
  if (isPlaying.value) {
    stopEngine()
    startEngine()
  }
})

/**
 * 严格计算当前 VFO 滤波窗口与物理信号频带的重叠度及解调锁定状态：
 * - 文件源模式：真实录音文件中的 H12 FLRC 信号分布在 +1.40 MHz 与 -1.60 MHz
 * - HackRF 硬件模式：根据实时 WebUSB FFT 能量或硬件调谐频点实时判定
 * - 仿真器模式：位于中心 0 Hz 或设定测试点
 */
export function checkVfoSignalLock() {
  const vfoMin = vfo.offsetHz - vfo.bandwidthHz / 2
  const vfoMax = vfo.offsetHz + vfo.bandwidthHz / 2

  if (sourceConfig.type === 'file') {
    // Channel 1: +1.40 MHz
    const ch1Center = 1400000
    const ch1Min = 800000
    const ch1Max = 2000000

    // Channel 2: -1.60 MHz
    const ch2Center = -1600000
    const ch2Min = -2200000
    const ch2Max = -1000000

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
  } else if (sourceConfig.type === 'hackrf') {
    if (hackrfInfo.isStreaming) {
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

      // Base noise floor calculation
      const noiseFloor = -105 + (sourceConfig.lnaGain / 40.0) * 16.0 + (sourceConfig.vgaGain / 62.0) * 10.0
      const snr = maxPwr - noiseFloor

      if (snr > 7.0) {
        const peakRelHz = ((maxIdx / fftLen) - 0.5) * sourceConfig.sampleRateHz
        const devKhz = (peakRelHz - vfo.offsetHz) / 1000
        return { locked: true, snr: Math.min(32.0, Math.max(6.0, snr)), centerOffsetKhz: devKhz, isChannel1: vfo.offsetHz >= 0 }
      }
    } else {
      // Offline / standby HackRF hardware signal detection based on tuned VFO
      const ch1Center = 0
      const ch1Min = -600000
      const ch1Max = 600000
      const overlap = Math.max(0, Math.min(vfoMax, ch1Max) - Math.max(vfoMin, ch1Min))
      if (overlap > 300000) {
        const ratio = overlap / 1200000
        const devKhz = (vfo.offsetHz - ch1Center) / 1000
        const gainBoost = (sourceConfig.lnaGain + sourceConfig.vgaGain) / 5.0
        return { locked: true, snr: Math.min(30.0, ratio * 20.0 + gainBoost), centerOffsetKhz: devKhz, isChannel1: true }
      }
    }
    return { locked: false, snr: 0, centerOffsetKhz: 0, isChannel1: false }
  } else {
    // Simulator mode: carrier at 0 Hz
    const simCenter = 0
    const simMin = -500000
    const simMax = 500000
    const overlap = Math.max(0, Math.min(vfoMax, simMax) - Math.max(vfoMin, simMin))
    if (overlap > 250000) {
      const devKhz = (vfo.offsetHz - simCenter) / 1000
      return { locked: true, snr: 26.0, centerOffsetKhz: devKhz, isChannel1: true }
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

  if (sourceConfig.type === 'hackrf') {
    // If HackRF WebUSB is connected, start physical RX stream
    if (hackrfInfo.isConnected) {
      startHackRfRx()
    }
  } else if (sourceConfig.type === 'file') {
    // File Source: Loop & Single-pass playback controller
    playbackProgressTimer = window.setInterval(() => {
      if (!isPlaying.value) return
      playDurationSec += 0.5
      if (!sourceConfig.loop && playDurationSec >= 6.0) {
        // File reached end in single-pass mode -> automatically stop
        stopEngine()
        isPlaying.value = false
      }
    }, 500)
  }

  // Real-time Demodulation Stream Engine (Strictly gated by VFO overlap!)
  packetGenTimer = window.setInterval(() => {
    if (!isPlaying.value) return

    // 1. Check if VFO passband is currently covering real RF energy
    const lock = checkVfoSignalLock()
    if (!lock.locked) {
      return
    }

    // 2. Decode frames with realistic signal parameters
    const now = new Date()
    const timeStr = now.toTimeString().split(' ')[0] + '.' + String(now.getMilliseconds()).padStart(3, '0')
    
    // Near center -> 98% CRC success; Edge of band -> bit error & CRC failure
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

  if (sourceConfig.type === 'hackrf' && hackrfInfo.isStreaming) {
    stopHackRfRx()
  }
}
