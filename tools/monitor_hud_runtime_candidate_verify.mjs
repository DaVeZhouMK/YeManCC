// Read-only candidate validation; --capture is explicitly required for first
// receipt. This is NOT a full release identity, install, game or CPU benchmark.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';
const args=process.argv.slice(2),capture=args.includes('--capture');
const root=path.resolve(args.find(a=>!a.startsWith('--'))??'../../../Build/Tasks/YMCC-Steam-HUD-Probe/desktop-output-20261011/runtime-handoff-20261011');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex'),readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
function safe(base,relative){
 assert(!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'),'Unsafe candidate relative path');
 const full=path.resolve(base,relative),canonicalBase=fs.realpathSync(base),canonical=fs.realpathSync(full);
 assert(canonical.toLowerCase().startsWith(canonicalBase.toLowerCase()+path.sep),'Candidate path escaped its explicit root');return full;
}
const frozen=readJson(path.join(root,'FROZEN-SOURCE.json')),source=safe(root,'Source/YeManCC');
assert(frozen.frozen===true&&Array.isArray(frozen.files)&&frozen.files.length>0,'Frozen source receipt is missing');
const sourceRecords=[];
for(const record of frozen.files){const bytes=fs.readFileSync(safe(source,record.relative));assert.equal(bytes.length,record.bytes,'Frozen source length drift: '+record.relative);assert.equal(hash(bytes),record.sha256,'Frozen source byte drift: '+record.relative);sourceRecords.push(record);}
const plugin='Build/App/Decky/plugins/ymcc-sidebar',payload=[];
function add(relative){const bytes=fs.readFileSync(safe(root,relative));payload.push({relative,bytes:bytes.length,sha256:hash(bytes)});return bytes;}
function files(relative){let result=[];for(const e of fs.readdirSync(safe(root,relative),{withFileTypes:true})){const next=relative+'/'+e.name;assert(!e.isSymbolicLink(),'Candidate links are forbidden');if(e.isDirectory())result.push(...files(next));else if(e.isFile())result.push(next);else throw Error('Unexpected candidate file type');}return result.sort();}
function pe(relative,machine){const b=add(relative);assert.equal(b.toString('ascii',0,2),'MZ','Missing PE header');const at=b.readUInt32LE(0x3c);assert.equal(b.toString('ascii',at,at+4),'PE\0\0');assert.equal(b.readUInt16LE(at+4),machine,'Wrong PE architecture');return b;}
const native=pe('Build/App/Native/YeManCC.exe',0x8664);
for(const marker of ['requireAdministrator','monitor.hudPulse.set','desktopHud.create'])assert(native.includes(Buffer.from(marker)),'Native contract marker missing: '+marker);
pe('Build/App/Native/YeManRecoveryService.exe',0x8664);pe('Build/App/Native/YMCCRtssProfileHelper.exe',0x8664);pe('Build/App/Native/YMCCOverlayBridge.dll',0x14c);
for(const file of files('Build/App/Web'))add(file);
for(const name of ['index.html','hud-output.html','app.config.json'])assert(payload.some(r=>r.relative==='Build/App/Web/'+name),'Required web entry missing: '+name);
const allow=['LICENSE.decky-api','LICENSE.vue','dist/index.js','package.json','plugin.json'].sort(),pluginFiles=files(plugin).map(p=>p.slice(plugin.length+1));assert.deepEqual(pluginFiles,allow);
const pluginCheck=readJson(path.join(root,'PLUGIN-CHECK.json'));assert.equal(pluginCheck.verified,true,'No compiled plugin/source byte-check receipt');
for(const file of pluginFiles){const bytes=add(plugin+'/'+file),expected=pluginCheck.files.find(f=>f.name===file);assert(expected,'Missing plugin byte-check row: '+file);assert.equal(hash(bytes),expected.sha256);assert.equal(bytes.length,expected.bytes);}
assert.equal(readJson(safe(root,plugin+'/plugin.json')).name,'ymcc-sidebar');
const sourceSha256=hash(Buffer.from(JSON.stringify(sourceRecords))),manifest={schema:'ymcc-hud-candidate-v1',sourceSha256,payload:payload.sort((a,b)=>a.relative.localeCompare(b.relative)),fullProductExport:false,installedRuntimeChanged:false,steamGameAccepted:false,standaloneGameAccepted:false,minimizedMainRuntimeAccepted:false,cpuMeasured:false};
const receipt=path.join(root,'CANDIDATE-MANIFEST.json');
if(capture){assert(!fs.existsSync(receipt),'Do not recapture/relabel an existing candidate');fs.writeFileSync(receipt,JSON.stringify(manifest,null,2));}
else {assert.deepEqual(readJson(receipt),manifest,'Candidate differs from its captured matching generation');}
const report={passed:true,frozenSourceFiles:sourceRecords.length,payloadFiles:manifest.payload.length,sourceSha256,exeSha256:payload.find(r=>r.relative.endsWith('/YeManCC.exe')).sha256,fullProductExport:false,installedRuntimeChanged:false,gameValidation:false,cpuMeasured:false,scope:'Exact frozen source bytes, complete task frontend, native PE architecture/contract markers, and formal plugin byte-check against one captured candidate generation. Does not validate pixels/input/real data/full product closure.'};
fs.writeFileSync(path.join(root,'CANDIDATE-VERIFY.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
