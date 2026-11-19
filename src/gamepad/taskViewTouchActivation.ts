import { windowApi, type WindowActionEvidence } from '@/bridge/api';

// Set only by an incoming OS event, never by HTMLElement.click/dispatchEvent.
const deliveredEvents = new WeakMap<Event, WindowActionEvidence>();
export const readTaskViewSystemTouchEvidence = (event?: Event): WindowActionEvidence | undefined =>
  event ? deliveredEvents.get(event) : undefined;

type TouchReceipt = { downAccepted: boolean; upAccepted: boolean; reason: string; win32Error?: number };
type Request = Parameters<typeof windowApi.touchTaskViewButton>[0];
interface Options {
  prepare?: typeof windowApi.prepareTaskViewTouch;
  inject?: (request: Request) => Promise<TouchReceipt>;
  timeoutMs?: number;
}

// A timeout is not an input-drain acknowledgement. Keep at most one narrow,
// passive fence per document for an unresolved contact, including across menu
// unmount/reopen. It retains no component/element and has no timer or polling.
const retiredContacts = new WeakMap<Document, () => void>();
const documentOwners = new WeakMap<Document, object>();
function retainRetiredContact(doc: Document, x: number, y: number, pointer: number | null): () => void {
  const existing = retiredContacts.get(doc);
  if (existing) return existing;
  const remove = () => {
    for (const type of ['pointerdown', 'pointerup', 'pointercancel', 'click']) doc.removeEventListener(type, fence, true);
    if (retiredContacts.get(doc) === remove) retiredContacts.delete(doc);
  };
  const fence = (event: Event) => {
    const input = event as PointerEvent & { sourceCapabilities?: { firesTouchEvents?: boolean } };
    if (!event.isTrusted || (input.pointerType !== 'touch' && !input.sourceCapabilities?.firesTouchEvents)) return;
    const atPoint = Math.abs(input.clientX - x) <= 8 && Math.abs(input.clientY - y) <= 8;
    const legacyClick = event.type === 'click' && !(input.pointerId > 0) && atPoint;
    if (pointer === null ? !atPoint : input.pointerId !== pointer && !legacyClick) return;
    if (event.type === 'pointerdown' && pointer === null) pointer = input.pointerId;
    event.preventDefault(); event.stopImmediatePropagation();
    if (event.type === 'pointercancel' || event.type === 'click') remove();
  };
  retiredContacts.set(doc, remove);
  for (const type of ['pointerdown', 'pointerup', 'pointercancel', 'click']) doc.addEventListener(type, fence, true);
  return remove;
}

// One explicit activation owns one contact and one incoming click. The native
// receipt is delivery evidence only; it must never replay the Shell operation.
export function createTaskViewTouchActivation(options: Options = {}) {
  const prepare = options.prepare ?? windowApi.prepareTaskViewTouch;
  const inject = options.inject ?? windowApi.touchTaskViewButton;
  const timeoutMs = options.timeoutMs ?? 1600;
  let active: { cancel: () => void; target: HTMLElement } | null = null;

  function cancel(): void { active?.cancel(); }

  async function request(target: HTMLElement, evidence: WindowActionEvidence, isCurrent: () => boolean): Promise<void> {
    if (active) return;
    const startedAt = performance.now();
    const doc = target.ownerDocument;
    if (documentOwners.has(doc)) throw new Error('上次触控切换仍在处理中，请稍后重试');
    if (retiredContacts.has(doc)) throw new Error('上次触控输入尚未确认，请先触摸一次切换程序按钮，再重试');
    const view = doc.defaultView;
    const rect = target.getBoundingClientRect();
    const x = (rect.left + rect.right) / 2, y = (rect.top + rect.bottom) / 2;
    if (!view || !isCurrent() || !target.isConnected || target.matches(':disabled') ||
        doc.visibilityState !== 'visible' || rect.width <= 0 || rect.height <= 0 ||
        x < 0 || y < 0 || x >= view.innerWidth || y >= view.innerHeight ||
        !target.contains(doc.elementFromPoint(x, y))) throw new Error('切换程序按钮当前不可触达，请重新呼出后重试');

    let cancelled = false, observed = false, noDelivery = false, terminalContact = false;
    let injectionStarted = false, finished = false;
    let deliveryEvidence = evidence;
    let retire: (() => void) | undefined;
    let pointer: number | null = null, released = false;
    let complete!: () => void, fail!: (error: Error) => void;
    const completion = new Promise<void>((resolve, reject) => { complete = resolve; fail = reject; });
    // The cancellation fence remains until the bounded native request settles,
    // so an old contact cannot activate another menu/control after it closes.
    const owner = { target, cancel: () => { cancelled = true; } };
    active = owner;
    documentOwners.set(doc, owner);

    function valid(event: MouseEvent): boolean {
      const now = target.getBoundingClientRect();
      return !cancelled && isCurrent() && target.isConnected && !target.matches(':disabled') &&
        doc.visibilityState === 'visible' && target.contains(event.target as Node | null) &&
        Math.abs(now.left - rect.left) <= 1 && Math.abs(now.top - rect.top) <= 1 &&
        Math.abs(now.width - rect.width) <= 1 && Math.abs(now.height - rect.height) <= 1 &&
        Math.abs(event.clientX - x) <= 8 && Math.abs(event.clientY - y) <= 8;
    }
    function suppress(event: Event): void { event.preventDefault(); event.stopImmediatePropagation(); }
    function onPointer(event: PointerEvent): void {
      if (!injectionStarted) {
        if (event.isTrusted && event.type === 'pointerdown' && target.contains(event.target as Node | null)) {
          cancelled = true;
          complete();
        }
        return;
      }
      if (!event.isTrusted || event.pointerType !== 'touch') return;
      // Even a cancelled/moved menu must track its own incoming contact until
      // cancel or click. Native frame acceptance does not drain renderer input.
      const atPoint = Math.abs(event.clientX - x) <= 8 && Math.abs(event.clientY - y) <= 8;
      if (event.type === 'pointerdown' && pointer === null && atPoint) pointer = event.pointerId;
      if (event.type === 'pointercancel' && event.pointerId === pointer) {
        terminalContact = true; cancelled = true; suppress(event); return;
      }
      if (observed || !valid(event)) { suppress(event); return; }
      if (event.type === 'pointerdown' && pointer === null) pointer = event.pointerId;
      else if (event.pointerId !== pointer) { suppress(event); return; }
      if (event.type === 'pointerup') released = true;
    }
    function onClick(event: MouseEvent): void {
      if (!injectionStarted) {
        // A real click while preparation is pending wins; no pointer was sent.
        if (target.contains(event.target as Node | null)) {
          if (event.isTrusted) { cancelled = true; complete(); }
          else suppress(event);
        }
        return;
      }
      // Ignore the original engine's subsequent scripted clicks; only the OS
      // touch sequence can enter the action while this request is armed.
      const source = event as MouseEvent & { pointerType?: string; pointerId?: number; sourceCapabilities?: { firesTouchEvents?: boolean } };
      const touch = source.pointerType === 'touch' || source.sourceCapabilities?.firesTouchEvents === true;
      if (event.isTrusted && touch && Math.abs(event.clientX - x) <= 8 && Math.abs(event.clientY - y) <= 8 &&
          (pointer === null || source.pointerId === undefined || source.pointerId <= 0 || source.pointerId === pointer)) terminalContact = true;
      if (observed || !event.isTrusted || !touch || !released || !valid(event) ||
          (typeof source.pointerId === 'number' && source.pointerId > 0 && source.pointerId !== pointer)) { suppress(event); return; }
      observed = true;
      deliveredEvents.set(event, { ...deliveryEvidence, diagnosticInputRoute: 'system-touch' });
      // Let this very event reach the existing Vue handler synchronously. A
      // fabricated JS click or waiting to dispatch another event loses this path.
      // Actual delivery wins even if its IPC receipt is lost; otherwise an
      // already executed action could later show a spurious touch timeout.
      queueMicrotask(complete);
    }
    doc.addEventListener('pointerdown', onPointer, true);
    doc.addEventListener('pointerup', onPointer, true);
    doc.addEventListener('pointercancel', onPointer, true);
    doc.addEventListener('click', onClick, true);
    const timer = setTimeout(() => fail(new Error(cancelled ? '本次触控切换已取消' :
      'Windows 触控切换未收到按钮响应，请重试')), timeoutMs);
    try {
      // Install rejection handling before waiting, including a transport timeout.
      void Promise.resolve().then(async () => {
        if (cancelled || finished || !isCurrent()) throw new Error('本次触控切换已取消');
        const prepared = await prepare(evidence);
        if (cancelled || finished || !isCurrent()) throw new Error('本次触控切换已取消');
        const touchTicket = prepared?.touchTicket, requestedAtUnixMs = prepared?.requestedAtUnixMs;
        const touchBudgetMs = Math.floor(1000 - (performance.now() - startedAt));
        if (typeof touchTicket !== 'number' || !Number.isSafeInteger(touchTicket) || touchTicket <= 0 ||
            typeof requestedAtUnixMs !== 'number' || !Number.isSafeInteger(requestedAtUnixMs) || requestedAtUnixMs <= 0)
          throw new Error(`触控切换准备失败${prepared?.reason ? `：${prepared.reason}` : ''}`);
        if (touchBudgetMs <= 0) throw new Error('触控切换请求已过期，请重试');
        const latest = target.getBoundingClientRect();
        if (!target.isConnected || target.matches(':disabled') || doc.visibilityState !== 'visible' ||
            Math.abs(latest.left - rect.left) > 1 || Math.abs(latest.top - rect.top) > 1 ||
            Math.abs(latest.width - rect.width) > 1 || Math.abs(latest.height - rect.height) > 1 ||
            x >= view.innerWidth || y >= view.innerHeight || !target.contains(doc.elementFromPoint(x, y)))
          throw new Error('切换程序按钮已移动或不可见，请重试');
        deliveryEvidence = { ...evidence, requestedAtUnixMs };
        injectionStarted = true;
        return inject({ ...deliveryEvidence, touchTicket, touchBudgetMs, x, y, viewportWidth: view.innerWidth, viewportHeight: view.innerHeight });
      }).then(receipt => {
        if (observed) { complete(); return; }
        noDelivery = receipt?.downAccepted === false && receipt?.upAccepted === false && typeof receipt?.reason === 'string';
        if (noDelivery) {
          retire?.();
          fail(new Error(`Windows 未接受触控切换：${receipt.reason}`));
        } else if (receipt?.downAccepted === true && receipt?.upAccepted === false) {
          cancelled = true;
          // A DOWN may already be queued: retain the fence until the timer.
        }
      }, (error: unknown) => {
        if (observed) complete();
        else if (!injectionStarted) fail(error instanceof Error ? error : new Error(String(error)));
        // Transport failure says nothing about delivery. An actual current
        // touch may still prove it within the original observation window.
      });
      await completion;
    } finally {
      finished = true;
      clearTimeout(timer);
      doc.removeEventListener('pointerdown', onPointer, true);
      doc.removeEventListener('pointerup', onPointer, true);
      doc.removeEventListener('pointercancel', onPointer, true);
      doc.removeEventListener('click', onClick, true);
      if (injectionStarted && !observed && !noDelivery && !terminalContact) retire = retainRetiredContact(doc, x, y, pointer);
      if (documentOwners.get(doc) === owner) documentOwners.delete(doc);
      if (active === owner) active = null;
    }
  }
  return { request, cancel };
}
