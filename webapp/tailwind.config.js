/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        radar: {
          "primary": "#ffffff",      // White
          "secondary": "#ffffff",    // White
          "green": "#ffffff",        // White
          "red": "#ffffff"           // White
        }
      },
    },
  },
  plugins: [],
}

