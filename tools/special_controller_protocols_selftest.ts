import {
  classifySpecialController,
  requiresSpecialSignalPath,
  MSI_CLAW,
  LENOVO_LEGION_TABLET,
  LENOVO_LEGION_S,
} from '../src/bridge/specialControllerProtocols';

for (const pid of MSI_CLAW.xInputProductIds) if (classifySpecialController(MSI_CLAW.vendorId, pid) !== 'msi-claw') throw new Error('MSI XInput classification failed');
for (const pid of MSI_CLAW.dInputProductIds) if (!requiresSpecialSignalPath(MSI_CLAW.vendorId, pid)) throw new Error('MSI DInput special path failed');
if (MSI_CLAW.modeValues.xInput !== 1 || MSI_CLAW.modeValues.directInput !== 2 || MSI_CLAW.modeValues.desktop !== 4) throw new Error('MSI HC mode value parity failed');
for (const pid of [...LENOVO_LEGION_TABLET.xInputProductIds, ...LENOVO_LEGION_TABLET.hidProductIds]) if (classifySpecialController(LENOVO_LEGION_TABLET.vendorId, pid) !== 'lenovo-legion') throw new Error('Lenovo Tablet classification failed');
for (const pid of [...LENOVO_LEGION_S.xInputProductIds, ...LENOVO_LEGION_S.hidProductIds]) if (classifySpecialController(LENOVO_LEGION_S.vendorId, pid) !== 'lenovo-legion') throw new Error('Lenovo S classification failed');
if (classifySpecialController(LENOVO_LEGION_TABLET.vendorId, LENOVO_LEGION_S.xInputProductIds[0]) !== 'generic-xinput') throw new Error('cross-VID Lenovo S PID admitted');
if (classifySpecialController(LENOVO_LEGION_S.vendorId, LENOVO_LEGION_TABLET.xInputProductIds[0]) !== 'generic-xinput') throw new Error('cross-VID Lenovo Tablet PID admitted');
if (classifySpecialController(0x045e, 0x028e) !== 'generic-xinput') throw new Error('generic Xbox classification failed');
console.log('special controller protocols selftest: PASS');
