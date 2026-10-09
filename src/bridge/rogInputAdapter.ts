/** ROG Xbox Ally controller contract derived from the locked HC source.
 *
 * Native transport lives in main.cpp and is gated on VID/PID 0B05:1ABE/1B4C,
 * feature report 0x5A, and a stable HID gamepad source.  Xbox 360 and GPD
 * never match this identity.  The public owner remains PhysicalInputOwnership.v1.
 */
export const ROG_VENDOR_ID = 0x0b05;
export const ROG_XBOX_ALLY_PRODUCT_IDS = [0x1abe, 0x1b4c] as const;
export const ROG_INPUT_HID_ID = 0x5a;

export type RogInputFace = 'rog-vendor-hid' | 'xbox-xinput';
export type RogControllerPhase = 'observed' | 'armed' | 'suppressed' | 'restoring' | 'restored' | 'failed';
export const ROG_SHARED_OWNER_POLICY = 'PhysicalInputOwnership.v1';

export interface RogDeviceIdentity {
  vendorId: number;
  productId: number;
  instanceId?: string;
  xInputUserIndex?: number;
}

export interface RogControllerSession {
  identity: RogDeviceIdentity;
  phase: RogControllerPhase;
  restoreRequired: boolean;
  epoch: number;
}

export function isRogXboxAlly(identity: RogDeviceIdentity): boolean {
  return identity.vendorId === ROG_VENDOR_ID &&
    (ROG_XBOX_ALLY_PRODUCT_IDS as readonly number[]).includes(identity.productId);
}

export function classifyRogFace(instanceId: string): RogInputFace | null {
  const value = instanceId.toUpperCase();
  if (value.includes(`VID_${ROG_VENDOR_ID.toString(16).padStart(4, '0').toUpperCase()}`) &&
      (value.includes('PID_1ABE') || value.includes('PID_1B4C'))) return 'rog-vendor-hid';
  if (value.includes('IG_00') && (value.includes('XINPUT') || value.includes('XUSB') || value.includes('VID_045E'))) return 'xbox-xinput';
  return null;
}

/** HC ROGAlly.XBoxController(false/true) report payload, kept explicit for audit. */
export function buildRogXboxModeReport(disabled: boolean): Uint8Array {
  return Uint8Array.from([0x5a, 0xd1, 0x0b, 0x01, disabled ? 0x02 : 0x01]);
}

export function armRogController(identity: RogDeviceIdentity, epoch = 1): RogControllerSession {
  if (!isRogXboxAlly(identity)) throw new Error('rog-identity-mismatch');
  return { identity, phase: 'armed', restoreRequired: false, epoch };
}

/** Suppression is only a state transition; a native transport must acknowledge the write. */
export function acknowledgeRogSuppressed(session: RogControllerSession): RogControllerSession {
  if (session.phase !== 'armed' || session.restoreRequired) throw new Error('rog-suppress-order');
  return { ...session, phase: 'suppressed', restoreRequired: true };
}

export function beginRogRestore(session: RogControllerSession): RogControllerSession {
  if (!session.restoreRequired || !['suppressed', 'failed', 'restoring'].includes(session.phase)) {
    throw new Error('rog-restore-not-required');
  }
  return { ...session, phase: 'restoring' };
}

export function acknowledgeRogRestored(session: RogControllerSession): RogControllerSession {
  if (session.phase !== 'restoring') throw new Error('rog-restore-order');
  return { ...session, phase: 'restored', restoreRequired: false, epoch: session.epoch + 1 };
}

export function failRogController(session: RogControllerSession): RogControllerSession {
  return session.restoreRequired ? { ...session, phase: 'failed' } : { ...session, phase: 'failed' };
}

export function canWriteRogXboxFace(session: RogControllerSession, bound: {
  report5A: boolean;
  stableInputSource: boolean;
}): boolean {
  return isRogXboxAlly(session.identity) &&
    session.phase === 'armed' &&
    !session.restoreRequired &&
    bound.report5A === true &&
    bound.stableInputSource === true;
}
