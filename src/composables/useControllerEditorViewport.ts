import { computed, onBeforeUnmount, onMounted, ref, type Ref } from 'vue';

// The approved editor uses 1280×720 CSS pixels (also 1080p at 150% DPI).
// ResizeObserver supplies CSS pixels after Windows/WebView scaling: multiplying
// by devicePixelRatio here would apply DPI a second time.
const DESIGN_WIDTH = 1280;
const DESIGN_HEIGHT = 720;
export function controllerEditorScale(width: number, height: number): number {
  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) return 1;
  return Math.max(1, Math.min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT));
}

/** Event-driven local zoom; it never changes the shell or runs per input frame. */
export function useControllerEditorViewport(overlay: Ref<HTMLElement | null>) {
  const scale = ref(1);
  let observer: ResizeObserver | null = null;
  const resize = () => {
    const el = overlay.value;
    if (el) scale.value = controllerEditorScale(el.clientWidth, el.clientHeight);
  };
  onMounted(() => {
    resize();
    if (typeof ResizeObserver === 'function' && overlay.value) {
      observer = new ResizeObserver(entries => {
        const entry = entries.find(entry => entry.target === overlay.value);
        if (entry) scale.value = controllerEditorScale(entry.contentRect.width, entry.contentRect.height);
      });
      observer.observe(overlay.value);
    } else window.addEventListener('resize', resize);
  });
  onBeforeUnmount(() => { observer?.disconnect(); window.removeEventListener('resize', resize); });
  const stageStyle = computed(() => ({
    zoom: scale.value,
    width: '100%',
    height: '100%',
  }));
  return { scale, stageStyle };
}
