// Execute the ORIGINAL strict native consumer against the current production Runtime.
// An uncooperative callback remains a failure; fixture release is cleanup, never acceptance.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]||'');if(!process.argv[2]||fs.existsSync(out))throw Error('Fresh explicit output required');fs.mkdirSync(out,{recursive:true});
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const config=JSON.parse(fs.readFileSync(path.join(root,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'),'utf8').replace(/^\uFEFF/,''));
const env={...process.env,INCLUDE:config.native.include.join(';'),LIB:config.native.lib.join(';'),PATH:path.dirname(config.native.compiler)+';'+process.env.PATH};
const mainFile=path.join(root,'native/main.cpp'),main=fs.readFileSync(mainFile,'utf8'),start=main.indexOf('    case WM_POWER_RESUME_COMMIT:'),body=main.indexOf('\n',start)+1,end=main.indexOf('            ipc_emit("power.resumed"',body);
if(start<0||end<body||!main.slice(body,end).includes('requestBindingRefresh(currentPowerGeneration())'))throw Error('Original committed-Ready extraction boundary changed');
fs.writeFileSync(path.join(out,'committed-ready-consumer.inc'),'static void consumeCommittedReady(){\n'+main.slice(body,end)+'        }\n}\n');
const source=path.join(root,'tools/ymcc_sim/global/decky-persistent-lifecycle/native-fixture.cpp'),runtime=path.join(root,'native/decky_sidebar_runtime.h'),exe=path.join(out,'persistent-runtime.exe');
const sources={[path.relative(root,source)]:digest(fs.readFileSync(source)),[path.relative(root,runtime)]:digest(fs.readFileSync(runtime)),[path.relative(root,mainFile)]:digest(Buffer.from(main))};
function run(id,program,args,timeout){const t=Date.now(),r=spawnSync(program,args,{cwd:out,env,encoding:'utf8',windowsHide:true,timeout,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,id+'.log'),(r.stdout||'')+(r.stderr||''));const row={id,exitCode:r.status,error:r.error?.message,elapsedMs:Date.now()-t};console.log(JSON.stringify(row));return row;}
const build=run('build',config.native.compiler,['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE','/I'+path.join(root,'native'),'/I'+path.join(root,'deps/json'),'/I'+out,source,'/Fo'+exe+'.obj','/Fe'+exe,'/link','advapi32.lib','user32.lib','userenv.lib','winhttp.lib','ws2_32.lib','bcrypt.lib'],60000);
if(build.exitCode!==0)process.exit(2);
const execution=run('native',exe,[path.join(out,'native-fixtures')],50000);
if(!fs.existsSync(path.join(out,'native-result.json'))){fs.writeFileSync(path.join(out,'probe-result.json'),JSON.stringify({passed:false,build,execution,sources,error:'No terminal native consumer receipt'},null,2));process.exit(2);}
const report=JSON.parse(fs.readFileSync(path.join(out,'native-result.json'),'utf8'));
const result={passed:report.passed,build,execution,sources,sourceUnchanged:Object.entries(sources).every(([rel,hash])=>digest(fs.readFileSync(path.join(root,rel)))===hash),cases:report.cases,uncooperativeCase:report.cases.find(c=>c.id==='native.stalled-binding-stop-requires-fixture-release'),fullShutdownAcceptance:report.passed};
fs.writeFileSync(path.join(out,'probe-result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({passed:result.passed,sourceUnchanged:result.sourceUnchanged,uncooperativeCase:result.uncooperativeCase}));process.exitCode=result.passed?0:1;
