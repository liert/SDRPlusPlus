<script setup lang="ts">
import { ref, computed } from 'vue'
import {
  packetHistory,
  clearPackets,
  totalPacketsCount,
  validCrcCount,
  crcSuccessRate,
  currentLang
} from '@/composables/useSdrEngine'
import type { DecodedPacket } from '@/types/sdr'
import { Trash2, Copy, Download, Search, CheckCircle2, XCircle } from 'lucide-vue-next'

const searchQuery = ref('')
const selectedPacket = ref<DecodedPacket | null>(null)
const copySuccess = ref(false)

const filteredPackets = computed(() => {
  if (!searchQuery.value) return packetHistory.value
  const q = searchQuery.value.toLowerCase()
  return packetHistory.value.filter(
    p => p.syncWord.toLowerCase().includes(q) ||
         p.payloadHex.toLowerCase().includes(q) ||
         p.payloadAscii.toLowerCase().includes(q)
  )
})

function selectPacket(p: DecodedPacket) {
  selectedPacket.value = p
}

function copyHex(hex: string) {
  navigator.clipboard.writeText(hex)
  copySuccess.value = true
  setTimeout(() => copySuccess.value = false, 1500)
}

function exportJson() {
  const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(packetHistory.value, null, 2))
  const dlAnchor = document.createElement('a')
  dlAnchor.setAttribute("href", dataStr)
  dlAnchor.setAttribute("download", `sdr_packets_${Date.now()}.json`)
  dlAnchor.click()
}
</script>

<template>
  <div class="h-full flex flex-col bg-sdr-panel border border-sdr-border rounded-lg overflow-hidden select-none">
    <!-- Header & Action Bar -->
    <div class="p-3 bg-sdr-dark/60 border-b border-sdr-border flex items-center justify-between gap-3 shrink-0">
      <div class="flex items-center gap-3">
        <h3 class="text-xs font-bold text-slate-200 flex items-center gap-1.5">
          <span>{{ currentLang === 'zh' ? '实时解调数据流 (Packet Stream)' : 'Live Packet Stream' }}</span>
        </h3>
        <span class="text-[11px] font-mono text-slate-400">
          Total: <b class="text-slate-200">{{ totalPacketsCount }}</b> |
          CRC OK: <b class="text-emerald-400">{{ validCrcCount }}</b> ({{ crcSuccessRate }}%)
        </span>
      </div>

      <!-- Search & Action Buttons -->
      <div class="flex items-center gap-2">
        <div class="relative">
          <Search class="w-3.5 h-3.5 text-slate-500 absolute left-2 top-2" />
          <input
            v-model="searchQuery"
            placeholder="搜索 Hex / 同步字..."
            class="bg-sdr-dark border border-sdr-border rounded pl-7 pr-2 py-1 text-xs text-slate-200 font-mono placeholder:text-slate-500 focus:outline-none focus:border-blue-500 w-36"
          />
        </div>

        <button
          @click="exportJson"
          title="导出 JSON"
          class="p-1.5 bg-sdr-dark hover:bg-slate-800 border border-sdr-border rounded text-slate-300 transition-colors"
        >
          <Download class="w-3.5 h-3.5" />
        </button>

        <button
          @click="clearPackets"
          title="清空日志"
          class="p-1.5 bg-rose-500/10 hover:bg-rose-500/20 border border-rose-500/30 rounded text-rose-400 transition-colors"
        >
          <Trash2 class="w-3.5 h-3.5" />
        </button>
      </div>
    </div>

    <!-- Main Content: Split Table + Detail View -->
    <div class="flex-1 flex flex-col min-h-0">
      <!-- 1. Packet List Table -->
      <div class="flex-1 overflow-y-auto overflow-x-hidden font-mono text-xs">
        <table class="w-full text-left border-collapse">
          <thead class="bg-sdr-dark/90 sticky top-0 text-[11px] text-slate-400 border-b border-sdr-border">
            <tr>
              <th class="py-1.5 px-3 w-16">序号</th>
              <th class="py-1.5 px-3 w-24">时间</th>
              <th class="py-1.5 px-3 w-28">同步字</th>
              <th class="py-1.5 px-2 w-14">掩码</th>
              <th class="py-1.5 px-2 w-16">CRC</th>
              <th class="py-1.5 px-3">载荷数据 (Payload Hex)</th>
            </tr>
          </thead>
          <tbody class="divide-y divide-sdr-border/40 text-slate-300">
            <tr
              v-for="p in filteredPackets"
              :key="p.id"
              @click="selectPacket(p)"
              :class="[
                'cursor-pointer transition-colors',
                selectedPacket?.id === p.id ? 'bg-blue-600/20' : 'hover:bg-sdr-dark/60'
              ]"
            >
              <td class="py-1.5 px-3 text-slate-500">#{{ String(p.id).padStart(4, '0') }}</td>
              <td class="py-1.5 px-3 text-slate-400 text-[11px]">{{ p.timestamp }}</td>
              <td class="py-1.5 px-3 font-bold text-cyan-400">{{ p.syncWord }}</td>
              <td class="py-1.5 px-2 text-amber-400">{{ p.mask }}</td>
              <td class="py-1.5 px-2">
                <span
                  :class="[
                    'inline-flex items-center gap-1 px-1.5 py-0.2 rounded text-[10px] font-sans font-bold',
                    p.crcValid ? 'bg-emerald-500/10 text-emerald-400' : 'bg-rose-500/10 text-rose-400'
                  ]"
                >
                  <component :is="p.crcValid ? CheckCircle2 : XCircle" class="w-2.5 h-2.5" />
                  {{ p.crcValid ? 'OK' : 'ERR' }}
                </span>
              </td>
              <td class="py-1.5 px-3 text-slate-300 truncate max-w-xs font-mono text-[11px]">
                {{ p.payloadHex }}
              </td>
            </tr>
            <tr v-if="filteredPackets.length === 0">
              <td colspan="6" class="py-8 text-center text-slate-500 font-sans">
                {{ currentLang === 'zh' ? '暂无捕获数据帧，点击播放开始监听...' : 'No packets captured yet. Press Play to start.' }}
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- 2. Detail Inspector Panel for Selected Packet -->
      <div v-if="selectedPacket" class="h-36 bg-sdr-dark/95 border-t border-sdr-border p-3 flex flex-col gap-2 shrink-0">
        <div class="flex items-center justify-between">
          <div class="flex items-center gap-2 text-xs font-mono">
            <span class="font-bold text-slate-200">Packet #{{ selectedPacket.id }}</span>
            <span class="text-slate-500">|</span>
            <span class="text-slate-400">Len: {{ selectedPacket.length }}B</span>
            <span class="text-slate-500">|</span>
            <span class="text-cyan-400">Sync: {{ selectedPacket.syncWord }}</span>
            <span class="text-slate-500">|</span>
            <span class="text-amber-400">Mask: {{ selectedPacket.mask }}</span>
            <span class="text-slate-500">|</span>
            <span class="text-purple-400">HW CRC: {{ selectedPacket.hwCrc }}</span>
          </div>

          <button
            @click="copyHex(selectedPacket.payloadHex)"
            class="flex items-center gap-1 text-[11px] text-blue-400 hover:text-blue-300"
          >
            <Copy class="w-3 h-3" />
            <span>{{ copySuccess ? '已复制!' : '复制十六进制' }}</span>
          </button>
        </div>

        <!-- Hex & ASCII Split Grid -->
        <div class="flex-1 grid grid-cols-2 gap-3 bg-sdr-panel p-2 rounded border border-sdr-border font-mono text-[11px] overflow-auto">
          <div>
            <span class="text-[10px] text-slate-500 block mb-0.5">HEX BYTES:</span>
            <div class="text-slate-200 leading-relaxed break-all select-text">
              {{ selectedPacket.payloadHex }}
            </div>
          </div>
          <div>
            <span class="text-[10px] text-slate-500 block mb-0.5">ASCII PREVIEW:</span>
            <div class="text-emerald-400 leading-relaxed break-all select-text">
              {{ selectedPacket.payloadAscii || '[Non-printable binary data]' }}
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>
