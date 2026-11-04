import {selectedNativeTools} from './ymcc_sim/global/release-host-environment.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const repo=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]??'');assert(process.argv[2]&&!fs.existsSync(out),'Use a fresh evidence directory.');fs.mkdirSync(out,{recursive:true});
const source=path.join(out,'source'),pins=[],seen=new Set();const sha=b=>crypto.createHash('sha256').update(b).digest('hex');const search=[path.join(repo,'native'),path.join(repo,'deps/json'),repo];
function freeze(file){file=path.resolve(file);if(seen.has(file.toLowerCase()))return;const relative=path.relative(repo,file);if(relative.startsWith('..')||path.isAbsolute(relative))return;seen.add(file.toLowerCase());const bytes=fs.readFileSync(file),target=path.join(source,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);assert.equal(sha(fs.readFileSync(file)),sha(bytes),'Concurrent input change: '+relative);pins.push({path:relative.replaceAll('\\','/'),bytes:bytes.length,sha256:sha(bytes)});for(const m of bytes.toString('utf8').matchAll(/^\s*#\s*include\s*"([^"\r\n]+)"/gm)){const found=[path.resolve(path.dirname(file),m[1]),...search.map(p=>path.resolve(p,m[1]))].find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());if(found)freeze(found);}}
freeze(path.join(repo,'tools/decky_binding_runtime_selftest.cpp'));freeze(path.join(repo,'tools/decky_binding_runtime_selftest.mjs'));freeze(path.join(repo,'deps/json/json.hpp'));freeze(path.join(repo,'native/decky_context_cleanup_worker.h'));
// Explicit focused counterexample, in the frozen compiler input only. Ordinary
// source pins remain the actual product preimage; never label an override as it.
const runtimeOverride=process.env.YMCC_DECKY_RUNTIME_HEADER_OVERRIDE;
if(runtimeOverride){const bytes=fs.readFileSync(runtimeOverride),target=path.join(source,'native/decky_sidebar_runtime.h');fs.writeFileSync(target,bytes);fs.writeFileSync(path.join(out,'SOURCE-OVERRIDES.json'),JSON.stringify({path:runtimeOverride,sha256:sha(bytes),compiledPath:target,reason:'explicit focused semantic counterexample'},null,2)+'\n');}
// Static event binding only: main.cpp is frozen for attribution, never run.
const mainFile=path.join(repo,'native/main.cpp'),mainBytes=fs.readFileSync(mainFile),mainText=mainBytes.toString('utf8');
const commit=mainText.match(/case WM_POWER_RESUME_COMMIT:[\s\S]*?(?=\n\s*case WM_POWER_INPUT_RECOVER:)/);
assert(commit,'Missing WM_POWER_RESUME_COMMIT block');
const directCall='g_deckySidebarRuntime.requestBindingRefresh(currentPowerGeneration())',sharedCall='deckySidebarResumeAfterIntent(currentPowerGeneration(),"committed-ready")';
const call=commit[0].includes(sharedCall)?sharedCall:directCall;
assert.equal(commit[0].split(call).length-1,1,'Committed resume must bind the existing production consumer exactly once');
assert(commit[0].includes('PowerLifecycle::Ready'),'Committed resume must retain the Ready guard');
const consumer={path:'native/main.cpp',sourceSha256:sha(mainBytes),event:'WM_POWER_RESUME_COMMIT',call,callLine:mainText.slice(0,commit.index+commit[0].indexOf(call)).split('\n').length,excerptSha256:sha(Buffer.from(commit[0])),verification:'static call binding; OS event dispatch not executed'};
// Execute the exact Decky-only consumer from the alternate S0 Ready path too.
const intent=mainText.match(/static bool deckySidebarResumeAfterIntent\(unsigned long long generation(?:,const char\* scope="sleep-intent-ready")?\) \{[\s\S]*?\n\}/);
assert(intent,'Missing alternate S0 Ready consumer');
const abort=mainText.slice(mainText.indexOf('static void sgAbortSleepIntent(const char* reason) {'),mainText.indexOf('// Idle owns no periodic wakeup.',mainText.indexOf('static void sgAbortSleepIntent(const char* reason) {')));
assert.equal(abort.split('deckySidebarResumeAfterIntent(generation)').length-1,1,'Actual S0 Ready path must notify Decky once');
assert(abort.indexOf('g_powerLifecycle.store(PowerLifecycle::Ready')<abort.indexOf('deckySidebarResumeAfterIntent(generation)'),'Decky may only consume committed Ready');
let intentText=intent[0];const override=process.env.YMCC_DECKY_WAKE_READY_OVERRIDE;
if(override){intentText=fs.readFileSync(override,'utf8');consumer.intentOverride={path:override,sha256:sha(Buffer.from(intentText)),scope:'explicit negative variant'};}
fs.writeFileSync(path.join(source,'native/decky_wake_ready_consumer.inc'),intentText+'\n');
const committedBegin=commit[0].indexOf('// YMCC Decky:'),committedEnd=commit[0].indexOf('ipc_emit("power.resumed"');
assert(committedBegin>=0&&committedEnd>committedBegin,'Missing Decky-only committed Ready consumer');
const committedConsumer=commit[0].slice(committedBegin,committedEnd).trim();
fs.writeFileSync(path.join(source,'native/decky_wake_committed_consumer.inc'),'inline void productionCommittedReady(){\n'+committedConsumer+'\n}\n');
consumer.committedReady={sourceSha256:sha(Buffer.from(committedConsumer)),execution:'Exact committed Decky slice compiled and run with real Runtime and typed binding; OS/session boundaries inert'};
consumer.alternateReady={path:'native/main.cpp',function:'deckySidebarResumeAfterIntent',sourceSha256:sha(Buffer.from(intent[0])),actualCallIn:'sgAbortSleepIntent after real Ready',execution:'Actual extracted function; explicit process/session IO adapters'};
const takeFunction=(start,end)=>{const begin=mainText.indexOf(start),finish=mainText.indexOf(end,begin);assert(begin>=0&&finish>begin,'Missing offline production consumer '+start);return mainText.slice(begin,finish).trim();};
const offlinePlan=takeFunction('static ymcc::deckysidebar::BindingPlan deckySidebarOwnedBindingPlan(', 'static void deckySidebarOwnedBindingDiagnostic(');
const offlineDiagnostic=takeFunction('static void deckySidebarOwnedBindingDiagnostic(', '// Existing high-frequency lane only;');
const admissionStart=mainText.match(/    config[.]launchAdmission=\s*\[\]\(\) -> ymcc::deckysidebar::LaunchAdmission \{/);const admissionBegin=admissionStart?.index??-1;
assert(admissionBegin>=0,'Missing actual launch admission');const admissionClose='\n        };';const admissionEnd=mainText.indexOf(admissionClose,admissionBegin)+admissionClose.length;assert(admissionEnd>admissionBegin,'Missing exact admission lambda end');
const offlineAdmission=mainText.slice(admissionBegin,admissionEnd).trim().replace(/^config.launchAdmission=/,'return ');
const offlineText=offlinePlan+'\n'+offlineDiagnostic+'\ninline auto actualAdmission(){'+offlineAdmission+'}\n';
fs.writeFileSync(path.join(source,'native/decky_offline_consumers.inc'),offlineText);
consumer.offlineIdentity={planSha256:sha(Buffer.from(offlinePlan)),diagnosticSha256:sha(Buffer.from(offlineDiagnostic)),admissionSha256:sha(Buffer.from(offlineAdmission)),compiledSha256:sha(Buffer.from(offlineText)),scope:'Exact current main plan, diagnostic and admission lambda; only surrounding process/broker observer boundary adapted'};

// Execute the real listener-table/owned-Job consumer in the existing Runtime
// ready case. Preserve its entire source after two explicit fixture seams:
// current Runtime passed by reference and exclusive high loopback test port.
const leaseBegin=mainText.indexOf('struct DeckyManagedBackendLease {');
const proofBegin=mainText.indexOf('static bool deckySidebarAcquireManagedBackend(',leaseBegin);
const proofEnd=mainText.indexOf('\n}',proofBegin)+2;
assert(leaseBegin>=0&&proofBegin>leaseBegin&&proofEnd>proofBegin,'Missing native managed backend proof');
const backendLease=mainText.slice(leaseBegin,proofBegin).trim();
const backendCurrentSource=mainText.slice(proofBegin,proofEnd);
const backendProofSource=backendCurrentSource.replaceAll('\r\n','\n').trim();
const signature='static bool deckySidebarAcquireManagedBackend(DeckyManagedBackendLease& result,bool* absent=nullptr)';
assert.equal(backendProofSource.split(signature).length-1,1,'Complete typed absence classifier required');
assert.equal(backendProofSource.split('port(row.dwLocalPort)==1337').length-1,1,'Exact owned backend port seam required');
const backendCompiled=backendProofSource.replace(signature,'static bool deckySidebarAcquireManagedBackend(DeckyManagedBackendLease& result,ymcc::deckysidebar::Runtime& g_deckySidebarRuntime,unsigned short fixturePort,bool* absent=nullptr)').replace('port(row.dwLocalPort)==1337','port(row.dwLocalPort)==fixturePort');

const cleanupWorkerText=fs.readFileSync(path.join(repo,'native/decky_context_cleanup_worker.h'),'utf8');
const actualProcessBirth=cleanupWorkerText.match(/^inline unsigned long long processBirth\(HANDLE process\)\{[^\r\n]+\}$/m)?.[0];
assert(actualProcessBirth,'Missing exact native GetProcessTimes birth function');

const backendText='namespace ymcc::deckyrepair::cleanupworker {\n'+actualProcessBirth+'\n}\nnamespace actualManagedBackend {\n'+backendLease+'\n'+backendCompiled+'\n}\n';
fs.writeFileSync(path.join(source,'native/decky_managed_backend_consumer.inc'),backendText);
fs.writeFileSync(path.join(out,'MANAGED-BACKEND-CONSUMER.json'),JSON.stringify({path:'native/main.cpp',sourceSha256:sha(Buffer.from(backendCurrentSource)),compiledProductSha256:sha(Buffer.from(backendProofSource)),compiledFixtureSha256:sha(Buffer.from(backendText)),actualNativeProcessBirthSha256:sha(Buffer.from(actualProcessBirth)),adaptations:['Runtime reference parameter','exclusive high loopback port'],ownershipAdapter:false,osListenerTableAdapter:false,scope:'Actual production classifier, actual already-owned Runtime Job and actual inert child listeners in existing native.typed-ready-rebind; no production applications or installed port.'},null,2)+'\n');

fs.writeFileSync(path.join(source,'native/main.cpp'),mainBytes);pins.push({path:'native/main.cpp',bytes:mainBytes.length,sha256:sha(mainBytes),role:'static event attribution plus extracted backend proof; complete main not compiled'});
fs.writeFileSync(path.join(out,'RESUME-CONSUMER.json'),JSON.stringify(consumer,null,2)+'\n');fs.writeFileSync(path.join(out,'RESUME-CONSUMER.cpp.txt'),commit[0]+'\n');
fs.writeFileSync(path.join(out,'SOURCE-PINS.json'),JSON.stringify({repo,source,files:pins},null,2));
const toolDeclaration=selectedNativeTools(),tools=toolDeclaration.native;
const env=Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]));env.PATH=path.dirname(tools.compiler)+';'+process.env.SystemRoot+'/System32';env.INCLUDE=tools.include.join(';');env.LIB=tools.lib.join(';');env.CL='';env._CL_='';env.LINK='';env._LINK_='';env.TEMP=path.join(out,'tmp');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP);
const root=path.join(out,'FixtureSteam');fs.mkdirSync(root);const exe=path.join(root,'steam.exe'),object=path.join(out,'binding-client.obj');

const sdkNames=['ws2tcpip.h','iphlpapi.h'];
const sdkPaths=sdkNames.map(name=>{const hit=tools.include.map(dir=>path.join(dir,name)).find(p=>fs.existsSync(p));assert(hit,'Missing known SDK header '+name);return hit;});
const sdkImport=tools.lib.map(dir=>path.join(dir,'iphlpapi.lib')).find(p=>fs.existsSync(p));assert(sdkImport,'Missing known SDK import library');sdkPaths.push(sdkImport);
const backendSdkInputs=sdkPaths.map(p=>{const b=fs.readFileSync(p);return{path:p,bytes:b.length,sha256:sha(b)};});
fs.writeFileSync(path.join(out,'BACKEND-SDK-INPUTS.json'),JSON.stringify({files:backendSdkInputs,scope:'Known added header/import inputs pinned before compiler and checked after native completion. Not complete SDK closure.'},null,2)+'\n');

const args=['/nologo','/Bv','/std:c++20','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/DNOMINMAX','/DUNICODE','/D_UNICODE','/I'+path.join(source,'native'),'/I'+path.join(source,'deps/json'),path.join(source,'tools/decky_binding_runtime_selftest.cpp'),'/sourceDependencies',path.join(out,'dependencies.json'),'/Fo'+object,'/Fe'+exe,'/link','advapi32.lib','ws2_32.lib','bcrypt.lib','winhttp.lib','user32.lib','userenv.lib','shell32.lib','iphlpapi.lib','/verbose:lib'];

// Close the compiler's actual include set against bytes recorded BEFORE it
// starts. Lexical discovery is a conservative superset, never proof that an
// include was used; /sourceDependencies supplies that actual-used evidence.
const knownInputPins=new Map(),compilerInputFiles=new Map(),lexicallyVisited=new Set();
const inputKey=p=>path.resolve(p).toLowerCase();
const compilerIncludeRoots=[path.join(source,'native'),path.join(source,'deps/json'),...tools.include];
function pinInput(file){const p=path.resolve(file),key=inputKey(p);if(knownInputPins.has(key))return knownInputPins.get(key);const b=fs.readFileSync(p),row={path:p,bytes:b.length,sha256:sha(b)};knownInputPins.set(key,row);return row;}
for(const expected of [...toolDeclaration.files,...(toolDeclaration.additionalActualDependencies??[])]){const actual=pinInput(expected.path);assert.equal(actual.sha256,expected.sha256.toLowerCase(),'Declared original tool input changed: '+expected.path);}
const knownToolInputCount=knownInputPins.size;
function resolveCompilerInput(name,current,quoted){return [...(quoted?[path.resolve(path.dirname(current),name)]:[]),...compilerIncludeRoots.map(dir=>path.resolve(dir,name))].find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());}
const linkLibraryNames=new Set(args.filter(x=>/\.lib$/i.test(x)));
function visitCompilerInput(file){const key=inputKey(file);if(lexicallyVisited.has(key))return;lexicallyVisited.add(key);const row=pinInput(file);compilerInputFiles.set(key,row);const text=fs.readFileSync(file,'utf8');for(const m of text.matchAll(/^\s*#\s*include\s*([<"])([^>"\r\n]+)[>"]/gm)){const found=resolveCompilerInput(m[2],file,m[1]==='"');if(found)visitCompilerInput(found);}for(const m of text.matchAll(/^\s*#\s*pragma\s+comment\s*\(\s*lib\s*,\s*"([^"\r\n]+)"/gim))linkLibraryNames.add(m[1]);}
visitCompilerInput(path.join(source,'tools/decky_binding_runtime_selftest.cpp'));
for(const name of linkLibraryNames){const file=tools.lib.map(dir=>path.join(dir,name)).find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());if(file)pinInput(file);else assert(!args.includes(name),'Explicit linked SDK import missing: '+name);}
const precompileInputPins=[...knownInputPins.values()];
fs.writeFileSync(path.join(out,'PRECOMPILE-INPUT-CLOSURE.json'),JSON.stringify({files:precompileInputPins,knownToolInputCount,lexicallyDiscoveredCompilerInputs:compilerInputFiles.size,role:'Pre-compiler superset; used include membership comes only from actual compiler dependencies; no business execution'},null,2)+'\n');

const buildStartedAtUtc=new Date().toISOString(),buildBegin=performance.now();
const build=spawnSync(tools.compiler,args,{cwd:out,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,'compile.log'),(build.stdout??'')+(build.stderr??''));
fs.writeFileSync(path.join(out,'BUILD.json'),JSON.stringify({pid:build.pid,startedAtUtc:buildStartedAtUtc,endedAtUtc:new Date().toISOString(),elapsedMs:performance.now()-buildBegin,timeoutMs:60000,compiler:tools.compiler,compilerSha256:sha(fs.readFileSync(tools.compiler)),args,exit:build.status,error:build.error?.message??null},null,2));assert.equal(build.status,0,'Entry fixture compile failed');

const compilerDependencies=JSON.parse(fs.readFileSync(path.join(out,'dependencies.json'),'utf8').replace(/^\uFEFF/,''));
assert(compilerDependencies?.Data&&Array.isArray(compilerDependencies.Data.Includes),'Actual compiler dependency receipt missing');
const actualUsedFiles=[compilerDependencies.Data.Source,...compilerDependencies.Data.Includes];
const actualLinkLibraries=[...new Set([...(build.stdout??'').matchAll(/[A-Za-z]:\\[^\r\n]*?\.lib(?=[:\s,(]|$)/gi)].map(m=>path.resolve(m[0])))];
const missingUsedInputs=[...actualUsedFiles,...actualLinkLibraries].filter(p=>!knownInputPins.has(inputKey(p)));
const changedClosedInputs=precompileInputPins.filter(row=>{const b=fs.readFileSync(row.path);return b.length!==row.bytes||sha(b)!==row.sha256;}).map(row=>row.path);
const compilerInputClosure={complete:missingUsedInputs.length===0&&changedClosedInputs.length===0,knownToolInputCount,precompilePins:precompileInputPins.length,actualIncludeFiles:compilerDependencies.Data.Includes.length,actualLinkedLibrariesObserved:actualLinkLibraries.length,actualUsedFiles,actualLinkLibraries,missingUsedInputs,changedClosedInputs,scope:'Actual compiler-used include set and traced resolved library paths checked against pre/post pinned superset. Original tool declaration independently checked. Does not imply full product/SDK installation closure.'};
fs.writeFileSync(path.join(out,'COMPILER-INPUT-CLOSURE.json'),JSON.stringify(compilerInputClosure,null,2)+'\n');
assert(compilerInputClosure.complete,'Compiler used an unpinned or changed input; no native execution admitted');

const cef=path.join(root,'bin','cef','fixture','steamwebhelper.exe');fs.mkdirSync(path.dirname(cef),{recursive:true});fs.copyFileSync(exe,cef);
const runStartedAtUtc=new Date().toISOString(),runBegin=performance.now();
const runArgs=[path.join(out,'runtime-fixtures'),...process.argv.slice(3)];
const run=spawnSync(exe,runArgs,{cwd:out,env,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,'run.log'),(run.stdout??'')+(run.stderr??''));
const sdkChanged=backendSdkInputs.filter(p=>{const b=fs.readFileSync(p.path);return b.length!==p.bytes||sha(b)!==p.sha256;}).map(p=>p.path);
fs.writeFileSync(path.join(out,'NATIVE-PROCESS.json'),JSON.stringify({pid:run.pid,executable:exe,args:runArgs,sha256:sha(fs.readFileSync(exe)),startedAtUtc:runStartedAtUtc,endedAtUtc:new Date().toISOString(),elapsedMs:performance.now()-runBegin,timeoutMs:45000,exitCode:run.status,signal:run.signal,error:run.error?.message??null},null,2)+'\n');
const changed=pins.filter(p=>sha(fs.readFileSync(path.join(repo,p.path)))!==p.sha256).map(p=>p.path);
const result={schema:'ymcc.binding-runtime-build-and-run.v1',at:new Date().toISOString(),status:run.status===0&&changed.length===0&&sdkChanged.length===0?'pass':'fail',compilerExit:build.status,runExit:run.status,error:run.error?.message??null,sourceInputs:pins.length,changedOriginalInputs:changed,changedKnownSdkInputs:sdkChanged,compilerInputClosure,managedBackendConsumer:JSON.parse(fs.readFileSync(path.join(out,'MANAGED-BACKEND-CONSUMER.json'),'utf8')),exeSha256:sha(fs.readFileSync(exe)),objectSha256:sha(fs.readFileSync(object)),selection:process.argv[3]??null,globalMissedEventProbeRequested:process.argv.includes('--global-missed-event'),resumeConsumer:consumer,scope:'Real production Runtime/Client/Owner plus source-built protocol peer, inert Loader/CEF. Original 3200ms deadline and release-after-measurement. Not real Steam bootstrap, field input or full package acceptance.',fullRuntimeMigrationPassed:false,hostReleasePassed:false,installed:false,fieldActions:0,sleepActions:0};
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));if(result.status!=='pass')process.exitCode=1;


