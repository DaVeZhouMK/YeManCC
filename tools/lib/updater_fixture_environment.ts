// Shared inert browser/power ports for the existing updater memory tests.
// No DOM rendering, OS power read, product process or installation is used.
export function installUpdaterFixtureEnvironment(win: any, receive: (event: {data:any}) => void): void {
  const doc = Object.assign(new EventTarget(), {
    visibilityState: 'visible', documentElement: {dataset: {theme: 'blue-black'}},
    createElement(tag: string) {
      if (tag !== 'template') throw new Error('Unexpected DOM rendering in updater memory test');
      return {content: {}};
    },
  });
  (globalThis as any).document = doc;
  const post = win.chrome.webview.postMessage.bind(win.chrome.webview);
  win.chrome.webview.postMessage = (request: any) => {
    if (request.cmd !== 'power.lifecycle') return post(request);
    queueMicrotask(() => receive({data: {id: request.id, result: {
      phase: 'ready', generation: 1, inputReady: true, hardwareWritesAllowed: true,
      known: true, publicationRevision: 1, notificationPending: false,
    }}}));
  };
}
