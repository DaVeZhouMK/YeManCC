/** Read PE/CLI metadata bytes only. Does not load an assembly or start a process. */
import {createHash} from 'node:crypto';
export function readPeModuleIdentity(bytes) {
  const need = value => { if (!value) throw Error('SLEEP_DLL_PE_METADATA_INVALID'); };
  const range = (offset, size) => { need(Number.isSafeInteger(offset) && Number.isSafeInteger(size) && offset >= 0 && size >= 0 && offset + size <= bytes.length); return offset; };
  const u16 = p => bytes.readUInt16LE(range(p, 2));
  const u32 = p => bytes.readUInt32LE(range(p, 4));
  need(Buffer.isBuffer(bytes) && bytes.length >= 64 && u16(0) === 0x5a4d);
  const pe = u32(0x3c); need(u32(pe) === 0x4550);
  const sectionCount = u16(pe + 6), optionalSize = u16(pe + 20), optional = pe + 24, magic = u16(optional);
  need(sectionCount > 0 && sectionCount <= 96 && [0x10b, 0x20b].includes(magic));
  range(optional, optionalSize);
  const directories = optional + (magic === 0x10b ? 96 : 112);
  need(directories + 15 * 8 <= optional + optionalSize && u32(directories - 4) >= 15);
  const sections = Array.from({length: sectionCount}, (_, i) => {
    const p = range(optional + optionalSize + i * 40, 40);
    return {rva: u32(p + 12), size: u32(p + 16), offset: u32(p + 20)};
  });
  const atRva = (rva, size) => {
    const found = sections.filter(s => rva >= s.rva && rva + size <= s.rva + s.size);
    need(found.length === 1); return range(found[0].offset + rva - found[0].rva, size);
  };
  const cli = atRva(u32(directories + 14 * 8), 24);
  const metadataSize = u32(cli + 12), metadata = atRva(u32(cli + 8), metadataSize);
  const metaRange = (p, n) => { need(p >= metadata && p + n <= metadata + metadataSize); return range(p, n); };
  need(u32(metadata) === 0x424a5342);
  const versionSize = u32(metadata + 12); metaRange(metadata + 16, versionSize);
  let cursor = metadata + 16 + Math.ceil(versionSize / 4) * 4;
  const streamCount = u16(metaRange(cursor + 2, 2)); cursor += 4; need(streamCount > 0 && streamCount <= 16);
  const streams = new Map();
  for (let i = 0; i < streamCount; i++) {
    const offset = u32(metaRange(cursor, 8)), size = u32(cursor + 4), nameStart = cursor + 8;
    let end = nameStart; while (end - nameStart < 32 && bytes[metaRange(end, 1)] !== 0) end++;
    need(end - nameStart < 32);
    const name = bytes.toString('ascii', nameStart, end); need(!streams.has(name));
    streams.set(name, {offset: metaRange(metadata + offset, size), size});
    cursor = nameStart + Math.ceil((end - nameStart + 1) / 4) * 4;
  }
  const tables = streams.get('#~') ?? streams.get('#-'), guid = streams.get('#GUID'), strings = streams.get('#Strings'), blob = streams.get('#Blob');
  need(tables && guid && strings && blob);
  const tableRange = (p, n) => { need(p >= tables.offset && p + n <= tables.offset + tables.size); return range(p, n); };
  tableRange(tables.offset, 24);
  const heaps = bytes[tables.offset + 6], valid = bytes.readBigUInt64LE(tables.offset + 8), counts = Array(64).fill(0);
  cursor = tables.offset + 24;
  for (let i = 0; i < 64; i++) if ((valid >> BigInt(i)) & 1n) { counts[i] = u32(tableRange(cursor, 4)); cursor += 4; }
  need(counts[0] === 1 && counts[32] === 1);
  const s = heaps & 1 ? 4 : 2, g = heaps & 2 ? 4 : 2, b = heaps & 4 ? 4 : 2;
  const index = n => counts[n] >= 0x10000 ? 4 : 2;
  const coded = (bits, ids) => Math.max(...ids.map(id => counts[id])) >= 2 ** (16 - bits) ? 4 : 2;
  const typeRef = coded(2, [2, 1, 27]), method = coded(1, [6, 10]);
  // ECMA table row widths through Assembly (32); later tables are not needed.
  const widths = [2+s+3*g, coded(2,[0,26,35,1])+2*s, 4+2*s+typeRef+index(4)+index(6), index(4), 2+s+b,
    index(6), 8+s+b+index(8), index(8), 4+s, index(2)+typeRef, coded(3,[2,1,26,6,27])+s+b,
    2+coded(2,[4,8,23])+b, coded(5,[6,4,1,2,8,9,10,0,14,23,20,17,26,27,32,35,38,39,40,42,44,43])+coded(3,[6,10])+b,
    coded(1,[4,8])+b, 2+coded(2,[2,6,32])+b, 6+index(2), 4+index(4), b, index(2)+index(20), index(20),
    2+s+typeRef, index(2)+index(23), index(23), 2+s+b, 2+index(6)+coded(1,[20,23]), index(2)+2*method,
    s, b, 2+coded(1,[4,6])+s+index(26), 4+index(4), 8, 4, 16+b+2*s];
  const starts = [];
  for (let i = 0; i <= 32; i++) { starts[i] = cursor; tableRange(cursor, counts[i] * widths[i]); cursor += counts[i] * widths[i]; }
  const heapIndex = (p, width) => width === 2 ? u16(p) : u32(p);
  const text = i => {
    need(i >= 0 && i < strings.size); let end = strings.offset + i;
    while (end < strings.offset + strings.size && bytes[end]) end++;
    need(end < strings.offset + strings.size); return bytes.toString('utf8', strings.offset + i, end);
  };
  const guidIndex = heapIndex(starts[0] + 2 + s, g); need(guidIndex > 0 && guidIndex * 16 <= guid.size);
  const p = guid.offset + (guidIndex - 1) * 16;
  const hex = (value, size) => value.toString(16).padStart(size, '0');
  const moduleVersionId = `${hex(u32(p),8)}-${hex(u16(p+4),4)}-${hex(u16(p+6),4)}-${bytes.subarray(p+8,p+10).toString('hex')}-${bytes.subarray(p+10,p+16).toString('hex')}`;
  const assembly = starts[32], flags = u32(assembly + 12), publicIndex = heapIndex(assembly + 16, b);
  let publicKeyToken = 'null';
  if (publicIndex) {
    need(publicIndex < blob.size); let q = blob.offset + publicIndex, length = bytes[range(q++,1)];
    if ((length & 0xc0) === 0x80) length = ((length & 0x3f) << 8) | bytes[range(q++,1)];
    else if ((length & 0xe0) === 0xc0) { length = (length & 0x1f) * 0x1000000 + bytes[range(q++,1)] * 0x10000 + bytes[range(q++,1)] * 0x100 + bytes[range(q++,1)]; }
    else need(length < 0x80);
    need(q + length <= blob.offset + blob.size && length > 0);
    const key = bytes.subarray(q, q + length);
    publicKeyToken = (flags & 1 ? Buffer.from(createHash('sha1').update(key).digest().subarray(-8)).reverse() : key).toString('hex');
  }
  const name = text(heapIndex(assembly + 16 + b, s)), culture = text(heapIndex(assembly + 16 + b + s, s)) || 'neutral';
  need(name && !/[,\r\n\0]/.test(name));
  const version = [2,4,6,8].map(offset => u16(assembly + 2 + offset)).join('.');
  return {moduleName: text(heapIndex(starts[0] + 2, s)), moduleVersionId,
    identity: `${name}, Version=${version}, Culture=${culture}, PublicKeyToken=${publicKeyToken}`};
}