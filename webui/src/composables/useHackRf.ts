import { ref } from 'vue'

// High-Performance FFT buffer shared from C++ Backend stream
export const liveHackRfFft = new Float32Array(1024).fill(-110)
export const hasLiveHackRfData = ref(false)
