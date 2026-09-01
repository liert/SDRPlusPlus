<script setup lang="ts">
import { ref } from 'vue'
import TopBar from './components/TopBar.vue'
import WaterfallSpectrum from './components/WaterfallSpectrum.vue'
import SourcePanel from './components/SourcePanel.vue'
import DemodPanel from './components/DemodPanel.vue'
import PacketInspector from './components/PacketInspector.vue'
import { currentLang, spectrumSettings } from './composables/useSdrEngine'
import { Radio, SlidersHorizontal, Settings2, BarChart2 } from 'lucide-vue-next'

const activeLeftTab = ref<'source' | 'demod' | 'display'>('demod')
</script>

<template>
  <div class="w-screen h-screen flex flex-col bg-sdr-dark text-slate-200 overflow-hidden font-sans select-none">
    <!-- Top Global Header Control Bar -->
    <TopBar />

    <!-- Main Workspace: Split into Left Controls and Right Canvas/Inspectors -->
    <div class="flex-1 flex min-h-0 p-3 gap-3">
      
      <!-- Left Sidebar Panel (Tabs: Source, Demod, Display) -->
      <aside class="w-80 flex flex-col bg-sdr-panel border border-sdr-border rounded-lg overflow-hidden shrink-0 shadow-lg">
        <!-- Sidebar Navigation Tabs -->
        <div class="flex border-b border-sdr-border bg-sdr-dark/80 p-1 gap-1">
          <button
            @click="activeLeftTab = 'demod'"
            :class="[
              'flex-1 flex items-center justify-center gap-1.5 py-1.5 px-2 rounded-md text-xs font-bold transition-colors',
              activeLeftTab === 'demod' ? 'bg-blue-600 text-white shadow' : 'text-slate-400 hover:text-slate-200'
            ]"
          >
            <Radio class="w-3.5 h-3.5" />
            <span>{{ currentLang === 'zh' ? '数字解调' : 'Demod' }}</span>
          </button>

          <button
            @click="activeLeftTab = 'source'"
            :class="[
              'flex-1 flex items-center justify-center gap-1.5 py-1.5 px-2 rounded-md text-xs font-bold transition-colors',
              activeLeftTab === 'source' ? 'bg-blue-600 text-white shadow' : 'text-slate-400 hover:text-slate-200'
            ]"
          >
            <SlidersHorizontal class="w-3.5 h-3.5" />
            <span>{{ currentLang === 'zh' ? '射频源' : 'Source' }}</span>
          </button>

          <button
            @click="activeLeftTab = 'display'"
            :class="[
              'flex-1 flex items-center justify-center gap-1.5 py-1.5 px-2 rounded-md text-xs font-bold transition-colors',
              activeLeftTab === 'display' ? 'bg-blue-600 text-white shadow' : 'text-slate-400 hover:text-slate-200'
            ]"
          >
            <Settings2 class="w-3.5 h-3.5" />
            <span>{{ currentLang === 'zh' ? '显示设置' : 'Display' }}</span>
          </button>
        </div>

        <!-- Sidebar Content Area -->
        <div class="flex-1 overflow-y-auto p-3.5">
          <!-- 1. Demodulation Controls -->
          <DemodPanel v-if="activeLeftTab === 'demod'" />

          <!-- 2. RF Source Controls -->
          <SourcePanel v-if="activeLeftTab === 'source'" />

          <!-- 3. Spectrum & Waterfall Display Settings -->
          <div v-if="activeLeftTab === 'display'" class="flex flex-col gap-4 text-xs">
            <div class="flex items-center gap-1.5 text-slate-300 font-semibold">
              <BarChart2 class="w-3.5 h-3.5 text-cyan-400" />
              <span>{{ currentLang === 'zh' ? '频谱显示设置' : 'Spectrum Settings' }}</span>
            </div>

            <!-- Dynamic Range: Min dBm & Max dBm -->
            <div class="grid grid-cols-2 gap-2">
              <div class="flex flex-col gap-1">
                <label class="text-slate-400 text-[11px]">底噪下限 (Min dBm)</label>
                <input
                  type="number"
                  v-model.number="spectrumSettings.minDb"
                  class="bg-sdr-dark border border-sdr-border rounded px-2 py-1 text-slate-200 font-mono"
                />
              </div>
              <div class="flex flex-col gap-1">
                <label class="text-slate-400 text-[11px]">电平上限 (Max dBm)</label>
                <input
                  type="number"
                  v-model.number="spectrumSettings.maxDb"
                  class="bg-sdr-dark border border-sdr-border rounded px-2 py-1 text-slate-200 font-mono"
                />
              </div>
            </div>

            <!-- Spectrum Smoothing Slider -->
            <div class="flex flex-col gap-1">
              <div class="flex justify-between text-slate-400 text-[11px]">
                <span>频谱平滑系数 (Smoothing)</span>
                <b class="text-slate-200 font-mono">{{ Math.round(spectrumSettings.smoothing * 100) }}%</b>
              </div>
              <input type="range" min="0" max="0.95" step="0.05" v-model.number="spectrumSettings.smoothing" class="w-full accent-blue-500" />
            </div>

            <!-- Checkboxes -->
            <div class="flex flex-col gap-2 pt-2 border-t border-sdr-border">
              <label class="flex items-center gap-2 cursor-pointer text-slate-300">
                <input type="checkbox" v-model="spectrumSettings.peakHold" class="rounded bg-sdr-dark border-sdr-border text-blue-500" />
                <span>开启峰值保持 (Peak Hold)</span>
              </label>

              <label class="flex items-center gap-2 cursor-pointer text-slate-300">
                <input type="checkbox" v-model="spectrumSettings.fillSpectrum" class="rounded bg-sdr-dark border-sdr-border text-blue-500" />
                <span>频谱渐变填充 (Gradient Fill)</span>
              </label>
            </div>
          </div>
        </div>
      </aside>

      <!-- Right Area: Split Waterfall (Top 55%) + Packet Inspector (Bottom 45%) -->
      <main class="flex-1 flex flex-col gap-3 min-w-0">
        <!-- 1. Waterfall & Spectrum Canvas Component -->
        <div class="flex-[0.55] min-h-[220px]">
          <WaterfallSpectrum />
        </div>

        <!-- 2. Packet Inspector Stream Component -->
        <div class="flex-[0.45] min-h-[180px]">
          <PacketInspector />
        </div>
      </main>
    </div>
  </div>
</template>
