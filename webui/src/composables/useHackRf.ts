import { ref } from 'vue'

// High-Performance FFT buffer shared from C++ Backend stream (capacity up to 4096 bins)
export let liveHackRfFft = new Float32Array(4096).fill(-110)
export const liveFftSize = ref(1024)
export const hasLiveHackRfData = ref(false)

export function updateLiveFftData(data: number[] | Float32Array) {
  if (data.length === 0) return
  if (liveHackRfFft.length !== data.length) {
    liveHackRfFft = new Float32Array(data.length)
  }
  liveHackRfFft.set(data)
  liveFftSize.value = data.length
  if (!hasLiveHackRfData.value) {
    hasLiveHackRfData.value = true
  }
}
