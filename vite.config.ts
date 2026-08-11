import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
import { fileURLToPath, URL } from 'node:url';

const configuredOutDir = process.env.YEMAN_BUILD_WEB_DIR?.trim();

// base './' => relative asset paths so the built dist works both from
// a file:// dev server and from the shell's virtual host (https://app.localhost).
export default defineConfig({
  base: './',
  plugins: [vue()],
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  build: {
    outDir: configuredOutDir || 'dist',
    emptyOutDir: true,
    target: 'es2020',
    assetsDir: 'assets',
  },
  server: {
    port: 5173,
    strictPort: false,
  },
});
