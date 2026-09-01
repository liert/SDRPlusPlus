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
import {
  Trash2,
  Copy,
  Download,
  Search,
  CheckCircle2,
  XCircle,
  FileCode,
  Layers,
  BarChart,
  Filter,
  Check
} from 'lucide-vue-next'

const emit = defineEmits<{
  (e: 'close'): void
}>()

const searchQuery = ref('')
const filterCrc = ref<'all' | 'valid' | 'invalid'>('all')
const selectedPacket = ref<DecodedPacket | null>(packetHistory.value[0] || null)
const copied = ref(false)

const filteredPackets = computed(() => {
  let list = packetHistory.value
  if (filterCrc.value === 'valid') {
    list = list.filter(p => p.crcValid)
  } else if (filterCrc.value === 'invalid') {
    list = list.filter(p => !p.crcValid)
  }

  if (searchQuery.value) {
    const q = searchQuery.value.toLowerCase()
    list = list.filter(
      p => p.syncWord.toLowerCase().includes(q) ||
           p.payloadHex.toLowerCase().includes(q) ||
           p.payloadAscii.toLowerCase().includes(q)
    )
  }
  return list
})

function copyHex(hex: string) {
  navigator.clipboard.writeText(hex)
  copied.value = true
  setTimeout(() => copied.value = false, 1500)
}

function exportJson() {
  const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(packetHistory.value, null, 2))
  const dlAnchor = document.createElement('a')
  dlAnchor.setAttribute("href", dataStr)
  dlAnchor.setAttribute("download", `sdr_protocol_capture_${Date.now()}.json`)
  dlAnchor.click()
}
</script>

<template>
  <div class="w-full h-full flex flex-col bg-sdr-dark text-slate-200 select-none overflow-hidden">
    <!-- Top Workbench Header -->
    <div class="h-12 bg-sdr-panel border-b border-sdr-border px-4 flex items-center justify-between shrink-0">
      <div class="flex items-center gap-3">
        <div class="flex items-center gap-2">
          <Layers class="w-4 h-4 text-cyan-400" />
          <h2 class="text-sm font-bold text-slate-100">
            {{ currentLang === 'zh' ? '深度协议分析工作台 (Protocol Workbench)' : 'Protocol Workbench' }}
          </h2>
        </div>

        <div class="h-4 w-px bg-sdr-border"></div>

        <!-- Filter Stats Summary -->
        <div class="flex items-center gap-3 text-xs font-mono text-slate-400">
          <span>总抓包: <b class="text-slate-200">{{ totalPacketsCount }}</b></span>
          <span>CRC成功: <b class="text-emerald-400">{{ validCrcCount }}</b></span>
          <span>有效率: <b class="text-cyan-400">{{ crcSuccessRate }}%</b></span>
        </div>
      </div>

      <!-- Action Buttons -->
      <div class="flex items-center gap-2.5">
        <!-- Search -->
        <div class="relative">
          <Search class="w-3.5 h-3.5 text-slate-500 absolute left-2.5 top-2" />
          <input
            v-model="searchQuery"
            placeholder="过滤搜索 Hex / ASCII / 同步字..."
            class="bg-sdr-dark border border-sdr-border rounded-md pl-8 pr-3 py-1 text-xs text-slate-200 font-mono placeholder:text-slate-500 focus:outline-none focus:border-cyan-500 w-56"
          />
        </div>

        <!-- CRC Filter Dropdown -->
        <select
          v-model="filterCrc"
          class="bg-sdr-dark border border-sdr-border rounded-md px-2.5 py-1 text-xs text-slate-300 focus:outline-none focus:border-cyan-500 font-sans"
        >
          <option value="all">全部报文 (All)</option>
          <option value="valid">仅 CRC 校验成功 (Valid)</option>
          <option value="invalid">仅 CRC 错误 (Errors)</option>
        </select>

        <!-- Export Button -->
        <button
          @click="exportJson"
          class="flex items-center gap-1.5 px-3 py-1 bg-sdr-dark hover:bg-slate-800 border border-sdr-border rounded-md text-xs font-medium text-slate-300 transition-colors"
        >
          <Download class="w-3.5 h-3.5" />
          <span>导出 JSON</span>
        </button>

        <!-- Clear Button -->
        <button
          @click="clearPackets"
          class="flex items-center gap-1.5 px-3 py-1 bg-rose-500/10 hover:bg-rose-500/20 border border-rose-500/30 rounded-md text-xs font-medium text-rose-400 transition-colors"
        >
          <Trash2 class="w-3.5 h-3.5" />
          <span>清空</span>
        </button>
      </div>
    </div>

    <!-- Main Workspace: Split into Left Packet Table and Right Protocol Inspector -->
    <div class="flex-1 flex min-h-0 divide-x divide-sdr-border">
      
      <!-- Left Column: Packet List Table (60% width) -->
      <div class="flex-[0.6] flex flex-col min-w-0 bg-sdr-panel/50">
        <div class="flex-1 overflow-auto font-mono text-xs">
          <table class="w-full text-left border-collapse">
            <thead class="bg-sdr-dark/95 sticky top-0 text-[11px] text-slate-400 border-b border-sdr-border z-10">
              <tr>
                <th class="py-2 px-3 w-16">序号</th>
                <th class="py-2 px-3 w-28">时间</th>
                <th class="py-2 px-3 w-28">载波频偏</th>
                <th class="py-2 px-3 w-28">物理同步字</th>
                <th class="py-2 px-2 w-16">掩码</th>
                <th class="py-2 px-2 w-20">CRC-32</th>
                <th class="py-2 px-3">载荷十六进制数据 (Payload Hex)</th>
              </tr>
            </thead>
            <tbody class="divide-y divide-sdr-border/40 text-slate-300">
              <tr
                v-for="p in filteredPackets"
                :key="p.id"
                @click="selectedPacket = p"
                :class="[
                  'cursor-pointer transition-colors',
                  selectedPacket?.id === p.id ? 'bg-blue-600/25 text-white' : 'hover:bg-sdr-dark/70'
                ]"
              >
                <td class="py-2 px-3 text-slate-500">#{{ String(p.id).padStart(4, '0') }}</td>
                <td class="py-2 px-3 text-slate-400 text-[11px]">{{ p.timestamp }}</td>
                <td class="py-2 px-3 text-slate-300">{{ p.freqOffsetKhz >= 0 ? '+' : '' }}{{ p.freqOffsetKhz }} kHz</td>
                <td class="py-2 px-3 font-bold text-cyan-400">{{ p.syncWord }}</td>
                <td class="py-2 px-2 text-amber-400">{{ p.mask }}</td>
                <td class="py-2 px-2">
                  <span
                    :class="[
                      'inline-flex items-center gap-1 px-1.5 py-0.5 rounded text-[10px] font-sans font-bold',
                      p.crcValid ? 'bg-emerald-500/15 text-emerald-400' : 'bg-rose-500/15 text-rose-400'
                    ]"
                  >
                    <component :is="p.crcValid ? CheckCircle2 : XCircle" class="w-3 h-3" />
                    {{ p.crcValid ? 'PASSED' : 'ERROR' }}
                  </span>
                </td>
                <td class="py-2 px-3 truncate max-w-xs font-mono text-[11px] text-slate-300">
                  {{ p.payloadHex }}
                </td>
              </tr>
              <tr v-if="filteredPackets.length === 0">
                <td colspan="7" class="py-16 text-center text-slate-500 font-sans text-sm">
                  没有匹配的报文记录，请开启播放并保持信号监听。
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

      <!-- Right Column: Deep Protocol Tree & Hex Dump Inspector (40% width) -->
      <div class="flex-[0.4] flex flex-col min-w-0 bg-sdr-dark/90 p-4 gap-4 overflow-y-auto">
        <div v-if="selectedPacket" class="flex flex-col gap-4">
          <!-- Inspector Header -->
          <div class="flex items-center justify-between pb-3 border-b border-sdr-border">
            <div class="flex items-center gap-2">
              <FileCode class="w-4 h-4 text-cyan-400" />
              <h3 class="text-sm font-bold text-slate-100">报文深度解析 (#{{ selectedPacket.id }})</h3>
            </div>

            <button
              @click="copyHex(selectedPacket.payloadHex)"
              class="flex items-center gap-1.5 text-xs text-cyan-400 hover:text-cyan-300 bg-cyan-500/10 hover:bg-cyan-500/20 px-2.5 py-1 rounded border border-cyan-500/30 transition-colors"
            >
              <component :is="copied ? Check : Copy" class="w-3.5 h-3.5" />
              <span>{{ copied ? '已复制十六进制!' : '复制 Hex 载荷' }}</span>
            </button>
          </div>

          <!-- Protocol Layer Breakdown Cards -->
          <div class="flex flex-col gap-2.5 text-xs font-sans">
            <!-- 1. Physical Layer Card -->
            <div class="bg-sdr-panel p-3 rounded-md border border-sdr-border flex flex-col gap-1.5">
              <span class="text-slate-400 font-bold uppercase tracking-wider text-[11px] flex items-center justify-between">
                <span>[PHY] 物理层前导与同步</span>
                <span class="text-emerald-400 font-mono">得分: {{ selectedPacket.score }}</span>
              </span>
              <div class="grid grid-cols-2 gap-2 text-[11px] font-mono text-slate-300">
                <div>AGC 前导: <b class="text-slate-100">32-bit (0101...)</b></div>
                <div>时序恢复: <b class="text-slate-100">21-bit (0x043EE2)</b></div>
                <div>同步字: <b class="text-cyan-400">{{ selectedPacket.syncWord }}</b></div>
                <div>载波偏置: <b class="text-slate-100">{{ selectedPacket.freqOffsetKhz }} kHz</b></div>
              </div>
            </div>

            <!-- 2. Framing & Descrambling Layer -->
            <div class="bg-sdr-panel p-3 rounded-md border border-sdr-border flex flex-col gap-1.5">
              <span class="text-slate-400 font-bold uppercase tracking-wider text-[11px]">[MAC] 差分解扰与校验</span>
              <div class="grid grid-cols-2 gap-2 text-[11px] font-mono text-slate-300">
                <div>差分还原: <b class="text-emerald-400">CumXOR OK</b></div>
                <div>CR=1 掩码: <b class="text-amber-400">{{ selectedPacket.mask }}</b></div>
                <div>硬件 CRC-32: <b class="text-purple-400">{{ selectedPacket.hwCrc }}</b></div>
                <div>校验状态: <b :class="selectedPacket.crcValid ? 'text-emerald-400' : 'text-rose-400'">{{ selectedPacket.crcValid ? 'PASSED (0x04C11DB7)' : 'FAILED' }}</b></div>
              </div>
            </div>

            <!-- 3. Payload Layer Card -->
            <div class="bg-sdr-panel p-3 rounded-md border border-sdr-border flex flex-col gap-1.5">
              <span class="text-slate-400 font-bold uppercase tracking-wider text-[11px]">[APP] 载荷明文信息 ({{ selectedPacket.length }} 字节)</span>
              <div class="text-[11px] font-mono text-slate-300">
                ASCII 提取: <span class="text-emerald-400 font-semibold">{{ selectedPacket.payloadAscii || '[无可见文本]' }}</span>
              </div>
            </div>
          </div>

          <!-- Full 16-byte Aligned Hex Dump Inspector -->
          <div class="flex flex-col gap-1.5">
            <span class="text-xs font-bold text-slate-400">十六进制与字符双栏 Dump (16-Byte Aligned Hex View):</span>
            <div class="bg-sdr-panel border border-sdr-border p-3 rounded-md font-mono text-xs text-slate-300 leading-relaxed overflow-x-auto select-text shadow-inner">
              <div class="text-cyan-300 break-all font-bold tracking-wider">
                {{ selectedPacket.payloadHex }}
              </div>
            </div>
          </div>
        </div>

        <div v-else class="h-full flex flex-col items-center justify-center text-slate-500 gap-2">
          <Layers class="w-8 h-8 stroke-1 text-slate-600" />
          <span class="text-xs">在左侧列表中点击任意报文查看分层深度解析</span>
        </div>
      </div>
    </div>
  </div>
</template>
