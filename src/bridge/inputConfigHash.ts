/**
 * T10 configuration fingerprinting.
 *
 * The host must bind a durable, normalized input configuration rather than a
 * UI patch.  Apply/ACK bookkeeping is deliberately excluded: it describes a
 * Host observation of the configuration and would otherwise change the hash
 * after an ACK without any user-config mutation.
 */
export const INPUT_CONFIG_HASH_SCHEMA = 'InputConfigHash.v1';

const APPLICATION_BOOKKEEPING_KEYS = new Set([
  'appliedRevision',
  'appliedConfigHash',
  'pendingConfigHash',
  'hostAcknowledgedRevision',
  'hostAcknowledgedConfigHash',
  'applyStatus',
]);

/** Return the complete durable input configuration, without Host observation fields. */
export function projectInputConfiguration(input: Record<string, unknown>): Record<string, unknown> {
  const projected: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(input)) {
    if (!APPLICATION_BOOKKEEPING_KEYS.has(key)) projected[key] = value;
  }
  return projected;
}

/** Canonical JSON: sorted object keys, JSON number rules, no non-finite values. */
export function canonicalizeInputConfiguration(value: unknown): string {
  const visit = (current: unknown): string => {
    if (current === null) return 'null';
    if (typeof current === 'string') return JSON.stringify(current);
    if (typeof current === 'boolean') return current ? 'true' : 'false';
    if (typeof current === 'number') {
      if (!Number.isFinite(current)) throw new Error('input-config-hash-non-finite-number');
      return Object.is(current, -0) ? '0' : String(current);
    }
    if (Array.isArray(current)) return `[${current.map((item) => visit(item)).join(',')}]`;
    if (typeof current === 'object') {
      const object = current as Record<string, unknown>;
      return `{${Object.keys(object).sort().map((key) => {
        const item = object[key];
        if (item === undefined) throw new Error(`input-config-hash-undefined:${key}`);
        return `${JSON.stringify(key)}:${visit(item)}`;
      }).join(',')}}`;
    }
    throw new Error(`input-config-hash-unsupported:${typeof current}`);
  };
  return visit(value);
}

function sha256Hex(text: string): string {
  const bytes = new TextEncoder().encode(text);
  const bitLength = BigInt(bytes.length) * 8n;
  const paddedLength = Math.ceil((bytes.length + 1 + 8) / 64) * 64;
  const data = new Uint8Array(paddedLength);
  data.set(bytes);
  data[bytes.length] = 0x80;
  for (let index = 0; index < 8; index += 1) {
    data[paddedLength - 1 - index] = Number((bitLength >> BigInt(index * 8)) & 0xffn);
  }

  const hash = new Uint32Array([
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  ]);
  const k = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
  ]);
  const words = new Uint32Array(64);
  const rotateRight = (word: number, count: number) => (word >>> count) | (word << (32 - count));

  for (let offset = 0; offset < data.length; offset += 64) {
    for (let index = 0; index < 16; index += 1) {
      const base = offset + index * 4;
      words[index] = ((data[base] << 24) | (data[base + 1] << 16) | (data[base + 2] << 8) | data[base + 3]) >>> 0;
    }
    for (let index = 16; index < 64; index += 1) {
      const s0 = rotateRight(words[index - 15], 7) ^ rotateRight(words[index - 15], 18) ^ (words[index - 15] >>> 3);
      const s1 = rotateRight(words[index - 2], 17) ^ rotateRight(words[index - 2], 19) ^ (words[index - 2] >>> 10);
      words[index] = (words[index - 16] + s0 + words[index - 7] + s1) >>> 0;
    }
    let [a, b, c, d, e, f, g, h] = hash;
    for (let index = 0; index < 64; index += 1) {
      const sigma1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
      const choose = (e & f) ^ (~e & g);
      const temp1 = (h + sigma1 + choose + k[index] + words[index]) >>> 0;
      const sigma0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
      const majority = (a & b) ^ (a & c) ^ (b & c);
      const temp2 = (sigma0 + majority) >>> 0;
      h = g; g = f; f = e; e = (d + temp1) >>> 0;
      d = c; c = b; b = a; a = (temp1 + temp2) >>> 0;
    }
    hash[0] = (hash[0] + a) >>> 0;
    hash[1] = (hash[1] + b) >>> 0;
    hash[2] = (hash[2] + c) >>> 0;
    hash[3] = (hash[3] + d) >>> 0;
    hash[4] = (hash[4] + e) >>> 0;
    hash[5] = (hash[5] + f) >>> 0;
    hash[6] = (hash[6] + g) >>> 0;
    hash[7] = (hash[7] + h) >>> 0;
  }
  return Array.from(hash, (word) => word.toString(16).padStart(8, '0')).join('');
}

export function computeInputConfigHash(input: Record<string, unknown>): string {
  const projection = projectInputConfiguration(input);
  const canonical = canonicalizeInputConfiguration({ schema: INPUT_CONFIG_HASH_SCHEMA, input: projection });
  return `sha256:${sha256Hex(canonical)}`;
}
