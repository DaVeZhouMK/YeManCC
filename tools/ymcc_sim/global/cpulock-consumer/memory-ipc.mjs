import assert from 'node:assert/strict';

// This is the only substituted product module: the external WebView IPC transport.
export const IPC_SOURCE = `
export const isNativeRuntime = false;
export function invoke(command, args = {}, options) { return globalThis.__ymccIpc.invoke(command, args, options); }
export function on() { throw new Error('UNREVIEWED_IPC_SUBSCRIPTION'); }
`;
export const SETTINGS_DIR = 'Z:\\YMCC-MEMORY-ONLY\\PowerControl';
export const POWER = {
  scheme: '1cb8b882-a900-4b9f-9bac-99d151e64441',
  sub: '54533251-82be-4824-96c1-47b60b740d00',
  freq: '75b0ae3f-bce0-45a7-8c89-c9611c25e100',
  freq2: '75b0ae3f-bce0-45a7-8c89-c9611c25e101',
  freq3: '75b0ae3f-bce0-45a7-8c89-c9611c25e102',
  min: '893dee8e-2bef-41e0-89c6-b55d0929964c',
  min2: '893dee8e-2bef-41e0-89c6-b55d0929964d',
  min3: '893dee8e-2bef-41e0-89c6-b55d0929964e',
  turbo: 'be337238-0d82-4146-a960-4f3749d470c7',
  sched: '36687f9e-e3a5-4dbf-b1dc-15eb381c6863',
  sched2: '36687f9e-e3a5-4dbf-b1dc-15eb381c6864',
  sched3: '36687f9e-e3a5-4dbf-b1dc-15eb381c6865',
  max: 'bc5038f7-23e0-4960-96da-33abaf5935ec',
  throttle: '3b04d4fd-1cc7-4f23-ab1c-d1337819c4bb',
};
const copy = value => structuredClone(value);
export class MemoryIpc {
  constructor() {
    this.calls = [];
    this.denied = [];
    this.document = null;
    this.revision = 1;
    this.writeMode = 'ok';
    this.readMode = 'ok';
    this.powerWriteMode = 'ok';
    this.powerReadMode = 'ok';
    this.values = new Map();
    this.acceptedPowerBatches = [];
    this.file = SETTINGS_DIR + '\\yeman-settings.json';
  }
  setPower(side, setting, value) { this.values.set(side + ':' + setting, value); }
  getPower(side, setting) { return this.values.get(side + ':' + setting); }
  deny(command, args, reason) {
    this.denied.push({ command, args: copy(args), reason });
    throw new Error('UNREVIEWED_MEMORY_IPC:' + reason);
  }
  async invoke(command, args, options) {
    const row = { command, args: copy(args), options: copy(options) };
    this.calls.push(row);
    try {
      const result = await this.dispatch(command, args);
      row.receipt = copy(result);
      return result;
    } catch (error) {
      row.error = error.message;
      throw error;
    }
  }
  async dispatch(command, args) {
    if (command === 'settings.read') {
      if (args.path !== this.file) return this.deny(command, args, 'settings path');
      if (this.readMode === 'throw') throw new Error('MEMORY_SETTINGS_READ_FAILURE');
      const stamp = String(this.revision);
      return args.stamp === stamp ? { stamp, unchanged: true } :
        { stamp, unchanged: false, content: JSON.stringify(this.document) };
    }
    if (command === 'settings.write') {
      if (args.path !== this.file) return this.deny(command, args, 'settings path');
      if (this.writeMode === 'throw') throw new Error('MEMORY_SETTINGS_WRITE_FAILURE');
      if (this.writeMode === 'false') return false;
      if (this.writeMode === 'rejected') return { ok: false, reason: 'memory-write-rejected' };
      if (this.writeMode === 'malformed') return { ok: true, content: '{broken-json' };
      assert.equal(typeof args.content, 'string');
      assert.ok(args.baseline && typeof args.baseline === 'object');
      this.document = JSON.parse(args.content);
      if (this.writeMode === 'committed-adjustment') this.document.cpu.lock.acFreq = 2444;
      this.revision++;
      return { ok: true, content: JSON.stringify(this.document) };
    }
    if (command === 'registry.read') {
      const setting = args.path.split('\\').at(-1);
      const prefix = `SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\${POWER.scheme}\\${POWER.sub}\\`;
      if (args.root !== 'HKLM' || args.path !== prefix + setting ||
          ![POWER.freq, POWER.min, POWER.turbo, POWER.sched].includes(setting) ||
          !['ACSettingIndex', 'DCSettingIndex'].includes(args.name)) return this.deny(command, args, 'power read contract');
      if (this.powerReadMode === 'throw') throw new Error('MEMORY_POWER_READ_UNSUPPORTED');
      if (this.powerReadMode === 'missing') return null;
      const side = args.name === 'ACSettingIndex' ? 'ac' : 'dc';
      return this.values.get(side + ':' + setting) ?? null;
    }
    if (command === 'registry.writePowerBatch') {
      const allowed = Object.entries(POWER).filter(([key]) => !['scheme', 'sub'].includes(key)).map(([, value]) => value);
      if (args.scheme !== POWER.scheme || args.subGroup !== POWER.sub ||
          !['ACSettingIndex', 'DCSettingIndex'].includes(args.valueName) || !Array.isArray(args.entries) ||
          args.entries.some(row => !allowed.includes(row.setting) || !Number.isFinite(row.value) || !Number.isInteger(row.value))) {
        return this.deny(command, args, 'power batch contract');
      }
      if (this.powerWriteMode === 'throw') throw new Error('MEMORY_POWER_BATCH_FAILURE');
      if (this.powerWriteMode === 'rejected') return { ok: false, written: 0, failed: args.entries.map(row => ({ setting: row.setting, code: 50 })) };
      const side = args.valueName === 'ACSettingIndex' ? 'ac' : 'dc';
      for (const row of args.entries) this.setPower(side, row.setting, row.value);
      this.acceptedPowerBatches.push(copy(args));
      return { ok: true, written: args.entries.length, failed: [] };
    }
    return this.deny(command, args, 'command');
  }
}
