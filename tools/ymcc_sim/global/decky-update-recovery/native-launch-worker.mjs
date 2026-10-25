import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn,spawnSync} from 'node:child_process';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {verifyPackageFixtureTools,PACKAGE_FIXTURE_AUTHORITY_SOURCES} from './package-fixture-tools.mjs';
import {NATIVE_LAUNCH_NOIO_SOURCE_INPUTS,getNativeLaunchNoIoToolDeclaration} from './native-launch-noio/index.mjs';

const core='G:/YeManCC-Work/Testing/YMCC',modulePath='tools/ymcc_sim/global/decky-update-recovery';
// Read-only declaration for the caller's source closure and adapter registration.
export const NATIVE_LAUNCH_WORKER_SOURCE_INPUTS=Object.freeze([
 modulePath+'/native-launch-worker.mjs',
 modulePath+'/native-launch-worker-bootstrap.mjs',
 modulePath+'/native-launch-worker.ps1'
]);
export const sourceInputs=NATIVE_LAUNCH_WORKER_SOURCE_INPUTS;
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const pin=file=>({path:path.resolve(file),sha256:hash(file)});
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\ufeff/,''));
const need=(ok,why)=>{if(!ok)throw Error('NATIVE_LAUNCH_WORKER_'+why);};
const key=file=>path.resolve(file).toLowerCase();
const inside=(parent,file)=>{const rel=path.relative(parent,file);return rel!==''&&rel!=='..'&&!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel);};
function plain(file){const actual=path.resolve(file);need(!fs.lstatSync(actual).isSymbolicLink()&&key(fs.realpathSync(actual))===key(actual),'NONCANONICAL_PATH:'+file);return actual;}
function directory(file){let check=path.resolve(file);while(!fs.existsSync(check))check=path.dirname(check);plain(check);fs.mkdirSync(file,{recursive:true});return plain(file);}
const save=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});

// deadlineMs is the calling main Node's ABSOLUTE Unix epoch deadline. All setup,
// compilation, execution and cleanup consume that shared budget (maximum240s).
// This returns immediately after starting the private helper. finish() never
// restarts a clock; stop() is idempotent, bounded and only controls our own child.
export function startNativeLaunchWorker(options){return start(options,'suite');}
// Explicit inert controls exercise the SAME Job/gate/parent owner. They cannot
// produce a suite PASS, select a command, relax a budget or grant product access.
export function startNativeLaunchWorkerControl(options){
 need(['normal','stall'].includes(options.behavior),'CONTROL_BEHAVIOR');
 return start(options,'control-'+options.behavior);
}
function start({repo,callerEvidence,deadlineMs},mode){
 const started=Date.now();
 need(Number.isSafeInteger(deadlineMs)&&deadlineMs-started>3500&&deadlineMs-started<=240000,'ABSOLUTE_SHARED_DEADLINE');
 repo=plain(repo);callerEvidence=plain(callerEvidence);
 need(inside(path.resolve(core,'Evidence'),callerEvidence),'CORE_CALLER_EVIDENCE');
 need(key(plain(fileURLToPath(import.meta.url)))===key(plain(path.join(repo,modulePath,'native-launch-worker.mjs'))),'CALLING_MODULE_OUTSIDE_REPO');
 const workParent=directory(path.join(core,'Workspace/Nl'));
 const work=plain(fs.mkdtempSync(path.join(workParent,'w')));
 const out=directory(path.join(callerEvidence,'native-launch-worker'));need(fs.readdirSync(out).length===0,'FRESH_CALLER_OUTPUT');
 const temp=directory(path.join(work,'t'));
 const packageTools=verifyPackageFixtureTools();need(packageTools.files.length===42,'ORIGINAL_42_TOOL_MEMBERS');
 const noIo=getNativeLaunchNoIoToolDeclaration();need(!noIo.blockedReason,'NOIO_TOOL_DECLARATION:'+noIo.blockedReason);
 const manifest=read(path.join(repo,modulePath,'native-launch-noio/native-tools.json'));
 const powershell=plain(manifest.powershell.path);need(hash(powershell)===manifest.powershell.sha256,'FIXED_WINDOWS_POWERSHELL');
 const {lockBundledNodeTools}=awaitlessAuthority(repo);
 const nodeGraph=lockBundledNodeTools(repo);
 const sourceFiles=[...new Set([...NATIVE_LAUNCH_NOIO_SOURCE_INPUTS,...PACKAGE_FIXTURE_AUTHORITY_SOURCES,
  ...NATIVE_LAUNCH_WORKER_SOURCE_INPUTS,'tools/decky_package_maintenance_selftest.ps1'])];
 const sources=sourceFiles.map(relative=>({...pin(plain(path.join(repo,relative))),relative}));
 const tools=new Map();for(const row of [...packageTools.files,...nodeGraph.tools,...noIo.artifactInputs.map(pin),pin(process.execPath)]){
  const previous=tools.get(key(row.path));need(!previous||previous.sha256===row.sha256,'CONFLICTING_TOOL_PIN');need(hash(plain(row.path))===row.sha256,'TOOL_CHANGED:'+row.path);tools.set(key(row.path),{path:path.resolve(row.path),sha256:row.sha256});
 }
 const identityScript=path.join(work,'parent-identity.ps1');
 fs.writeFileSync(identityScript,"$ErrorActionPreference='Stop'\n$p=[Diagnostics.Process]::GetProcessById("+process.pid+")\ntry { [ordered]@{pid=$p.Id;birthTicks=$p.StartTime.ToUniversalTime().Ticks.ToString();image=$p.MainModule.FileName}|ConvertTo-Json -Compress } finally {$p.Dispose()}\n");
 const identityStart=Date.now();
 const identity=spawnSync(powershell,['-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',identityScript],{windowsHide:true,timeout:Math.min(5000,deadlineMs-Date.now()-1500),encoding:'utf8',env:{...process.env,TEMP:temp,TMP:temp}});
 fs.writeFileSync(path.join(work,'parent-identity.stdout.log'),identity.stdout||'');fs.writeFileSync(path.join(work,'parent-identity.stderr.log'),identity.stderr||'');
 need(identity.status===0&&!identity.error,'EXACT_PARENT_IDENTITY');
 const parent=JSON.parse(identity.stdout.trim());need(parent.pid===process.pid&&key(parent.image)===key(process.execPath),'REAL_CALLING_NODE_IDENTITY');
 const request={schemaVersion:1,kind:'private-native-launch-worker',mode,repo,work,callerEvidence:out,nonce:crypto.randomBytes(32).toString('hex'),parent,node:pin(process.execPath),powershell:pin(powershell),deadlineMs,runDeadlineMs:deadlineMs-1500,cleanupMs:1500,sources,tools:[...tools.values()],packageManifest:pin(path.join(repo,modulePath,'package-fixture-tools.json')),driver:path.join(repo,'tools/steam_native_launch_regression.mjs'),driverArgs:['--run','--evidence-dir',path.join(work,'noio')],bootstrap:path.join(repo,modulePath,'native-launch-worker-bootstrap.mjs'),createdAt:new Date(started).toISOString(),identityQuery:{elapsedMs:Date.now()-identityStart,processId:identity.pid,script:pin(identityScript)}};
 const requestFile=path.join(work,'request.json');save(requestFile,request);
 const helper=path.join(repo,modulePath,'native-launch-worker.ps1');
 const stdout=path.join(work,'helper.stdout.log'),stderr=path.join(work,'helper.stderr.log');
 const stdoutFd=fs.openSync(stdout,'wx'),stderrFd=fs.openSync(stderr,'wx');
 let child;try{child=spawn(powershell,['-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',helper,'-Request',requestFile,'-ExpectedSha256',hash(requestFile),'-GuardianLauncher'],{cwd:repo,windowsHide:true,env:{...process.env,TEMP:temp,TMP:temp},stdio:['pipe',stdoutFd,stderrFd]});}finally{fs.closeSync(stdoutFd);fs.closeSync(stderrFd);}
 child.stdin.on('error',()=>{});
 save(path.join(out,'PREPARED.json'),{request:pin(requestFile),work,mode,helperProcessId:child.pid,helperSource:pin(helper),mainParent:parent,deadlineMs,originalDriver:pin(request.driver),driverArgs:request.driverArgs,noIoSuiteRequested:mode==='suite'});
 let forced=false,stopping=false,deadlineStop=false,closed=false,spawnError=null,stopGuard=null;
 const settled=new Promise(resolve=>{child.once('error',error=>{spawnError=error.message;});child.once('close',(code,signal)=>{closed=true;resolve({code,signal,observedAt:new Date().toISOString()});});});
 function signalStop(expired=false){if(!stopping&&!closed){deadlineStop=expired;stopping=true;child.stdin.write('STOP '+request.nonce+'\n');stopGuard=setTimeout(()=>{if(!closed){forced=true;child.kill();}},Math.max(1,Math.min(3000,deadlineMs-Date.now())));}}
 const cutoff=setTimeout(()=>signalStop(true),Math.max(1,request.runDeadlineMs-Date.now()));
 const guard=setTimeout(()=>{if(!closed){forced=true;child.kill();}},Math.max(1,deadlineMs-Date.now()));
 const final=settled.then(exit=>{
  clearTimeout(cutoff);clearTimeout(guard);clearTimeout(stopGuard);child.stdin.destroy();
  const ownerFile=path.join(work,'owner-result.json'),bootstrapFile=path.join(work,'bootstrap-result.json'),launcherFile=path.join(work,'guardian-launcher-result.json'),guardianFile=path.join(work,'guardian-started.json');
  const owner=fs.existsSync(ownerFile)?read(ownerFile):null,bootstrap=fs.existsSync(bootstrapFile)?read(bootstrapFile):null,launcher=fs.existsSync(launcherFile)?read(launcherFile):null;
  const changed=[...sources,...request.tools].filter(row=>{try{return hash(row.path)!==row.sha256;}catch{return true;}}).map(row=>row.path);
  const clean=!!owner&&launcher?.status==='EXIT_OBSERVED'&&launcher.ownerExitObserved===true&&launcher.ownedProcessHandlesAfter===0&&launcher.ownerPid===owner.helperPid&&owner.jobCloseSucceeded===true&&owner.childExitObserved===true&&owner.jobActiveProcessesAtClose===0&&owner.ownedHandlesAfter===0;
  const success=!forced&&!spawnError&&!deadlineStop&&exit.code===0&&clean&&owner.status==='COMPLETED'&&bootstrap?.status==='COMPLETED'&&!changed.length&&Date.now()<=deadlineMs;
  const receipt={schemaVersion:1,ok:success,mode,status:success?(mode==='suite'?'PASS':'CONTROL_PASS'):(owner?.status??'OWNER_RECEIPT_MISSING'),noIoSuiteExecuted:bootstrap?.suiteExecuted===true,originalResult:bootstrap?.originalResult??null,work,deadlineMs,mainParent:parent,helper:{role:'guardian-launcher',pid:child.pid,guardianPid:owner?.helperPid??null,exit,...(spawnError?{spawnError}:{}),forced},sourceChanged:changed.length>0,changed,resourcesDestroyed:clean,owner,launcher,bootstrap,stopReason:stopping?(deadlineStop?'shared-deadline':'caller-stop'):null,raw:{request:pin(requestFile),helperStdout:pin(stdout),helperStderr:pin(stderr),owner:owner?pin(ownerFile):null,launcher:launcher?pin(launcherFile):null,guardianStarted:fs.existsSync(guardianFile)?pin(guardianFile):null,guardianStdout:fs.existsSync(path.join(work,'guardian.stdout.log'))?pin(path.join(work,'guardian.stdout.log')):null,guardianStderr:fs.existsSync(path.join(work,'guardian.stderr.log'))?pin(path.join(work,'guardian.stderr.log')):null,bootstrap:bootstrap?pin(bootstrapFile):null,originalRaw:bootstrap?.raw??null}};
  if(forced)receipt.status='FORCED_HELPER_EXIT_UNCONFIRMED_CLEANUP';if(changed.length)receipt.status='SOURCE_OR_TOOL_CHANGED';if(deadlineStop||Date.now()>deadlineMs)receipt.status='TIMEOUT';
  save(path.join(out,'result.json'),receipt);return receipt;
 });
 return {work,evidence:out,helperPid:child.pid,guardianLauncherPid:child.pid,guardianStarted:path.join(work,'guardian-started.json'),deadlineMs,finish:()=>final,stop:()=>{signalStop();return final;}};
}
// Static import keeps the ordinary standard authority implementation bound to
// this repo; no user-supplied runtime or unreviewed worker command is accepted.
import * as standardAuthority from '../bundled-node-tool-authority.mjs';
function awaitlessAuthority(repo){need(key(plain(path.join(repo,'tools/ymcc_sim/global/bundled-node-tool-authority.mjs')))===key(plain(fileURLToPath(new URL('../bundled-node-tool-authority.mjs',import.meta.url)))),'AUTHORITY_OUTSIDE_REPO');return standardAuthority;}