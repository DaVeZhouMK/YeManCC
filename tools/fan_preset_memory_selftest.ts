import { DEFAULT_FAN_PRESET_CURVES, normalizeFanPresetCurves } from '@/bridge/fanFeature';

const soft = [
  { tempC: 0, dutyPercent: 10 }, { tempC: 40, dutyPercent: 25 },
  { tempC: 70, dutyPercent: 55 }, { tempC: 100, dutyPercent: 85 },
];
const balanced = [
  { tempC: 0, dutyPercent: 35 }, { tempC: 40, dutyPercent: 45 },
  { tempC: 70, dutyPercent: 70 }, { tempC: 100, dutyPercent: 95 },
];

// A legacy one-curve document migrates only to the active preset.
const migrated = normalizeFanPresetCurves(undefined, soft, 'soft');
if (JSON.stringify(migrated.soft) !== JSON.stringify(soft) ||
    JSON.stringify(migrated.balanced) !== JSON.stringify(DEFAULT_FAN_PRESET_CURVES.balanced) ||
    JSON.stringify(migrated.aggressive) !== JSON.stringify(DEFAULT_FAN_PRESET_CURVES.aggressive)) {
  throw new Error(`legacy curve migration was not isolated to the active preset: ${JSON.stringify(migrated)}`);
}

// Once all three curves are persisted, loading one preset must not overwrite
// the other two. This is the regression that previously reset custom curves.
const persisted = normalizeFanPresetCurves({
  soft,
  balanced,
  aggressive: DEFAULT_FAN_PRESET_CURVES.aggressive,
}, undefined, 'balanced');
if (JSON.stringify(persisted.soft) !== JSON.stringify(soft) ||
    JSON.stringify(persisted.balanced) !== JSON.stringify(balanced) ||
    JSON.stringify(persisted.aggressive) !== JSON.stringify(DEFAULT_FAN_PRESET_CURVES.aggressive)) {
  throw new Error(`preset curves were not retained independently: ${JSON.stringify(persisted)}`);
}

console.log(JSON.stringify({ ok: true, checks: [
  'legacy-curve-migrates-to-active-preset-only',
  'soft-balanced-aggressive-curves-load-independently',
] }));
