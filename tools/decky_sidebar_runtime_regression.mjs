// Runtime-only consumer regression; compiles source-built inert children, never installed apps.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'..');
const out=path.resolve(process.argv[2]||'');
if(!process.argv[2]||fs.existsSync(out))throw Error('Fresh explicit evidence directory required');
fs.mkdirSync(out,{recursive:true});
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const tools=JSON.parse(fs.readFileSync(path.join(root,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'),'utf8').replace(/^\uFEFF/,''));
const env={...process.env,INCLUDE:tools.native.include.join(';'),LIB:tools.native.lib.join(';'),PATH:path.dirname(tools.native.compiler)+';'+process.env.PATH};
const report={passed:false,realSteamActions:0,officialLoaderExecuted:false,fullUncooperativeShutdownAccepted:false,cases:[],sources:{}};
for(const rel of ['native/decky_sidebar_runtime.h','tools/decky_sidebar_runtime_preparation_selftest.cpp','tools/decky_sidebar_runtime_regression.mjs'])report.sources[rel]=sha(path.join(root,rel));
function execute(id,program,args,timeout=30000){
 const start=Date.now(),r=spawnSync(program,args,{cwd:out,env,windowsHide:true,encoding:'utf8',timeout,maxBuffer:4*1024*1024});
 fs.writeFileSync(path.join(out,id+'.log'),(r.stdout||'')+(r.stderr||''));
 const row={id,passed:r.status===0&&!r.error,exitCode:r.status,elapsedMs:Date.now()-start,error:r.error?.message};report.cases.push(row);console.log(JSON.stringify(row));return row.passed;
}
const selected=process.argv[3]?.split(',');
const tests=[
 ['preparation','tools/decky_sidebar_runtime_preparation_selftest.cpp',['preparation-fixture'],40000],
 ['runtime','tools/decky_sidebar_runtime_selftest.cpp',[]],
 ['startup-race','tools/decky_sidebar_startup_race_selftest.cpp',[]],
 ['lifecycle','tools/decky_sidebar_lifecycle_selftest.cpp',[]],
 ['context','tools/decky_sidebar_context_selftest.cpp',[]],
 ['health-close','tools/decky_sidebar_health_close_selftest.cpp',['health-close-fixture']],
 ['crash-breaker','tools/decky_sidebar_crash_breaker_selftest.cpp',['crash-fixture'],40000],
 ['admission','tools/decky_sidebar_admission_selftest.cpp',['admission-fixture'],90000],
 ['readiness-diagnostics','tools/decky_sidebar_readiness_diagnostics_selftest.cpp',[]],
 ['manual-startup','tools/steam_manual_startup_selftest.cpp',['manual-startup-fixture']],
 ['startup-observation','tools/steam_startup_observation_selftest.cpp',['startup-observation-fixture']],
 ['ui-mode-shared','tools/steam_ui_mode_shared_selftest.cpp',[]]
];
const chosen=selected?tests.filter(t=>selected.includes(t[0])):tests;
const legacyRoot=path.resolve('G:/YeManCC-Work/Build/Tasks/YMCC-Decky-Input27/Build');
const legacyIds=new Set(['startup-race','lifecycle','context']);
const legacyReceipt=path.resolve(legacyRoot,'../validation/CONTEXT-LIFECYCLE-IMPLEMENTATION06.json');
for(const [id,source,args,timeout] of chosen){
 const legacy=legacyIds.has(id);if(legacy)fs.mkdirSync(legacyRoot,{recursive:true});
 const exe=legacy?path.join(legacyRoot,'runtime22-'+path.basename(out)+'-'+id+'.exe'):path.join(out,id+'.exe');
 const priorReceipt=id==='context'&&fs.existsSync(legacyReceipt)?fs.readFileSync(legacyReceipt):null;
 if(priorReceipt)fs.writeFileSync(path.join(out,'context-prior-receipt.json'),priorReceipt);
 if(id==='startup-observation')execute('extract-startup-observation',process.execPath,[path.join(root,'tools/steam_startup_observation_extract.mjs'),out,path.join(root,'native/main.cpp')]);report.sources[source]=sha(path.join(root,source));
 const built=execute('build-'+id,tools.native.compiler,['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE','/I'+path.join(root,'native'),'/I'+path.join(root,'deps/json'),'/I'+out,path.join(root,source),'/Fo'+exe+'.obj','/Fe'+exe,'/link','advapi32.lib','user32.lib','userenv.lib','winhttp.lib','ws2_32.lib','bcrypt.lib'],60000);
 if(built)execute(id,exe,args.map(arg=>path.join(out,arg)),timeout);
 if(id==='context'&&fs.existsSync(legacyReceipt)){
  fs.copyFileSync(legacyReceipt,path.join(out,'context-generated-receipt.json'));
  if(priorReceipt)fs.writeFileSync(legacyReceipt,priorReceipt);
 }
 if(legacy&&built){fs.copyFileSync(exe,path.join(out,id+'.exe'));report.cases.at(-1).executedImage=exe;}
}
report.sourceUnchanged=Object.entries(report.sources).every(([rel,digest])=>sha(path.join(root,rel))===digest);
report.passed=report.sourceUnchanged&&report.cases.every(c=>c.passed)&&report.cases.length===chosen.length*2+(chosen.some(t=>t[0]==='startup-observation')?1:0);
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({passed:report.passed,cases:report.cases.length,failed:report.cases.filter(c=>!c.passed).map(c=>c.id),sourceUnchanged:report.sourceUnchanged}));process.exitCode=report.passed?0:1;
