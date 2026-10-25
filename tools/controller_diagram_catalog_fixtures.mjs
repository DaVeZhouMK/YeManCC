// Browser acceptance fixtures from the existing native OemKeyDef catalog.
// These are simulated profiles, never a hardware detection result.
const key = (keyId, label, backIndex, physicalForm = 'hid-button', triggerCapability = 'full') => ({ keyId, label, backIndex, physicalForm, triggerCapability });
export const diagramCatalogFixtures = {
  none: { familyId: 'unknown', supported: false, keys: [] },
  two: { familyId: 'msi-claw', supported: true, keys: [
    key('claw', 'CLAW', null, 'wmi-event', 'click-only'), key('qs', 'QS', null, 'wmi-event', 'click-only'),
    key('m1', 'M1', 2), key('m2', 'M2', 1),
  ] },
  four: { familyId: 'lenovo-legion-go', supported: true, keys: [
    key('legionfrontl', 'LegionL', null), key('legionfrontr', 'LegionR', null),
    key('legionr', 'Y1', 1), key('legionm3', 'M3', 2), key('legiony2', 'Y2', 3), key('legiony3', 'Y3', 4),
    key('legionl', 'M1（背键）', null), key('legionm2', 'M2（背键）', null),
  ] },
  unavailable: { familyId: 'msi-claw', supported: true, keys: [
    { ...key('claw', 'CLAW', null, 'wmi-event', 'click-only'), captureImplemented: false },
    key('qs', 'QS', null, 'wmi-event', 'click-only'),
    { ...key('m1', 'M1', 2), captureImplemented: false }, key('m2', 'M2', 1),
  ] },
};
