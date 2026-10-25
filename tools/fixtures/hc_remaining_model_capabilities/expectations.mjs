// Independent oracle: fixed HC C#/JSON plus the explicitly reviewed YMCC source-ownership policy.
// capture=true means software implemented; no runtime, Windows I/O or hardware observation is inferred.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { requireHcMotionMatrix } from '../../lib/hc_motion_matrix_guard.mjs';
const hash = (value, algorithm = 'sha256') => createHash(algorithm).update(value).digest('hex');
export const targets = ['AYANEO3', 'AYANEOAIR2', 'AYANEOAIRPro1S', 'AYANEOAIRPlusNxx', 'AYANEOFlip11',
  'OneXPlayerX2', 'OneXPlayerX2MiniPro', 'OneXPlayer3', 'SteamMachine', 'AYANEONEXT2'];
// Application policy comes from the parent/user contract, never from candidate C++.
// HC supplies capabilities/chords; YMCC deliberately owns only these vendor roles.
const oneXApplicationPolicy = {
  schema:'hc-onex-application-capture-policy-v1',
  source:'explicit-parent-user-contract-20261017',
  softwareImplementationOnly:true,
  runtimeReadyProven:false,
  turboWmiTakeover:false,
  vendorTurboCapture:false,
  vendorKeyboardCapture:false,
  miniVendorRearCapture:false,
  legalKeys:['turbo','keyboard','home','l4','r4'],
  routes:{
    OneXPlayerX2:{turbo:'keyboard-vk',keyboard:'keyboard-vk',home:'onex-x2-vendor-click',l4:'onex-x2-vendor-held',r4:'onex-x2-vendor-held'},
    OneXPlayer3:{turbo:'keyboard-vk',keyboard:'keyboard-vk',home:'onex-x2-vendor-click',l4:'onex-x2-vendor-held',r4:'onex-x2-vendor-held'},
    OneXPlayerX2MiniPro:{turbo:'keyboard-vk',keyboard:'keyboard-vk',home:'onex-x2-vendor-click',l4:'keyboard-vk',r4:'keyboard-vk'},
  },
};
// Explicit source-status correction; the catalog remains declared, with no physical reader.
const steamDeckCapturePolicy = {
  schema:'hc-steamdeck-unimplemented-capture-policy-v1',
  source:'explicit-parent-user-contract-20261017',
  family:'ValveSteamDeck',
  fullName:'HandheldCompanion.Devices.SteamDeck',
  catalogKeys:['special','special2'],
  captureFields:{
    captureImplemented:false,
    captureRoute:'physical-neptune-reader-unimplemented',
    captureReason:'physical-neptune-reader-not-integrated',
  },
};
const axes = ['X', 'Y', 'Z'];
export function closeBrace(text, open) {
  let depth = 0, state = 'code';
  for (let i = open; i < text.length; ++i) {
    const c = text[i], n = text[i + 1];
    if (state === 'line') { if (c === '\n') state = 'code'; continue; }
    if (state === 'block') { if (c === '*' && n === '/') { state = 'code'; ++i; } continue; }
    if (state === 'string' || state === 'char') {
      if (c === '\\') { ++i; continue; }
      if (c === (state === 'string' ? '"' : "'")) state = 'code';
      continue;
    }
    if (c === '/' && n === '/') { state = 'line'; ++i; }
    else if (c === '/' && n === '*') { state = 'block'; ++i; }
    else if (c === '"') state = 'string';
    else if (c === "'") state = 'char';
    else if (c === '{') ++depth;
    else if (c === '}' && --depth === 0) return i;
  }
  throw Error('Unbalanced source definition');
}
// Strip comments without changing positions; quoted strings are retained.
const clean = text => text.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,
  token => token.startsWith('/') ? token.replace(/[^\n]/g, ' ') : token);
function switchLabels(text, open, end) {
  const labels = []; let depth = 0;
  for (let i = open + 1; i < end; ++i) {
    if (text[i] === '"' || text[i] === "'") {
      const quote = text[i]; while (++i < end) { if (text[i] === '\\') ++i; else if (text[i] === quote) break; }
    } else if (text[i] === '{') ++depth;
    else if (text[i] === '}') --depth;
    else if (depth === 0) {
      const m = /^(?:case\s+"([^"]+)"|default)\s*:/.exec(text.slice(i));
      if (m) { labels.push({ start: i, end: i + m[0].length, value: m[1] }); i += m[0].length - 1; }
    }
  }
  return labels;
}
function parseTargetIdentities(text, inventory) {
  const code = clean(text);
  const switches = [...code.matchAll(/switch\s*\(\s*(ManufacturerName(?:\.ToUpper\(\))?|ProductName|Processor|Version)\s*\)\s*\{/g)]
    .map(m => { const open = code.indexOf('{', m.index), end = closeBrace(code, open);
      return { field: m[1].split('.')[0], open, end, labels: switchLabels(code, open, end) }; });
  const identities = [];
  for (const cls of targets) {
    const found = [...code.matchAll(new RegExp(`new\\s+${cls}\\s*\\(\\)`, 'g'))];
    if (found.length !== 1) throw Error(`Fixed IDevice ${cls}: expected one constructor identity`);
    const at = found[0].index, fields = {};
    for (const sw of switches.filter(s => s.open < at && at < s.end)) {
      const previous = sw.labels.filter(l => l.start < at); let i = previous.length - 1;
      if (i < 0) throw Error(`No switch labels for ${cls}`);
      const selected = [previous[i].value];
      while (i > 0 && !code.slice(previous[i - 1].end, previous[i].start).trim()) selected.unshift(previous[--i].value);
      if (selected.some(v => v === undefined)) throw Error(`Unexpected default identity for ${cls}`);
      fields[sw.field] = selected;
    }
    const inv = inventory.identities.filter(r => r.deviceClass === cls);
    const expected = Object.fromEntries(inv[0]?.switches.map(s => [s.field, s.values]) || []);
    if (inv.length !== 1 || JSON.stringify(fields) !== JSON.stringify(expected)) throw Error(`IDevice/inventory identity disagreement for ${cls}`);
    for (const manufacturer of fields.ManufacturerName) for (const product of fields.ProductName)
      identities.push({ deviceClass: cls, manufacturer, product, processor: '', systemVersion: '', systemProduct: '' });
  }
  if (identities.length !== 63) throw Error(`Expected 63 fixed new identity combinations including NEXT2, got ${identities.length}`);
  return identities;
}
const canonical = {
  'AYANEO Button': ['ckbig', 27], 'Custom Key Big': ['ckbig', 27], 'Custom Key Small': ['cksmall', 28],
  LC: ['cktl', 29], RC: ['cktr', 30], 'Custom Key Top Left': ['cktl', 29], 'Custom Key Top Right': ['cktr', 30],
  T: ['ayat', 31], LC1: ['m1', 20], LC2: ['l4', 33], RC1: ['m2', 21], RC2: ['r4', 32],
  Turbo: ['turbo', 40], Keyboard: ['keyboard', 35], M1: ['l4', 33], M2: ['r4', 32],
};
function vk(name) {
  if (/^F(?:[1-9]|1[0-9]|2[0-4])$/.test(name)) return 0x6f + Number(name.slice(1));
  const codes = { RControl: 0x11, LControl: 0x11, LWin: 0x5b, LMenu: 0x12, RMenu: 0x12, D: 0x44, O: 0x4f };
  if (!(name in codes)) throw Error(`Unreviewed upstream key token ${name}`);
  return codes[name];
}
// Stable input-bit contract, independent of the production enum/evaluator.
const vkCodes = [0x11, 0x12, 0x10, 0x5b, 0x72, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e,
  0x7f, 0x80, 0x81, 0x84, 0x85, 0x86, 0x87, 0x4c, 0x52, 0x4f, 0x47, 0x54, 0x44, 0x1b, 0x2e, 0x2c, 0x09, 0x82, 0x83];
function kbMask(keys) { return keys.reduce((mask, key) => { const index = vkCodes.indexOf(key); if (index < 0) throw Error(`Unknown VK ${key}`); return mask + 2 ** index; }, 0); }
export function generateExpectations(workspace, fixtures) {
  const upstream = path.join(workspace, 'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream');
  const inventoryPath = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/inventory/upstream-inventory.json');
  const pinsBytes = fs.readFileSync(path.join(fixtures, 'source-pins.json')), pins = JSON.parse(pinsBytes);
  const manifestBytes = fs.readFileSync(path.join(upstream, 'source-manifest.json')), manifest = JSON.parse(manifestBytes);
  const inventoryBytes = fs.readFileSync(inventoryPath), inventory = JSON.parse(inventoryBytes);
  if (hash(manifestBytes) !== pins.manifestSha256 || hash(inventoryBytes) !== pins.inventorySha256) throw Error('Pinned manifest/inventory SHA256 mismatch');
  const sources = new Map(), inputs = [];
  for (const pin of pins.files) {
    const file = path.join(upstream, 'sources/hc', pin.path), bytes = fs.readFileSync(file);
    const row = manifest.find(r => r.project === 'hc' && r.path === pin.path);
    const gitBlob = hash(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`), bytes]), 'sha1');
    if (!row || row.commit !== pins.commit || row.gitBlob !== pin.gitBlob || row.sha256.toLowerCase() !== pin.sha256 || hash(bytes) !== pin.sha256 || gitBlob !== pin.gitBlob)
      throw Error(`Fixed source SHA256/gitBlob mismatch: ${pin.path}`);
    sources.set(pin.path, bytes.toString('utf8')); inputs.push({ ...pin, bytes: bytes.length });
  }
  const get = p => { if (!sources.has(p)) throw Error(`Unpinned required HC source ${p}`); return sources.get(p); };
  const prefix = 'HandheldCompanion/';
  const idevice = get(prefix + 'Devices/IDevice.cs');
  const identities = parseTargetIdentities(idevice, inventory);
  const classes = new Map();
  for (const [file, text] of sources) {
    if (!file.startsWith(prefix + 'Devices/') || !file.endsWith('.cs')) continue;
    const re = /\b(?:public|internal)\s+(?:abstract\s+|sealed\s+)?class\s+(\w+)(?:\s*:\s*([\w.]+))?/g;
    for (const m of clean(text).matchAll(re)) {
      const open = text.indexOf('{', m.index), end = closeBrace(text, open);
      classes.set(m[1], { name: m[1], parent: m[2]?.split('.').at(-1), file, text: text.slice(open, end + 1) });
    }
  }
  const chainOf = name => { const chain = [], seen = new Set(); while (classes.has(name)) { if (seen.has(name)) throw Error('HC inheritance cycle'); seen.add(name); chain.unshift(classes.get(name)); name = classes.get(name).parent; } return chain; };
  const helper = get(prefix + 'Helpers/DeviceConfigurationHelper.cs');
  if (!helper.includes('File.Exists(filePath)') || !helper.includes('parentType.Name, originalDeviceClassName') || !helper.includes('type?.BaseType')) throw Error('HC configuration fallback changed');
  if (!idevice.includes("_axisRemapIndices[0] = AxisSwap['X']") || !get(prefix + 'Sensors/IMUGyrometer.cs').includes('readingAxis[remapIndices[0]] = rawX') || !get(prefix + 'Sensors/IMUAccelerometer.cs').includes('readingAxis[remapIndices[0]] = rawX')) throw Error('HC remap execution contract changed');
  const samples = [[1,2,3],[1,0,0],[0,1,0],[0,0,1],[-7.25,2.5,-0.125],[0,0,0],[1234.5,-678.25,90.125],[1e-9,-1e-8,1e-7]];
  const models = targets.map(name => {
    const chain = chainOf(name), inv = inventory.devices.find(d => d.name === name);
    if (!inv || JSON.stringify(chain.map(c => c.name)) !== JSON.stringify(inv.chain)) throw Error(`HC inherited class chain mismatch ${name}`);
    let keys = [];
    for (const cls of chain) {
      const ctor = clean(cls.text).match(new RegExp(`\\b(?:public|protected)\\s+${cls.name}\\s*\\([^)]*\\)\\s*(?::[^{}]*)?\\{`));
      if (!ctor) continue;
      const open = cls.text.indexOf('{', ctor.index), body = cls.text.slice(open, closeBrace(cls.text, open) + 1);
      if (body.includes('OEMChords.Clear()')) keys = [];
      for (const removal of clean(body).matchAll(/OEMChords\.RemoveAll\([^;]*ButtonFlags\.(\w+)[^;]*;/g)) keys = keys.filter(k=>k.flag!==removal[1]);
      const chords = /OEMChords\.Add\(new KeyboardChord\("([^"]+)",\s*\[([^\]]+)\],\s*\[([^\]]+)\],\s*(true|false),\s*ButtonFlags\.(\w+)/g;
      for (const m of clean(body).matchAll(chords)) {
        if (m[4] === 'true') continue; // Silenced G-Key does not expose an input capability.
        if (!canonical[m[1]]) throw Error(`Unreviewed chord ${name}/${m[1]}`);
        const names = s => [...s.matchAll(/KeyCode\.(\w+)/g)].map(m => m[1]);
        const pressVks = names(m[2]).map(vk), releaseVks = names(m[3]).map(vk);
        if (m[4] !== 'false' || [...pressVks].sort().join() !== [...releaseVks].sort().join()) throw Error('Unsupported chord edge contract');
        const [id, bit] = canonical[m[1]], backIndex = name === 'OneXPlayerX2MiniPro' ? ({ l4:1, r4:2 }[id] || 0) : 0;
        keys.push({ id, label:m[1], hcName:m[1], flag:m[5], pressVks, releaseVks, kbMask:kbMask(pressVks), canonicalBit:String(2 ** bit), backIndex, source:cls.file });
      }
    }
    keys = [...new Map(keys.map(k=>[k.id+':'+k.kbMask,k])).values()]; // Alternate release orders describe the same press capability.
    const triedConfigurations = [];
    let config, configurationFile;
    for (const cls of [...chain].reverse()) {
      const candidate = prefix + `Resources/Devices/${cls.name}.json`; triedConfigurations.push(candidate);
      if (sources.has(candidate)) { config = JSON.parse(get(candidate)); configurationFile = candidate; break; }
      // Confirm absence on disk, not just absence in the frozen manifest.
      if (fs.existsSync(path.join(upstream,'sources/hc',candidate))) throw Error(`Unpinned fallback configuration ${candidate}`);
    }
    const matrices = [];
    if (config) for (const [field,sensor] of [['GyroMatrix','gyro'],['AcceleroMatrix','accel']]) {
      const matrix = config[field]; if (!matrix) throw Error(`Fixed configuration missing ${field}`);
      requireHcMotionMatrix(matrix,`${configurationFile}#${field}`);
      const remap = input => {
        const assigned = [NaN,NaN,NaN];
        // HC AxisSwap is INPUT -> OUTPUT. Assign each input, then apply output signs.
        for (let i=0;i<3;++i) assigned[axes.indexOf(matrix.AxisSwap[axes[i]])] = input[i];
        if (assigned.some(Number.isNaN)) throw Error(`Non-permutation HC remap ${name}`);
        return assigned.map((value,i) => value * matrix.Axis[axes[i]]);
      };
      const symbolic = remap([1,2,3]).map(value => `${value<0?'neg-':''}${axes[Math.abs(value)-1].toLowerCase()}`).join('-');
      matrices.push({ sensor, matrix, recipe:symbolic, source:configurationFile, samples:samples.map(input=>({input,output:remap(input)})) });
    }
    const profile = { AYANEO3:'Ayaneo3', AYANEOAIR2:'AyaneoCec', AYANEOAIRPro1S:'AyaneoCec', AYANEOAIRPlusNxx:'AyaneoCec', AYANEOFlip11:'AyaneoFlip1SKb', OneXPlayerX2:'OneXX1', OneXPlayer3:'OneXX1', OneXPlayerX2MiniPro:'OneXX2MiniPro', SteamMachine:'None', AYANEONEXT2:'AyaneoNext2' }[name];
    const family = name === 'SteamMachine' ? 'ValveSteamMachine' : name.startsWith('OneX') ? 'OneXPlayer' : 'Ayanero';
    const catalog = [...new Map(keys.map(k=>[k.id,{id:k.id,label:k.label,backIndex:k.backIndex,form:'KeyboardVk',clickOnly:false,capture:true}])).values()];
    if (name.startsWith('OneX')) {
      catalog.splice(2,0,{id:'home',label:'Home',backIndex:0,form:'HidButton',clickOnly:true,capture:true});
      for (const [id,label,backIndex] of [['l4','M1',1],['r4','M2',2]]) {
        const found = catalog.find(k=>k.id===id);
        if (found) found.label=label;
        else catalog.push({id,label,backIndex,form:'HidButton',clickOnly:false,capture:true});
      }
      const routes = oneXApplicationPolicy.routes[name];
      if (!routes || catalog.map(k=>k.id).join(',') !== oneXApplicationPolicy.legalKeys.join(','))
        throw Error(`OneX application catalog differs from reviewed five-key policy: ${name}`);
      for (const key of catalog) {
        key.capture=true;
        key.captureRoute=routes[key.id];
        if (!key.captureRoute) throw Error(`Missing reviewed capture route: ${name}/${key.id}`);
      }
    }
    const stableCatalogOrder = name === 'AYANEO3' || name === 'AYANEONEXT2'
      ? ['ckbig','cksmall','cktl','cktr','ayat','m1','l4','m2','r4']
      : name.startsWith('AYANEO') ? ['ckbig','cksmall','cktl','cktr'] : catalog.map(k=>k.id);
    catalog.sort((a,b)=>stableCatalogOrder.indexOf(a.id)-stableCatalogOrder.indexOf(b.id));
    return {name,fullName:'HandheldCompanion.Devices.'+name,family,profile,chain:chain.map(c=>c.name),keys,catalog,matrices,triedConfigurations,configurationFile:configurationFile||null};
  });
  const expectedCounts = [9,4,4,4,8,2,4,2,0,9];
  models.forEach((m,i)=>{if(m.keys.length!==expectedCounts[i])throw Error(`HC chord inheritance count ${m.name}: ${m.keys.length}`);});
  const legacyIdentities = [];
  for (const row of inventory.identities.filter(r=>!targets.includes(r.deviceClass))) {
    let combinations = [{deviceClass:row.deviceClass,manufacturer:'UNKNOWN',product:'UNKNOWN',processor:'',systemVersion:'',systemProduct:''}];
    const fields={ManufacturerName:'manufacturer',ProductName:'product',Processor:'processor',Version:'systemVersion'};
    for (const sw of row.switches) {
      const values = sw.values.length ? sw.values : ['UNKNOWN'];
      combinations = combinations.flatMap(c=>values.map(v=>({...c,[fields[sw.field]]:v})));
    }
    for (const combo of combinations) {
      if (row.deviceClass==='AYANEONEXTLite4500U') combo.processor='AMD Ryzen 5 4500U';
      if (row.deviceClass==='AYANEONEXTLite') combo.processor='AMD Ryzen 7 4800U';
      legacyIdentities.push(combo);
    }
  }
  if (new Set(legacyIdentities.map(r=>r.deviceClass)).size!==80) throw Error('Expected 80 frozen legacy class cases');
  if (legacyIdentities.length!==181 || legacyIdentities.filter(r=>r.deviceClass==='SteamDeck').length!==2)
    throw Error('Expected 181 frozen legacy aliases, including Jupiter/Galileo SteamDeck');
  return {schema:'hc-remaining-model-oracle-v2',hcCommit:pins.commit,identities,models,legacyIdentities,vkCodes,oneXApplicationPolicy,steamDeckCapturePolicy,
    integrity:{pinsSha256:hash(pinsBytes),inventory:{path:inventoryPath,sha256:hash(inventoryBytes)},manifestSha256:hash(manifestBytes),inputs},
    oracleBoundary:'No production identity/profile/catalog/matrix/capture table contributes expected aliases, chords, canonical bits, remapped values or source routes. Source routes/software capture are reviewed YMCC application policy, not independent proof of runtime readiness or device I/O.'};
}