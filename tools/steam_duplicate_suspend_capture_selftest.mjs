import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);const output=args[args.indexOf('--output')+1];const reference=args[args.indexOf('--reference-header')+1];
if(!args.includes('--output')||!args.includes('--reference-header')||!path.isAbsolute(output)||!path.isAbsolute(reference))throw Error('Explicit absolute --output and --reference-header required');
fs.mkdirSync(output,{recursive:true});
function extract(s,signature){
 const start=s.indexOf(signature);if(start<0||s.indexOf(signature,start+signature.length)>=0)throw Error('Ambiguous function '+signature);
 let depth=0,began=false,quote='',line=false,block=false,escape=false;
 for(let i=s.indexOf('{',start);i<s.length;i++){
  const c=s[i],n=s[i+1];if(line){if(c==='\n')line=false;continue;}if(block){if(c==='*'&&n==='/'){block=false;i++;}continue;}
  if(quote){if(escape){escape=false;continue;}if(c==='\\'){escape=true;continue;}if(c===quote)quote='';continue;}
  if(c==='/'&&n==='/'){line=true;i++;continue;}if(c==='/'&&n==='*'){block=true;i++;continue;}if(c==='"'||c==="'"){quote=c;continue;}
  if(c==='{'){depth++;began=true;}else if(c==='}'&&!--depth&&began)return s.slice(start,i+1);
 }throw Error('Unclosed function '+signature);
}
const rows=[];
for(const [variant,header]of[['reference',reference],['candidate',path.join(root,'native/sleep_steam_recovery.h')]]){
 const out=path.join(output,variant);fs.mkdirSync(out,{recursive:true});const raw=fs.readFileSync(header),s=raw.toString('utf8');
 const a=s.indexOf('struct Captured {'),b=s.indexOf('// Read-only baseline fence;',a);if(a<0||b<0)throw Error('Missing storage seam');
 const signatures=['static Captured wakeBoundaryFor(','static void prepareSuspend()','static void cancelPreparedSuspend()','static void captureSuspend('];if(s.includes('static void closeSuspendEpisode()'))signatures.push('static void closeSuspendEpisode()');
 fs.writeFileSync(path.join(out,'steam_duplicate_suspend_capture_functions.inc'),s.slice(a,b)+'\n'+signatures.map(x=>extract(s,x)).join('\n')+'\n');
 const auto=fs.readFileSync(path.join(root,'native/sleep_recovery_auto.h'),'utf8');const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
 let resumeInclude='';
 if(variant==='candidate'){
  const closeWrapper=extract(auto,'static void sleepRecoveryAutoCloseSuspendEpisode()');const armWrapper=extract(auto,'static void sleepRecoveryAutoArm(');
  const resumePattern=/if \(w == PBT_APMRESUMEAUTOMATIC \|\| w == PBT_APMRESUMESUSPEND \|\| w == PBT_APMRESUMECRITICAL\)\s*sleepRecoveryAutoCloseSuspendEpisode\(\);/g;
  const found=[...main.matchAll(resumePattern)];if(found.length!==1)throw Error('Resume-hook cardinality changed');
  const readyPattern=/g_powerLifecycle\.store\(PowerLifecycle::Ready, std::memory_order_release\);/g;
  const readyStores=[...main.matchAll(readyPattern)];if(readyStores.length!==3)throw Error('Ready publisher inventory changed');
  for(const m of readyStores){if(!/sleepRecoveryAutoCloseSuspendEpisode\(\);\s*$/.test(main.slice(0,m.index)))throw Error('Ready publisher missing close hook');}
  const readySnippet='sleepRecoveryAutoCloseSuspendEpisode();'+readyStores[0][0];
  resumeInclude='namespace SleepSteamRecovery{static void closeSuspendEpisode(){::closeSuspendEpisode();}}\nnamespace SleepRecoveryAuto{static void arm(unsigned long long){}}\n'+closeWrapper+'\n'+armWrapper+'\nstatic constexpr unsigned PBT_APMRESUMEAUTOMATIC=1,PBT_APMRESUMESUSPEND=2,PBT_APMRESUMECRITICAL=3;\nstatic void dispatchProductionResumeNotice(unsigned w){'+found[0][0]+'}\nstatic void publishProductionReady(){'+readySnippet+'}\n';
 }else resumeInclude='static void dispatchProductionResumeNotice(unsigned){}\nstatic void publishProductionReady(){g_powerLifecycle=PowerLifecycle::Ready;}\nstatic void sleepRecoveryAutoArm(unsigned long long){}\n';
 fs.writeFileSync(path.join(out,'steam_duplicate_suspend_capture_resume.inc'),resumeInclude);
 const exe=path.join(out,'steam_duplicate_suspend_capture_selftest.exe');const obj=path.join(out,'steam_duplicate_suspend_capture_selftest.obj');
 const build=spawnSync('cl.exe',['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/Od','/I'+out,'/I'+path.join(root,'native'),'/I'+path.join(root,'deps/json'),path.join(root,'tools/steam_duplicate_suspend_capture_selftest.cpp'),'/Fo'+obj,'/Fe'+exe],{cwd:out,encoding:'utf8',windowsHide:true});
 fs.writeFileSync(path.join(out,'build.log'),(build.stdout||'')+(build.stderr||''));if(build.status!==0)throw Error(variant+' compile failed: '+(build.stdout||'')+(build.stderr||''));
 const run=spawnSync(exe,[],{cwd:out,encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'result.json'),run.stdout||'');
 rows.push({variant,header,sha256:crypto.createHash('sha256').update(raw).digest('hex'),buildExit:build.status,runExit:run.status,...JSON.parse(run.stdout)});
}
const good=rows[0].runExit===1&&rows[0].failed>0&&rows[1].runExit===0&&rows[1].failed===0;
const summary={schema:1,passed:good,referenceRegressionReproduced:rows[0].failed>0,results:rows,scope:'real production capture functions with bounded dependencies; no runtime recovery claim'};
fs.writeFileSync(path.join(output,'VALIDATION.json'),JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify(summary));process.exitCode=good?0:1;
