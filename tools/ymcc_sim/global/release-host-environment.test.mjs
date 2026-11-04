// Local Windows metadata controls only; no Loader, product, compiler or hosted identity.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import assert from 'node:assert/strict';import test,{after} from 'node:test';
import {spawnSync} from 'node:child_process';import {pathToFileURL} from 'node:url';import {performance} from 'node:perf_hooks';
const source=path.resolve(import.meta.dirname,'../../..'),canonical=path.resolve('G:/YeManCC-Work/Testing/YMCC');
assert.equal(process.platform,'win32');assert(fs.existsSync(canonical));
assert.notEqual(process.env.GITHUB_ACTIONS,'true','These controls require the actual local environment, never fake hosted flags');
const workspace=path.join(canonical,'Workspace'),own=fs.mkdtempSync(path.join(workspace,'LocalCoreControls-'));
const files=['tools/ymcc_sim/global/release-host-environment.mjs','tools/ymcc_sim/global/release-host-environment.ps1','tools/initialize-release-check-host.ps1','tools/release-check-host/initialize-historical-inputs.ps1','tools/ymcc_sim/global/release-host-environment.test.mjs'];
const history='tools/release-check-host/history/DeckyParentMigration-20261023/a1-2547/history';
const inputs=[...files,...['result.json','native-result.json','consumer-result.json'].map(n=>history+'/'+n)];
let isolated;
function isolatedRepo(){
 if(isolated)return isolated;
 isolated=path.join(own,'source','Mainline','YeManCC-source','YeManCC');
 for(const name of inputs){fs.mkdirSync(path.dirname(path.join(isolated,name)),{recursive:true});fs.copyFileSync(path.join(source,name),path.join(isolated,name));}
 return isolated;
}
// Mainline needs an owned Workspace source for the original scope assertions.
// Already frozen sources can be read directly; the history mutation below always uses our copy.
const repo=source.toLowerCase().startsWith(workspace.toLowerCase()+path.sep)&&/[\\/]Mainline[\\/]YeManCC-source[\\/]YeManCC$/i.test(source)?source:isolatedRepo();
const api=await import(pathToFileURL(path.join(repo,files[0]))),pwsh='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe',processes=[];
const sha=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const marker=(core,patch={})=>({core:path.resolve(core),kind:'ymcc-local-frozen-core-v1',owner:'YMCC-local-check',parentCore:canonical,repo,schemaVersion:1,...patch});
function fresh(){return fs.mkdtempSync(path.join(own,'run-'));}
function writeMarker(core,value=marker(core)){fs.writeFileSync(path.join(core,'.ymcc-local-check-core.json'),JSON.stringify(value));}
function withCore(core,body){const before=process.env.YMCC_SIM_CORE_ROOT;try{if(core===undefined)delete process.env.YMCC_SIM_CORE_ROOT;else process.env.YMCC_SIM_CORE_ROOT=core;return body();}finally{if(before===undefined)delete process.env.YMCC_SIM_CORE_ROOT;else process.env.YMCC_SIM_CORE_ROOT=before;}}
function ps(args,core){const started=performance.now(),env={...process.env};if(core===undefined)delete env.YMCC_SIM_CORE_ROOT;else env.YMCC_SIM_CORE_ROOT=core;const r=spawnSync(pwsh,['-NoLogo','-NoProfile','-NonInteractive',...args],{encoding:'utf8',env,timeout:4000,windowsHide:true});processes.push({args,status:r.status,signal:r.signal,error:r.error?.message??null,elapsedMs:performance.now()-started,stdout:r.stdout,stderr:r.stderr});return r;}
const q=s=>s.replaceAll("'","''");
const psRoot=core=>ps(['-Command',". '"+q(path.join(repo,files[1]))+"'; Get-YmccSimulationCoreRoot"],core);
const initialize=(core,sourceRepo=repo)=>ps(['-File',path.join(sourceRepo,files[2]),'-LocalFrozenWorkspace','-LocalCoreRoot',core]);
after(()=>fs.writeFileSync(path.join(own,'CONTROL-OBSERVATION.json'),JSON.stringify({scope:'Local metadata controls only',source,repo,isolatedRepo:isolated??null,sourceInputs:inputs.map(name=>({name,sha256:sha(path.join(source,name))})),sourceCopies:isolated?inputs.map(name=>({name,sha256:sha(path.join(isolated,name))})):[],node:{path:process.execPath,sha256:sha(process.execPath)},pwsh:{path:pwsh,sha256:sha(pwsh)},processes,hostedFlagsNotSpoofed:true,productOrCompilerExecutions:0},null,2)+'\n'));
test('local frozen initializer emits a distinct exact owner, copies original historical bytes and resolves identically in Node and PS',()=>{const core=fresh(),r=initialize(core);assert.equal(r.status,0,r.stderr);const m=JSON.parse(fs.readFileSync(path.join(core,'.ymcc-local-check-core.json'),'utf8'));assert.deepEqual(m,marker(core));assert(!fs.existsSync(path.join(core,'.ymcc-release-check-core.json')));assert.match(r.stdout,/"hosted":false/);assert.equal(withCore(core,()=>api.simulationCorePath('Evidence')),path.join(core,'Evidence'));const p=psRoot(core);assert.equal(p.status,0,p.stderr);assert.equal(p.stdout.trim(),core);for(const name of ['result.json','native-result.json','consumer-result.json'])assert.equal(sha(path.join(core,'Evidence/DeckyParentMigration-20261023/a1-2547/history',name)),sha(path.join(repo,history,name)));const second=initialize(core);assert.equal(second.status,0,second.stderr);});
test('default core cannot silently cover a nested frozen source, and relative output escapes reject',()=>{assert.throws(()=>withCore(undefined,()=>api.simulationCorePath()),/SIMULATION_CORE_SCOPE_INVALID/);const core=fresh();writeMarker(core);assert.throws(()=>withCore(core,()=>api.simulationCorePath('../outside')),/SIMULATION_CORE_CHILD_PATH_INVALID/);assert.throws(()=>withCore(repo,()=>api.simulationCorePath()),/SIMULATION_CORE_SCOPE_INVALID/);});
test('Node and PS both reject wrong owner, source, core, canonical parent, schema type, kind and unknown authority fields',()=>{for(const change of [{owner:['YMCC-local-check']},{kind:['ymcc-local-frozen-core-v1']},{owner:'YMCC-release-check'},{repo:path.join(own,'other')},{core:path.join(own,'other')},{parentCore:path.join(own,'other')},{schemaVersion:'1'},{schemaVersion:true},{kind:'other'},{allowHosted:true}]){const core=fresh();writeMarker(core,marker(core,change));assert.throws(()=>withCore(core,()=>api.simulationCorePath()),/LOCAL_SIMULATION_CORE_OWNER_INVALID/);const r=psRoot(core);assert.notEqual(r.status,0);assert.match(r.stderr,/LOCAL_SIMULATION_CORE_OWNER_INVALID/);}});
test('local core outside the canonical Workspace is rejected by Node and PS',()=>{const core=fs.mkdtempSync(path.join(canonical,'Evidence','LocalCoreControl-'));writeMarker(core);assert.throws(()=>withCore(core,()=>api.simulationCorePath()),/LOCAL_SIMULATION_CORE_SCOPE_INVALID/);const r=psRoot(core);assert.notEqual(r.status,0);assert.match(r.stderr,/LOCAL_SIMULATION_CORE_SCOPE_INVALID/);});
test('a reparse core cannot acquire authority even with matching logical marker paths',()=>{const actual=fresh(),alias=path.join(own,'alias-core');fs.symlinkSync(actual,alias,'junction');writeMarker(actual,marker(alias));assert.throws(()=>withCore(alias,()=>api.simulationCorePath()),/LOCAL_SIMULATION_CORE_REPARSE_PATH/);const r=psRoot(alias);assert.notEqual(r.status,0);assert.match(r.stderr,/LOCAL_SIMULATION_CORE_REPARSE_PATH/);});
test('initializer preserves foreign existing files and refuses output directory reparses',()=>{const foreign=fresh(),file=path.join(foreign,'KEEP.txt');fs.writeFileSync(file,'KEEP');const bad=initialize(foreign);assert.notEqual(bad.status,0);assert.match(bad.stderr,/LOCAL_FROZEN_CORE_NOT_EMPTY/);assert.equal(fs.readFileSync(file,'utf8'),'KEEP');const core=fresh(),target=fresh();writeMarker(core);fs.symlinkSync(target,path.join(core,'Evidence'),'junction');const before=fs.readdirSync(target);const linked=initialize(core);assert.notEqual(linked.status,0);assert.match(linked.stderr,/LOCAL_SIMULATION_CORE_REPARSE_PATH/);assert.deepEqual(fs.readdirSync(target),before);});
test('historical input drift is rejected rather than accepted as fresh historical evidence',()=>{const driftRepo=isolatedRepo(),file=path.join(driftRepo,history,'result.json'),before=fs.readFileSync(file),core=fresh();try{fs.writeFileSync(file,Buffer.concat([before,Buffer.from(' ')]));const r=initialize(core,driftRepo);assert.notEqual(r.status,0);assert.match(r.stderr,/Historical source changed/);assert(!fs.existsSync(path.join(core,'Evidence/DeckyParentMigration-20261023/a1-2547/history/result.json')));}finally{fs.writeFileSync(file,before);}});
test('ordinary hosted initializer still refuses this real local desktop',()=>{const r=ps(['-File',path.join(repo,files[2])]);assert.notEqual(r.status,0);assert.match(r.stderr,/disposable GitHub runner/);});

test('standalone historical initialization rejects an injected marker reparse attribute before reading owner or copying bytes',()=>{
 const core=fresh();writeMarker(core);const script=path.join(repo,files[3]);const command="function global:Get-Item { param([string]$LiteralPath,[switch]$Force) if($LiteralPath -eq '"+q(path.join(core,'.ymcc-local-check-core.json'))+"'){[pscustomobject]@{Attributes=[IO.FileAttributes]::ReparsePoint}}else{Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath -Force:$Force} }; & '"+q(script)+"' -CoreRoot '"+q(core)+"' -LocalFrozenWorkspace";
 const r=ps(['-Command',command]);assert.notEqual(r.status,0);assert.match(r.stderr,/LOCAL_SIMULATION_CORE_REPARSE_PATH/);assert(!fs.existsSync(path.join(core,'Evidence')));
});
test('real resolved redirects reject even when lstat symbolic-link classification is injected false',()=>{
 const actual=fresh(),alias=path.join(own,'classification-alias');fs.symlinkSync(actual,alias,'junction');writeMarker(actual,marker(alias));const lstat=fs.lstatSync;
 try{fs.lstatSync=(file,...args)=>path.resolve(file)===path.resolve(alias)?{isSymbolicLink:()=>false}:lstat(file,...args);assert.throws(()=>withCore(alias,()=>api.simulationCorePath()),/LOCAL_SIMULATION_CORE_REPARSE_PATH/);}finally{fs.lstatSync=lstat;}
});
