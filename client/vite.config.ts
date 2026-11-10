import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// https://vite.dev/config/
export default defineConfig({
  // Electron mở bản build bằng file:// nên asset phải là đường dẫn tương đối
  // (mặc định "/" sẽ trỏ về gốc ổ đĩa và trang trắng tinh).
  base: './',
  plugins: [react()],
})
