/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        sdr: {
          dark: '#0f1117',
          panel: '#161922',
          border: '#232838',
          accent: '#3b82f6',
          cyan: '#06b6d4',
          emerald: '#10b981',
          amber: '#f59e0b',
          rose: '#f43f5e'
        }
      },
      fontFamily: {
        mono: ['"Cascadia Code"', 'Consolas', 'Menlo', 'monospace']
      }
    },
  },
  plugins: [],
}
