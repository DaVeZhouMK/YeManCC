import { applyRogXboxAllyXGyroMatrix, classifyRogGyroProvider, isRogOemHidMonitorDevice, ROG_GYRO_UNIT } from '../src/bridge/rogGyroProtocol';
if (ROG_GYRO_UNIT !== 'deg/s') throw new Error('HC Windows gyrometer unit must be deg/s');
if (!isRogOemHidMonitorDevice(0x0b05, 0x1b4c)) throw new Error('ROG OEM monitor identity failed');
if (classifyRogGyroProvider(0x0b05, 0x1b4c) !== 'none') throw new Error('shared ROG OEM PID must not select a gyro matrix');
if (classifyRogGyroProvider(0x045e, 0x028e) !== 'none') throw new Error('Xbox pad classified as ROG gyro');
const mapped = applyRogXboxAllyXGyroMatrix({ x: 0.2, y: 0.4, z: 0.6 });
if (mapped.x !== 0.2 || mapped.y !== 0.6 || mapped.z !== -0.4) throw new Error('ROG gyro matrix mismatch vs IMUGyrometer remap');
console.log('ROG gyro protocol selftest: PASS');
