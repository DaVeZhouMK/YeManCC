import {
  armRogController,
  acknowledgeRogSuppressed,
  acknowledgeRogRestored,
  beginRogRestore,
  buildRogXboxModeReport,
  canWriteRogXboxFace,
  classifyRogFace,
  isRogXboxAlly,
  ROG_SHARED_OWNER_POLICY,
} from '../src/bridge/rogInputAdapter';

if (!isRogXboxAlly({ vendorId: 0x0b05, productId: 0x1b4c })) throw new Error('ROG identity failed');
if (ROG_SHARED_OWNER_POLICY !== 'PhysicalInputOwnership.v1') throw new Error('ROG policy drift');
if (isRogXboxAlly({ vendorId: 0x045e, productId: 0x028e })) throw new Error('foreign identity accepted');
if (classifyRogFace('HID\\VID_0B05&PID_1B4C&MI_05&IG_00') !== 'rog-vendor-hid') throw new Error('vendor face failed');
if (classifyRogFace('HID\\VID_045E&PID_028E&IG_00\\XINPUT') !== 'xbox-xinput') throw new Error('xinput face failed');
if (classifyRogFace('HID\\VID_0B05&PID_1B4C&MI_01') !== 'rog-vendor-hid') throw new Error('vendor composite failed');
if (Array.from(buildRogXboxModeReport(true)).join(',') !== '90,209,11,1,2') throw new Error('disable report drift');
if (Array.from(buildRogXboxModeReport(false)).join(',') !== '90,209,11,1,1') throw new Error('restore report drift');

let session = armRogController({ vendorId: 0x0b05, productId: 0x1b4c, xInputUserIndex: 0 });
if (!canWriteRogXboxFace(session, { report5A: true, stableInputSource: true })) throw new Error('armed ROG should be writable');
if (canWriteRogXboxFace(session, { report5A: true, stableInputSource: false })) throw new Error('missing HID source was writable');
if (canWriteRogXboxFace({ identity: { vendorId: 0x045e, productId: 0x028e }, phase: 'armed', restoreRequired: false, epoch: 1 }, { report5A: true, stableInputSource: true })) {
  throw new Error('xbox 360 identity was writable');
}
session = acknowledgeRogSuppressed(session);
if (canWriteRogXboxFace(session, { report5A: true, stableInputSource: true })) throw new Error('already suppressed session stayed writable');
session = beginRogRestore(session);
session = acknowledgeRogRestored(session);
if (session.phase !== 'restored' || session.restoreRequired || session.epoch !== 2) throw new Error('lifecycle failed');
console.log('ROG input adapter selftest: PASS');
