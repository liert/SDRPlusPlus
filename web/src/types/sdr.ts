export type SourceType = 'hackrf' | 'rtlsdr' | 'bladerf' | 'limesdr' | 'plutosdr' | 'usrp' | 'file' | 'simulator'

export type FileFormat = 'raw_int8' | 'raw_int16' | 'raw_float32' | 'wav'

export type ModulationType = 'FLRC' | '2FSK' | 'GFSK' | 'CPFSK' | '4FSK' | 'OOK' | 'AM' | 'FM'

export type MaskMode = 'auto' | '0x99' | '0x66' | 'none' | 'custom'

export interface VfoState {
  id: string
  name: string
  offsetHz: number
  bandwidthHz: number
  color: string
  enabled: boolean
}

export interface SourceConfig {
  type: SourceType
  centerFreqHz: number
  sampleRateHz: number
  lnaGain: number
  vgaGain: number
  ampEnable: boolean
  biasT: boolean
  filePath: string
  fileFormat: FileFormat
  loop: boolean
}

export interface DemodConfig {
  modulation: ModulationType
  preset: string
  symbolRate: number
  deviation: number
  filterCutoff: number
  enableAgcPreamble: boolean
  agcThreshold: number
  enableTimingPreamble: boolean
  timingTolerance: number
  autoSyncWord: boolean
  syncWord: string
  maskMode: MaskMode
  customMask: number
  differentialDecode: boolean
  enableHwCrc: boolean
  defaultPayloadLen: number
}

export interface DecodedPacket {
  id: number
  timestamp: string
  freqOffsetKhz: number
  syncWord: string
  mask: string
  payloadHex: string
  payloadAscii: string
  hwCrc: string
  crcValid: boolean
  score: number
  length: number
}

export interface SpectrumSettings {
  minDb: number
  maxDb: number
  fftSize: number
  smoothing: number
  colormap: 'turbo' | 'viridis' | 'plasma' | 'electric' | 'greyscale'
  fillSpectrum: boolean
  peakHold: boolean
}
