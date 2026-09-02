import { reactive, ref } from 'vue'
import type { HackRfDeviceInfo, SourceConfig } from '@/types/sdr'

export const HACKRF_USB_VID = 0x1d50
export const HACKRF_USB_PID = 0x6089

export enum HackRfVendorRequest {
  SET_TRANSCEIVER_MODE = 1,
  MAX2837_WRITE = 2,
  MAX2837_READ = 3,
  SI5351C_WRITE = 4,
  SI5351C_READ = 5,
  SAMPLE_RATE_SET = 6,
  BASEBAND_FILTER_BANDWIDTH_SET = 7,
  RFFC5071_WRITE = 8,
  RFFC5071_READ = 9,
  SPIFLASH_ERASE = 10,
  SPIFLASH_WRITE = 11,
  SPIFLASH_READ = 12,
  BOARD_ID_READ = 14,
  VERSION_STRING_READ = 15,
  SET_FREQ = 16,
  SET_AMP_ENABLE = 17,
  BOARD_PARTID_SERIALNO_READ = 18,
  SET_LNA_GAIN = 19,
  SET_VGA_GAIN = 20,
  SET_TXVGA_GAIN = 21,
  ANTENNA_ENABLE = 23,
  SET_FREQ_EXPLICIT = 24,
  RESET = 29
}

export enum HackRfTransceiverMode {
  OFF = 0,
  RECEIVE = 1,
  TRANSMIT = 2,
  SS = 3,
  CPLD_UPDATE = 4
}

const BOARD_NAMES: Record<number, string> = {
  0: 'Jellybean',
  1: 'Jawbreaker',
  2: 'HackRF One (r1..r8)',
  3: 'rad1o',
  4: 'HackRF One (r9+)',
  255: 'Unrecognized Board'
}

const MAX2837_FILTER_BW = [
  1750000, 2500000, 3500000, 5000000, 5500000, 6000000, 7000000, 8000000,
  9000000, 10000000, 12000000, 14000000, 15000000, 20000000, 24000000, 28000000
]

export function computeBasebandFilterBw(bandwidthHz: number): number {
  for (let i = 0; i < MAX2837_FILTER_BW.length; i++) {
    if (MAX2837_FILTER_BW[i] >= bandwidthHz) {
      return MAX2837_FILTER_BW[i]
    }
  }
  return 28000000
}

export const isWebUsbAvailable = typeof navigator !== 'undefined' && 'usb' in navigator

export const hackrfInfo = reactive<HackRfDeviceInfo>({
  isSupported: isWebUsbAvailable,
  isConnected: false,
  isStreaming: false,
  status: 'disconnected',
  deviceName: 'HackRF One',
  serialNumber: '',
  version: '',
  boardName: 'HackRF One',
  boardId: 2,
  rxBytesTotal: 0,
  rxRateMBps: 0,
  error: null
})

export const liveHackRfFft = new Float32Array(1024).fill(-110)
export const hasLiveHackRfData = ref(false)

let activeUsbDevice: USBDevice | null = null
let rxRateTimer: number | null = null
let rxBytesSinceLastInterval = 0

// FFT Helper precomputations
const FFT_SIZE = 1024
const sinTable = new Float32Array(FFT_SIZE)
const cosTable = new Float32Array(FFT_SIZE)
const hannWindow = new Float32Array(FFT_SIZE)
const bitRevTable = new Uint16Array(FFT_SIZE)

for (let i = 0; i < FFT_SIZE; i++) {
  const angle = (-2 * Math.PI * i) / FFT_SIZE
  sinTable[i] = Math.sin(angle)
  cosTable[i] = Math.cos(angle)
  hannWindow[i] = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (FFT_SIZE - 1)))

  let rev = 0
  let val = i
  for (let b = 0; b < 10; b++) {
    rev = (rev << 1) | (val & 1)
    val >>= 1
  }
  bitRevTable[i] = rev
}

const realBuf = new Float32Array(FFT_SIZE)
const imagBuf = new Float32Array(FFT_SIZE)

/**
 * Computes 1024-point Complex FFT from Int8 IQ buffer
 */
export function computeFftFromInt8(int8: Int8Array, outDb: Float32Array) {
  const numPairs = Math.min(FFT_SIZE, Math.floor(int8.length / 2))
  for (let i = 0; i < FFT_SIZE; i++) {
    if (i < numPairs) {
      const w = hannWindow[i]
      realBuf[bitRevTable[i]] = (int8[i * 2] / 128.0) * w
      imagBuf[bitRevTable[i]] = (int8[i * 2 + 1] / 128.0) * w
    } else {
      realBuf[bitRevTable[i]] = 0
      imagBuf[bitRevTable[i]] = 0
    }
  }

  for (let len = 2; len <= FFT_SIZE; len <<= 1) {
    const half = len >> 1
    const step = FFT_SIZE / len
    for (let i = 0; i < FFT_SIZE; i += len) {
      for (let j = 0; j < half; j++) {
        const idx = j * step
        const c = cosTable[idx]
        const s = sinTable[idx]
        const uR = realBuf[i + j]
        const uI = imagBuf[i + j]
        const vR = realBuf[i + j + half] * c - imagBuf[i + j + half] * s
        const vI = realBuf[i + j + half] * s + imagBuf[i + j + half] * c
        realBuf[i + j] = uR + vR
        imagBuf[i + j] = uI + vI
        realBuf[i + j + half] = uR - vR
        imagBuf[i + j + half] = uI - vI
      }
    }
  }

  const halfFft = FFT_SIZE >> 1
  for (let i = 0; i < FFT_SIZE; i++) {
    const srcIdx = (i + halfFft) % FFT_SIZE
    const r = realBuf[srcIdx]
    const im = imagBuf[srcIdx]
    const pwr = r * r + im * im + 1e-12
    const dbm = 10 * Math.log10(pwr) - 15.0
    outDb[i] = Math.max(-120, Math.min(10, dbm))
  }
}

/**
 * Connect to HackRF One device via WebUSB
 */
export async function connectHackRf(config: SourceConfig): Promise<boolean> {
  if (!isWebUsbAvailable) {
    hackrfInfo.error = '当前浏览器不支持 WebUSB API，请使用 Chrome / Edge / Opera 等 Chromium 浏览器。'
    hackrfInfo.status = 'error'
    return false
  }

  try {
    hackrfInfo.status = 'connecting'
    hackrfInfo.error = null

    // Request USB device pair dialog
    const device = await navigator.usb.requestDevice({
      filters: [
        { vendorId: HACKRF_USB_VID, productId: HACKRF_USB_PID },
        { vendorId: 0x1fc9, productId: 0x000c } // LPC DFU
      ]
    })

    if (!device) {
      hackrfInfo.status = 'disconnected'
      return false
    }

    await device.open()

    if (!device.configuration || device.configuration.configurationValue !== 1) {
      try {
        await device.selectConfiguration(1)
      } catch (e) {
        console.warn('selectConfiguration warning:', e)
      }
    }

    try {
      await device.claimInterface(0)
    } catch (e) {
      console.warn('claimInterface warning:', e)
    }

    activeUsbDevice = device
    hackrfInfo.deviceName = device.productName || 'HackRF One'

    // Reset transceiver mode to OFF first
    await setHackRfTransceiverMode(HackRfTransceiverMode.OFF)

    // Read board info
    try {
      const boardIdRes = await device.controlTransferIn({
        requestType: 'vendor',
        recipient: 'device',
        request: HackRfVendorRequest.BOARD_ID_READ,
        value: 0,
        index: 0
      }, 1)
      if (boardIdRes.data && boardIdRes.data.byteLength >= 1) {
        hackrfInfo.boardId = boardIdRes.data.getUint8(0)
        hackrfInfo.boardName = BOARD_NAMES[hackrfInfo.boardId] || `Board #${hackrfInfo.boardId}`
      }
    } catch (e) {
      console.warn('Read Board ID warning:', e)
      hackrfInfo.boardName = 'HackRF One'
    }

    // Read version string
    try {
      const verRes = await device.controlTransferIn({
        requestType: 'vendor',
        recipient: 'device',
        request: HackRfVendorRequest.VERSION_STRING_READ,
        value: 0,
        index: 0
      }, 64)
      if (verRes.data) {
        const bytes = new Uint8Array(verRes.data.buffer, verRes.data.byteOffset, verRes.data.byteLength)
        hackrfInfo.version = new TextDecoder('utf-8').decode(bytes).replace(/\0/g, '').trim()
      }
    } catch (e) {
      console.warn('Read Version warning:', e)
      hackrfInfo.version = 'v2024.x'
    }

    // Read serial number
    try {
      const snRes = await device.controlTransferIn({
        requestType: 'vendor',
        recipient: 'device',
        request: HackRfVendorRequest.BOARD_PARTID_SERIALNO_READ,
        value: 0,
        index: 0
      }, 24)
      if (snRes.data && snRes.data.byteLength >= 24) {
        const s0 = snRes.data.getUint32(8, false).toString(16).padStart(8, '0')
        const s1 = snRes.data.getUint32(12, false).toString(16).padStart(8, '0')
        const s2 = snRes.data.getUint32(16, false).toString(16).padStart(8, '0')
        const s3 = snRes.data.getUint32(20, false).toString(16).padStart(8, '0')
        hackrfInfo.serialNumber = `${s0}${s1}${s2}${s3}`
      }
    } catch (e) {
      console.warn('Read SerialNo warning:', e)
      hackrfInfo.serialNumber = device.serialNumber || '0000000000000000'
    }

    // Apply initial RF parameters safely
    await updateHackRfSampleRate(config.sampleRateHz)
    await updateHackRfFrequency(config.centerFreqHz)
    await updateHackRfLnaGain(config.lnaGain)
    await updateHackRfVgaGain(config.vgaGain)
    await updateHackRfAmp(config.ampEnable)
    await updateHackRfBiasT(config.biasT)

    hackrfInfo.isConnected = true
    hackrfInfo.status = 'connected'
    hackrfInfo.error = null
    return true
  } catch (err: any) {
    console.error('Failed to open HackRF device:', err)
    hackrfInfo.error = err?.message || '无法打开 HackRF 设备，请检查驱动 (WinUSB) 或设备占用。'
    hackrfInfo.status = 'error'
    hackrfInfo.isConnected = false
    activeUsbDevice = null
    return false
  }
}

/**
 * Disconnect HackRF One
 */
export async function disconnectHackRf() {
  await stopHackRfRx()
  if (activeUsbDevice) {
    try {
      await setHackRfTransceiverMode(HackRfTransceiverMode.OFF)
      await activeUsbDevice.releaseInterface(0)
      await activeUsbDevice.close()
    } catch (e) {
      console.warn('Error closing HackRF device:', e)
    }
  }
  activeUsbDevice = null
  hackrfInfo.isConnected = false
  hackrfInfo.isStreaming = false
  hackrfInfo.status = 'disconnected'
  hasLiveHackRfData.value = false
}

/**
 * Set HackRF Transceiver Mode (1 = RX, 0 = OFF)
 */
export async function setHackRfTransceiverMode(mode: HackRfTransceiverMode) {
  if (!activeUsbDevice || !activeUsbDevice.opened) return
  try {
    await activeUsbDevice.controlTransferOut({
      requestType: 'vendor',
      recipient: 'device',
      request: HackRfVendorRequest.SET_TRANSCEIVER_MODE,
      value: mode,
      index: 0
    })
  } catch (err: any) {
    console.warn('setHackRfTransceiverMode warning:', err)
  }
}

/**
 * Update Center Frequency in Hz
 */
export async function updateHackRfFrequency(freqHz: number) {
  if (!activeUsbDevice || !activeUsbDevice.opened) return
  try {
    const buf = new ArrayBuffer(8)
    const view = new DataView(buf)
    const mhz = Math.floor(freqHz / 1e6)
    const hz = Math.round(freqHz % 1e6)
    view.setUint32(0, mhz, true)
    view.setUint32(4, hz, true)
    await activeUsbDevice.controlTransferOut({
      requestType: 'vendor',
      recipient: 'device',
      request: HackRfVendorRequest.SET_FREQ,
      value: 0,
      index: 0
    }, buf)
  } catch (err) {
    console.warn('Failed to set HackRF frequency:', err)
  }
}

/**
 * Update Sample Rate in Hz
 */
export async function updateHackRfSampleRate(sampleRateHz: number) {
  if (!activeUsbDevice || !activeUsbDevice.opened) return
  try {
    const buf = new ArrayBuffer(8)
    const view = new DataView(buf)
    view.setUint32(0, sampleRateHz, true)
    view.setUint32(4, 1, true)
    await activeUsbDevice.controlTransferOut({
      requestType: 'vendor',
      recipient: 'device',
      request: HackRfVendorRequest.SAMPLE_RATE_SET,
      value: 0,
      index: 0
    }, buf)

    // Also auto-set baseband filter
    const bw = computeBasebandFilterBw(Math.round(sampleRateHz * 0.75))
    await updateHackRfFilterBw(bw)
  } catch (err) {
    console.warn('Failed to set HackRF sample rate:', err)
  }
}

/**
 * Update Baseband Filter Bandwidth in Hz
 */
export async function updateHackRfFilterBw(bwHz: number) {
  if (!activeUsbDevice || !activeUsbDevice.opened) return
  try {
    await activeUsbDevice.controlTransferOut({
      requestType: 'vendor',
      recipient: 'device',
      request: HackRfVendorRequest.BASEBAND_FILTER_BANDWIDTH_SET,
      value: bwHz & 0xFFFF,
      index: (bwHz >>> 16) & 0xFFFF
    })
  } catch (err) {
    console.warn('Failed to set HackRF filter bandwidth:', err)
  }
}

/**
 * Update LNA Gain (0 .. 40 dB, step 8) -> IN transfer in libhackrf
 */
export async function updateHackRfLnaGain(lnaGain: number) {
  if (!activeUsbDevice || !activeUsbDevice.opened) return
  try {
    const val = Math.max(0, Math.min(40, lnaGain & ~0x07))
    await activeUsbDevice.controlTransferIn({
      requestType: 'vendor',
      recipient: 'device',
      request: HackRfVendorRequest.SET_LNA_GAIN,
      value: 0,
      index: val
    }, 1)
  } catch (err) {
    console.warn('Failed to set HackRF LNA gain:', err)
  }
}

/**
 * Update VGA Gain (0 .. 62 dB, step 2) -> IN transfer in libhackrf
 */
export async function updateHackRfVgaGain(vgaGain: number) {
  if (!activeUsbDevice || !activeUsbDevice.opened) return
  try {
    const val = Math.max(0, Math.min(62, vgaGain & ~0x01))
    await activeUsbDevice.controlTransferIn({
      requestType: 'vendor',
      recipient: 'device',
      request: HackRfVendorRequest.SET_VGA_GAIN,
      value: 0,
      index: val
    }, 1)
  } catch (err) {
    console.warn('Failed to set HackRF VGA gain:', err)
  }
}

/**
 * Update RF Amp (+14 dB)
 */
export async function updateHackRfAmp(amp: boolean) {
  if (!activeUsbDevice || !activeUsbDevice.opened) return
  try {
    await activeUsbDevice.controlTransferOut({
      requestType: 'vendor',
      recipient: 'device',
      request: HackRfVendorRequest.SET_AMP_ENABLE,
      value: amp ? 1 : 0,
      index: 0
    })
  } catch (err) {
    console.warn('Failed to set HackRF RF amp:', err)
  }
}

/**
 * Update Antenna Bias-T (5V)
 */
export async function updateHackRfBiasT(biasT: boolean) {
  if (!activeUsbDevice || !activeUsbDevice.opened) return
  try {
    await activeUsbDevice.controlTransferOut({
      requestType: 'vendor',
      recipient: 'device',
      request: HackRfVendorRequest.ANTENNA_ENABLE,
      value: biasT ? 1 : 0,
      index: 0
    })
  } catch (err) {
    console.warn('Failed to set HackRF Bias-T:', err)
  }
}

/**
 * Start High-Performance RX Stream
 */
export async function startHackRfRx(onSamples?: (samples: Int8Array) => void) {
  if (!activeUsbDevice || !activeUsbDevice.opened || hackrfInfo.isStreaming) return

  try {
    // Clear any pending halts on endpoint 1 IN
    try {
      await activeUsbDevice.clearHalt('in', 1)
    } catch {}

    await setHackRfTransceiverMode(HackRfTransceiverMode.RECEIVE)
    hackrfInfo.isStreaming = true
    hackrfInfo.status = 'streaming'
    hasLiveHackRfData.value = true

    rxBytesSinceLastInterval = 0

    // Measure throughput every second
    if (rxRateTimer) clearInterval(rxRateTimer)
    rxRateTimer = window.setInterval(() => {
      hackrfInfo.rxRateMBps = +(rxBytesSinceLastInterval / (1024 * 1024)).toFixed(2)
      rxBytesSinceLastInterval = 0
    }, 1000)

    // Continuous USB Bulk Read Loop
    const chunkSize = 65536
    const runRxLoop = async () => {
      while (hackrfInfo.isStreaming && activeUsbDevice && activeUsbDevice.opened) {
        try {
          const res = await activeUsbDevice.transferIn(1, chunkSize)
          if (res.status === 'ok' && res.data && res.data.byteLength > 0) {
            const int8 = new Int8Array(res.data.buffer, res.data.byteOffset, res.data.byteLength)
            hackrfInfo.rxBytesTotal += int8.length
            rxBytesSinceLastInterval += int8.length

            // Compute live FFT
            computeFftFromInt8(int8, liveHackRfFft)

            if (onSamples) {
              onSamples(int8)
            }
          } else if (res.status === 'stall') {
            await activeUsbDevice.clearHalt('in', 1)
          }
        } catch (readErr: any) {
          if (!hackrfInfo.isStreaming) break
          console.warn('USB transfer error:', readErr)
          try {
            await activeUsbDevice.clearHalt('in', 1)
          } catch {}
          await new Promise(r => setTimeout(r, 20))
        }
      }
    }

    runRxLoop()
  } catch (err: any) {
    console.error('Failed to start HackRF RX stream:', err)
    hackrfInfo.error = '启动 HackRF 采样流失败: ' + (err?.message || err)
    hackrfInfo.status = 'error'
    hackrfInfo.isStreaming = false
    hasLiveHackRfData.value = false
  }
}

/**
 * Stop RX Stream
 */
export async function stopHackRfRx() {
  hackrfInfo.isStreaming = false
  if (rxRateTimer) {
    clearInterval(rxRateTimer)
    rxRateTimer = null
  }
  hackrfInfo.rxRateMBps = 0

  if (activeUsbDevice && activeUsbDevice.opened) {
    try {
      await setHackRfTransceiverMode(HackRfTransceiverMode.OFF)
      hackrfInfo.status = 'connected'
    } catch (e) {
      console.warn('Error stopping transceiver mode:', e)
    }
  } else {
    hackrfInfo.status = 'disconnected'
  }
}
