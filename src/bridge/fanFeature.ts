import { computed, ref, toRaw } from 'vue';
import { readSettingsSection, saveSettingsSection } from './settingsRepository';
import { createFanApiAdapter, type FanApiAdapter, type FanNode } from './fanApi';
import { configureFanDiagnosticLogging } from './fanDiagnostics';

export type FanPreset = 'soft' | 'balanced' | 'aggressive';

export interface FanFeatureSettings {
  featureEnabled: boolean;
  configured: boolean;
  deviceIdentity?: Record<string, unknown> | null;
  preset: FanPreset;
  motionEnabled: boolean;
  diagnosticLoggingEnabled: boolean;
  nodes: FanNode[];
  /** The last edited curve for each preset. `nodes` is the active preset copy. */
  presetCurves: Record<FanPreset, FanNode[]>;
}

// Formal integration gate. Setting either gate back to false is a reversible
// rollback: the source, route, host package and saved configuration remain in
// place, but no Fan UI or native Host path is used.
export const FAN_IMPORT_ENABLED = true;
// Real integration is enabled again. The offline preview package used this
// switch temporarily; keeping it explicit makes the rollback boundary easy to
// audit without changing the Fan API or deleting any files.
export const FAN_FORCE_PREVIEW = false;

/** Factory defaults are kept in the feature layer so persisted settings do
 * not depend on the view component. Each preset has its own curve and can be
 * edited independently. */
export const DEFAULT_FAN_PRESET_CURVES: Record<FanPreset, FanNode[]> = {
  soft: [
    { tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 15 },
    { tempC: 69, dutyPercent: 30 }, { tempC: 100, dutyPercent: 70 },
  ],
  balanced: [
    { tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 },
    { tempC: 70, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 },
  ],
  aggressive: [
    { tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 30 },
    { tempC: 70, dutyPercent: 75 }, { tempC: 100, dutyPercent: 100 },
  ],
};

const DEFAULT_FAN_SETTINGS: FanFeatureSettings = {
  // New installations are eligible by default. The navigation remains hidden
  // until a successful HC handshake (or a remembered device configuration).
  featureEnabled: true, configured: false, deviceIdentity: null,
  preset: 'balanced', motionEnabled: true,
  diagnosticLoggingEnabled: false,
  nodes: structuredClone(DEFAULT_FAN_PRESET_CURVES.balanced),
  presetCurves: structuredClone(DEFAULT_FAN_PRESET_CURVES),
};
const settings = ref<FanFeatureSettings>({ ...DEFAULT_FAN_SETTINGS });
const initialized = ref(false);
const handshakeSupported = ref(false);
export const fanMotionEnabled = ref(DEFAULT_FAN_SETTINGS.motionEnabled);
export const fanControlActive = ref(false);
export const fanNavigationMotion = computed(() => fanControlActive.value && fanMotionEnabled.value);
/** Expected duty used by the navigation fan glyph.  This is deliberately
 * separate from hardware telemetry: HC may expose no independent RPM value. */
export const fanNavigationDuty = ref(0);
export const fanNavigationSpinDuration = computed(() => {
  const duty = Math.max(0, Math.min(100, fanNavigationDuty.value));
  return `${Math.max(0.35, 2.2 - duty * 0.018)}s`;
});

export function setFanNavigationDuty(duty: number): void {
  fanNavigationDuty.value = Number.isFinite(duty)
    ? Math.max(0, Math.min(100, duty))
    : 0;
}
let initialization: Promise<FanFeatureSettings> | null = null;
export const fanFeatureEnabled = computed(() => initialized.value && FAN_IMPORT_ENABLED &&
  settings.value.featureEnabled && (FAN_FORCE_PREVIEW || settings.value.configured || handshakeSupported.value));

/**
 * Return a deep plain-data snapshot.  A shallow spread of a Vue reactive
 * settings object can retain a Proxy in `nodes` after handshake persistence
 * updates; structuredClone then throws and FanView renders as an empty page.
 */
function snapshotFanSettings(source: FanFeatureSettings = settings.value): FanFeatureSettings {
  const raw = toRaw(source) as FanFeatureSettings;
  const identity = raw.deviceIdentity == null ? null : structuredClone(toRaw(raw.deviceIdentity));
  const rawNodes = Array.isArray(raw.nodes) ? raw.nodes : DEFAULT_FAN_SETTINGS.nodes;
  const activePreset = isFanPreset(raw.preset) ? raw.preset : DEFAULT_FAN_SETTINGS.preset;
  const presetCurves = normalizeFanPresetCurves(raw.presetCurves, rawNodes, activePreset);
  return {
    featureEnabled: raw.featureEnabled === true,
    configured: raw.configured === true,
    deviceIdentity: identity,
    preset: activePreset,
    motionEnabled: raw.motionEnabled !== false,
    diagnosticLoggingEnabled: raw.diagnosticLoggingEnabled === true,
    nodes: presetCurves[activePreset].map((item) => {
      const node = toRaw(item) as FanNode;
      return { tempC: Number(node.tempC), dutyPercent: Number(node.dutyPercent) };
    }),
    presetCurves,
  };
}

function isFanPreset(value: unknown): value is FanPreset {
  return value === 'soft' || value === 'balanced' || value === 'aggressive';
}

function normalizeNodes(value: unknown): FanNode[] {
  if (!Array.isArray(value) || value.length !== 4) return structuredClone(DEFAULT_FAN_SETTINGS.nodes);
  const nodes = value.map((item) => ({
    tempC: Number((item as any)?.tempC),
    dutyPercent: Number((item as any)?.dutyPercent),
  }));
  const valid = nodes.every((node) => Number.isFinite(node.tempC) && Number.isFinite(node.dutyPercent));
  // Node 1 temperature is anchored at 0°C, but its duty is editable. The
  // only curve invariant is monotonicity plus the node-4 minimum duty.
  if (!valid || nodes[0].tempC !== 0 || nodes[2].tempC > 85 ||
      nodes.some((node, index) => index > 0 &&
        (node.tempC < nodes[index - 1].tempC || node.dutyPercent < nodes[index - 1].dutyPercent)) ||
      nodes[3].dutyPercent < 50) {
    return structuredClone(DEFAULT_FAN_SETTINGS.nodes);
  }
  return nodes;
}

export function normalizeFanPresetCurves(
  value: unknown,
  legacyNodes: unknown,
  activePreset: FanPreset,
): Record<FanPreset, FanNode[]> {
  const curves = structuredClone(DEFAULT_FAN_PRESET_CURVES);
  const persistedPresets = new Set<FanPreset>();
  if (value && typeof value === 'object' && !Array.isArray(value)) {
    for (const preset of ['soft', 'balanced', 'aggressive'] as const) {
      const candidate = (value as Record<string, unknown>)[preset];
      if (Array.isArray(candidate) && candidate.length === 4) {
        const normalized = normalizeNodes(candidate);
        // `normalizeNodes` falls back to the balanced default for invalid
        // data. Only replace a preset when the stored curve itself is valid,
        // so a corrupt one cannot overwrite another preset's default.
        const valid = normalized.every((node, index) => {
          const item = candidate[index] as any;
          return Number(item?.tempC) === node.tempC && Number(item?.dutyPercent) === node.dutyPercent;
        });
        if (valid) {
          curves[preset] = normalized;
          persistedPresets.add(preset);
        }
      }
    }
  }
  // Migrate the pre-preset-curves format: its one active curve belongs only
  // to the previously active preset; the other two remain factory defaults.
  if (!persistedPresets.has(activePreset) && Array.isArray(legacyNodes)) {
    const migrated = normalizeNodes(legacyNodes);
    const itemized = legacyNodes.length === 4 && migrated.every((node, index) => {
      const item = legacyNodes[index] as any;
      return Number(item?.tempC) === node.tempC && Number(item?.dutyPercent) === node.dutyPercent;
    });
    if (itemized) curves[activePreset] = migrated;
  }
  return curves;
}

export function getFanPresetCurve(preset: FanPreset): FanNode[] {
  const current = snapshotFanSettings();
  return structuredClone(current.presetCurves[preset] ?? DEFAULT_FAN_PRESET_CURVES[preset]);
}

export async function initializeFanFeature(): Promise<FanFeatureSettings> {
  if (!initialization) {
    initialization = (async () => {
      try {
        if (FAN_FORCE_PREVIEW) {
          settings.value = structuredClone(DEFAULT_FAN_SETTINGS);
          initialized.value = true;
          handshakeSupported.value = true;
          return { ...settings.value };
        }
        const saved = await readSettingsSection<Partial<FanFeatureSettings>>('fan');
        const activePreset = isFanPreset(saved.preset) ? saved.preset : DEFAULT_FAN_SETTINGS.preset;
        const presetCurves = normalizeFanPresetCurves(saved.presetCurves, saved.nodes, activePreset);
        settings.value = {
          ...DEFAULT_FAN_SETTINGS, ...saved,
          // `featureEnabled:false` was written by the old preview build. Once
          // the formal fan integration gate is enabled, that legacy value
          // must not hide the navigation on a supported device. Eligibility
          // is still gated by the HC handshake/remembered identity below;
          // the reversible global rollback remains the formal build Gate.
          featureEnabled: FAN_IMPORT_ENABLED,
          configured: saved.configured === true,
          motionEnabled: saved.motionEnabled !== false,
          preset: activePreset,
          presetCurves,
          nodes: structuredClone(presetCurves[activePreset]),
        };
        fanMotionEnabled.value = settings.value.motionEnabled;
        try {
          await configureFanDiagnosticLogging(settings.value.diagnosticLoggingEnabled);
        } catch {
          // A pre-log-switch native shell cannot persist fan diagnostics, but
          // must not make the rest of the Fan settings disappear.
          settings.value = { ...settings.value, diagnosticLoggingEnabled: false };
        }
      } catch { settings.value = structuredClone(DEFAULT_FAN_SETTINGS); }
      initialized.value = true;
      return { ...settings.value };
    })();
  }
  return initialization;
}

export function getFanFeatureSettings(): FanFeatureSettings {
  return snapshotFanSettings();
}
export async function setFanFeatureEnabled(enabled: boolean): Promise<void> {
  settings.value = { ...settings.value, featureEnabled: FAN_IMPORT_ENABLED && enabled === true };
  await saveSettingsSection('fan', { featureEnabled: settings.value.featureEnabled });
}

export async function rememberFanDevice(identity: Record<string, unknown> | null): Promise<void> {
  settings.value = { ...settings.value, configured: identity !== null, deviceIdentity: identity };
  if (FAN_FORCE_PREVIEW) return;
  await saveSettingsSection('fan', { configured: settings.value.configured, deviceIdentity: identity });
}

export async function recordFanHandshake(
  supported: boolean,
  identity?: Record<string, unknown> | null,
): Promise<void> {
  handshakeSupported.value = supported;
  if (FAN_FORCE_PREVIEW) return;
  if (supported && identity) {
    settings.value = { ...settings.value, configured: true, deviceIdentity: identity };
    await saveSettingsSection('fan', { configured: true, deviceIdentity: identity });
  }
}

export async function saveFanCurve(nodes: readonly FanNode[], preset: FanFeatureSettings['preset']): Promise<void> {
  const next = nodes.map((node) => ({ tempC: node.tempC, dutyPercent: node.dutyPercent }));
  const presetCurves = normalizeFanPresetCurves(
    settings.value.presetCurves,
    settings.value.nodes,
    settings.value.preset,
  );
  presetCurves[preset] = structuredClone(next);
  settings.value = { ...settings.value, nodes: next, preset, presetCurves };
  if (FAN_FORCE_PREVIEW) return;
  await saveSettingsSection('fan', { nodes: next, preset, presetCurves });
}

export async function setFanMotionEnabled(enabled: boolean): Promise<void> {
  settings.value = { ...settings.value, motionEnabled: enabled };
  fanMotionEnabled.value = enabled;
  if (FAN_FORCE_PREVIEW) return;
  await saveSettingsSection('fan', { motionEnabled: enabled });
}

export async function setFanDiagnosticLoggingEnabled(enabled: boolean): Promise<void> {
  await configureFanDiagnosticLogging(enabled);
  settings.value = { ...settings.value, diagnosticLoggingEnabled: enabled };
  if (!FAN_FORCE_PREVIEW) await saveSettingsSection('fan', { diagnosticLoggingEnabled: enabled });
}

export function setFanControlActive(active: boolean): void {
  fanControlActive.value = active;
}

export function createConfiguredFanAdapter(): FanApiAdapter {
  return createFanApiAdapter({ enabled: settings.value.featureEnabled === true });
}
