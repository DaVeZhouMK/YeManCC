import assert from 'node:assert/strict';
import { CONTROLLER_PERSONA_OPTIONS, isControllerPersona, isVirtualOutputPersona, migrateControllerPersona } from '../src/bridge/controllerPersona';
import { newControllerShortcutRule, readControllerShortcutRules, validateControllerShortcutRules } from '../src/bridge/controllerShortcutRules';

assert.deepEqual(CONTROLLER_PERSONA_OPTIONS.map(option => option.value), ['disabled', 'steamdeck', 'dualsense-edge', 'elite']);
for (const persona of ['steamdeck', 'dualsense-edge', 'elite']) {
  assert.equal(isControllerPersona(persona), true);
  assert.equal(isVirtualOutputPersona(persona), true);
  assert.equal(migrateControllerPersona(persona), persona);
}
assert.equal(isControllerPersona('disabled'), true);
assert.equal(isVirtualOutputPersona('disabled'), false);
for (const [old, current] of [['xbox360', 'elite'], ['dualshock4', 'dualsense-edge'], ['dualsense', 'dualsense-edge']]) {
  assert.equal(isControllerPersona(old), false, 'retired identity must not be an active controller');
  assert.equal(isVirtualOutputPersona(old), false, 'retired identity must not reach virtual creation');
  assert.equal(migrateControllerPersona(old), current, 'only persisted configuration migrates');
  for (const enabled of [false, true]) {
    const raw = { ...newControllerShortcutRule('legacy', 'input.virtualGamepadToggle'), enabled,
      inputs: [{ source: 'controller' as const, code: 'a' }], params: { preset: old } };
    assert.ok(validateControllerShortcutRules([{ ...raw, enabled: true }]).length > 0, 'newly selected retired preset must fail validation');
    const [rule] = readControllerShortcutRules({ shortcutRules: [raw] });
    assert.equal(rule.params.preset, current);
    assert.equal(rule.enabled, enabled, 'migration must not enable disabled shortcuts');
    assert.deepEqual(validateControllerShortcutRules([rule]), []);
  }
}
for (const unknown of [null, undefined, true, 1, {}, 'unknown', 'dualsense-edge\0', 'Elite']) {
  assert.equal(isControllerPersona(unknown), false);
  assert.equal(isVirtualOutputPersona(unknown), false);
  assert.equal(migrateControllerPersona(unknown), 'disabled');
}
console.log('current controller personas: PASS (current catalog, strict admission, persisted migration, disabled shortcuts, invalid boundaries)');