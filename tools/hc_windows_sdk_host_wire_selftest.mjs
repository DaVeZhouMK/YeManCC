import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {spawnSync} from 'node:child_process';
const self=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(self),'..');
const corePath=path.join(repo,'tools/fixtures/windows_owner_host_wire','.sdk-core-'+process.pid+'.mjs');
let core=fs.readFileSync(path.join(repo,'tools/fixtures/windows_owner_host_wire/compose.mjs'),'utf8');
const cut=core.indexOf(" cpp=once(cpp,'  const auto defaultStamp=");if(cut<0)throw Error('Current wire composition boundary missing');
core=core.slice(0,cut)+" if(cpp.includes('@@'))throw Error('Unresolved SDK wire marker');return retainedPrefix+cpp;\n}\n";
fs.writeFileSync(corePath,core);
let text=fs.readFileSync(path.join(repo,'tools/hc_windows_sdk_owner_selftest.mjs'),'utf8');
function once(from,to){if(text.split(from).length!==2)throw Error('SDK wire runner seam: '+from.slice(0,100));text=text.replace(from,()=>to);}
once("import fs from 'node:fs';",`import {compose} from './fixtures/windows_owner_host_wire/${path.basename(corePath)}';\nimport {composeSdkWireCases,copySdkWireHeaders,runSdkHostConsumer} from './fixtures/hc_generic_windows/sdk_host_wire.mjs';\nimport fs from 'node:fs';`);
once("['','--rewrite-sdk-sequence','--skip-owner-process','--skip-owner-retirement']","['','--drop-readback','--wire-calibrated-as-raw','--omit-final-source-guard']");
once("'WindowsSdkOwner-20261018'","'WindowsSdkOwnerHostWire-20261018'");
once('freeze(fileURLToPath(import.meta.url));',`freeze(fileURLToPath(import.meta.url));freeze(${JSON.stringify(self)});freeze(${JSON.stringify(corePath)});freeze(path.join(repo,'tools/hc_windows_sdk_owner_selftest.mjs'));freeze(path.join(repo,'tools/fixtures/hc_generic_windows/sdk_host_wire.mjs'));freeze(path.join(repo,'tools/fixtures/windows_owner_host_wire/compose.mjs'));freeze(path.join(repo,'tools/fixtures/windows_owner_host_wire/recorder.cpp'));`);
once(" let code=preamble+transport+'\\n'+base+'\\n'+harness;",` let code=preamble+transport+'\\n'+base+'\\n'+harness;
 const rawNativeCode=code,pad=freeze(path.join(repo,'native/screen_touchpads.h')).toString('utf8'),wireTemplate=freeze(path.join(repo,'tools/fixtures/hc_motion_host_wire/native.cpp')).toString('utf8');
 result.wireBindings=[];copySdkWireHeaders(repo,out,freeze);
 code=compose({base:code,source,pad,wireTemplate,control,bind:b=>result.wireBindings.push({symbol:b.symbol,sha256:sha(b.code),kind:b.kind})});
 code=composeSdkWireCases(code,control);`);
once('/link ole32.lib oleaut32.lib','/link ole32.lib oleaut32.lib advapi32.lib');
once("assert.equal(run.status,control?1:0,'Run '+name);",`const nativeMutant=['--drop-readback','--omit-final-source-guard'].includes(control);assert.equal(run.status,nativeMutant?1:0,'Run '+name);`);
once("if(control)assert.ok(report.failures.length>0);else assert.equal(report.status,'pass');",`if(nativeMutant){assert.ok(report.failures.length>0);assert.ok(report.failures.every(f=>f.group==='native-owner-host-wire'));}else assert.equal(report.status,'pass');
  const wireFile=path.join(dir,'owner-host-wire.jsonl'),wireRows=fs.readFileSync(wireFile,'utf8').trim().split(/\\r?\\n/).filter(Boolean).map(line=>JSON.parse(line));
  const configuration=result.configurations.at(-1);configuration.wireFile=wireFile;configuration.wireRows=wireRows.length;configuration.wireSha256=sha(fs.readFileSync(wireFile));configuration.wireModels=[...new Set(wireRows.map(r=>r.model))];assert.equal(wireRows.length,336);assert.equal(configuration.wireModels.length,56);save();`);
once(" const live=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),pieces=synthetic(live);",` const host=runSdkHostConsumer({repo,ws:path.resolve(repo,'../../..'),out,control,freeze});if(host){result.hostProof=host.proof;result.host=host.result;}
 const live=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),pieces=synthetic(live);
 const currentWire=[];compose({base:rawNativeCode,source:live,pad:fs.readFileSync(path.join(repo,'native/screen_touchpads.h'),'utf8'),wireTemplate,control,bind:b=>currentWire.push({symbol:b.symbol,sha256:sha(b.code),kind:b.kind})});assert.deepEqual(currentWire,result.wireBindings,'Current wire source drift');`);
const generated=path.join(repo,'tools','.sdk-owner-host-wire-'+process.pid+'.mjs');fs.writeFileSync(generated,text);
const run=spawnSync('node',[generated,...process.argv.slice(2)],{cwd:repo,windowsHide:true,encoding:'utf8',timeout:240000,maxBuffer:8e6});
console.log(run.stdout||'');if(run.stderr)console.error(run.stderr);if(run.error)console.error(run.error);process.exitCode=run.status??1;