import { subscribeMonitorGameLogo } from './monitorGameLogo';
// Reuse YMCC's existing owned WebView2 child-window API. This is explicitly a
// desktop preview, NOT the Steam game carrier. The child never collects hardware.
import { windowApi } from './api';
import { isNativeRuntime, on } from './ipc';
import { acquireMonitorDemand, readMonitorSnapshot, subscribeMonitorSnapshot, subscribeMonitorTdpState, type MonitorSnapshotView, type TdpRuntimeSnapshot } from './monitorData';
import { hudFromUniformSnapshot, normalizeMonitorOutput, type HudData, type MonitorOutputPreferences } from './monitorHudModel';
import { createLatestDisplayFeed, monitorSnapshotDisplayKey } from './monitorDisplayFeed';
import { monitorSnapshotHasFreshData } from './monitorHudFreshness';
export const HUD_PREVIEW_ROUTE = '/monitor/hud-preview';
export const HUD_PREVIEW_MAX_MS = 5 * 60 * 1000;
export const canOpenNativeHudPreview = isNativeRuntime && typeof BroadcastChannel !== 'undefined';
export type HudPreviewPacket =
  | { kind: 'ymcc-hud-preview'; token: string; seq: number; data: HudData; expiresAt: number }
  | { kind: 'ymcc-hud-preview-appearance'; token: string; seq: number; preferences: MonitorOutputPreferences };
export interface HudPreviewOwnerDeps {
  available: () => boolean;
  token: () => string;
  now: () => number;
  monotonicNow?: () => number;
  createChild: (options: { title: string; width: number; height: number; url: string }) => Promise<number>;
  closeChild: (id: number) => Promise<boolean>;
  channel: (name: string) => { postMessage(message: unknown): void; close(): void; onmessage: ((event: { data: any }) => void) | null };
  subscribe: (callback: (snapshot: MonitorSnapshotView | null) => void) => () => void;
  subscribeTdpState?: (callback: (value: TdpRuntimeSnapshot) => void) => () => void;
  subscribeGameLogo?: (callback: (image: string) => void) => () => void;
  acquire: () => () => void;
  readOnce: () => Promise<MonitorSnapshotView | null>;
  childClosed: (callback: (id: number) => void) => () => void;
  setTimer: (callback: () => void, ms: number) => any;
  clearTimer: (timer: any) => void;
  baseUrl: () => string;
}
type PreviewSample = { snapshot: MonitorSnapshotView | null; tdpState: TdpRuntimeSnapshot | null; invalidate: boolean; gameLogo?: string; logoRevision?: number };

export function createHudPreviewOwner(deps: HudPreviewOwnerDeps) {
  const snapshotDown = (snapshot: MonitorSnapshotView | null) => !monitorSnapshotHasFreshData(snapshot, deps.now());
  let prefs = normalizeMonitorOutput();
  let current: { id: number | null; token: string; channel: ReturnType<HudPreviewOwnerDeps['channel']>; release: () => void; off: () => void; tdpOff: () => void; logoOff: (() => void) | null; setLogo: () => void; closedOff: () => void; dataOff: () => void; feed: ReturnType<typeof createLatestDisplayFeed<PreviewSample>>; deadline: any; readyTimer: any; ready: boolean; resolve: (id: number) => void; reject: (error: Error) => void } | null = null;
  let disposed = false, seq = 0;
  function publishData(sample: PreviewSample) {
    const owned = current; if (!owned || !owned.ready || disposed) return;
    const now = deps.now(), value = sample.snapshot;
    const data = { ...hudFromUniformSnapshot(value, now, sample.tdpState), gameLogo: sample.gameLogo || '' };
    const deadlines = Object.values(data.freshUntil ?? {}).filter((v): v is number => typeof v === 'number' && v > now && v <= now + 6000);
    owned.channel.postMessage({ kind: 'ymcc-hud-preview', token: owned.token, seq: ++seq,
      data, expiresAt: deadlines.length ? Math.max(...deadlines) : now } satisfies HudPreviewPacket);
  }
  function publishAppearance() {
    const owned = current; if (!owned || !owned.ready || disposed) return;
    owned.channel.postMessage({ kind: 'ymcc-hud-preview-appearance', token: owned.token, seq: ++seq, preferences: prefs } satisfies HudPreviewPacket);
  }
  function release(owned: NonNullable<typeof current>, close: boolean) {
    if (current === owned) current = null;
    owned.logoOff?.(); owned.logoOff = null;
    deps.clearTimer(owned.deadline); deps.clearTimer(owned.readyTimer);
    for (const cleanup of [owned.off, owned.tdpOff, owned.closedOff, owned.dataOff, () => owned.feed.dispose(), owned.release, () => owned.channel.close()]) { try { cleanup(); } catch { /* finish own-resource cleanup */ } }
    if (!owned.ready) owned.reject(Error('真实预览窗口未完成握手，或已关闭。'));
    if (close && owned.id !== null) void deps.closeChild(owned.id).catch(() => {});
  }
  return {
    async open(preferences: MonitorOutputPreferences): Promise<number> {
      if (disposed) throw Error('Preview owner disposed');
      if (!deps.available()) throw Error('当前浏览器没有 YMCC 原生窗口接口；不能以浏览器弹窗冒充真实预览。');
      prefs = normalizeMonitorOutput(preferences);
      if (current) { current.setLogo(); publishAppearance(); if (current.ready && current.id !== null) return current.id; throw Error('真实预览窗口正在创建，请稍候。'); }
      const token = deps.token(); if (!/^[a-zA-Z0-9-]{16,80}$/.test(token)) throw Error('Invalid preview token');
      const channel = deps.channel('ymcc-hud-preview-' + token);
      let resolve!: (id: number) => void, reject!: (error: Error) => void;
      const ready = new Promise<number>((yes, no) => { resolve = yes; reject = no; });
      // Attach a rejection observer immediately, before the asynchronous HWND create.
      void ready.catch(() => {});
      let snapshotRevision = 0;
      let sample: PreviewSample = { snapshot: null, tdpState: null, invalidate: true };
      const feed = createLatestDisplayFeed<PreviewSample>(sample, {
        now: deps.monotonicNow ?? deps.now, key: value => monitorSnapshotDisplayKey(value.snapshot) + '|' + (value.tdpState?.revision ?? -1) + '|' + (value.logoRevision ?? 0),
        unavailable: value => value.invalidate, setTimer: deps.setTimer, clearTimer: deps.clearTimer,
      });
      const acceptSnapshot = (value: MonitorSnapshotView | null) => {
        ++snapshotRevision;
        sample = { ...sample, snapshot: value, invalidate: snapshotDown(value) && !snapshotDown(sample.snapshot) };
        feed.push(sample);
      };
      const owned = { dataOff: () => {}, feed, id: null as number | null, token, channel, release: () => {}, off: () => {}, tdpOff: () => {}, logoOff: null as (() => void) | null, setLogo: () => {}, closedOff: () => {}, deadline: null as any, readyTimer: null as any, ready: false, resolve, reject };
      owned.setLogo = () => {
        const wanted = prefs.enabled && prefs.output === 'ymcc' && prefs.ymcc.underlay === 'game-logo';
        if (wanted && !owned.logoOff && deps.subscribeGameLogo) owned.logoOff = deps.subscribeGameLogo(image => {
          if (current !== owned) return;
          sample = { ...sample, gameLogo: image, logoRevision: (sample.logoRevision ?? 0) + 1 }; feed.push(sample);
        });
        if (!wanted && owned.logoOff) { owned.logoOff(); owned.logoOff = null;
          sample = { ...sample, gameLogo: '', logoRevision: (sample.logoRevision ?? 0) + 1 }; feed.push(sample); }
      };
      current = owned;
      const acknowledge = () => { if (current !== owned || owned.ready || owned.id === null) return; owned.ready = true; deps.clearTimer(owned.readyTimer); publishAppearance(); owned.dataOff = feed.subscribe(publishData); owned.resolve(owned.id); };
      let childReady = false;
      channel.onmessage = event => {
        const data = event.data; if (current !== owned || data?.token !== token || data?.kind !== 'ymcc-hud-preview-ready') return;
        childReady = true; acknowledge();
      };
      try {
      owned.off = deps.subscribe(value => { if (current === owned) acceptSnapshot(value); });
      owned.tdpOff = deps.subscribeTdpState?.(value => { if (current === owned) { sample = { ...sample, tdpState: value, invalidate: value.status !== 'applied' && sample.tdpState?.status === 'applied' }; feed.push(sample); } }) ?? (() => {});
      owned.closedOff = deps.childClosed(id => { if (current === owned && owned.id === id) release(owned, false); });
      owned.release = deps.acquire();
      owned.deadline = deps.setTimer(() => { if (current === owned) release(owned, true); }, HUD_PREVIEW_MAX_MS);
      owned.readyTimer = deps.setTimer(() => { if (current === owned && !owned.ready) release(owned, true); }, 15000);
        const url = deps.baseUrl().split('#')[0] + '#' + HUD_PREVIEW_ROUTE + '?channel=' + encodeURIComponent(token);
        const id = await deps.createChild({ title: 'YMCC 真实数据 · 桌面预览（非 Steam 叠加层）', width: 660, height: 680, url });
        if (!Number.isSafeInteger(id) || id <= 0) throw Error('Native preview returned invalid child ID');
        owned.id = id;
        if (current !== owned || disposed) { await deps.closeChild(id); throw Error('Preview owner closed during creation'); }
        if (childReady) acknowledge();
        const readRevision = snapshotRevision;
        const first = await deps.readOnce(); if (current === owned && snapshotRevision === readRevision) acceptSnapshot(first);
        return await ready;
      } catch (error) { if (current === owned) release(owned, true); throw error; }
    },
    update(preferences: MonitorOutputPreferences) { prefs = normalizeMonitorOutput(preferences); current?.setLogo(); publishAppearance(); },
    close() { if (current) release(current, true); },
    dispose() { disposed = true; if (current) release(current, true); },
    isOpen() { return current?.ready === true; },
  };
}
export function createNativeHudPreviewOwner() {
  return createHudPreviewOwner({
    subscribeGameLogo: subscribeMonitorGameLogo,
    available: () => canOpenNativeHudPreview,
    token: () => crypto.randomUUID(), now: () => Date.now(), monotonicNow: () => performance.now(),
    createChild: options => windowApi.createChild(options), closeChild: id => windowApi.closeChild(id),
    channel: name => new BroadcastChannel(name),
    subscribe: callback => subscribeMonitorSnapshot(callback),
    subscribeTdpState: callback => subscribeMonitorTdpState(callback),
    acquire: () => acquireMonitorDemand('ymcc-native-hud-preview', { top: true, fps: true }),
    readOnce: () => readMonitorSnapshot(),
    childClosed: callback => on<{ id: number }>('window.childClosed', event => callback(event.id)),
    setTimer: (callback, ms) => window.setTimeout(callback, ms), clearTimer: timer => window.clearTimeout(timer),
    baseUrl: () => window.location.href,
  });
}