import source from './controllerDiagramData.json';
import type { OemSpecialKey, OemKeysSnapshot } from './controllerShortcutRules';

export type DiagramPersona = 'steamdeck' | 'ps5' | 'xbox';
export interface DiagramControl {
  code: string;
  label: string;
  box: number[];
  mask?: string;
  hitClip?: string;
  buttonIndex?: number;
  backIndex?: number;
  aliases?: string[];
  stick?: number;
  thumb?: { x: number; y: number; size: number; travel: number; image: string; fillMask: string };
}
export interface DiagramVectorNode {
  id: string;
  tag: string;
  attrs: Record<string, unknown>;
  children: DiagramVectorNode[];
  text?: string;
  fillFor?: string[];
  visibleFor?: string[];
  availabilityFor?: string;
  stick?: number;
  travel?: number[];
}
export interface DiagramModel {
  width: number;
  height: number;
  image: string;
  controls: DiagramControl[];
  fillColor?: string;
  nodes?: DiagramVectorNode[];
}
export const CONTROLLER_DIAGRAMS = source as Record<DiagramPersona, DiagramModel>;

export function controllerDiagramPersona(persona?: string): DiagramPersona {
  if (persona === 'steamdeck') return 'steamdeck';
  if (['ps5', 'dualsense-edge'].includes(persona || '')) return 'ps5';
  return 'xbox';
}

export function diagramAxis(value: unknown): number {
  const n = Number(value);
  return Number.isFinite(n) ? Math.max(-1, Math.min(1, n)) : 0;
}

function semanticName(value: string): string { return value.toLowerCase().replace(/[\s_-]/g, ''); }
export function diagramOemKey(control: DiagramControl, catalog: OemKeysSnapshot): OemSpecialKey | undefined {
  if (!catalog.supported) return undefined;
  // A visual slot never renumbers a keyId or changes the saved input contract.
  if (control.backIndex != null) return catalog.keys.find(key => key.backIndex === control.backIndex);
  const aliases = (control.aliases || []).map(semanticName);
  return catalog.keys.find(key => aliases.includes(semanticName(key.keyId)) || aliases.includes(semanticName(key.label)));
}

export function diagramControlState(control: DiagramControl, buttons: readonly boolean[], mapped: readonly number[], pressedOem: readonly string[], mappedOem: readonly string[], catalog: OemKeysSnapshot) {
  const key = diagramOemKey(control, catalog);
  // Rear slots are a picture of the native catalog, never assumed hardware.
  const available = control.backIndex != null
    ? Boolean(key && key.captureImplemented !== false)
    : !key || key.captureImplemented !== false;
  const standardLive = control.buttonIndex != null && Boolean(buttons[control.buttonIndex]);
  const standardMapped = control.buttonIndex != null && mapped.includes(control.buttonIndex);
  return {
    live: available && (standardLive || Boolean(key && pressedOem.includes(key.keyId))),
    mapped: available && (standardMapped || Boolean(key && mappedOem.includes(key.keyId))),
    available,
  };
}
