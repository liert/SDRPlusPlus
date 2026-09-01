<script setup lang="ts">
import { sourceConfig, currentLang } from '@/composables/useSdrEngine'
import { HardDrive, Sliders, Zap, Repeat } from 'lucide-vue-next'

const sampleRateOptions = [
  { label: '2.000 MSPS (2 MHz)', value: 2000000 },
  { label: '4.000 MSPS (4 MHz)', value: 4000000 },
  { label: '8.000 MSPS (8 MHz)', value: 8000000 },
  { label: '10.000 MSPS (10 MHz)', value: 1000000 },
  { label: '12.000 MSPS (12 MHz)', value: 12000000 },
  { label: '20.000 MSPS (20 MHz)', value: 20000000 }
]
</script>

<template>
  <div class="flex flex-col gap-4 text-xs">
    <!-- 1. Source Hardware Type Selector -->
    <div class="flex flex-col gap-1.5">
      <label class="text-slate-400 font-semibold flex items-center gap-1.5">
        <HardDrive class="w-3.5 h-3.5 text-cyan-400" />
        <span>{{ currentLang === 'zh' ? '射频输入源类型 (Source Device)' : 'Source Device' }}</span>
      </label>
      <select
        v-model="sourceConfig.type"
        class="w-full bg-sdr-dark border border-sdr-border rounded-md px-3 py-1.5 text-slate-200 focus:outline-none focus:border-cyan-500 font-sans"
      >
        <option value="file">📁 File Source (本地 IQ / WAV 文件)</option>
        <option value="hackrf">📡 HackRF One (SDR 硬件)</option>
        <option value="rtlsdr">📡 RTL-SDR (DVB-T 硬件)</option>
        <option value="bladerf">📡 BladeRF (全双工 SDR)</option>
        <option value="limesdr">📡 LimeSDR (MIMO SDR)</option>
        <option value="plutosdr">📡 ADALM-PLUTO (IIO 硬件)</option>
        <option value="usrp">📡 Ettus USRP (UHD)</option>
        <option value="simulator">🧪 RF Signal Simulator (信号发生器)</option>
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
    <div v-if="sourceConfig.type === 'file'" class="flex flex-col gap-2 p-3 bg-sdr-dark/60 rounded-md border border-sdr-border">
      <div class="flex items-center justify-between">
        <span class="text-slate-300 font-semibold flex items-center gap-1.5">
          <HardDrive class="w-3.5 h-3.5 text-blue-400" />
          <span>{{ currentLang === 'zh' ? '文件源设置' : 'File Settings' }}</span>
        </span>
        <label class="flex items-center gap-1 text-[11px] text-slate-400 cursor-pointer">
          <input type="checkbox" v-model="sourceConfig.loop" class="rounded bg-sdr-dark border-sdr-border text-blue-500" />
          <span>{{ currentLang === 'zh' ? '循环回放' : 'Loop' }}</span>
        </label>
      </div>

      <!-- File Path Input -->
      <div class="flex flex-col gap-1">
        <label class="text-[11px] text-slate-400">{{ currentLang === 'zh' ? '文件路径 (File Path)' : 'File Path' }}</label>
        <input
          v-model="sourceConfig.filePath"
          class="w-full bg-sdr-panel border border-sdr-border rounded px-2 py-1 text-slate-300 font-mono text-[11px] focus:outline-none focus:border-blue-500"
          placeholder="例如: hackrf/fresh_pairing_2400_8.iq"
        />
      </div>

      <!-- Format Mode -->
      <div class="flex flex-col gap-1">
        <label class="text-[11px] text-slate-400">{{ currentLang === 'zh' ? '数据编码格式' : 'Format Mode' }}</label>
        <select
          v-model="sourceConfig.fileFormat"
          class="w-full bg-sdr-panel border border-sdr-border rounded px-2 py-1 text-slate-200 text-xs focus:outline-none focus:border-blue-500"
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
