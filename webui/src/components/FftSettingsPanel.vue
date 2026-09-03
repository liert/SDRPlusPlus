<script setup lang="ts">
import { computed } from 'vue'
import {
  spectrumSettings,
  sourceConfig,
  currentLang,
  autoScaleFftRange,
  resetFftDefaults,
  applyFftPreset,
  hasLiveHackRfData,
  dspIpcFps
} from '@/composables/useSdrEngine'
import type { FftWindowType, ColormapType, PeakDecaySpeed } from '@/types/sdr'
import {
  Activity,
  Sliders,
  Sparkles,
  RotateCcw,
  Palette,
  Eye,
  TrendingUp,
  BarChart2,
  Zap,
  Gauge,
  Layers,
  Check
} from 'lucide-vue-next'

// Resolution Bandwidth calculation: SampleRate / FFT_Size
const rbwKhz = computed(() => {
  if (!sourceConfig.sampleRateHz || !spectrumSettings.fftSize) return 0
  return (sourceConfig.sampleRateHz / spectrumSettings.fftSize / 1000).toFixed(2)
})

// Frame interval calculation: 1000 / fftRate
const frameIntervalMs = computed(() => {
  return (1000 / (spectrumSettings.fftRate || 60)).toFixed(1)
})

// Window function options with metadata
const windowOptions: { value: FftWindowType; label: string; descZh: string; descEn: string; sidelobe: string }[] = [
  {
    value: 'blackman_harris',
    label: 'Blackman-Harris 4-Term',
    descZh: 'SDR 推荐首选，超低旁瓣 (-92 dB)，动态范围广',
    descEn: 'SDR Default, low sidelobes (-92 dB), wide dynamic range',
    sidelobe: '-92 dB'
  },
  {
    value: 'hann',
    label: 'Hann (Hanning)',
    descZh: '通用均衡窗，主瓣较窄，突发及瞬态信号友好',
    descEn: 'Balanced, narrow mainlobe, ideal for fast transients',
    sidelobe: '-31 dB'
  },
  {
    value: 'hamming',
    label: 'Hamming',
    descZh: '经典窄带窗，快速初始衰减',
    descEn: 'Narrowband standard, rapid initial drop-off',
    sidelobe: '-43 dB'
  },
  {
    value: 'blackman',
    label: 'Blackman 3-Term',
    descZh: '经典三阶平滑窗，适中分辨率',
    descEn: 'Classic 3-term, moderate resolution',
    sidelobe: '-58 dB'
  },
  {
    value: 'nuttall',
    label: 'Nuttall (4-Term)',
    descZh: '极佳旁瓣抑制 (-98 dB)，适合微弱信号排查',
    descEn: 'Ultra low sidelobes (-98 dB), weak signal hunting',
    sidelobe: '-98 dB'
  },
  {
    value: 'flat_top',
    label: 'Flat Top',
    descZh: '幅度测量精度极高 (误差 <0.01 dB)，适合功率定标',
    descEn: 'Peak amplitude calibration accuracy (<0.01 dB error)',
    sidelobe: '-70 dB'
  },
  {
    value: 'rectangular',
    label: 'Rectangular (None)',
    descZh: '无加窗，最高频率分辨率，适合纯单音连续波',
    descEn: 'No window, maximum frequency resolution for pure CW',
    sidelobe: '-13 dB'
  }
]

// Colormap choices with gradient style preview
const colormapOptions: { value: ColormapType; label: string; gradient: string }[] = [
  {
    value: 'turbo',
    label: 'Google Turbo',
    gradient: 'linear-gradient(to right, #30123b, #4686fb, #1ae4b6, #a2fc3c, #fb8022, #7a0403)'
  },
  {
    value: 'viridis',
    label: 'Viridis',
    gradient: 'linear-gradient(to right, #440154, #3b528b, #21908d, #5dc963, #fde725)'
  },
  {
    value: 'plasma',
    label: 'Plasma',
    gradient: 'linear-gradient(to right, #0d0887, #6a00a8, #b12a90, #e16462, #fca636, #f0f921)'
  },
  {
    value: 'inferno',
    label: 'Inferno',
    gradient: 'linear-gradient(to right, #000004, #420a68, #932667, #dd513a, #fca50a, #fcffa4)'
  },
  {
    value: 'electric',
    label: 'Electric Blue',
    gradient: 'linear-gradient(to right, #000000, #0055ff, #00ffff, #ffffff)'
  },
  {
    value: 'hot',
    label: 'Thermal Hot',
    gradient: 'linear-gradient(to right, #000000, #ff0000, #ffff00, #ffffff)'
  },
  {
    value: 'greyscale',
    label: 'Monochrome',
    gradient: 'linear-gradient(to right, #000000, #888888, #ffffff)'
  }
]

const fftSizeOptions = [512, 1024, 2048, 4096]
const fftRateOptions = [15, 30, 60, 90, 120]
const decaySpeedOptions: { value: PeakDecaySpeed; labelZh: string; labelEn: string }[] = [
  { value: 'fast', labelZh: '快', labelEn: 'Fast' },
  { value: 'medium', labelZh: '中', labelEn: 'Med' },
  { value: 'slow', labelZh: '慢', labelEn: 'Slow' },
  { value: 'infinite', labelZh: '保持', labelEn: 'Inf' }
]

const splitRatioOptions = [
  { value: 0.25, label: '25% / 75%' },
  { value: 0.38, label: '38% / 62%' },
  { value: 0.50, label: '50% / 50%' },
  { value: 0.65, label: '65% / 35%' }
]
</script>

<template>
  <div class="flex flex-col gap-4 text-xs select-none">
    <!-- Top Status / Realtime DSP Telemetry Pill -->
    <div class="bg-sdr-dark/90 border border-sdr-border rounded-lg p-2.5 flex flex-col gap-2">
      <div class="flex items-center justify-between text-[11px]">
        <span class="text-slate-400 flex items-center gap-1.5 font-medium">
          <Activity class="w-3.5 h-3.5 text-cyan-400" />
          <span>{{ currentLang === 'zh' ? '实时 FFT DSP 指标' : 'FFT DSP Metrics' }}</span>
        </span>
        <span class="px-1.5 py-0.5 rounded bg-emerald-500/10 text-emerald-400 font-mono text-[10px] font-bold border border-emerald-500/20">
          {{ dspIpcFps > 0 ? `${dspIpcFps} FPS` : `${spectrumSettings.fftRate} FPS` }}
        </span>
      </div>

      <div class="grid grid-cols-3 gap-1.5 pt-1 border-t border-slate-800/80 font-mono text-[10px]">
        <div class="flex flex-col bg-slate-900/60 p-1.5 rounded border border-slate-800/50">
          <span class="text-slate-500 text-[9px] font-sans">分辨率带宽 (RBW)</span>
          <b class="text-cyan-300 font-bold mt-0.5">{{ rbwKhz }} kHz</b>
        </div>
        <div class="flex flex-col bg-slate-900/60 p-1.5 rounded border border-slate-800/50">
          <span class="text-slate-500 text-[9px] font-sans">计算周期</span>
          <b class="text-amber-300 font-bold mt-0.5">{{ frameIntervalMs }} ms</b>
        </div>
        <div class="flex flex-col bg-slate-900/60 p-1.5 rounded border border-slate-800/50">
          <span class="text-slate-500 text-[9px] font-sans">动态范围</span>
          <b class="text-indigo-300 font-bold mt-0.5">{{ spectrumSettings.maxDb - spectrumSettings.minDb }} dB</b>
        </div>
      </div>
    </div>

    <!-- Quick Scenario Presets -->
    <div class="flex flex-col gap-1.5">
      <div class="flex items-center justify-between text-slate-400 text-[11px] font-semibold">
        <span class="flex items-center gap-1">
          <Sparkles class="w-3.5 h-3.5 text-amber-400" />
          <span>{{ currentLang === 'zh' ? '场景预设档位' : 'Preset Profiles' }}</span>
        </span>
      </div>
      <div class="grid grid-cols-2 gap-1.5">
        <button
          @click="applyFftPreset('flrc')"
          class="px-2 py-1.5 rounded bg-slate-800 hover:bg-slate-750 active:scale-95 border border-slate-700 hover:border-blue-500/50 text-left transition-all group"
        >
          <div class="text-slate-200 font-medium text-[11px] group-hover:text-blue-300">🛸 SX1280 突发</div>
          <div class="text-[9px] text-slate-400">1024点 · 60FPS · 快速</div>
        </button>

        <button
          @click="applyFftPreset('weak')"
          class="px-2 py-1.5 rounded bg-slate-800 hover:bg-slate-750 active:scale-95 border border-slate-700 hover:border-blue-500/50 text-left transition-all group"
        >
          <div class="text-slate-200 font-medium text-[11px] group-hover:text-blue-300">🔍 微弱信号发现</div>
          <div class="text-[9px] text-slate-400">4096点 · Nuttall · 深度平滑</div>
        </button>

        <button
          @click="applyFftPreset('fhss')"
          class="px-2 py-1.5 rounded bg-slate-800 hover:bg-slate-750 active:scale-95 border border-slate-700 hover:border-blue-500/50 text-left transition-all group"
        >
          <div class="text-slate-200 font-medium text-[11px] group-hover:text-blue-300">⚡ 跳频全景捕获</div>
          <div class="text-[9px] text-slate-400">1024点 · 90FPS · 2x瀑布</div>
        </button>

        <button
          @click="applyFftPreset('hi_res')"
          class="px-2 py-1.5 rounded bg-slate-800 hover:bg-slate-750 active:scale-95 border border-slate-700 hover:border-blue-500/50 text-left transition-all group"
        >
          <div class="text-slate-200 font-medium text-[11px] group-hover:text-blue-300">🎚️ 高精频谱仪</div>
          <div class="text-[9px] text-slate-400">4096点 · Flat-Top 定标</div>
        </button>
      </div>
    </div>

    <!-- Section 1: 🎛️ FFT Core Settings -->
    <div class="flex flex-col gap-3 pt-2 border-t border-sdr-border">
      <div class="flex items-center gap-1.5 text-slate-300 font-semibold text-xs">
        <Sliders class="w-3.5 h-3.5 text-cyan-400" />
        <span>{{ currentLang === 'zh' ? 'FFT 计算与算法参数' : 'FFT Core Algorithm' }}</span>
      </div>

      <!-- FFT Size (Points) -->
      <div class="flex flex-col gap-1.5">
        <div class="flex justify-between text-slate-400 text-[11px]">
          <span>FFT 点数 (分辨率)</span>
          <span class="text-cyan-300 font-mono font-bold">{{ spectrumSettings.fftSize }} 点</span>
        </div>
        <div class="grid grid-cols-4 gap-1 p-0.5 bg-sdr-dark border border-sdr-border rounded-lg">
          <button
            v-for="size in fftSizeOptions"
            :key="size"
            @click="spectrumSettings.fftSize = size"
            :class="[
              'py-1 rounded text-[11px] font-mono font-semibold transition-all',
              spectrumSettings.fftSize === size
                ? 'bg-blue-600 text-white shadow shadow-blue-500/40'
                : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800'
            ]"
          >
            {{ size }}
          </button>
        </div>
      </div>

      <!-- Window Function -->
      <div class="flex flex-col gap-1">
        <label class="text-slate-400 text-[11px]">窗函数 (Window Function)</label>
        <select
          v-model="spectrumSettings.fftWindow"
          class="bg-sdr-dark border border-sdr-border rounded-lg px-2.5 py-1.5 text-slate-200 font-sans focus:outline-none focus:border-blue-500 cursor-pointer"
        >
          <option v-for="opt in windowOptions" :key="opt.value" :value="opt.value" class="bg-sdr-panel">
            {{ opt.label }} ({{ opt.sidelobe }})
          </option>
        </select>
        <p class="text-[10px] text-slate-400 italic px-0.5">
          {{ windowOptions.find(w => w.value === spectrumSettings.fftWindow)?.descZh }}
        </p>
      </div>

      <!-- FFT Rate / Refresh Rate -->
      <div class="flex flex-col gap-1.5">
        <div class="flex justify-between text-slate-400 text-[11px]">
          <span>FFT 刷新速率 (FPS)</span>
          <span class="text-emerald-400 font-mono font-bold">{{ spectrumSettings.fftRate }} FPS</span>
        </div>
        <div class="grid grid-cols-5 gap-1 p-0.5 bg-sdr-dark border border-sdr-border rounded-lg">
          <button
            v-for="rate in fftRateOptions"
            :key="rate"
            @click="spectrumSettings.fftRate = rate"
            :class="[
              'py-1 rounded text-[10px] font-mono font-semibold transition-all',
              spectrumSettings.fftRate === rate
                ? 'bg-emerald-600 text-white shadow shadow-emerald-500/40'
                : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800'
            ]"
          >
            {{ rate }}
          </button>
        </div>
      </div>
    </div>

    <!-- Section 2: 📊 Amplitude & dB Scale -->
    <div class="flex flex-col gap-3 pt-2 border-t border-sdr-border">
      <div class="flex items-center justify-between">
        <span class="flex items-center gap-1.5 text-slate-300 font-semibold text-xs">
          <BarChart2 class="w-3.5 h-3.5 text-cyan-400" />
          <span>{{ currentLang === 'zh' ? '幅度标尺与动态范围 (dBm)' : 'Amplitude Scale' }}</span>
        </span>

        <!-- One-click Auto Scale Button -->
        <button
          @click="autoScaleFftRange"
          class="px-2 py-0.5 bg-blue-600/80 hover:bg-blue-600 active:scale-95 text-white rounded text-[10px] font-medium flex items-center gap-1 shadow-sm transition-all"
          title="根据当前实时底噪与信号峰值自动校准上下限"
        >
          <Zap class="w-3 h-3 text-amber-300" />
          <span>{{ currentLang === 'zh' ? '一键自适应' : 'Auto Scale' }}</span>
        </button>
      </div>

      <!-- Min / Max dB Inputs -->
      <div class="grid grid-cols-2 gap-2">
        <div class="flex flex-col gap-1 bg-sdr-dark p-2 rounded-lg border border-sdr-border">
          <label class="text-slate-400 text-[10px]">底噪下限 (Min dBm)</label>
          <div class="flex items-center gap-1">
            <input
              type="number"
              v-model.number="spectrumSettings.minDb"
              step="5"
              min="-160"
              max="0"
              class="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-slate-200 font-mono text-xs focus:outline-none focus:border-blue-500"
            />
            <span class="text-slate-500 text-[10px]">dBm</span>
          </div>
        </div>

        <div class="flex flex-col gap-1 bg-sdr-dark p-2 rounded-lg border border-sdr-border">
          <label class="text-slate-400 text-[10px]">上限峰值 (Max dBm)</label>
          <div class="flex items-center gap-1">
            <input
              type="number"
              v-model.number="spectrumSettings.maxDb"
              step="5"
              min="-80"
              max="30"
              class="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-slate-200 font-mono text-xs focus:outline-none focus:border-blue-500"
            />
            <span class="text-slate-500 text-[10px]">dBm</span>
          </div>
        </div>
      </div>

      <!-- dB Scale Presets -->
      <div class="flex items-center gap-1">
        <button
          @click="spectrumSettings.minDb = -100; spectrumSettings.maxDb = 10"
          class="flex-1 py-1 bg-slate-800 hover:bg-slate-700 rounded text-[10px] text-slate-300 border border-slate-700"
        >
          标准 (-100~+10)
        </button>
        <button
          @click="spectrumSettings.minDb = -120; spectrumSettings.maxDb = -20"
          class="flex-1 py-1 bg-slate-800 hover:bg-slate-700 rounded text-[10px] text-slate-300 border border-slate-700"
        >
          高敏 (-120~-20)
        </button>
        <button
          @click="spectrumSettings.minDb = -80; spectrumSettings.maxDb = 20"
          class="flex-1 py-1 bg-slate-800 hover:bg-slate-700 rounded text-[10px] text-slate-300 border border-slate-700"
        >
          强信号 (-80~+20)
        </button>
      </div>
    </div>

    <!-- Section 3: 🌊 Smoothing & Peak Hold -->
    <div class="flex flex-col gap-3 pt-2 border-t border-sdr-border">
      <div class="flex items-center gap-1.5 text-slate-300 font-semibold text-xs">
        <TrendingUp class="w-3.5 h-3.5 text-cyan-400" />
        <span>{{ currentLang === 'zh' ? '平滑滤波与峰值保持' : 'Smoothing & Peak Hold' }}</span>
      </div>

      <!-- Smoothing Slider -->
      <div class="flex flex-col gap-1 bg-sdr-dark p-2.5 rounded-lg border border-sdr-border">
        <div class="flex justify-between text-slate-400 text-[11px]">
          <span>指数平滑滤波 (Smoothing)</span>
          <b class="text-cyan-300 font-mono">{{ Math.round(spectrumSettings.smoothing * 100) }}%</b>
        </div>
        <input
          type="range"
          min="0"
          max="0.95"
          step="0.05"
          v-model.number="spectrumSettings.smoothing"
          class="w-full accent-blue-500 cursor-pointer h-1.5 bg-slate-800 rounded-lg"
        />
        <div class="flex justify-between text-[9px] text-slate-500 pt-0.5">
          <span>0% (实时响应)</span>
          <span>50% (均衡)</span>
          <span>95% (平稳稳定)</span>
        </div>
      </div>

      <!-- Peak Hold & Decay Speed -->
      <div class="flex flex-col gap-2 bg-sdr-dark p-2.5 rounded-lg border border-sdr-border">
        <div class="flex items-center justify-between">
          <label class="flex items-center gap-2 cursor-pointer text-slate-300 text-xs font-medium">
            <input
              type="checkbox"
              v-model="spectrumSettings.peakHold"
              class="rounded bg-slate-900 border-slate-700 text-blue-500 focus:ring-0 w-3.5 h-3.5"
            />
            <span>峰值保持 (Peak Hold)</span>
          </label>
        </div>

        <div v-if="spectrumSettings.peakHold" class="flex flex-col gap-1 pt-1.5 border-t border-slate-800">
          <span class="text-slate-400 text-[10px]">峰值衰减速度 (Decay Speed)</span>
          <div class="grid grid-cols-4 gap-1">
            <button
              v-for="d in decaySpeedOptions"
              :key="d.value"
              @click="spectrumSettings.peakDecaySpeed = d.value"
              :class="[
                'py-1 rounded text-[10px] font-medium transition-colors',
                spectrumSettings.peakDecaySpeed === d.value
                  ? 'bg-blue-600 text-white font-bold'
                  : 'bg-slate-800 hover:bg-slate-700 text-slate-300'
              ]"
            >
              {{ currentLang === 'zh' ? d.labelZh : d.labelEn }}
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- Section 4: 🎨 Waterfall & Visual Rendering -->
    <div class="flex flex-col gap-3 pt-2 border-t border-sdr-border">
      <div class="flex items-center gap-1.5 text-slate-300 font-semibold text-xs">
        <Palette class="w-3.5 h-3.5 text-cyan-400" />
        <span>{{ currentLang === 'zh' ? '瀑布图色板与视觉渲染' : 'Waterfall & Visuals' }}</span>
      </div>

      <!-- Colormap Selector with Gradient Swatches -->
      <div class="flex flex-col gap-1.5">
        <label class="text-slate-400 text-[11px]">瀑布图色板 (Colormap)</label>
        <div class="grid grid-cols-1 gap-1.5">
          <button
            v-for="cm in colormapOptions"
            :key="cm.value"
            @click="spectrumSettings.colormap = cm.value"
            :class="[
              'flex items-center justify-between p-1.5 rounded-lg border transition-all',
              spectrumSettings.colormap === cm.value
                ? 'bg-slate-800 border-blue-500 ring-1 ring-blue-500/50 shadow'
                : 'bg-sdr-dark border-sdr-border hover:bg-slate-800/60'
            ]"
          >
            <span class="text-[11px] font-medium text-slate-200 flex items-center gap-1.5">
              <Check v-if="spectrumSettings.colormap === cm.value" class="w-3 h-3 text-blue-400" />
              <span :class="spectrumSettings.colormap === cm.value ? 'text-blue-300 font-bold' : ''">{{ cm.label }}</span>
            </span>
            <div class="w-24 h-3.5 rounded border border-slate-700 shadow-inner" :style="{ background: cm.gradient }"></div>
          </button>
        </div>
      </div>

      <!-- Waterfall Speed & Split Ratio -->
      <div class="grid grid-cols-2 gap-2">
        <!-- Waterfall Speed -->
        <div class="flex flex-col gap-1 bg-sdr-dark p-2 rounded-lg border border-sdr-border">
          <label class="text-slate-400 text-[10px]">滚动速率 (Speed)</label>
          <div class="grid grid-cols-3 gap-1">
            <button
              v-for="spd in [1, 2, 3]"
              :key="spd"
              @click="spectrumSettings.waterfallSpeed = spd"
              :class="[
                'py-1 rounded text-[10px] font-mono font-bold transition-colors',
                spectrumSettings.waterfallSpeed === spd
                  ? 'bg-blue-600 text-white'
                  : 'bg-slate-800 hover:bg-slate-700 text-slate-300'
              ]"
            >
              {{ spd }}x
            </button>
          </div>
        </div>

        <!-- Split Ratio -->
        <div class="flex flex-col gap-1 bg-sdr-dark p-2 rounded-lg border border-sdr-border">
          <label class="text-slate-400 text-[10px]">频谱/瀑布比例</label>
          <select
            v-model.number="spectrumSettings.splitRatio"
            class="bg-slate-900 border border-slate-700 rounded px-1.5 py-1 text-slate-200 text-[10px] focus:outline-none"
          >
            <option v-for="r in splitRatioOptions" :key="r.value" :value="r.value">
              {{ r.label }}
            </option>
          </select>
        </div>
      </div>

      <!-- Line Width & Visual Toggles -->
      <div class="flex flex-col gap-2 bg-sdr-dark p-2.5 rounded-lg border border-sdr-border">
        <!-- Line Width -->
        <div class="flex items-center justify-between text-[11px] text-slate-300">
          <span>频谱线条粗细</span>
          <div class="flex gap-1">
            <button
              v-for="lw in [1, 1.5, 2, 3]"
              :key="lw"
              @click="spectrumSettings.lineWidth = lw"
              :class="[
                'px-2 py-0.5 rounded text-[10px] font-mono transition-colors',
                spectrumSettings.lineWidth === lw
                  ? 'bg-blue-600 text-white font-bold'
                  : 'bg-slate-800 text-slate-400 hover:text-slate-200'
              ]"
            >
              {{ lw }}px
            </button>
          </div>
        </div>

        <!-- Gradient Fill Toggle -->
        <label class="flex items-center gap-2 cursor-pointer text-slate-300 text-[11px] pt-1.5 border-t border-slate-800">
          <input
            type="checkbox"
            v-model="spectrumSettings.fillSpectrum"
            class="rounded bg-slate-900 border-slate-700 text-blue-500 focus:ring-0 w-3.5 h-3.5"
          />
          <span>频谱面积发光渐变 (Gradient Fill)</span>
        </label>

        <!-- Grid Lines Toggle -->
        <label class="flex items-center gap-2 cursor-pointer text-slate-300 text-[11px]">
          <input
            type="checkbox"
            v-model="spectrumSettings.showGrid"
            class="rounded bg-slate-900 border-slate-700 text-blue-500 focus:ring-0 w-3.5 h-3.5"
          />
          <span>频率与分贝网格线 (Grid Graticule)</span>
        </label>
      </div>
    </div>

    <!-- Reset to Defaults Button -->
    <div class="pt-2">
      <button
        @click="resetFftDefaults"
        class="w-full py-2 bg-slate-800 hover:bg-slate-700 active:scale-98 border border-slate-700 hover:border-slate-600 rounded-lg text-slate-300 font-medium text-xs flex items-center justify-center gap-1.5 transition-all shadow-sm"
      >
        <RotateCcw class="w-3.5 h-3.5 text-slate-400" />
        <span>{{ currentLang === 'zh' ? '恢复 FFT 与显示默认参数' : 'Reset to Defaults' }}</span>
      </button>
    </div>
  </div>
</template>
