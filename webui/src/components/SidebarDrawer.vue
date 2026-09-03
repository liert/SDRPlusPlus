<script setup lang="ts">
import { ref } from 'vue'
import SourcePanel from './SourcePanel.vue'
import DemodPanel from './DemodPanel.vue'
import FftSettingsPanel from './FftSettingsPanel.vue'
import { currentLang } from '@/composables/useSdrEngine'
import {
  SlidersHorizontal,
  Radio,
  Settings2,
  X,
  Pin,
  PinOff
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
        :title="currentLang === 'zh' ? 'FFT与频谱显示参数' : 'FFT & Display Settings'"
      >
        <Settings2 class="w-4 h-4" />
      </button>
    </div>

    <!-- Sliding Drawer Panel -->
    <div
      v-if="activeTab !== null"
      :class="[
        'bg-sdr-panel/95 backdrop-blur-2xl border-r border-sdr-border shadow-2xl flex flex-col transition-all duration-300',
        isPinned ? 'relative' : 'absolute left-12 top-0 bottom-0 z-40'
      ]"
      style="width: 350px;"
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
          <span v-else>{{ currentLang === 'zh' ? 'FFT 与频谱显示参数' : 'FFT & Display' }}</span>
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

        <!-- 3. FFT & Display Settings Panel -->
        <FftSettingsPanel v-if="activeTab === 'display'" />
      </div>
    </div>
  </div>
</template>
