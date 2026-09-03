export function generateColormapLut(name: string): Uint8ClampedArray {
  const lut = new Uint8ClampedArray(256 * 4)

  for (let i = 0; i < 256; i++) {
    const t = i / 255
    let r = 0, g = 0, b = 0

    if (name === 'turbo') {
      // Google Turbo Colormap polynomial approximation
      r = Math.min(1, Math.max(0, 0.1357 + t * (4.6153 + t * (-42.6603 + t * (132.131 + t * (-152.9423 + t * 59.2863))))))
      g = Math.min(1, Math.max(0, 0.0914 + t * (2.1941 + t * (4.8429 + t * (-14.185 + t * (4.2772 + t * 2.8295))))))
      b = Math.min(1, Math.max(0, 0.1066 + t * (12.585 + t * (-67.142 + t * (158.058 + t * (-177.348 + t * 74.453))))))
    } else if (name === 'plasma') {
      r = Math.sin(t * Math.PI * 0.9)
      g = Math.pow(t, 2.0)
      b = Math.cos(t * Math.PI * 0.5)
    } else if (name === 'viridis') {
      r = Math.min(1, Math.max(0, -0.05 + 1.6 * t - 0.6 * t * t))
      g = Math.min(1, Math.max(0, 0.1 + 1.2 * t - 0.4 * t * t))
      b = Math.min(1, Math.max(0, 0.3 + 0.9 * t - 1.2 * t * t))
    } else if (name === 'inferno') {
      r = Math.min(1, Math.max(0, 0.05 + 1.8 * t * t))
      g = Math.min(1, Math.max(0, -0.1 + 1.2 * Math.pow(t, 1.8)))
      b = Math.min(1, Math.max(0, 0.4 * t + 0.6 * Math.pow(t, 4.0)))
    } else if (name === 'hot') {
      r = Math.min(1, Math.max(0, t * 2.8))
      g = Math.min(1, Math.max(0, (t - 0.35) * 2.5))
      b = Math.min(1, Math.max(0, (t - 0.75) * 4.0))
    } else if (name === 'electric') {
      if (t < 0.2) { r = 0; g = 0; b = t * 5 }
      else if (t < 0.5) { r = 0; g = (t - 0.2) * 3.33; b = 1 }
      else if (t < 0.8) { r = (t - 0.5) * 3.33; g = 1; b = 1 - (t - 0.5) * 2 }
      else { r = 1; g = 1; b = (t - 0.8) * 5 }
    } else {
      // Greyscale
      r = t; g = t; b = t
    }

    lut[i * 4 + 0] = Math.round(r * 255)
    lut[i * 4 + 1] = Math.round(g * 255)
    lut[i * 4 + 2] = Math.round(b * 255)
    lut[i * 4 + 3] = 255
  }

  return lut
}
