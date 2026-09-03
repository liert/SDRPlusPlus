<script setup lang="ts">
import { ref, watch } from 'vue'
import { invoke } from '@tauri-apps/api/core'
import {
  sourceConfig,
  currentLang,
  isPlaying,
  isBackendConnected,
  availableSources,
  availableDevices,
  isScanningDevices,
  refreshBackendDevices,
  sendBackendCommand,
  fileErrorNotice,
  setCenterFreq,
  adjustCenterFreq,
  setSampleRate,
  logUi
} from '@/composables/useSdrEngine'
import {
  HardDrive,
  Sliders,
  FolderOpen,
  CheckCircle2,
  AlertCircle,
  Radio,
  Cpu,
  RefreshCw,
  Zap,
  Check,
  AlertTriangle,
  Play,
  Plus,
  Minus
} from 'lucide-vue-next'

const selectedFileName = ref<string>(sourceConfig.filePath || '')
const manualPathInput = ref<string>(sourceConfig.filePath || '')
const fileLoaded = ref(true)

const isEditingFreq = ref(false)
const freqInputMhz = ref<string>((sourceConfig.centerFreqHz / 1e6).toFixed(3))

watch(() => sourceConfig.centerFreqHz, (newHz) => {
  if (!isEditingFreq.value) {
    freqInputMhz.value = (newHz / 1e6).toFixed(3)
  }
})

function submitFreqChange() {
  const mhz = parseFloat(freqInputMhz.value)
  if (!isNaN(mhz) && mhz > 0) {
    setCenterFreq(mhz * 1e6)
    freqInputMhz.value = (sourceConfig.centerFreqHz / 1e6).toFixed(3)
  } else {
    freqInputMhz.value = (sourceConfig.centerFreqHz / 1e6).toFixed(3)
  }
  isEditingFreq.value = false
}

const sampleRateOptions = [
  { label: '2 MHz', value: 2000000 },
  { label: '4 MHz', value: 4000000 },
  { label: '8 MHz', value: 8000000 },
  { label: '10 MHz', value: 10000000 },
  { label: '12 MHz', value: 12000000 },
  { label: '20 MHz', value: 20000000 }
]

function parseFilenameParams(fullPath: string) {
  const norm = fullPath.replace(/\\/g, '/')
  const filename = norm.split('/').pop() || ''
  
  // Detect frequency like _2403_8 or 2403MHz
  const matchFreq = filename.match(/_([0-9]{4,5})_/)
  if (matchFreq && matchFreq[1]) {
    const mhz = parseFloat(matchFreq[1])
    if (mhz >= 100 && mhz <= 6000) {
      sourceConfig.centerFreqHz = Math.round(mhz * 1e6)
      logUi(`Auto-detected Center Frequency from filename: ${mhz} MHz`)
    }
  }

  // Detect sample rate like _8.iq or _8M.iq
  const matchSr = filename.match(/_[0-9]+_([0-9]{1,2})\./) || filename.match(/_([0-9]+)M(?:SPS)?\./i)
  if (matchSr && matchSr[1]) {
    const msps = parseFloat(matchSr[1])
    if (msps >= 1 && msps <= 60) {
      sourceConfig.sampleRateHz = Math.round(msps * 1e6)
      logUi(`Auto-detected Sample Rate from filename: ${msps} MSPS`)
    }
  }

  // Detect format
  const ext = filename.split('.').pop()?.toLowerCase()
  if (ext === 'iq' || ext === 'raw' || ext === 'bin') {
    sourceConfig.fileFormat = 'raw_int8'
  } else if (ext === 'wav') {
    sourceConfig.fileFormat = 'wav'
  }
}

async function triggerFilePicker() {
  try {
    const chosen = await invoke<string | null>('open_iq_file_dialog')
    if (chosen) {
      manualPathInput.value = chosen
      applyFilePath(chosen)
    }
  } catch (err: any) {
    logUi(`open_iq_file_dialog error: ${err?.message || err}`, 'ERROR')
  }
}

function applyFilePath(path: string) {
  if (!path || !path.trim()) return
  const cleanPath = path.trim().replace(/^["']|["']$/g, '') // remove surrounding quotes
  sourceConfig.filePath = cleanPath
  selectedFileName.value = cleanPath
  manualPathInput.value = cleanPath
  fileLoaded.value = true

  parseFilenameParams(cleanPath)

  if (sourceConfig.type === 'file') {
    sendBackendCommand('set_source', {
      source: 'File Source',
      path: cleanPath
    })
  }
}
</script>

<template>
  <div class="flex flex-col gap-4 text-xs">
    <!-- 1. Source Hardware Type Selector -->
    <div class="flex flex-col gap-1.5">
      <label class="text-slate-400 font-semibold flex items-center justify-between">
        <span class="flex items-center gap-1.5">
          <HardDrive class="w-3.5 h-3.5 text-cyan-400" />
          <span>{{ currentLang === 'zh' ? '信号源类型 (Source Manager)' : 'Source Manager' }}</span>
        </span>
        <span :class="['text-[10px] px-1.5 py-0.5 rounded border font-mono flex items-center gap-1', isBackendConnected ? 'bg-amber-500/10 text-amber-400 border-amber-500/20' : 'bg-slate-800 text-slate-400 border-slate-700']">
          <span :class="['w-1.5 h-1.5 rounded-full', isBackendConnected ? 'bg-amber-400 animate-pulse' : 'bg-slate-500']"></span>
          <span>{{ isBackendConnected ? 'C++ 后端就绪' : '离线模式' }}</span>
        </span>
      </label>
      <select
        v-model="sourceConfig.type"
        class="w-full bg-sdr-dark border border-sdr-border rounded-md px-3 py-1.5 text-slate-200 focus:outline-none focus:border-cyan-500 font-sans font-medium"
      >
        <option value="hackrf">HackRF</option>
        <option value="file">File</option>
        <option value="rtlsdr">RTL-SDR</option>
        <option value="bladerf">BladeRF</option>
        <option value="limesdr">LimeSDR</option>
        <option value="plutosdr">ADALM-PLUTO</option>
        <option value="simulator">RF Signal Simulator</option>
      </select>
    </div>

    <!-- 2. Hardware Device Selector (HackRF / RTL-SDR) -->
    <div v-if="sourceConfig.type === 'hackrf' || sourceConfig.type === 'rtlsdr'" class="flex flex-col gap-1.5 p-3 bg-sdr-dark/60 rounded-md border border-sdr-border">
      <div class="flex items-center justify-between">
        <label class="text-slate-300 font-bold flex items-center gap-1.5">
          <Cpu class="w-3.5 h-3.5 text-cyan-400" />
          <span>{{ currentLang === 'zh' ? '硬件设备管理 (Device Manager)' : 'Device Manager' }}</span>
        </label>
        
        <button
          @click="refreshBackendDevices"
          :disabled="isScanningDevices"
          class="flex items-center gap-1 text-[11px] text-cyan-400 hover:text-cyan-300 bg-cyan-500/10 hover:bg-cyan-500/20 px-2 py-0.5 rounded border border-cyan-500/30 transition-all disabled:opacity-50"
          title="扫描 USB 物理设备列表"
        >
          <RefreshCw :class="['w-3 h-3', isScanningDevices ? 'animate-spin' : '']" />
          <span>{{ isScanningDevices ? (currentLang === 'zh' ? '正在扫描...' : 'Scanning...') : (currentLang === 'zh' ? '刷新' : 'Refresh') }}</span>
        </button>
      </div>

      <!-- Device Dropdown Selector -->
      <div class="flex flex-col gap-1 pt-1">
        <select
          v-model="sourceConfig.deviceSerial"
          class="w-full bg-sdr-panel border border-sdr-border rounded px-2.5 py-1.5 text-slate-200 text-xs focus:outline-none focus:border-cyan-500 font-mono"
        >
          <option v-if="availableDevices.length === 0" value="">
            {{ isBackendConnected ? '未检测到设备' : 'C++ 后端未运行' }}
          </option>
          <option v-for="dev in availableDevices" :key="dev.serial" :value="dev.serial">
            {{ dev.name || 'HackRF One' }} (SN: {{ dev.serial.slice(0, 8) }}...)
          </option>
        </select>
      </div>

      <!-- Device Information Card -->
      <div class="p-2 bg-sdr-panel/80 rounded border border-sdr-border/60 flex flex-col gap-1 text-[11px] font-mono text-slate-300">
        <div class="flex justify-between">
          <span class="text-slate-400">驱动后端:</span>
          <span class="text-emerald-400 font-bold">C++ libhackrf / DSP</span>
        </div>
        <div class="flex justify-between">
          <span class="text-slate-400">设备状态:</span>
          <span :class="isBackendConnected ? 'text-amber-400 font-bold' : 'text-slate-400'">
            {{ isBackendConnected ? (isPlaying ? '● 正在高速采集中' : '● 设备就绪 (待机)') : '○ 离线' }}
          </span>
        </div>
      </div>
    </div>

    <!-- 3. Tuning & Sample Rate -->
    <div class="grid grid-cols-2 gap-3">
      <!-- Center Frequency Input -->
      <div class="flex flex-col gap-1">
        <div class="flex justify-between items-center text-slate-400 font-semibold text-[11px]">
          <span>{{ currentLang === 'zh' ? '中心频率 (MHz)' : 'Center Freq (MHz)' }}</span>
          <div class="flex items-center gap-1 font-mono text-[10px]">
            <button @click="adjustCenterFreq(-1e6)" class="px-1 bg-slate-800 hover:bg-slate-700 rounded text-slate-300" title="-1 MHz">-1M</button>
            <button @click="adjustCenterFreq(1e6)" class="px-1 bg-slate-800 hover:bg-slate-700 rounded text-slate-300" title="+1 MHz">+1M</button>
          </div>
        </div>
        <div class="flex items-center gap-1">
          <input
            type="number"
            step="0.001"
            v-model="freqInputMhz"
            @focus="isEditingFreq = true"
            @blur="submitFreqChange"
            @keydown.enter="submitFreqChange"
            class="w-full bg-sdr-dark border border-sdr-border rounded px-2.5 py-1 text-slate-200 font-mono text-xs focus:outline-none focus:border-cyan-500"
          />
        </div>
      </div>

      <!-- Sample Rate -->
      <div class="flex flex-col gap-1">
        <label class="text-slate-400 font-semibold">{{ currentLang === 'zh' ? '采样率 (MSPS)' : 'Sample Rate' }}</label>
        <select
          v-model="sourceConfig.sampleRateHz"
          class="w-full bg-sdr-dark border border-sdr-border rounded px-2.5 py-1 text-slate-200 font-mono focus:outline-none focus:border-cyan-500"
        >
          <option v-for="opt in sampleRateOptions" :key="opt.value" :value="opt.value">
            {{ opt.label }}
          </option>
        </select>
      </div>
    </div>

    <!-- 4. Hardware Gain Controls (HackRF / RTL / etc.) -->
    <div v-if="sourceConfig.type !== 'file' && sourceConfig.type !== 'simulator'" class="flex flex-col gap-3 p-3 bg-sdr-dark/60 rounded-md border border-sdr-border">
      <div class="flex items-center gap-1.5 text-slate-300 font-semibold">
        <Sliders class="w-3.5 h-3.5 text-emerald-400" />
        <span>{{ currentLang === 'zh' ? '射频增益调节 (RF Gains)' : 'RF Gain Controls' }}</span>
      </div>

      <!-- LNA Gain -->
      <div class="flex flex-col gap-1">
        <div class="flex justify-between text-slate-400 text-[11px]">
          <span>LNA Gain (低噪放)</span>
          <b class="text-slate-200 font-mono">{{ sourceConfig.lnaGain }} dB</b>
        </div>
        <input type="range" min="0" max="40" step="8" v-model.number="sourceConfig.lnaGain" class="w-full accent-cyan-500" />
      </div>

      <!-- VGA Gain -->
      <div class="flex flex-col gap-1">
        <div class="flex justify-between text-slate-400 text-[11px]">
          <span>VGA Gain (可变增益)</span>
          <b class="text-slate-200 font-mono">{{ sourceConfig.vgaGain }} dB</b>
        </div>
        <input type="range" min="0" max="62" step="2" v-model.number="sourceConfig.vgaGain" class="w-full accent-cyan-500" />
      </div>

      <!-- Amp & Bias-T Switches -->
      <div class="grid grid-cols-2 gap-2 pt-1">
        <label class="flex items-center gap-1.5 cursor-pointer text-slate-300">
          <input type="checkbox" v-model="sourceConfig.ampEnable" class="rounded bg-sdr-dark border-sdr-border text-cyan-500" />
          <span>RF Amp (+14dB)</span>
        </label>
        <label class="flex items-center gap-1.5 cursor-pointer text-slate-300">
          <input type="checkbox" v-model="sourceConfig.biasT" class="rounded bg-sdr-dark border-sdr-border text-cyan-500" />
          <span>Bias-T (5V)</span>
        </label>
      </div>
    </div>

    <!-- 5. File Source Specific Controls -->
    <div v-else-if="sourceConfig.type === 'file'" class="flex flex-col gap-3 p-3 bg-sdr-dark/60 rounded-md border border-sdr-border">
      <!-- Error Warning Banner if file missing -->
      <div v-if="fileErrorNotice || !sourceConfig.filePath" class="p-2.5 rounded-lg bg-rose-500/10 border border-rose-500/30 flex items-start gap-2 text-rose-300 text-[11px]">
        <AlertTriangle class="w-4 h-4 text-rose-400 shrink-0 mt-0.5" />
        <div class="flex flex-col">
          <span class="font-bold">{{ fileErrorNotice || (currentLang === 'zh' ? '未载入任何 IQ 文件' : 'No IQ file loaded') }}</span>
          <span class="text-slate-400 text-[10px] mt-0.5">{{ currentLang === 'zh' ? '请点击下方按钮选择 .iq 文件或粘贴完整路径后再启动采集。' : 'Please select an .iq file or enter a valid path.' }}</span>
        </div>
      </div>

      <div class="flex items-center justify-between">
        <span class="text-slate-300 font-semibold flex items-center gap-1.5">
          <HardDrive class="w-3.5 h-3.5 text-blue-400" />
          <span>{{ currentLang === 'zh' ? '文件源设置' : 'File Settings' }}</span>
        </span>

        <!-- Loop Playback Switch -->
        <label class="flex items-center gap-1.5 text-xs text-slate-300 cursor-pointer bg-sdr-panel px-2 py-0.5 rounded border border-sdr-border">
          <input type="checkbox" v-model="sourceConfig.loop" class="rounded bg-sdr-dark border-sdr-border text-blue-500" />
          <span :class="sourceConfig.loop ? 'text-cyan-400 font-bold' : 'text-slate-400'">
            {{ currentLang === 'zh' ? '循环回放 (Loop)' : 'Loop' }}
          </span>
        </label>
      </div>

      <!-- File Browse Action Button -->
      <button
        @click="triggerFilePicker"
        class="w-full py-2 px-3 bg-blue-600 hover:bg-blue-500 active:scale-98 text-white rounded-md font-bold flex items-center justify-center gap-2 shadow-lg shadow-blue-900/30 transition-all text-xs"
      >
        <FolderOpen class="w-4 h-4" />
        <span>{{ currentLang === 'zh' ? '📂 打开系统文件选择窗口 (Native Dialog)...' : 'Browse File...' }}</span>
      </button>

      <!-- Manual File Path Input & Apply -->
      <div class="flex flex-col gap-1">
        <label class="text-[10px] text-slate-400">完整文件路径 (可直接粘贴或修改):</label>
        <div class="flex gap-1.5">
          <input
            type="text"
            v-model="manualPathInput"
            @keydown.enter="applyFilePath(manualPathInput)"
            placeholder="如 C:\path\to\file.iq"
            class="flex-1 bg-slate-900 border border-slate-700 rounded px-2 py-1 text-slate-200 font-mono text-[11px] focus:outline-none focus:border-blue-500"
          />
          <button
            @click="applyFilePath(manualPathInput)"
            class="px-2.5 py-1 bg-slate-800 hover:bg-slate-700 active:scale-95 text-cyan-300 border border-slate-700 rounded text-[11px] font-bold"
          >
            载入
          </button>
        </div>
      </div>

      <!-- Loaded File Status Card -->
      <div class="p-2 bg-sdr-panel rounded border border-sdr-border flex flex-col gap-1 text-[11px] font-mono">
        <div class="flex items-center justify-between">
          <span class="text-slate-400 flex items-center gap-1">
            <component :is="fileLoaded ? CheckCircle2 : AlertCircle" :class="['w-3.5 h-3.5', fileLoaded ? 'text-emerald-400' : 'text-amber-400']" />
            <span class="font-sans font-bold">{{ fileLoaded ? (currentLang === 'zh' ? '文件就绪' : 'File Ready') : 'No file' }}</span>
          </span>
        </div>
        <div class="text-cyan-300 font-bold break-all select-text text-[10px]" :title="selectedFileName">
          {{ selectedFileName || '未选择文件' }}
        </div>
      </div>

      <!-- Format Mode Selection -->
      <div class="flex flex-col gap-1">
        <label class="text-[11px] text-slate-400 font-semibold">{{ currentLang === 'zh' ? '数据编码格式 (Format)' : 'Format Mode' }}</label>
        <select
          v-model="sourceConfig.fileFormat"
          class="w-full bg-sdr-panel border border-sdr-border rounded px-2.5 py-1.5 text-slate-200 text-xs focus:outline-none focus:border-blue-500 font-sans font-medium"
        >
          <option value="raw_int8">Raw Signed-Int8 (HackRF 8-bit)</option>
          <option value="raw_float32">Raw Float32 (32-bit Complex)</option>
          <option value="raw_int16">Raw Int16 (16-bit PCM)</option>
          <option value="wav">WAV (RIFF Header)</option>
        </select>
      </div>
    </div>
  </div>
</template>
