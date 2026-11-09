/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        background: '#070a12',
        surface: '#0b0f19',
        card: 'rgba(15, 23, 42, 0.75)',
        'card-hover': 'rgba(22, 33, 58, 0.9)',
        border: 'rgba(255, 255, 255, 0.08)',
        'border-active': 'rgba(99, 102, 241, 0.5)',
      },
      fontFamily: {
        sans: ['-apple-system', 'BlinkMacSystemFont', '"Segoe UI"', 'Roboto', '"Helvetica Neue"', 'Arial', 'sans-serif'],
        mono: ['Consolas', '"Cascadia Mono"', 'ui-monospace', 'monospace'],
      },
      animation: {
        'pulse-subtle': 'pulseSubtle 2s infinite ease-in-out',
      },
      keyframes: {
        pulseSubtle: {
          '0%, 100%': { opacity: '1', transform: 'scale(1)' },
          '50%': { opacity: '0.6', transform: 'scale(1.12)' },
        }
      }
    },
  },
  plugins: [],
}
