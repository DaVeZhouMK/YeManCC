import type { WindowActionEvidence } from '@/bridge/api';

let gamepadActivationTarget: EventTarget | null = null;

// This marker exists only during the engine's synchronous click dispatch.
export function withGamepadActivation<T>(target: EventTarget, activate: () => T): T {
  const previous = gamepadActivationTarget;
  gamepadActivationTarget = target;
  try {
    return activate();
  } finally {
    gamepadActivationTarget = previous;
  }
}

// Snapshot currentTarget before an await clears the event dispatch context.
// isTrusted and the scope marker are diagnostics, never authorization or proof
// of a human gesture. Pointerless trusted clicks keep their source unspecified.
export function readActivationEvidence(event?: MouseEvent): Pick<WindowActionEvidence,
  'diagnosticActivation' | 'diagnosticEventTrusted' | 'diagnosticClickDetail'> {
  const evidence: ReturnType<typeof readActivationEvidence> = { diagnosticActivation: 'unknown' };
  const currentTarget = event?.currentTarget;
  const trusted = event?.isTrusted;
  if (currentTarget && currentTarget === gamepadActivationTarget) {
    evidence.diagnosticActivation = 'gamepad-engine';
  } else if (trusted === false) {
    evidence.diagnosticActivation = 'scripted';
  } else if (trusted === true) {
    const pointerType = (event as MouseEvent & { pointerType?: string }).pointerType;
    switch (pointerType) {
      case 'touch': evidence.diagnosticActivation = 'trusted-touch'; break;
      case 'pen': evidence.diagnosticActivation = 'trusted-pen'; break;
      case 'mouse': evidence.diagnosticActivation = 'trusted-mouse'; break;
      default: evidence.diagnosticActivation = 'trusted-other';
    }
  }
  if (typeof trusted === 'boolean') evidence.diagnosticEventTrusted = trusted;
  const detail = event?.detail;
  if (typeof detail === 'number' && Number.isFinite(detail)) {
    evidence.diagnosticClickDetail = Math.max(0, Math.min(3, Math.trunc(detail)));
  }
  return evidence;
}
