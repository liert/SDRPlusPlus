<script setup lang="ts">
import { ref } from 'vue'
import { sourceConfig, currentLang, isPlaying } from '@/composables/useSdrEngine'
import { HardDrive, Sliders, FolderOpen, CheckCircle2, AlertCircle } from 'lucide-vue-next'

const fileInputRef = ref<HTMLInputElement | null>(null)
const selectedFileName = ref<string>(sourceConfig.filePath || 'fresh_pairing_2400_8.iq')
const selectedFileSize = ref<string>('91.4 MB')
const fileLoaded = ref(true)

const sampleRateOptions = [
  { label: '2.000 MSPS (2 MHz)', value: 2000000 },
  { label: '4.000 MSPS (4 MHz)', value: 4000000 },
  { label: '8.000 MSPS (8 MHz)', value: 8000000 },
  { label: '10.000 MSPS (10 MHz)', value: 1000000 },
  { label: '12.000 MSPS (12 MHz)', value: 12000000 },
  { label: '20.000 MSPS (20 MHz)', value: 20000000 }
]

function triggerFilePicker() {
  if (fileInputRef.value) {
    fileInputRef.value.click()
  }
}

function onFileSelected(e: Event) {
  const target = e.target as HTMLInputElement
  if (target.files && target.files.length > 0) {
    const file = target.files[0]
    selectedFileName.value = file.name
    sourceConfig.filePath = file.name
    selectedFileSize.value = (file.size / (1024 * 1024)).toFixed(1) + ' MB'
    fileLoaded.value = true

    // Auto detect format by extension
    const ext = file.name.split('.').pop()?.toLowerCase()
    if (ext === 'iq' || ext === 'raw') {
      sourceConfig.fileFormat = 'raw_int8'
    } else if (ext === 'wav') {
      sourceConfig.fileFormat = 'wav'
    } else if (ext === 'bin') {
      sourceConfig.fileFormat = 'raw_int8'
    }
  }
}
</script>

<template>
  <div class="flex flex-col gap-4 text-xs">
    <!-- Hidden File Input for Native OS Dialog -->
    <input
      type="file"
      ref="fileInputRef"
      accept=".iq,.wav,.bin,.raw,*"
      @change="onFileSelected"
      class="hidden"
    />

    <!-- 1. Source Hardware Type Selector -->
    <div class="flex flex-col gap-1.5">
      <label class="text-slate-400 font-semibold flex items-center gap-1.5">
        <HardDrive class="w-3.5 h-3.5 text-cyan-400" />
        <span>{{ currentLang === 'zh' ? '射频输入源类型 (Source Device)' : 'Source Device' }}</span>
      </label>
      <select
        v-model="sourceConfig.type"
        class="w-full bg-sdr-dark border border-sdr-border rounded-md px-3 py-1.5 text-slate-200 focus:outline-none focus:border-cyan-500 font-sans font-medium"
      >
        <option value="file">📁 File Source (本地 IQ / WAV 录音文件)</option>
        <option value="hackrf">📡 HackRF One (SDR 硬件)</option>
        <option value="rtlsdr">📡 RTL-SDR (DVB-T 硬件)</option>
        <option value="bladerf">📡 BladeRF (全双工 SDR)</option>
        <option value="limesdr">📡 LimeSDR (MIMO SDR)</option>
        <option value="plutosdr">📡 ADALM-PLUTO (IIO 硬件)</option>
        <option value="usrp">📡 Ettus USRP (UHD)</option>
        <option value="simulator">🧪 RF Signal Simulator (仿真发生器)</option>
      </select>
    </div>

    <!-- 2. Tuning & Sample Rate -->
    <div class="grid grid-cols-2 gap-3">
      <!-- Center Frequency Input -->
      <div class="flex flex-col gap-1">
        <label class="text-slate-400 font-semibold">{{ currentLang === 'zh' ? '中心频率 (MHz)' : 'Center Freq (MHz)' }}</label>
        <input
          type="number"
          step="0.001"
          :value="(sourceConfig.centerFreqHz / 1e6).toFixed(3)"
          @input="(e: any) => sourceConfig.centerFreqHz = Math.round(Number(e.target.value) * 1e6)"
          class="w-full bg-sdr-dark border border-sdr-border rounded px-2.5 py-1 text-slate-200 font-mono focus:outline-none focus:border-cyan-500"
        />
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

    <!-- 3. File Source Specific Controls -->
    <div v-if="sourceConfig.type === 'file'" class="flex flex-col gap-3 p-3 bg-sdr-dark/60 rounded-md border border-sdr-border">
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
        <span>{{ currentLang === 'zh' ? '📂 打开文件选择窗口...' : 'Browse File...' }}</span>
      </button>

      <!-- Loaded File Status Card -->
      <div class="p-2 bg-sdr-panel rounded border border-sdr-border flex flex-col gap-1 text-[11px] font-mono">
        <div class="flex items-center justify-between">
          <span class="text-slate-400 flex items-center gap-1">
            <component :is="fileLoaded ? CheckCircle2 : AlertCircle" :class="['w-3.5 h-3.5', fileLoaded ? 'text-emerald-400' : 'text-amber-400']" />
            <span class="font-sans font-bold">{{ fileLoaded ? (currentLang === 'zh' ? '文件就绪' : 'File Ready') : 'No file' }}</span>
          </span>
          <span class="text-slate-400 font-mono">{{ selectedFileSize }}</span>
        </div>
        <div class="text-cyan-300 font-bold truncate select-text" :title="selectedFileName">
          {{ selectedFileName }}
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

    <!-- 4. Hardware Gain Controls (HackRF / RTL / etc.) -->
    <div v-else class="flex flex-col gap-3 p-3 bg-sdr-dark/60 rounded-md border border-sdr-border">
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
  </div>
</template>
