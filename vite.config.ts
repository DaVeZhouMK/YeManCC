import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
import { RADIAL_USER_AVAILABLE } from './src/radial/availability';
import { fileURLToPath, URL } from 'node:url';

const configuredOutDir = process.env.YEMAN_BUILD_WEB_DIR?.trim();
const configuredPublicDir = process.env.YEMAN_BUILD_PUBLIC_DIR?.trim();

// base './' => relative asset paths so the built dist works both from
// a file:// dev server and from the shell's virtual host (https://app.localhost).
export default defineConfig({
  base: './',
  publicDir: configuredPublicDir || 'public',
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
    rollupOptions: { input: { ...(RADIAL_USER_AVAILABLE ? { radial: fileURLToPath(new URL('./radial-output.html', import.meta.url)) } : {}), main: fileURLToPath(new URL('./index.html', import.meta.url)), hud: fileURLToPath(new URL('./hud-output.html', import.meta.url)), notice: fileURLToPath(new URL('./notice-output.html', import.meta.url)) } },
  },
  server: {
    port: 5173,
    strictPort: false,
  },
});
