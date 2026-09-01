<script setup lang="ts">
import { ref } from 'vue'
import {
  packetHistory,
  totalPacketsCount,
  validCrcCount,
  crcSuccessRate,
  currentLang,
  isVfoLockedOnSignal,
  checkVfoSignalLock,
  isPlaying
} from '@/composables/useSdrEngine'
import { ChevronRight, ChevronLeft, Activity, ShieldCheck, Maximize2, Zap, Radio, AlertTriangle } from 'lucide-vue-next'

const emit = defineEmits<{
  (e: 'open-full-workbench'): void
}>()

const isCollapsed = ref(false)
</script>

<template>
  <div
    :class="[
      'absolute right-3 top-16 z-30 transition-all duration-300 flex select-none',
      isCollapsed ? 'translate-x-[calc(100%-28px)]' : 'translate-x-0'
    ]"
  >
    <!-- Toggle Collapse Tab Button -->
    <button
      @click="isCollapsed = !isCollapsed"
      class="w-7 h-28 my-auto bg-sdr-panel/90 hover:bg-slate-800 border-l border-y border-sdr-border rounded-l-md flex flex-col items-center justify-center gap-1 text-slate-400 hover:text-cyan-400 shadow-xl backdrop-blur-md"
      title="折叠/展开协议 HUD"
    >
      <component :is="isCollapsed ? ChevronLeft : ChevronRight" class="w-4 h-4" />
      <span class="text-[10px] font-sans font-bold [writing-mode:vertical-lr] tracking-widest text-slate-300">
        {{ currentLang === 'zh' ? '协议监视' : 'PROTOCOL' }}
      </span>
    </button>

    <!-- Main HUD Box -->
    <div class="w-84 h-[490px] bg-sdr-panel/95 backdrop-blur-xl border border-sdr-border rounded-r-lg shadow-2xl flex flex-col overflow-hidden" style="width: 330px;">
      <!-- Header -->
      <div class="p-2.5 bg-sdr-dark/80 border-b border-sdr-border flex items-center justify-between">
        <div class="flex items-center gap-2">
          <Zap class="w-3.5 h-3.5 text-amber-400" />
          <span class="text-xs font-bold text-slate-200">
            {{ currentLang === 'zh' ? '实时协议简报' : 'Protocol HUD' }}
          </span>
        </div>

        <button
          @click="emit('open-full-workbench')"
          class="flex items-center gap-1 text-[11px] text-blue-400 hover:text-blue-300 bg-blue-500/10 hover:bg-blue-500/20 px-2 py-0.5 rounded border border-blue-500/30 transition-colors"
          title="打开全屏深度协议工作台"
        >
          <Maximize2 class="w-3 h-3" />
          <span>{{ currentLang === 'zh' ? '全屏分析' : 'Full Workbench' }}</span>
        </button>
      </div>

      <!-- VFO Real-time Passband Signal Lock State Badge -->
      <div class="p-2 bg-sdr-dark/90 border-b border-sdr-border flex items-center justify-between text-xs font-mono">
        <div class="flex items-center gap-1.5">
          <Radio :class="['w-3.5 h-3.5', isVfoLockedOnSignal ? 'text-emerald-400 animate-pulse' : 'text-slate-500']" />
          <span :class="isVfoLockedOnSignal ? 'text-emerald-300 font-bold' : 'text-slate-400'">
            {{ isVfoLockedOnSignal ? '● 载波锁定 (LOCKED)' : '○ 未覆盖信号 (NO SIGNAL)' }}
          </span>
        </div>
        <span v-if="isVfoLockedOnSignal" class="text-[10px] text-emerald-400 bg-emerald-500/10 px-1.5 py-0.5 rounded border border-emerald-500/20">
          SNR: {{ checkVfoSignalLock().snr.toFixed(1) }} dB
        </span>
      </div>

      <!-- Quick Metrics Bar -->
      <div class="grid grid-cols-2 gap-2 p-2 bg-sdr-dark/40 border-b border-sdr-border/60 text-[11px] font-mono">
        <div class="flex items-center gap-1.5 text-slate-400">
          <Activity class="w-3.5 h-3.5 text-cyan-400" />
          <span>总帧数: <b class="text-slate-200">{{ totalPacketsCount }}</b></span>
        </div>
        <div class="flex items-center gap-1.5 text-slate-400">
          <ShieldCheck class="w-3.5 h-3.5 text-emerald-400" />
          <span>CRC成功: <b class="text-emerald-400">{{ crcSuccessRate }}%</b></span>
        </div>
      </div>

      <!-- Live Mini Stream List -->
      <div class="flex-1 overflow-y-auto p-2 flex flex-col gap-1.5 font-mono text-[11px]">
        <!-- Warning when VFO moved away from signal -->
        <div v-if="isPlaying && !isVfoLockedOnSignal" class="p-3 bg-amber-500/10 border border-amber-500/30 rounded-md flex flex-col gap-1 text-amber-300 text-xs font-sans">
          <div class="flex items-center gap-1.5 font-bold">
            <AlertTriangle class="w-4 h-4 text-amber-400" />
            <span>VFO 偏离信号通带</span>
          </div>
          <p class="text-[11px] text-slate-300 leading-relaxed">
            当前 VFO 滤波窗口位于底噪区（无载波能量），解调器已停止输出。请在瀑布图上将 VFO 框拖动到 <b>+1.40 MHz</b> 或 <b>-1.60 MHz</b> 信号峰处！
          </p>
        </div>

        <div
          v-for="p in packetHistory.slice(0, 15)"
          :key="p.id"
          class="p-2 bg-sdr-dark/60 hover:bg-slate-800/80 border border-sdr-border/60 rounded flex flex-col gap-1 transition-colors"
        >
          <div class="flex items-center justify-between text-[10px]">
            <span class="text-slate-400">#{{ String(p.id).padStart(4, '0') }} {{ p.timestamp }}</span>
            <span
              :class="[
                'px-1 py-0.2 rounded font-sans font-bold',
                p.crcValid ? 'bg-emerald-500/20 text-emerald-400' : 'bg-rose-500/20 text-rose-400'
              ]"
            >
              {{ p.crcValid ? 'CRC OK' : 'CRC ERR' }}
            </span>
          </div>

          <div class="flex items-center gap-2 text-slate-300 text-[11px]">
            <span class="text-cyan-400 font-bold">Sync: {{ p.syncWord }}</span>
            <span class="text-slate-500">|</span>
            <span class="text-amber-400">Mask: {{ p.mask }}</span>
            <span class="text-slate-500">|</span>
            <span class="text-slate-400">{{ p.length }}B</span>
          </div>

          <!-- Hex Preview -->
          <div class="text-[10px] text-slate-400 truncate bg-sdr-panel/70 px-1.5 py-0.5 rounded">
            {{ p.payloadHex }}
          </div>
        </div>

        <div v-if="packetHistory.length === 0 && (!isPlaying || isVfoLockedOnSignal)" class="py-12 text-center text-slate-500 font-sans text-xs">
          {{ currentLang === 'zh' ? '正在监听空中载波数据帧...' : 'Listening for RF frames...' }}
        </div>
      </div>
    </div>
  </div>
</template>
