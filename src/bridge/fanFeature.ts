import { computed, ref, shallowRef, toRaw } from 'vue';
import { topMonitorData } from './topmon';
import { monitorCpuTemperature } from './monitorSnapshotSignals';
import { readSettingsSection, saveSettingsSection, updateSettings } from './settingsRepository';
import { createFanApiAdapter, type FanApiAdapter, type FanNode } from './fanApi';
import { validateFanNodes } from './fanCurve';
import { adoptFanDiagnosticLoggingState, configureFanDiagnosticLogging, readFanDiagnosticLogging } from './fanDiagnostics';

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
  // until the current process completes a successful HC handshake. A remembered
  // device is retained for retry/profile context, but never grants visibility.
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
const navigationCurve = shallowRef<FanNode[]>([]);
// Ignore unrelated monitor fields while preserving current temperature validity.
const navigationTemperature = computed(() => monitorCpuTemperature(topMonitorData.value));
const previewNavigationDuty = ref(0);
export const fanNavigationDuty = computed(() => {
  if (FAN_FORCE_PREVIEW) return previewNavigationDuty.value;
  if (!fanControlActive.value || navigationCurve.value.length === 0) return 0;
  // Reuse the existing shared monitor sample; navigation owns no reader/timer.
  const temperature = navigationTemperature.value;
  if (temperature === null) return 0;
  const curve = navigationCurve.value;
  if (temperature <= curve[0].tempC) return curve[0].dutyPercent;
  for (let i = 1; i < curve.length; i += 1) {
    if (temperature > curve[i].tempC) continue;
    const previous = curve[i - 1], current = curve[i];
    const span = current.tempC - previous.tempC;
    return Math.round(span <= 0 ? current.dutyPercent : previous.dutyPercent +
      (current.dutyPercent - previous.dutyPercent) * (temperature - previous.tempC) / span);
  }
  return curve[curve.length - 1].dutyPercent;
});
export const fanNavigationSpinDuration = computed(() => {
  const duty = Math.max(0, Math.min(100, fanNavigationDuty.value));
  return `${Math.max(0.35, 2.2 - duty * 0.018)}s`;
});

/** Preview compatibility only. Page/mirror state cannot override live navigation. */
export function setFanNavigationDuty(duty: number): void {
  if (!FAN_FORCE_PREVIEW) return;
  previewNavigationDuty.value = Number.isFinite(duty)
    ? Math.max(0, Math.min(100, duty))
    : 0;
}
let initialization: Promise<void> | null = null;
export const fanFeatureEnabled = computed(() => initialized.value && FAN_IMPORT_ENABLED &&
  settings.value.featureEnabled && (FAN_FORCE_PREVIEW || handshakeSupported.value));

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

function normalizeNodes(value: unknown): FanNode[] | null {
  if (!Array.isArray(value) || value.length !== 4) return null;
  const nodes = value.map((item) => ({
    tempC: Number((item as any)?.tempC),
    dutyPercent: Number((item as any)?.dutyPercent),
  }));
  // Persisted percentage curves share the editor's existing bounds and node rules.
  return validateFanNodes(nodes) ? nodes : null;
}

/** Reject invalid data without building a default curve only to discard it. */
function normalizedPersistedCurve(value: unknown): FanNode[] | null {
  if (!Array.isArray(value) || value.length !== 4) return null;
  const normalized = normalizeNodes(value);
  if (normalized === null) return null;
  // Select only a faithful numeric normalization of this input.
  return normalized.every((node, index) => {
    const item = value[index] as { tempC?: unknown; dutyPercent?: unknown } | null | undefined;
    return Number(item?.tempC) === node.tempC && Number(item?.dutyPercent) === node.dutyPercent;
  }) ? normalized : null;
}

export function normalizeFanPresetCurves(
  value: unknown,
  legacyNodes: unknown,
  activePreset: FanPreset,
): Record<FanPreset, FanNode[]> {
  const curves: Record<FanPreset, FanNode[] | null> = { soft: null, balanced: null, aggressive: null };
  if (value && typeof value === 'object' && !Array.isArray(value)) {
    for (const preset of ['soft', 'balanced', 'aggressive'] as const) {
      const normalized = normalizedPersistedCurve((value as Record<string, unknown>)[preset]);
      if (normalized !== null) {
        curves[preset] = normalized;
      }
    }
  }
  // Migrate the pre-preset-curves format: its one active curve belongs only
  // to the previously active preset; the other two remain factory defaults.
  if (curves[activePreset] === null) {
    const migrated = normalizedPersistedCurve(legacyNodes);
    if (migrated !== null) curves[activePreset] = migrated;
  }
  // Allocate defaults only for presets that have no valid stored or migrated curve.
  return {
    soft: curves.soft ?? structuredClone(DEFAULT_FAN_PRESET_CURVES.soft),
    balanced: curves.balanced ?? structuredClone(DEFAULT_FAN_PRESET_CURVES.balanced),
    aggressive: curves.aggressive ?? structuredClone(DEFAULT_FAN_PRESET_CURVES.aggressive),
  };
}

export function getFanPresetCurve(preset: FanPreset): FanNode[] {
  const raw = toRaw(settings.value);
  const activePreset = isFanPreset(raw.preset) ? raw.preset : DEFAULT_FAN_SETTINGS.preset;
  const table: unknown = raw.presetCurves;
  const candidate = table && typeof table === 'object' && !Array.isArray(table)
    ? (table as Record<string, unknown>)[preset] : undefined;
  const persisted = normalizedPersistedCurve(candidate);
  if (persisted !== null) return persisted;
  if (preset === activePreset) {
    const legacy = normalizedPersistedCurve(Array.isArray(raw.nodes) ? raw.nodes : DEFAULT_FAN_SETTINGS.nodes);
    if (legacy !== null) return legacy;
  }
  return structuredClone(DEFAULT_FAN_PRESET_CURVES[preset]);
}

export async function initializeFanFeature(): Promise<FanFeatureSettings> {
  if (!initialization) {
    initialization = (async () => {
      handshakeSupported.value = false;
      try {
        if (FAN_FORCE_PREVIEW) {
          settings.value = structuredClone(DEFAULT_FAN_SETTINGS);
          initialized.value = true;
          handshakeSupported.value = true;
          return;
        }
        const saved = await readSettingsSection<Partial<FanFeatureSettings>>('fan');
        const activePreset = isFanPreset(saved.preset) ? saved.preset : DEFAULT_FAN_SETTINGS.preset;
        const presetCurves = normalizeFanPresetCurves(saved.presetCurves, saved.nodes, activePreset);
        settings.value = {
          ...DEFAULT_FAN_SETTINGS, ...saved,
          // `featureEnabled:false` was written by the old preview build. Once
          // the formal fan integration gate is enabled, that legacy value is
          // retained only for retry/profile context. Navigation eligibility
          // is still gated by the current HC handshake below;
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
          // 203/Fan b141（P1b / CP-10）：初始化**不得**把 `fan` 节里的旧默认值推给统一日志门
          // ——那会覆盖已持久化的 `domainLogs.virtual=true`（S4 的 logging-enabled →
          // logging-disabled 翻转）。这里只**读回**权威值并采纳；写只发生在用户明确操作时。
          const authoritative = await readFanDiagnosticLogging();
          if (authoritative !== null) {
            settings.value = { ...settings.value, diagnosticLoggingEnabled: authoritative };
            // b142：读回之后还必须把权威值**采纳为桥内日志门**，否则 `fanDiagnosticLog()`
            // 仍被本模块的 ref（默认 false）挡掉，整场会话的 lifecycle.* 行会静默丢失。
            adoptFanDiagnosticLoggingState(authoritative);
          }
        } catch {
          // A pre-log-switch native shell cannot persist fan diagnostics, but
          // must not make the rest of the Fan settings disappear.
          settings.value = { ...settings.value, diagnosticLoggingEnabled: false };
        }
      } catch { settings.value = structuredClone(DEFAULT_FAN_SETTINGS); }
      initialized.value = true;

    })();
  }
  await initialization;
  // Share only initialization work. Each caller owns a current plain snapshot.
  return snapshotFanSettings();
}

/** Observe only the preset name; keep Vue dependency tracking and validation. */
export function getFanSelectedPreset(): FanPreset {
  const preset = settings.value.preset;
  return isFanPreset(preset) ? preset : DEFAULT_FAN_SETTINGS.preset;
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

/** Preview compatibility only; production state comes from the lifecycle owner. */
export function setFanControlActive(active: boolean): void {
  if (!FAN_FORCE_PREVIEW) return;
  fanControlActive.value = active;
}

/** Current, authenticated control receipts, independent of FanView activation. */
export function setFanNavigationControl(active: boolean, curve: readonly FanNode[]): void {
  fanControlActive.value = active;
  const next = active ? curve : [];
  if (next.length !== navigationCurve.value.length || next.some((node, i) =>
    node.tempC !== navigationCurve.value[i].tempC || node.dutyPercent !== navigationCurve.value[i].dutyPercent)) {
    navigationCurve.value = next.map(node => ({ tempC: node.tempC, dutyPercent: node.dutyPercent }));
  }
}

export function createConfiguredFanAdapter(): FanApiAdapter {
  return createFanApiAdapter({ enabled: settings.value.featureEnabled === true });
}

/** Select an existing curve without replacing other saved curves or creating an ON intent. */
export async function saveFanPresetSelection(preset: FanPreset, expected: Record<string, unknown>): Promise<void> {
  const fingerprint = (value: Record<string, unknown>) => JSON.stringify([value.preset, value.nodes, value.presetCurves]);
  let committed: Partial<FanFeatureSettings> | null = null;
  await updateSettings(document => {
    const raw = document.fan as Record<string, unknown>;
    if (fingerprint(raw) !== fingerprint(expected)) throw new Error('SHORTCUT_SOURCE_CHANGED');
    const active: FanPreset = isFanPreset(raw.preset) ? raw.preset : 'balanced';
    const curves = normalizeFanPresetCurves(raw.presetCurves, raw.nodes, active);
    const oldTable = raw.presetCurves && typeof raw.presetCurves === 'object' && !Array.isArray(raw.presetCurves) ? raw.presetCurves as Record<string, unknown> : {};
    const table = { ...oldTable };
    // Preserve a real legacy active curve before changing its preset identity.
    if (!(active in table) && Array.isArray(raw.nodes) && normalizedPersistedCurve(raw.nodes)) table[active] = structuredClone(raw.nodes);
    if (!(preset in table)) table[preset] = structuredClone(curves[preset]);
    document.fan = { ...raw, preset, nodes: structuredClone(curves[preset]), presetCurves: table };
    committed = structuredClone(document.fan) as Partial<FanFeatureSettings>;
  });
  const selected = committed as Partial<FanFeatureSettings> | null;
  if (selected) settings.value = { ...settings.value, ...selected, preset,
    nodes: structuredClone(selected.nodes || []),
    presetCurves: normalizeFanPresetCurves(selected.presetCurves, selected.nodes, preset) };
}
