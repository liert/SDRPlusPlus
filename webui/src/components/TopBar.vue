<script setup lang="ts">
import {
  isPlaying,
  togglePlay,
  sourceConfig,
  currentLang,
  totalPacketsCount,
  validCrcCount,
  crcSuccessRate,
  fps,
  isBackendConnected
} from '@/composables/useSdrEngine'
import { hackrfInfo } from '@/composables/useHackRf'
import {
  Play,
  Square,
  Activity,
  Radio,
  Globe,
  Cpu,
  Layers,
  Maximize2,
  Usb,
  Zap
} from 'lucide-vue-next'

const props = defineProps<{
  activeView: 'rf' | 'protocol' | 'split'
}>()

const emit = defineEmits<{
  (e: 'change-view', view: 'rf' | 'protocol' | 'split'): void
}>()

function formatFreq(freqHz: number): string {
  const mhz = (freqHz / 1e6).toFixed(6)
  const parts = mhz.split('.')
  return `${parts[0]}.${parts[1].slice(0, 3)} ${parts[1].slice(3, 6)}`
}
</script>

<template>
  <header class="h-13 bg-sdr-panel border-b border-sdr-border flex items-center justify-between px-3 select-none shrink-0 gap-3">
    <!-- Left: Play/Stop & Source Indicator -->
    <div class="flex items-center gap-2.5">
      <!-- Play / Stop Button -->
      <button
        @click="togglePlay"
        :class="[
          'flex items-center gap-2 px-3.5 py-1.5 rounded-md font-bold text-xs transition-all shadow-md active:scale-95',
          isPlaying
            ? 'bg-rose-500 hover:bg-rose-600 text-white shadow-rose-900/30'
            : 'bg-emerald-500 hover:bg-emerald-600 text-white shadow-emerald-900/30'
        ]"
      >
        <component :is="isPlaying ? Square : Play" class="w-3.5 h-3.5 fill-current" />
        <span>{{ isPlaying ? (currentLang === 'zh' ? '停止 (Stop)' : 'Stop') : (currentLang === 'zh' ? '启动 (Play)' : 'Start') }}</span>
      </button>

      <!-- Source & C++ Backend Status Badge -->
      <div class="hidden sm:flex items-center gap-2 bg-sdr-dark/80 border border-sdr-border px-2.5 py-1 rounded text-xs font-mono">
        <component :is="sourceConfig.type === 'hackrf' ? Usb : Radio" class="w-3.5 h-3.5 text-cyan-400" />
        <span class="text-slate-400 font-sans">源:</span>
        <b class="text-slate-200 uppercase">{{ sourceConfig.type }}</b>
        <span class="text-slate-500">|</span>
        <span class="text-emerald-400">{{ (sourceConfig.sampleRateHz / 1e6).toFixed(3) }} MSPS</span>

        <!-- Backend or WebUSB Pill -->
        <span class="text-slate-500">|</span>
        <span v-if="isBackendConnected" class="text-amber-400 font-bold flex items-center gap-1">
          <Zap class="w-3 h-3 text-amber-400 animate-pulse" />
          <span>C++ 后端推流中</span>
        </span>
        <span v-else-if="sourceConfig.type === 'hackrf'" :class="hackrfInfo.isConnected ? 'text-cyan-400 font-bold' : 'text-slate-500'">
          {{ hackrfInfo.isConnected ? (hackrfInfo.isStreaming ? '● WebUSB 采集中' : '● WebUSB 已连接') : '○ WebUSB 未连接' }}
        </span>
        <span v-else class="text-blue-400">
          ● 本地文件
        </span>
      </div>
    </div>

    <!-- Center: Large Digital Tuner Display -->
    <div class="flex items-center gap-3 bg-sdr-dark/95 border border-sdr-border px-4 py-1 rounded-lg shadow-inner">
      <div class="flex flex-col items-center">
        <span class="text-[9px] text-slate-500 uppercase tracking-widest font-sans font-semibold">中心频率 (Center Freq)</span>
        <div class="flex items-baseline gap-1 font-mono font-bold tracking-wider text-lg text-cyan-300">
          <span>{{ formatFreq(sourceConfig.centerFreqHz) }}</span>
          <span class="text-[10px] text-slate-400 font-sans">MHz</span>
        </div>
      </div>
    </div>

    <!-- Right: View Switcher, Stats & Language -->
    <div class="flex items-center gap-2.5 text-xs font-mono">
      <!-- Primary View Switcher Tabs -->
      <div class="flex bg-sdr-dark border border-sdr-border rounded-md p-0.5 gap-0.5 font-sans">
        <button
          @click="emit('change-view', 'rf')"
          :class="[
            'px-2.5 py-1 rounded text-xs font-medium transition-colors flex items-center gap-1',
            activeView === 'rf' ? 'bg-blue-600 text-white font-bold shadow' : 'text-slate-400 hover:text-slate-200'
          ]"
        >
          <Radio class="w-3 h-3" />
          <span>{{ currentLang === 'zh' ? '🌊 射频全景' : 'RF Studio' }}</span>
        </button>

        <button
          @click="emit('change-view', 'protocol')"
          :class="[
            'px-2.5 py-1 rounded text-xs font-medium transition-colors flex items-center gap-1',
            activeView === 'protocol' ? 'bg-blue-600 text-white font-bold shadow' : 'text-slate-400 hover:text-slate-200'
          ]"
        >
          <Layers class="w-3 h-3" />
          <span>{{ currentLang === 'zh' ? '🧩 协议工作台' : 'Protocol' }}</span>
        </button>

        <button
          @click="emit('change-view', 'split')"
          :class="[
            'px-2 py-1 rounded text-xs font-medium transition-colors flex items-center gap-1',
            activeView === 'split' ? 'bg-blue-600 text-white font-bold shadow' : 'text-slate-400 hover:text-slate-200'
          ]"
          :title="currentLang === 'zh' ? '分屏联合视图' : 'Split View'"
        >
          <Maximize2 class="w-3 h-3" />
          <span>{{ currentLang === 'zh' ? '联合' : 'Split' }}</span>
        </button>
      </div>

      <!-- Live FPS & Health Indicators -->
      <div class="hidden md:flex items-center gap-2.5 bg-sdr-dark/70 border border-sdr-border px-2.5 py-1 rounded">
        <div class="flex items-center gap-1 text-slate-400">
          <Activity class="w-3.5 h-3.5 text-emerald-400" />
          <span><b class="text-slate-200">{{ fps }}</b> FPS</span>
        </div>
      </div>

      <!-- Language Selector -->
      <div class="flex items-center gap-1 text-slate-300 bg-sdr-dark/80 border border-sdr-border px-2 py-1 rounded">
        <Globe class="w-3 h-3 text-slate-400" />
        <select v-model="currentLang" class="bg-transparent text-slate-200 focus:outline-none cursor-pointer font-sans text-[11px]">
          <option value="zh" class="bg-sdr-panel">简体中文</option>
          <option value="en" class="bg-sdr-panel">English</option>
        </select>
      </div>
    </div>
  </header>
</template>
