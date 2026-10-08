/**
 * HC has a Windows-IMU route, but it selects the device matrix from the
 * machine's SMBIOS/DMI model (RC71L/RC72LA/RC73YA/RC73XA), not from the
 * shared ASUS OEM-HID VID/PID below. A PID-only caller therefore has no
 * admitted gyro provider or matrix.
 */
export const ROG_GYRO_PROVIDER = 'windows-imu';
export const ROG_GYRO_UNIT = 'deg/s';
export const ROG_VENDOR_ID = 0x0b05;
export const ROG_OEM_HID_MONITOR_PRODUCT_IDS = [0x1abe, 0x1b4c] as const;
export const HC_GYRO_THRESHOLD_DPS = 2000;

/** XboxROGAllyX GyroMatrix from locked HC Devices/ASUS/XboxROGAllyX.cs */
export const ROG_XBOX_ALLY_X_GYRO_MATRIX = {
  axis: { x: 1, y: 1, z: -1 },
  swap: { X: 'X' as const, Y: 'Z' as const, Z: 'Y' as const },
};

export const ROG_XBOX_ALLY_X_ACCEL_MATRIX = {
  axis: { x: -1, y: -1, z: 1 },
  swap: { X: 'X' as const, Y: 'Z' as const, Z: 'Y' as const },
};

export type RogGyroSample = { x: number; y: number; z: number };
type Axis = 'X' | 'Y' | 'Z';

/**
 * The matching IDs are HC ROGAlly OEM-input monitor IDs. They are useful for
 * that face only; four HC DMI SKUs sharing them use different gyro matrices.
 */
export function isRogOemHidMonitorDevice(vendorId: number, productId: number): boolean {
  return vendorId === ROG_VENDOR_ID && (ROG_OEM_HID_MONITOR_PRODUCT_IDS as readonly number[]).includes(productId);
}

export function classifyRogGyroProvider(vendorId: number, productId: number): typeof ROG_GYRO_PROVIDER | 'none' {
  // HC does not use these shared OEM HID IDs as an IMU matrix discriminator.
  // Keep PID-only observation fail-closed until a HC-equivalent DMI identity
  // receipt binds an exact SKU to its matrix pair.
  void vendorId;
  void productId;
  return 'none';
}

function axisIndex(axis: Axis): 0 | 1 | 2 {
  return axis === 'Y' ? 1 : axis === 'Z' ? 2 : 0;
}

/** IMUGyrometer/IMUAccelerometer: readingAxis[remap[input]] = raw; then * Axis. */
export function applyHcDeviceImuMatrix(
  sample: RogGyroSample,
  matrix: { axis: RogGyroSample; swap: { X: Axis; Y: Axis; Z: Axis } },
): RogGyroSample {
  const raw = [sample.x, sample.y, sample.z];
  const remap = [axisIndex(matrix.swap.X), axisIndex(matrix.swap.Y), axisIndex(matrix.swap.Z)];
  const out = [0, 0, 0];
  out[remap[0]] = raw[0];
  out[remap[1]] = raw[1];
  out[remap[2]] = raw[2];
  return { x: out[0] * matrix.axis.x, y: out[1] * matrix.axis.y, z: out[2] * matrix.axis.z };
}

export function applyRogXboxAllyXGyroMatrix(sample: RogGyroSample): RogGyroSample {
  return applyHcDeviceImuMatrix(sample, ROG_XBOX_ALLY_X_GYRO_MATRIX);
}
