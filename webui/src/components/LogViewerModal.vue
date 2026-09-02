<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import { invoke } from '@tauri-apps/api/core'
import { isTauriEnv, isBackendConnected, currentLang } from '@/composables/useSdrEngine'
import { Terminal, RefreshCw, X, Play, RotateCcw, Copy, Check } from 'lucide-vue-next'

const emit = defineEmits<{
  (e: 'close'): void
}>()

const logContent = ref('加载日志中...')
const isRefreshing = ref(false)
const isRestarting = ref(false)
const copied = ref(false)
let autoRefreshTimer: number | null = null

async function fetchLogs() {
  isRefreshing.value = true
  if (isTauriEnv) {
    try {
      const logs = await invoke<string>('get_backend_logs')
      logContent.value = logs || '暂无日志记录。'
    } catch (e: any) {
      logContent.value = `获取日志失败: ${e?.message || e}`
    }
  } else {
    try {
      const resp = await fetch('http://127.0.0.1:5259/api/status')
      const data = await resp.json()
      logContent.value = JSON.stringify(data, null, 2)
    } catch (e: any) {
      logContent.value = `网络模式获取状态失败: ${e?.message || e}`
    }
  }
  isRefreshing.value = false
}

async function restartBackend() {
  if (!isTauriEnv) return
  isRestarting.value = true
  try {
    await invoke('restart_backend')
    setTimeout(() => {
      fetchLogs()
      isRestarting.value = false
    }, 800)
  } catch (e) {
    isRestarting.value = false
  }
}

function copyLogs() {
  navigator.clipboard.writeText(logContent.value)
  copied.value = true
  setTimeout(() => { copied.value = false }, 2000)
}

onMounted(() => {
  fetchLogs()
  autoRefreshTimer = window.setInterval(fetchLogs, 2000)
})

onUnmounted(() => {
  if (autoRefreshTimer) clearInterval(autoRefreshTimer)
})
</script>

<template>
  <div class="fixed inset-0 bg-black/70 backdrop-blur-sm z-50 flex items-center justify-center p-4">
    <div class="bg-sdr-panel border border-sdr-border rounded-xl shadow-2xl w-full max-w-4xl flex flex-col h-[80vh] overflow-hidden">
      <!-- Header -->
      <div class="h-12 bg-sdr-dark border-b border-sdr-border flex items-center justify-between px-4 select-none shrink-0">
        <div class="flex items-center gap-2 font-mono text-sm font-bold text-cyan-300">
          <Terminal class="w-4 h-4 text-cyan-400" />
          <span>{{ currentLang === 'zh' ? 'C++ 后端运行与调试日志 (sdrpp_backend.log)' : 'C++ Backend Runtime Logs' }}</span>
          <span :class="['px-2 py-0.5 rounded text-[10px] font-sans font-bold', isBackendConnected ? 'bg-emerald-950 text-emerald-400 border border-emerald-800' : 'bg-rose-950 text-rose-400 border border-rose-800']">
            {{ isBackendConnected ? (currentLang === 'zh' ? '后端运行中' : 'Running') : (currentLang === 'zh' ? '后端离线' : 'Offline') }}
          </span>
        </div>

        <div class="flex items-center gap-2">
          <!-- Copy Button -->
          <button
            @click="copyLogs"
            class="px-2.5 py-1 bg-sdr-panel hover:bg-slate-700 border border-sdr-border rounded text-xs text-slate-300 flex items-center gap-1 transition-colors"
          >
            <component :is="copied ? Check : Copy" class="w-3.5 h-3.5 text-cyan-400" />
            <span>{{ copied ? (currentLang === 'zh' ? '已复制' : 'Copied') : (currentLang === 'zh' ? '复制日志' : 'Copy') }}</span>
          </button>

          <!-- Refresh Button -->
          <button
            @click="fetchLogs"
            :class="['px-2.5 py-1 bg-sdr-panel hover:bg-slate-700 border border-sdr-border rounded text-xs text-slate-300 flex items-center gap-1 transition-colors', isRefreshing ? 'opacity-50' : '']"
          >
            <RefreshCw :class="['w-3.5 h-3.5 text-emerald-400', isRefreshing ? 'animate-spin' : '']" />
            <span>{{ currentLang === 'zh' ? '刷新' : 'Refresh' }}</span>
          </button>

          <!-- Restart Backend Button (Tauri only) -->
          <button
            v-if="isTauriEnv"
            @click="restartBackend"
            :class="['px-2.5 py-1 bg-amber-950/80 hover:bg-amber-900 border border-amber-800 rounded text-xs text-amber-300 flex items-center gap-1 transition-colors font-bold', isRestarting ? 'opacity-50' : '']"
          >
            <RotateCcw :class="['w-3.5 h-3.5', isRestarting ? 'animate-spin' : '']" />
            <span>{{ currentLang === 'zh' ? '重启 C++ 后端' : 'Restart Backend' }}</span>
          </button>

          <!-- Close Button -->
          <button
            @click="emit('close')"
            class="p-1 hover:bg-rose-900/50 hover:text-rose-400 rounded text-slate-400 transition-colors"
          >
            <X class="w-5 h-5" />
          </button>
        </div>
      </div>

      <!-- Log Content Area -->
      <div class="flex-1 bg-[#0b0f19] p-4 overflow-y-auto font-mono text-xs text-slate-300 leading-relaxed whitespace-pre-wrap select-text">
        {{ logContent }}
      </div>

      <!-- Footer Info -->
      <div class="h-8 bg-sdr-dark border-t border-sdr-border px-4 flex items-center justify-between text-[11px] text-slate-400 font-mono shrink-0">
        <span>日志路径: <code>sdrpp_bin/sdrpp_backend.log</code></span>
        <span>自动刷新: 2 秒 / 次</span>
      </div>
    </div>
  </div>
</template>
