<script setup lang="ts">
import { ref } from 'vue'
import SourcePanel from './SourcePanel.vue'
import DemodPanel from './DemodPanel.vue'
import { currentLang, spectrumSettings } from '@/composables/useSdrEngine'
import {
  SlidersHorizontal,
  Radio,
  Settings2,
  X,
  Pin,
  PinOff,
  BarChart2
} from 'lucide-vue-next'

const activeTab = ref<'source' | 'demod' | 'display' | null>('source')
const isPinned = ref(false)

function toggleTab(tab: 'source' | 'demod' | 'display') {
  if (activeTab.value === tab && !isPinned.value) {
    activeTab.value = null
  } else {
    activeTab.value = tab
  }
}

function closeDrawer() {
  if (!isPinned.value) {
    activeTab.value = null
  }
}
</script>

<template>
  <div class="relative z-30 flex select-none pointer-events-auto">
    <!-- Left Icon Navigation Strip -->
    <div class="w-12 bg-sdr-panel/90 backdrop-blur-md border-r border-sdr-border flex flex-col items-center py-3 gap-3 shrink-0 shadow-2xl">
      <!-- 1. Source Tab Button (First) -->
      <button
        @click="toggleTab('source')"
        :class="[
          'w-9 h-9 rounded-lg flex items-center justify-center transition-all',
          activeTab === 'source'
            ? 'bg-blue-600 text-white shadow-lg shadow-blue-500/30'
            : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800'
        ]"
        :title="currentLang === 'zh' ? '射频输入源设置' : 'RF Source'"
      >
        <SlidersHorizontal class="w-4 h-4" />
      </button>

      <!-- 2. Demodulation Tab Button (Second) -->
      <button
        @click="toggleTab('demod')"
        :class="[
          'w-9 h-9 rounded-lg flex items-center justify-center transition-all',
          activeTab === 'demod'
            ? 'bg-blue-600 text-white shadow-lg shadow-blue-500/30'
            : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800'
        ]"
        :title="currentLang === 'zh' ? '数字解调与物理层参数' : 'Demodulation'"
      >
        <Radio class="w-4 h-4" />
      </button>

      <!-- 3. Display Tab Button (Third) -->
      <button
        @click="toggleTab('display')"
        :class="[
          'w-9 h-9 rounded-lg flex items-center justify-center transition-all',
          activeTab === 'display'
            ? 'bg-blue-600 text-white shadow-lg shadow-blue-500/30'
            : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800'
        ]"
        :title="currentLang === 'zh' ? '频谱显示与色谱' : 'Display Settings'"
      >
        <Settings2 class="w-4 h-4" />
      </button>
    </div>

    <!-- Sliding Drawer Panel -->
    <div
      v-if="activeTab !== null"
      :class="[
        'w-84 bg-sdr-panel/95 backdrop-blur-2xl border-r border-sdr-border shadow-2xl flex flex-col transition-all duration-300',
        isPinned ? 'relative' : 'absolute left-12 top-0 bottom-0 z-40'
      ]"
      style="width: 330px;"
    >
      <!-- Drawer Header -->
      <div class="h-11 px-3.5 bg-sdr-dark/80 border-b border-sdr-border flex items-center justify-between shrink-0">
        <span class="text-xs font-bold text-slate-200 flex items-center gap-1.5">
          <component
            :is="activeTab === 'source' ? SlidersHorizontal : (activeTab === 'demod' ? Radio : Settings2)"
            class="w-3.5 h-3.5 text-cyan-400"
          />
          <span v-if="activeTab === 'source'">{{ currentLang === 'zh' ? '射频输入源设置' : 'RF Source' }}</span>
          <span v-else-if="activeTab === 'demod'">{{ currentLang === 'zh' ? '数字解调配置' : 'Demodulation' }}</span>
          <span v-else>{{ currentLang === 'zh' ? '频谱显示设置' : 'Display' }}</span>
        </span>

        <div class="flex items-center gap-1">
          <!-- Pin / Unpin Button -->
          <button
            @click="isPinned = !isPinned"
            class="p-1 hover:bg-slate-800 rounded text-slate-400 hover:text-slate-200 transition-colors"
            :title="isPinned ? '取消固定' : '固定侧边栏'"
          >
            <component :is="isPinned ? Pin : PinOff" class="w-3.5 h-3.5 text-blue-400" />
          </button>

          <!-- Close Drawer Button -->
          <button
            v-if="!isPinned"
            @click="closeDrawer"
            class="p-1 hover:bg-slate-800 rounded text-slate-400 hover:text-slate-200 transition-colors"
          >
            <X class="w-3.5 h-3.5" />
          </button>
        </div>
      </div>

      <!-- Drawer Body Content -->
      <div class="flex-1 overflow-y-auto p-3.5">
        <!-- 1. Source Panel -->
        <SourcePanel v-if="activeTab === 'source'" />

        <!-- 2. Demodulation Panel -->
        <DemodPanel v-if="activeTab === 'demod'" />

        <!-- 3. Display Settings Panel -->
        <div v-if="activeTab === 'display'" class="flex flex-col gap-4 text-xs">
          <div class="flex items-center gap-1.5 text-slate-300 font-semibold">
            <BarChart2 class="w-3.5 h-3.5 text-cyan-400" />
            <span>{{ currentLang === 'zh' ? '频谱图参数调节' : 'Spectrum Parameters' }}</span>
          </div>

          <!-- Min/Max dB -->
          <div class="grid grid-cols-2 gap-2">
            <div class="flex flex-col gap-1">
              <label class="text-slate-400 text-[11px]">底噪 (Min dBm)</label>
              <input
                type="number"
                v-model.number="spectrumSettings.minDb"
                class="bg-sdr-dark border border-sdr-border rounded px-2 py-1 text-slate-200 font-mono"
              />
            </div>
            <div class="flex flex-col gap-1">
              <label class="text-slate-400 text-[11px]">上限 (Max dBm)</label>
              <input
                type="number"
                v-model.number="spectrumSettings.maxDb"
                class="bg-sdr-dark border border-sdr-border rounded px-2 py-1 text-slate-200 font-mono"
              />
            </div>
          </div>

          <!-- Smoothing -->
          <div class="flex flex-col gap-1">
            <div class="flex justify-between text-slate-400 text-[11px]">
              <span>频谱平滑滤波</span>
              <b class="text-slate-200 font-mono">{{ Math.round(spectrumSettings.smoothing * 100) }}%</b>
            </div>
            <input type="range" min="0" max="0.95" step="0.05" v-model.number="spectrumSettings.smoothing" class="w-full accent-blue-500" />
          </div>

          <!-- Checkbox Toggles -->
          <div class="flex flex-col gap-2 pt-2 border-t border-sdr-border">
            <label class="flex items-center gap-2 cursor-pointer text-slate-300">
              <input type="checkbox" v-model="spectrumSettings.peakHold" class="rounded bg-sdr-dark border-sdr-border text-blue-500" />
              <span>峰值保持 (Peak Hold)</span>
            </label>

            <label class="flex items-center gap-2 cursor-pointer text-slate-300">
              <input type="checkbox" v-model="spectrumSettings.fillSpectrum" class="rounded bg-sdr-dark border-sdr-border text-blue-500" />
              <span>频谱面积渐变着色 (Gradient Fill)</span>
            </label>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>
