<script setup lang="ts">
import { ref } from 'vue'
import {
  isPlaying,
  togglePlay,
  sourceConfig,
  currentLang,
  totalPacketsCount,
  validCrcCount,
  crcSuccessRate,
  fps,
  isBackendConnected,
  fileErrorNotice,
  adjustCenterFreq
} from '@/composables/useSdrEngine'
import {
  Play,
  Square,
  Activity,
  Radio,
  Globe,
  Cpu,
  Layers,
  Maximize2,
  Zap,
  Terminal,
  AlertTriangle
} from 'lucide-vue-next'
import LogViewerModal from './LogViewerModal.vue'

const showLogModal = ref(false)

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

      <!-- Missing File Warning Toast Alert in TopBar -->
      <div v-if="fileErrorNotice" class="flex items-center gap-1.5 px-2.5 py-1 rounded bg-rose-500/20 border border-rose-500/40 text-rose-300 text-xs animate-bounce font-medium">
        <AlertTriangle class="w-3.5 h-3.5 text-rose-400 shrink-0" />
        <span>{{ fileErrorNotice }}</span>
      </div>

      <!-- Source & C++ Backend Status Badge & Log Trigger -->
      <div class="hidden sm:flex items-center gap-2 bg-sdr-dark/80 border border-sdr-border px-2.5 py-1 rounded text-xs font-mono">
        <Radio class="w-3.5 h-3.5 text-cyan-400" />
        <span class="text-slate-400 font-sans">源:</span>
        <b class="text-slate-200 uppercase">{{ sourceConfig.type }}</b>
        <span class="text-slate-500">|</span>
        <span class="text-emerald-400">{{ (sourceConfig.sampleRateHz / 1e6).toFixed(3) }} MSPS</span>

        <!-- Backend Status Pill -->
        <span class="text-slate-500">|</span>
        <span v-if="isBackendConnected" class="text-amber-400 font-bold flex items-center gap-1">
          <Zap class="w-3 h-3 text-amber-400 animate-pulse" />
          <span>{{ isPlaying ? 'C++ 后端采集中' : 'C++ 后端已连接' }}</span>
        </span>
        <span v-else class="text-rose-400 font-bold flex items-center gap-1">
          <span class="w-1.5 h-1.5 rounded-full bg-rose-500 animate-ping"></span>
          <span>C++ 后端离线</span>
        </span>

        <!-- View Logs Button -->
        <button
          @click="showLogModal = true"
          class="ml-1 px-1.5 py-0.5 bg-slate-800 hover:bg-slate-700 border border-slate-700 text-cyan-300 rounded text-[10px] flex items-center gap-1 transition-colors"
          title="查看 C++ 后端日志"
        >
          <Terminal class="w-3 h-3 text-cyan-400" />
          <span>日志</span>
        </button>
      </div>
    </div>

    <!-- Center: Large Digital Tuner Display with wheel & step support -->
    <div
      class="flex items-center gap-1.5 bg-sdr-dark/95 border border-sdr-border px-2.5 py-1 rounded-lg shadow-inner select-none cursor-pointer"
      @wheel.prevent="(e) => adjustCenterFreq(e.deltaY < 0 ? 100000 : -100000)"
      title="可直接在此处使用鼠标滚轮微调中心频率 (±100kHz)"
    >
      <button
        @click.stop="adjustCenterFreq(-1e6)"
        class="px-1.5 py-0.5 rounded bg-slate-800 hover:bg-slate-700 active:scale-95 text-slate-400 hover:text-cyan-300 text-[10px] font-mono font-bold transition-colors"
        title="-1 MHz"
      >
        -1M
      </button>

      <div class="flex flex-col items-center px-1">
        <span class="text-[9px] text-slate-500 uppercase tracking-widest font-sans font-semibold">中心频率 (Center Freq)</span>
        <div class="flex items-baseline gap-1 font-mono font-bold tracking-wider text-lg text-cyan-300">
          <span>{{ formatFreq(sourceConfig.centerFreqHz) }}</span>
          <span class="text-[10px] text-slate-400 font-sans">MHz</span>
        </div>
      </div>

      <button
        @click.stop="adjustCenterFreq(1e6)"
        class="px-1.5 py-0.5 rounded bg-slate-800 hover:bg-slate-700 active:scale-95 text-slate-400 hover:text-cyan-300 text-[10px] font-mono font-bold transition-colors"
        title="+1 MHz"
      >
        +1M
      </button>
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

  <!-- Log Viewer Modal -->
  <LogViewerModal v-if="showLogModal" @close="showLogModal = false" />
</template>
