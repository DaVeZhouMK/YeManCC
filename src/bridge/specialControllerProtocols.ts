/** HC-derived controller protocol identities that must not be flattened to a
 * generic Xbox mapping. These are classification records only; no HID writes.
 */
export type SpecialControllerProtocol = 'msi-claw' | 'lenovo-legion' | 'generic-xinput';

export const MSI_CLAW = {
  vendorId: 0x0db0,
  xInputProductIds: [0x1901] as const,
  dInputProductIds: [0x1902, 0x1903] as const,
  /** Exact locked-HC ClawA1M.GamepadMode numeric values; not a HID command. */
  modeValues: { offline: 0, xInput: 1, directInput: 2, msi: 3, desktop: 4, bios: 5, testing: 6 } as const,
} as const;

/** HC Devices/Lenovo/LegionGoTablet.cs: only 17EF:6182/3/4/5 and 17EF:61EB/C/D/E. */
export const LENOVO_LEGION_TABLET = {
  vendorId: 0x17ef,
  xInputProductIds: [0x6182, 0x61eb] as const,
  hidProductIds: [0x6183, 0x6184, 0x6185, 0x61ec, 0x61ed, 0x61ee] as const,
} as const;

/** HC Devices/Lenovo/LegionGoSZ1.cs: only 1A86:E310/E311. */
export const LENOVO_LEGION_S = {
  vendorId: 0x1a86,
  xInputProductIds: [0xe310] as const,
  hidProductIds: [0xe311] as const,
} as const;

/**
 * Compatibility aggregate for capability displays only. Never use the
 * flattened PID lists for admission: HC binds each PID to its own VID.
 * No fallback poller, semantic signal name, HID report, or OEM write is
 * declared here because current YMCC has no HC-derived transport/receipt.
 */
export const LENOVO_LEGION = {
  vendorIds: [LENOVO_LEGION_TABLET.vendorId, LENOVO_LEGION_S.vendorId] as const,
  xInputProductIds: [...LENOVO_LEGION_TABLET.xInputProductIds, ...LENOVO_LEGION_S.xInputProductIds] as const,
  legionSProductIds: LENOVO_LEGION_S.xInputProductIds,
  hidProductIds: [...LENOVO_LEGION_TABLET.hidProductIds, ...LENOVO_LEGION_S.hidProductIds] as const,
  variants: [LENOVO_LEGION_TABLET, LENOVO_LEGION_S] as const,
} as const;

function has(values: readonly number[], value: number): boolean { return values.includes(value); }

export function classifySpecialController(vendorId: number, productId: number): SpecialControllerProtocol {
  if (vendorId === MSI_CLAW.vendorId &&
      (has(MSI_CLAW.xInputProductIds, productId) || has(MSI_CLAW.dInputProductIds, productId))) return 'msi-claw';
  if ((vendorId === LENOVO_LEGION_TABLET.vendorId &&
       (has(LENOVO_LEGION_TABLET.xInputProductIds, productId) || has(LENOVO_LEGION_TABLET.hidProductIds, productId))) ||
      (vendorId === LENOVO_LEGION_S.vendorId &&
       (has(LENOVO_LEGION_S.xInputProductIds, productId) || has(LENOVO_LEGION_S.hidProductIds, productId)))) return 'lenovo-legion';
  return 'generic-xinput';
}

export function requiresSpecialSignalPath(vendorId: number, productId: number): boolean {
  return classifySpecialController(vendorId, productId) !== 'generic-xinput';
}
