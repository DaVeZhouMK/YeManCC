import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {REPO,SELFTEST,resolveToolchain} from './adapter.mjs';

const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
export function verifyResult(result,toolchain) {
 assert.equal(result.pass,true,'Existing selftest must pass');
 assert.equal(result.sourceCurrent,true,'Selftest source fence must remain current');
 assert.equal(result.selectedHostRulesCurrent,true,'Selected Host rules must remain current');
 assert.equal(key(result.capturedToolchain),key(toolchain.root),'Result must use the selected captured toolchain');
 const expected=['Release','Checked','negative-enum-error-absent','negative-property-error-absent','negative-observed-id-authority-alphabet','negative-forget-family'];
 assert.deepEqual(result.variants.map(row=>row.variant),expected,'All six actual compiled variants are required');
 for(const row of result.variants) {
  assert.equal(row.pass,true,'Compiled variant failed: '+row.variant);
  assert(row.cases>0&&row.assertions>0,'Actual fixture assertions required');
  if(row.positive) assert.equal(row.failureCount,0);
  else assert(row.failureCount>0&&typeof row.requiredCase==='string','Compiled negative must reject its intended original case');
 }
 assert.deepEqual(result.variants.map(row=>row.positive),[true,true,false,false,false,false]);
 const registered=new Map(toolchain.frozen.map(row=>[key(row.path),row.sha256]));
 assert(result.boundToolchainInputs.length>0,'Actual compiler/linker closure required');
 for(const row of result.boundToolchainInputs) assert.equal(registered.get(key(row.path)),row.sha256,'Actual compiler/linker dependency must be registered: '+row.path);
 assert.equal(result.scope.deviceApisExecuted,false);
 assert.equal(result.scope.productOrHostExecuted,false);
 assert.equal(result.scope.deviceServiceDriverOrSteamOperations,false);
 // RESULT.status is an unchanged historical label; production integration is outside this run's acceptance.
}

export function run(args,{env=process.env,spawn=spawnSync}={}) {
 assert(args.length===2&&args[0]==='--out'&&path.isAbsolute(args[1]),'Use --out ABS_FRESH_DIRECTORY');
 const out=path.resolve(args[1]);
 assert(!fs.existsSync(out),'Output must be fresh');
 const toolchain=resolveToolchain({env});
 for(const row of toolchain.bindings) assert.equal(hash(row.path),row.sha256,'Captured tool/dependency identity changed: '+row.path);
 const before=toolchain.artifacts.map(file=>({path:file,sha256:hash(file)}));
 const child=spawn(process.execPath,[path.join(REPO,SELFTEST),'--out',out,'--toolchain',toolchain.root],
  {cwd:REPO,env,encoding:'utf8',windowsHide:true,timeout:165000,maxBuffer:4*1024*1024});
 if(child.stdout) process.stdout.write(child.stdout);
 if(child.stderr) process.stderr.write(child.stderr);
 if(child.error) throw child.error;
 assert(!child.signal,'Selftest child terminated by signal: '+child.signal);
 if(child.status!==0) return child.status??1;
 verifyResult(JSON.parse(fs.readFileSync(path.join(out,'RESULT.json'),'utf8')),toolchain);
 for(const row of before) assert.equal(hash(row.path),row.sha256,'Captured artifact changed during selftest: '+row.path);
 return 0;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
 try { process.exitCode=run(process.argv.slice(2)); }
 catch(error) { console.error(error.stack??String(error));process.exitCode=1; }
}
