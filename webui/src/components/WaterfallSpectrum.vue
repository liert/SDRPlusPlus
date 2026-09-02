<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import {
  isPlaying,
  sourceConfig,
  vfo,
  spectrumSettings,
  currentLang,
  isBackendConnected,
  hasLiveHackRfData,
  dspIpcFps,
  dspSeq,
  logUi
} from '@/composables/useSdrEngine'
import { liveHackRfFft } from '@/composables/useHackRf'
import { generateColormapLut } from '@/composables/useColormaps'

const spectrumCanvas = ref<HTMLCanvasElement | null>(null)
const waterfallCanvas = ref<HTMLCanvasElement | null>(null)
const containerRef = ref<HTMLDivElement | null>(null)

let animFrameId: number | null = null
let colormapLut = generateColormapLut(spectrumSettings.colormap)

// Offline double-buffer waterfall canvases for smooth 60 FPS GPU scrolling
let canvasA: HTMLCanvasElement | null = null
let canvasB: HTMLCanvasElement | null = null
let ctxA: CanvasRenderingContext2D | null = null
let ctxB: CanvasRenderingContext2D | null = null
let activeWaterfallIdx = 0

// Preallocated single row buffer to eliminate garbage collection lag
let cachedRowImg: ImageData | null = null
let cachedRowWidth = 0

// Simulated / Live FFT power spectrum (1024 bins)
const fftSize = 1024
const currentFft = new Float32Array(fftSize).fill(-90)
const peakFft = new Float32Array(fftSize).fill(-120)

// Dragging & VFO interaction state
const isDraggingVfo = ref(false)
let dragStartX = 0
let initialOffsetHz = 0
let activeCanvasWidth = 1000

watch(() => spectrumSettings.colormap, (newMap) => {
  colormapLut = generateColormapLut(newMap)
})

function getRowImageData(ctx: CanvasRenderingContext2D, width: number): ImageData {
  if (!cachedRowImg || cachedRowWidth !== width) {
    cachedRowImg = ctx.createImageData(width, 1)
    cachedRowWidth = width
  }
  return cachedRowImg
}

function handleResize() {
  if (!containerRef.value || !spectrumCanvas.value || !waterfallCanvas.value) return
  const rect = containerRef.value.getBoundingClientRect()
  const w = Math.max(100, Math.floor(rect.width))
  const h = Math.max(100, Math.floor(rect.height))
  activeCanvasWidth = w

  const specH = Math.floor(h * 0.38)
  const wtfH = h - specH

  spectrumCanvas.value.width = w
  spectrumCanvas.value.height = specH

  waterfallCanvas.value.width = w
  waterfallCanvas.value.height = wtfH

  if (!canvasA) canvasA = document.createElement('canvas')
  if (!canvasB) canvasB = document.createElement('canvas')

  canvasA.width = w
  canvasA.height = wtfH
  canvasB.width = w
  canvasB.height = wtfH

  ctxA = canvasA.getContext('2d', { willReadFrequently: true })
  ctxB = canvasB.getContext('2d', { willReadFrequently: true })

  if (ctxA) { ctxA.fillStyle = '#090b10'; ctxA.fillRect(0, 0, w, wtfH); }
  if (ctxB) { ctxB.fillStyle = '#090b10'; ctxB.fillRect(0, 0, w, wtfH); }
}

function updateSimulatedFft(time: number) {
  const minDb = spectrumSettings.minDb
  const maxDb = spectrumSettings.maxDb

  // Case 1: Real-time Live FFT from C++ Backend when streaming
  if (isPlaying.value && hasLiveHackRfData.value) {
    for (let i = 0; i < fftSize; i++) {
      const liveVal = liveHackRfFft[i]
      const alpha = 1.0 - spectrumSettings.smoothing
      currentFft[i] = currentFft[i] * (1 - alpha) + liveVal * alpha

      if (currentFft[i] > peakFft[i]) {
        peakFft[i] = currentFft[i]
      } else {
        peakFft[i] -= 0.25
      }
    }
    return
  }

  // Case 2: Standby idle noise floor when stopped
  for (let i = 0; i < fftSize; i++) {
    const alpha = 0.2
    const idleNoise = minDb + Math.random() * 3.0
    currentFft[i] = currentFft[i] * (1 - alpha) + idleNoise * alpha
    peakFft[i] = Math.max(minDb, peakFft[i] - 0.5)
  }
}

let renderLogCounter = 0

function renderSpectrum(ctx: CanvasRenderingContext2D, width: number, height: number) {
  ctx.clearRect(0, 0, width, height)

  renderLogCounter++
  if (renderLogCounter % 180 === 1) {
    logUi(`[Canvas Spectrum] w=${width}, h=${height}, isPlaying=${isPlaying.value}, hasData=${hasLiveHackRfData.value}, centerDb=${currentFft[512].toFixed(1)} dBm, liveCenterDb=${liveHackRfFft[512].toFixed(1)} dBm`)
  }

  const minDb = spectrumSettings.minDb
  const maxDb = spectrumSettings.maxDb
  const dbRange = maxDb - minDb

  // 1. Draw Grid & dB Lines
  ctx.strokeStyle = '#1e2433'
  ctx.lineWidth = 1
  ctx.fillStyle = '#64748b'
  ctx.font = '10px "Cascadia Code", monospace'

  for (let db = maxDb; db >= minDb; db -= 15) {
    const y = ((maxDb - db) / dbRange) * height
    ctx.beginPath()
    ctx.moveTo(0, y)
    ctx.lineTo(width, y)
    ctx.stroke()
    ctx.fillText(`${db} dBm`, 6, y - 4)
  }

  // 2. Draw Frequency Grid
  const numFreqTicks = 8
  for (let t = 0; t <= numFreqTicks; t++) {
    const x = (t / numFreqTicks) * width
    ctx.beginPath()
    ctx.moveTo(x, 0)
    ctx.lineTo(x, height)
    ctx.stroke()

    const freqHz = sourceConfig.centerFreqHz + (t / numFreqTicks - 0.5) * sourceConfig.sampleRateHz
    const freqStr = (freqHz / 1e6).toFixed(3) + ' MHz'
    ctx.fillText(freqStr, x + 4, height - 6)
  }

  // 3. Draw VFO Highlight Band on Spectrum
  const vfoCenterX = width * (0.5 + vfo.offsetHz / sourceConfig.sampleRateHz)
  const vfoWidthPx = (vfo.bandwidthHz / sourceConfig.sampleRateHz) * width
  const vfoLeft = vfoCenterX - vfoWidthPx / 2

  ctx.fillStyle = 'rgba(59, 130, 246, 0.18)'
  ctx.fillRect(vfoLeft, 0, vfoWidthPx, height)

  ctx.strokeStyle = '#3b82f6'
  ctx.lineWidth = 1.5
  ctx.beginPath()
  ctx.moveTo(vfoCenterX, 0)
  ctx.lineTo(vfoCenterX, height)
  ctx.stroke()

  // 4. Draw Peak Hold Line
  if (spectrumSettings.peakHold) {
    ctx.strokeStyle = '#60a5fa'
    ctx.lineWidth = 1
    ctx.beginPath()
    for (let i = 0; i < fftSize; i++) {
      const x = (i / (fftSize - 1)) * width
      const y = Math.min(height, Math.max(0, ((maxDb - peakFft[i]) / dbRange) * height))
      if (i === 0) ctx.moveTo(x, y)
      else ctx.lineTo(x, y)
    }
    ctx.stroke()
  }

  // 5. Draw Active Spectrum Line with Gradient Fill
  const grad = ctx.createLinearGradient(0, 0, 0, height)
  grad.addColorStop(0, 'rgba(6, 182, 212, 0.4)')
  grad.addColorStop(0.6, 'rgba(59, 130, 246, 0.15)')
  grad.addColorStop(1, 'rgba(15, 23, 42, 0.0)')

  ctx.beginPath()
  for (let i = 0; i < fftSize; i++) {
    const x = (i / (fftSize - 1)) * width
    const y = Math.min(height, Math.max(0, ((maxDb - currentFft[i]) / dbRange) * height))
    if (i === 0) ctx.moveTo(x, y)
    else ctx.lineTo(x, y)
  }

  if (spectrumSettings.fillSpectrum) {
    ctx.lineTo(width, height)
    ctx.lineTo(0, height)
    ctx.closePath()
    ctx.fillStyle = grad
    ctx.fill()
  }

  ctx.strokeStyle = '#22d3ee'
  ctx.lineWidth = 1.5
  ctx.beginPath()
  for (let i = 0; i < fftSize; i++) {
    const x = (i / (fftSize - 1)) * width
    const y = Math.min(height, Math.max(0, ((maxDb - currentFft[i]) / dbRange) * height))
    if (i === 0) ctx.moveTo(x, y)
    else ctx.lineTo(x, y)
  }
  ctx.stroke()

  // 6. Draw Telemetry HUD
  if (width > 300) {
    ctx.fillStyle = 'rgba(15, 23, 42, 0.85)'
    ctx.fillRect(width - 245, 6, 238, 22)
    ctx.strokeStyle = '#334155'
    ctx.lineWidth = 1
    ctx.strokeRect(width - 245, 6, 238, 22)

    ctx.fillStyle = isPlaying.value ? '#34d399' : '#94a3b8'
    ctx.font = '10px "Cascadia Code", monospace'
    const hudText = isPlaying.value
      ? `⚡ SHM: ${dspIpcFps.value || 60} FPS | Seq: ${dspSeq.value}`
      : `⏸️ 待机中 (点击启动采集)`
    ctx.fillText(hudText, width - 238, 21)
  }
}

function renderWaterfall(ctx: CanvasRenderingContext2D, width: number, height: number) {
  if (!canvasA || !canvasB || !ctxA || !ctxB) return

  const srcCanvas = activeWaterfallIdx === 0 ? canvasA : canvasB
  const dstCanvas = activeWaterfallIdx === 0 ? canvasB : canvasA
  const dstCtx = activeWaterfallIdx === 0 ? ctxB : ctxA

  if (renderLogCounter % 180 === 1) {
    logUi(`[Canvas Waterfall] w=${width}, h=${height}, isPlaying=${isPlaying.value}, hasData=${hasLiveHackRfData.value}, activeIdx=${activeWaterfallIdx}`)
  }

  // 1. Advance waterfall buffer smoothly ONLY when streaming
  if (isPlaying.value && hasLiveHackRfData.value) {
    // Blit from srcCanvas to dstCanvas with 1px downward shift (100% valid GPU texture copy)
    dstCtx.drawImage(srcCanvas, 0, 0, width, height - 1, 0, 1, width, height - 1)

    const rowImg = getRowImageData(dstCtx, width)
    const minDb = spectrumSettings.minDb
    const maxDb = spectrumSettings.maxDb
    const dbRange = maxDb - minDb

    const data = rowImg.data
    for (let x = 0; x < width; x++) {
      const binIdx = Math.floor((x / width) * fftSize)
      const val = currentFft[binIdx]
      const norm = Math.min(1, Math.max(0, (val - minDb) / dbRange))
      const lutIdx = Math.floor(norm * 255)

      const px = x * 4
      data[px + 0] = colormapLut[lutIdx * 4 + 0]
      data[px + 1] = colormapLut[lutIdx * 4 + 1]
      data[px + 2] = colormapLut[lutIdx * 4 + 2]
      data[px + 3] = 255
    }

    dstCtx.putImageData(rowImg, 0, 0)
    activeWaterfallIdx = 1 - activeWaterfallIdx
  }

  // 2. Draw active waterfall image to visible screen
  const currentRenderCanvas = (isPlaying.value && hasLiveHackRfData.value) ? dstCanvas : srcCanvas
  ctx.clearRect(0, 0, width, height)
  ctx.drawImage(currentRenderCanvas, 0, 0)

  // 3. Always draw updated VFO highlight overlay (synced with spectrum at all times!)
  const vfoCenterX = width * (0.5 + vfo.offsetHz / sourceConfig.sampleRateHz)
  const vfoWidthPx = (vfo.bandwidthHz / sourceConfig.sampleRateHz) * width
  const vfoLeft = vfoCenterX - vfoWidthPx / 2

  ctx.fillStyle = 'rgba(59, 130, 246, 0.15)'
  ctx.fillRect(vfoLeft, 0, vfoWidthPx, height)

  ctx.strokeStyle = 'rgba(59, 130, 246, 0.9)'
  ctx.lineWidth = 1.5
  ctx.strokeRect(vfoLeft, 0, vfoWidthPx, height)

  // Center tuning red line
  ctx.strokeStyle = '#ef4444'
  ctx.lineWidth = 1.5
  ctx.beginPath()
  ctx.moveTo(vfoCenterX, 0)
  ctx.lineTo(vfoCenterX, height)
  ctx.stroke()
}

function loop(t: number) {
  const time = t * 0.001
  updateSimulatedFft(time)

  if (spectrumCanvas.value) {
    const ctx = spectrumCanvas.value.getContext('2d')
    if (ctx) renderSpectrum(ctx, spectrumCanvas.value.width, spectrumCanvas.value.height)
  }

  if (waterfallCanvas.value) {
    const ctx = waterfallCanvas.value.getContext('2d')
    if (ctx) renderWaterfall(ctx, waterfallCanvas.value.width, waterfallCanvas.value.height)
  }

  animFrameId = requestAnimationFrame(loop)
}

function onCanvasMouseDown(e: MouseEvent, targetCanvas: HTMLCanvasElement | null) {
  if (!targetCanvas) return
  const rect = targetCanvas.getBoundingClientRect()
  const x = e.clientX - rect.left
  const width = rect.width
  activeCanvasWidth = width

  const vfoCenterX = width * (0.5 + vfo.offsetHz / sourceConfig.sampleRateHz)
  const vfoWidthPx = (vfo.bandwidthHz / sourceConfig.sampleRateHz) * width

  if (Math.abs(x - vfoCenterX) < vfoWidthPx / 2) {
    isDraggingVfo.value = true
    dragStartX = e.clientX
    initialOffsetHz = vfo.offsetHz
  } else {
    const freqRatio = x / width - 0.5
    vfo.offsetHz = Math.round(freqRatio * sourceConfig.sampleRateHz)
    isDraggingVfo.value = true
    dragStartX = e.clientX
    initialOffsetHz = vfo.offsetHz
  }
}

function onGlobalMouseMove(e: MouseEvent) {
  if (!isDraggingVfo.value || activeCanvasWidth <= 0) return
  const dx = e.clientX - dragStartX
  const dHz = (dx / activeCanvasWidth) * sourceConfig.sampleRateHz
  vfo.offsetHz = Math.round(initialOffsetHz + dHz)
}

function onGlobalMouseUp() {
  isDraggingVfo.value = false
}

function onWheel(e: WheelEvent) {
  e.preventDefault()
  const delta = e.deltaY < 0 ? 100000 : -100000
  vfo.bandwidthHz = Math.max(100000, Math.min(sourceConfig.sampleRateHz, vfo.bandwidthHz + delta))
}

let resizeObserver: ResizeObserver | null = null

onMounted(() => {
  window.addEventListener('resize', handleResize)
  window.addEventListener('mousemove', onGlobalMouseMove)
  window.addEventListener('mouseup', onGlobalMouseUp)
  
  if (containerRef.value) {
    resizeObserver = new ResizeObserver(() => {
      handleResize()
    })
    resizeObserver.observe(containerRef.value)
  }

  handleResize()
  animFrameId = requestAnimationFrame(loop)
})

onUnmounted(() => {
  if (resizeObserver) {
    resizeObserver.disconnect()
    resizeObserver = null
  }
  window.removeEventListener('resize', handleResize)
  window.removeEventListener('mousemove', onGlobalMouseMove)
  window.removeEventListener('mouseup', onGlobalMouseUp)
  if (animFrameId) cancelAnimationFrame(animFrameId)
})
</script>

<template>
  <div ref="containerRef" class="relative w-full h-full flex flex-col bg-sdr-dark select-none overflow-hidden border border-sdr-border rounded-lg shadow-inner">
    <!-- Top Visual Toolbar Controls -->
    <div class="absolute top-2 right-3 z-20 flex items-center gap-2 bg-sdr-panel/85 backdrop-blur-md px-3 py-1.5 rounded-md border border-sdr-border text-xs">
      <span class="text-slate-400 font-mono">
        VFO: <b class="text-blue-400">{{ (vfo.offsetHz >= 0 ? '+' : '') + (vfo.offsetHz / 1e3).toFixed(1) }} kHz</b>
      </span>
      <div class="h-3.5 w-px bg-sdr-border"></div>
      <span class="text-slate-400 font-mono">
        BW: <b class="text-emerald-400">{{ (vfo.bandwidthHz / 1e3).toFixed(0) }} kHz</b>
      </span>
      <div class="h-3.5 w-px bg-sdr-border"></div>

      <!-- Colormap Dropdown -->
      <select v-model="spectrumSettings.colormap" class="bg-sdr-dark border border-sdr-border rounded px-2 py-0.5 text-slate-300 focus:outline-none focus:border-blue-500 font-sans text-xs">
        <option value="turbo">Turbo 色谱</option>
        <option value="viridis">Viridis 翠绿</option>
        <option value="plasma">Plasma 等离子</option>
        <option value="electric">Electric 电光</option>
        <option value="greyscale">Greyscale 灰阶</option>
      </select>
    </div>

    <!-- 1. Spectrum Canvas Area -->
    <div class="relative w-full flex-[0.38] min-h-[140px] cursor-crosshair">
      <canvas
        ref="spectrumCanvas"
        class="w-full h-full block"
        @mousedown="(e) => onCanvasMouseDown(e, spectrumCanvas)"
        @wheel="onWheel"
      ></canvas>
    </div>

    <!-- Divider Bar -->
    <div class="h-1 bg-sdr-border w-full flex items-center justify-center">
      <div class="w-12 h-0.5 bg-slate-600 rounded"></div>
    </div>

    <!-- 2. Waterfall Canvas Area -->
    <div class="relative w-full flex-[0.62] cursor-crosshair">
      <canvas
        ref="waterfallCanvas"
        class="w-full h-full block"
        @mousedown="(e) => onCanvasMouseDown(e, waterfallCanvas)"
        @wheel="onWheel"
      ></canvas>
    </div>
  </div>
</template>
