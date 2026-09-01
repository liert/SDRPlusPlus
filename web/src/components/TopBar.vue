<script setup lang="ts">
import {
  isPlaying,
  togglePlay,
  sourceConfig,
  currentLang,
  totalPacketsCount,
  validCrcCount,
  crcSuccessRate,
  fps
} from '@/composables/useSdrEngine'
import { Play, Square, Activity, Radio, Volume2, Globe, Cpu } from 'lucide-vue-next'

function formatFreq(freqHz: number): string {
  const mhz = (freqHz / 1e6).toFixed(6)
  const parts = mhz.split('.')
  return `${parts[0]}.${parts[1].slice(0, 3)} ${parts[1].slice(3, 6)}`
}
</script>

<template>
  <header class="h-14 bg-sdr-panel border-b border-sdr-border flex items-center justify-between px-4 select-none shrink-0">
    <!-- Left: Play/Stop & Engine State -->
    <div class="flex items-center gap-3">
      <!-- Play / Stop Button -->
      <button
        @click="togglePlay"
        :class="[
          'flex items-center gap-2 px-4 py-1.5 rounded-lg font-bold text-sm transition-all shadow-md active:scale-95',
          isPlaying
            ? 'bg-rose-500 hover:bg-rose-600 text-white shadow-rose-900/30'
            : 'bg-emerald-500 hover:bg-emerald-600 text-white shadow-emerald-900/30'
        ]"
      >
        <component :is="isPlaying ? Square : Play" class="w-4 h-4 fill-current" />
        <span>{{ isPlaying ? (currentLang === 'zh' ? '停止 (Stop)' : 'Stop') : (currentLang === 'zh' ? '启动 (Play)' : 'Start') }}</span>
      </button>

      <!-- Source Indicator Badge -->
      <div class="flex items-center gap-2 bg-sdr-dark/80 border border-sdr-border px-3 py-1 rounded-md text-xs font-mono">
        <Radio class="w-3.5 h-3.5 text-cyan-400" />
        <span class="text-slate-400 font-sans">源:</span>
        <b class="text-slate-200 uppercase">{{ sourceConfig.type }}</b>
        <span class="text-slate-500">|</span>
        <span class="text-emerald-400">{{ (sourceConfig.sampleRateHz / 1e6).toFixed(3) }} MSPS</span>
      </div>
    </div>

    <!-- Center: Large Digital Tuner Display -->
    <div class="flex items-center gap-3 bg-sdr-dark/90 border border-sdr-border px-5 py-1 rounded-lg shadow-inner">
      <div class="flex flex-col items-center">
        <span class="text-[10px] text-slate-500 uppercase tracking-widest font-sans font-semibold">中心频率 (Center Frequency)</span>
        <div class="flex items-baseline gap-1 font-mono font-bold tracking-wider text-xl text-cyan-300">
          <span>{{ formatFreq(sourceConfig.centerFreqHz) }}</span>
          <span class="text-xs text-slate-400 font-sans">MHz</span>
        </div>
      </div>
    </div>

    <!-- Right: Stats, Health & Language Selector -->
    <div class="flex items-center gap-4 text-xs font-mono">
      <!-- FPS & CRC Health -->
      <div class="flex items-center gap-3 bg-sdr-dark/70 border border-sdr-border px-3 py-1.5 rounded-md">
        <div class="flex items-center gap-1.5 text-slate-400">
          <Activity class="w-3.5 h-3.5 text-emerald-400" />
          <span>FPS: <b class="text-slate-200">{{ fps }}</b></span>
        </div>
        <div class="h-3 w-px bg-sdr-border"></div>
        <div class="flex items-center gap-1.5 text-slate-400">
          <Cpu class="w-3.5 h-3.5 text-blue-400" />
          <span>CRC: <b :class="Number(crcSuccessRate) > 90 ? 'text-emerald-400' : 'text-amber-400'">{{ crcSuccessRate }}%</b></span>
        </div>
      </div>

      <!-- Language Selector -->
      <div class="flex items-center gap-1.5 text-slate-300 bg-sdr-dark/80 border border-sdr-border px-2 py-1 rounded-md">
        <Globe class="w-3.5 h-3.5 text-slate-400" />
        <select v-model="currentLang" class="bg-transparent text-slate-200 focus:outline-none cursor-pointer font-sans text-xs">
          <option value="zh" class="bg-sdr-panel">简体中文</option>
          <option value="en" class="bg-sdr-panel">English</option>
        </select>
      </div>
    </div>
  </header>
</template>
