<script setup lang="ts">
import { demodConfig, applyPreset, currentLang } from '@/composables/useSdrEngine'
import { Radio, ShieldCheck, Binary, Cpu } from 'lucide-vue-next'

const presetOptions = [
  'SX1280 FLRC (1.3 Mbps)',
  'SX1280 FLRC (520 kbps)',
  'SX1280 FLRC (260 kbps)',
  'Custom 2FSK / GFSK'
]

const modulationOptions = ['FLRC', '2FSK', 'GFSK', 'CPFSK', '4FSK', 'OOK', 'AM', 'FM']
</script>

<template>
  <div class="flex flex-col gap-4 text-xs">
    <!-- 1. Presets & Modulation Selection -->
    <div class="grid grid-cols-2 gap-3">
      <div class="flex flex-col gap-1">
        <label class="text-slate-400 font-semibold">{{ currentLang === 'zh' ? '解调预设 (Preset)' : 'Preset' }}</label>
        <select
          v-model="demodConfig.preset"
          @change="applyPreset(demodConfig.preset)"
          class="w-full bg-sdr-dark border border-sdr-border rounded px-2.5 py-1 text-slate-200 focus:outline-none focus:border-blue-500 font-sans"
        >
          <option v-for="p in presetOptions" :key="p" :value="p">{{ p }}</option>
        </select>
      </div>

      <div class="flex flex-col gap-1">
        <label class="text-slate-400 font-semibold">{{ currentLang === 'zh' ? '调制模式 (Modulation)' : 'Modulation' }}</label>
        <select
          v-model="demodConfig.modulation"
          class="w-full bg-sdr-dark border border-sdr-border rounded px-2.5 py-1 text-slate-200 focus:outline-none focus:border-blue-500 font-sans font-bold"
        >
          <option v-for="m in modulationOptions" :key="m" :value="m">{{ m }}</option>
        </select>
      </div>
    </div>

    <!-- 2. Physical Layer Modulation Fine-Tuning -->
    <div class="p-3 bg-sdr-dark/60 rounded-md border border-sdr-border flex flex-col gap-2.5">
      <div class="flex items-center gap-1.5 text-slate-300 font-semibold">
        <Radio class="w-3.5 h-3.5 text-blue-400" />
        <span>{{ currentLang === 'zh' ? '物理层解调参数' : 'Physical Demod Params' }}</span>
      </div>

      <div class="grid grid-cols-3 gap-2">
        <!-- Bitrate -->
        <div class="flex flex-col gap-0.5">
          <label class="text-[11px] text-slate-400">比特率 (kbps)</label>
          <input
            type="number"
            :value="demodConfig.symbolRate / 1e3"
            @input="(e: any) => demodConfig.symbolRate = Math.max(1000, Number(e.target.value) * 1e3)"
            class="w-full bg-sdr-panel border border-sdr-border rounded px-2 py-1 text-slate-200 font-mono focus:outline-none focus:border-blue-500"
          />
        </div>

        <!-- Deviation -->
        <div class="flex flex-col gap-0.5">
          <label class="text-[11px] text-slate-400">频偏 (kHz)</label>
          <input
            type="number"
            :value="demodConfig.deviation / 1e3"
            @input="(e: any) => demodConfig.deviation = Math.max(1000, Number(e.target.value) * 1e3)"
            class="w-full bg-sdr-panel border border-sdr-border rounded px-2 py-1 text-slate-200 font-mono focus:outline-none focus:border-blue-500"
          />
        </div>

        <!-- Filter Cutoff -->
        <div class="flex flex-col gap-0.5">
          <label class="text-[11px] text-slate-400">低通截止 (kHz)</label>
          <input
            type="number"
            :value="demodConfig.filterCutoff / 1e3"
            @input="(e: any) => demodConfig.filterCutoff = Math.max(1000, Number(e.target.value) * 1e3)"
            class="w-full bg-sdr-panel border border-sdr-border rounded px-2 py-1 text-slate-200 font-mono focus:outline-none focus:border-blue-500"
          />
        </div>
      </div>
    </div>

    <!-- 3. Preamble & Sync Settings -->
    <div class="p-3 bg-sdr-dark/60 rounded-md border border-sdr-border flex flex-col gap-2.5">
      <div class="flex items-center gap-1.5 text-slate-300 font-semibold">
        <Binary class="w-3.5 h-3.5 text-emerald-400" />
        <span>{{ currentLang === 'zh' ? '前导码与时序恢复' : 'Preamble & Timing Sync' }}</span>
      </div>

      <div class="flex items-center justify-between">
        <label class="flex items-center gap-1.5 cursor-pointer text-slate-300">
          <input type="checkbox" v-model="demodConfig.enableAgcPreamble" class="rounded bg-sdr-dark border-sdr-border text-emerald-500" />
          <span>32位 AGC 前导码 (0101...)</span>
        </label>
        <span class="text-slate-400 font-mono text-[11px]">门限: {{ demodConfig.agcThreshold }}</span>
      </div>
      <input type="range" min="1.0" max="8.0" step="0.5" v-model.number="demodConfig.agcThreshold" class="w-full accent-emerald-500 -mt-1" />

      <div class="flex items-center justify-between pt-1">
        <label class="flex items-center gap-1.5 cursor-pointer text-slate-300">
          <input type="checkbox" v-model="demodConfig.enableTimingPreamble" class="rounded bg-sdr-dark border-sdr-border text-emerald-500" />
          <span>21位时序恢复前导</span>
        </label>
        <div class="flex items-center gap-1 text-[11px] text-slate-400">
          <span>容差:</span>
          <select v-model.number="demodConfig.timingTolerance" class="bg-sdr-panel border border-sdr-border rounded px-1 py-0.5 text-slate-200">
            <option :value="1">1 bit</option>
            <option :value="2">2 bit</option>
            <option :value="3">3 bit</option>
            <option :value="4">4 bit</option>
          </select>
        </div>
      </div>
    </div>

    <!-- 4. Sync Word, Mask & CRC Verification -->
    <div class="p-3 bg-sdr-dark/60 rounded-md border border-sdr-border flex flex-col gap-2.5">
      <div class="flex items-center gap-1.5 text-slate-300 font-semibold">
        <ShieldCheck class="w-3.5 h-3.5 text-amber-400" />
        <span>{{ currentLang === 'zh' ? '同步字与差分解扰' : 'Sync Word & Descrambler' }}</span>
      </div>

      <!-- Auto Sync Checkbox -->
      <label class="flex items-center gap-1.5 cursor-pointer text-slate-300">
        <input type="checkbox" v-model="demodConfig.autoSyncWord" class="rounded bg-sdr-dark border-sdr-border text-amber-500" />
        <span class="font-medium text-amber-300">{{ currentLang === 'zh' ? '自动同步字捕获 (推荐: 捕获任意有效帧)' : 'Auto Sync Word Capture' }}</span>
      </label>

      <!-- Sync Word Hex (if manual) -->
      <div v-if="!demodConfig.autoSyncWord" class="flex flex-col gap-1">
        <label class="text-[11px] text-slate-400">同步字 (Hex, 4字节)</label>
        <input
          v-model="demodConfig.syncWord"
          maxlength="8"
          class="w-full bg-sdr-panel border border-sdr-border rounded px-2 py-1 text-cyan-300 font-mono font-bold uppercase focus:outline-none focus:border-amber-500"
          placeholder="54313253"
        />
      </div>

      <!-- Mask Mode -->
      <div class="flex flex-col gap-1">
        <label class="text-[11px] text-slate-400">CR=1 掩码模式</label>
        <select
          v-model="demodConfig.maskMode"
          class="w-full bg-sdr-panel border border-sdr-border rounded px-2 py-1 text-slate-200 text-xs focus:outline-none focus:border-amber-500"
        >
          <option value="auto">自动双掩码 (Auto 0x99 / 0x66)</option>
          <option value="0x99">固定 0x99 (遥控器发送端)</option>
          <option value="0x66">固定 0x66 (接收机应答端)</option>
          <option value="none">无掩码 (0x00)</option>
        </select>
      </div>

      <!-- Switches -->
      <div class="grid grid-cols-2 gap-2 pt-1">
        <label class="flex items-center gap-1.5 cursor-pointer text-slate-300 text-[11px]">
          <input type="checkbox" v-model="demodConfig.differentialDecode" class="rounded bg-sdr-dark border-sdr-border text-amber-500" />
          <span>差分 CumXOR</span>
        </label>
        <label class="flex items-center gap-1.5 cursor-pointer text-slate-300 text-[11px]">
          <input type="checkbox" v-model="demodConfig.enableHwCrc" class="rounded bg-sdr-dark border-sdr-border text-amber-500" />
          <span>硬件 CRC-32 校验</span>
        </label>
      </div>
    </div>
  </div>
</template>
