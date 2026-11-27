export interface BackgroundHls {
  destroy(): void;
  on(event: string, callback: (...args: any[]) => void): void;
  loadSource(url: string): void;
  attachMedia(video: HTMLVideoElement): void;
}

interface BackgroundHlsConstructor {
  new (options: Record<string, boolean | number>): BackgroundHls;
  isSupported(): boolean;
  Events: { MEDIA_ATTACHED: string; ERROR: string };
}

export function currentBackgroundHls(): BackgroundHlsConstructor | undefined {
  return (window as Window & { Hls?: BackgroundHlsConstructor }).Hls;
}

let pending: Promise<BackgroundHlsConstructor | undefined> | undefined;

export function loadBackgroundHls(): Promise<BackgroundHlsConstructor | undefined> {
  const loaded = currentBackgroundHls();
  if (loaded) return Promise.resolve(loaded);
  if (pending) return pending;

  const script = document.createElement('script');
  script.async = true;
  script.src = new URL('./hls.min.js', document.baseURI).href;
  const attempt = new Promise<BackgroundHlsConstructor | undefined>(resolve => {
    function finish() {
      window.clearTimeout(timeout);
      script.onload = script.onerror = null;
      script.remove();
      resolve(currentBackgroundHls());
    }
    const timeout = window.setTimeout(finish, 5000);
    script.onload = script.onerror = finish;
    try { document.head.appendChild(script); }
    catch { finish(); }
  });
  pending = attempt;
  void attempt.then(() => { pending = undefined; });
  return attempt;
}
