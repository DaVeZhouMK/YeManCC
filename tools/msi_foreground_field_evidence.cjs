'use strict';
// Offline exported-log analysis only. No process/device/driver/network access,
// no ZIP execution, and no registration as an app timer or capture service.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const defaultRoster=[
 {label:'B2-修正回读基线',nativeSha256:'94cf745f5161b7f5621e19090d827af605f8606bc6a97d9f7bb95d8df245b8be'},
 {label:'C-微星类型交接',nativeSha256:'138e8fdb9c9c0863087596d6aa2bd33176381d9a7b600465129af079d9505d41'}
].map(r=>({...r,helperExeSha256:'9371f290db004731890b3a52c6c5a149820bef891a0813be00da22647f75424b',helperDllSha256:'c255ab447e5f647ca517201e0432cad0ba8073982cf2e43e5b515f369f769c2d'}));
const digest=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const hex=v=>typeof v==='string'&&/^[0-9a-f]{64}$/i.test(v.trim())?v.trim().toLowerCase():null;
// Producers use local ISO wall-clock timestamps, not epoch UTC. Compare within
// this one producer without inferring a timezone from the analysis machine.
function wallClock(s){const m=typeof s==='string'&&s.match(/^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,3}))?$/);if(!m)return null;const value=Date.UTC(+m[1],+m[2]-1,+m[3],+m[4],+m[5],+m[6],+(m[7]||'0').padEnd(3,'0'));const normalized=new Date(value).toISOString();return normalized.slice(0,19)===s.slice(0,19)?value:null;}
const wallString=n=>new Date(n).toISOString().slice(0,23);
const countBy=(rows,fn)=>{const result={};for(const r of rows){const k=String(fn(r));result[k]=(result[k]||0)+1;}return result;};
function analyze(directory,options={}){
 const dir=path.resolve(directory),manifestFile=path.join(dir,'YeManCC-export-manifest.txt');
 const minutes=options.windowMinutes===undefined?30:Number(options.windowMinutes);
 if(!Number.isFinite(minutes)||minutes<=0||minutes>1440)throw Error('windowMinutes must be in (0,1440]');
 const text=fs.readFileSync(manifestFile,'utf8').replace(/^\uFEFF/,'');const manifest={};
 text.split('\n').forEach(s=>{const m=s.replace(/\r+$/,'').match(/^([A-Za-z][A-Za-z0-9]*)=(.*)$/);if(m)manifest[m[1]]=m[2].trim();});
 const pid=Number(manifest.processId),nativeSha256=hex(manifest.nativeSha256);
 if(!Number.isSafeInteger(pid)||pid<=0||!nativeSha256)throw Error('manifest must identify a current positive processId and full nativeSha256');
 const roster=options.roster||defaultRoster;
 if(!Array.isArray(roster)||roster.some(r=>!hex(r.nativeSha256)||!hex(r.helperExeSha256)||!hex(r.helperDllSha256)))throw Error('roster must provide full native/apphost/business SHA256 values');
 const matchingBuilds=roster.filter(r=>hex(r.nativeSha256)===nativeSha256);if(matchingBuilds.length>1)throw Error('ambiguous native identity in roster');
 const build=matchingBuilds[0]||null,all=[],files=[],malformed=[],badTimes=[];
 for(const name of ['native-lifecycle.log','native-detail.log']){
  const file=path.join(dir,name);if(!fs.existsSync(file)){files.push({file,present:false});continue;}
  files.push({file,present:true,sha256:digest(file)});
  fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'').split(/\r?\n/).forEach((line,i)=>{
   if(!line.trim())return;let data;try{data=JSON.parse(line);}catch(e){malformed.push({file,line:i+1});return;}
   if(!data||data.pid!==pid)return;const stamp=wallClock(data.time);
   if(stamp===null){badTimes.push({file,line:i+1,time:data.time});return;}
   all.push({reference:{file,line:i+1},data,wallClock:stamp});
  });
 }
 if(!files.find(f=>f.present&&f.file.endsWith('native-lifecycle.log')))throw Error('native-lifecycle.log is required');
 if(!all.length)throw Error('no timestamped records from manifest current PID');
 let end=-Infinity;for(const r of all)end=Math.max(end,r.wallClock);const start=end-minutes*60000;
 const rows=all.filter(r=>r.wallClock>=start&&r.wallClock<=end).sort((a,b)=>a.wallClock-b.wallClock);
 const native=rows.filter(r=>r.data.event==='msi-native-foreground-isolation');
 const sourceRetries=rows.filter(r=>r.data.event==='msi-native-foreground-source-retry');
 const managed=rows.filter(r=>r.data.event==='msi-foreground-isolation');
 const ui=rows.filter(r=>r.data.event==='gamepad-ui-input-emitted');
 const starts=rows.filter(r=>r.data.event==='input-host-started');
 const notifications=rows.filter(r=>r.data.event==='power-device-wake'),groups=new Map();
 for(const r of notifications){const key=String(r.data.powerGeneration);if(!groups.has(key))groups.set(key,[]);groups.get(key).push(r);}
 const groupRows=[...groups].map(([generation,rs])=>({generation:Number(generation),first:rs[0],last:rs.at(-1),notificationCount:rs.length})).sort((a,b)=>a.first.wallClock-b.first.wallClock);
 const resumeGroups=groupRows.map((g,i)=>{
  const next=groupRows[i+1]?.first.wallClock??end+1;
  const focus=rows.filter(r=>r.data.event==='focus-foreground-changed'&&r.wallClock>=g.first.wallClock&&r.wallClock<Math.min(next,g.last.wallClock+2000));
  const emissions=ui.filter(r=>r.wallClock>=g.first.wallClock&&r.wallClock<next);
  return{powerGeneration:g.generation,firstNotification:g.first.data.time,lastNotification:g.last.data.time,notificationCount:g.notificationCount,notificationReferences:[g.first.reference,g.last.reference],foregroundNearNotification:focus.map(r=>({reference:r.reference,time:r.data.time,exe:r.data.fgExe,isSelf:r.data.fgIsSelf})),subsequentInternalUiEmissions:emissions.map(r=>({reference:r.reference,time:r.data.time,action:r.data.action,foreground:r.data.foreground})),meaning:'notifications and internal command emissions, not actual sleep-cycle count or UI/Steam delivery proof'};
 });
 const helperRows=native.map(r=>{
  const d=r.data,exe=hex(d.helperExeSha256),dll=hex(d.helperDllSha256);
  const state=d.helperPending?'pending-same-handle':d.helperExitCode===259?'not-running-or-unrecorded-sentinel':d.helperExitCode===0&&d.cycleAccepted?'command-accepted-not-input-proof':'terminal-or-unverified';
  return{reference:r.reference,time:d.time,phase:d.phase,state,helperExitCode:d.helperExitCode,helperPending:d.helperPending,cycleAccepted:d.cycleAccepted,recordedExeSha256:exe,recordedDllSha256:dll,exeMatchesRegisteredBuild:build&&exe?exe===hex(build.helperExeSha256):null,dllMatchesRegisteredBuild:build&&dll?dll===hex(build.helperDllSha256):null,identityScope:d.helperIdentityScope||'not-recorded',xusbGameTargets:Array.isArray(d.xusbGameTargets)?d.xusbGameTargets:null,xusbEvidence:d.xusbTargetEvidence||'not-recorded'};
 });
 const managedWrites=managed.filter(r=>r.data.verificationScope==='config-only');
 const report={schemaVersion:1,generatedAt:new Date().toISOString(),mode:'read-only-offline-export-analysis',directory:dir,manifest:{file:manifestFile,sha256:digest(manifestFile),pid,nativeSha256},identity:{registeredBuild:build?.label||null,matchesRegisteredTestPackage:!!build,classification:build?'registered-test-build':'unregistered-or-earlier-build-not-B2C-acceptance'},window:{minutes,start:wallString(start),end:wallString(end),timeBasis:'producer-local-wall-clock-only'},files,unattributedMalformedRows:malformed,invalidCurrentPidTimestamps:badTimes,counts:{currentPidRowsInWindow:rows.length,nativeLeaseStateRows:native.length,nativeSourceRetryRows:sourceRetries.length,managedRows:managed.length,managedConfigReceiptRows:managedWrites.length,internalUiEmissionRows:ui.length,hostStartedRows:starts.length,resumeNotificationRows:notifications.length,resumeGenerationGroups:resumeGroups.length},managedStates:countBy(managed,r=>r.data.state),managedConfigReasons:countBy(managedWrites,r=>r.data.reason),helperReceipts:helperRows,hostStarts:starts.map(r=>({reference:r.reference,time:r.data.time,epoch:r.data.epoch,persona:r.data.persona,intentOrigin:r.data.intentOrigin,physicalSourcePending:r.data.physicalSourcePending,meaning:'intentOrigin is a code label, not evidence of a human operation'})),resumeGroups,hardwareAcceptanceProven:false,inferencesAllowed:['full native build comparison and current-PID scope','configuration/helper command receipts with stated scope','internal navigation command emissions only'],inferencesNotAllowed:['Steam device count/name/open record proves a delivered button','helperExitCode=0 proves external input isolation','no source-retry event proves the new warm-return code passed','post-resume command emission proves UI rendering/delivery','intentOrigin=operator-setting proves human operation','several exports from one PID/native hash are independent trials']};
 return report;
}
if(require.main===module){try{const args=process.argv.slice(2);const dir=args.shift();if(!dir)throw Error('Usage: node msi_foreground_field_evidence.cjs <unpacked-dir> [--window-minutes N] [--roster JSON] [--output JSON]');let out,options={};while(args.length){const k=args.shift(),v=args.shift();if(v===undefined)throw Error('missing argument for '+k);if(k==='--window-minutes')options.windowMinutes=Number(v);else if(k==='--roster')options.roster=JSON.parse(fs.readFileSync(v,'utf8').replace(/^\uFEFF/,''));else if(k==='--output')out=path.resolve(v);else throw Error('unknown argument '+k);}const result=analyze(dir,options);if(out)fs.writeFileSync(out,JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(out?{output:out,identity:result.identity,window:result.window,counts:result.counts,malformedRows:result.unattributedMalformedRows.length,hardwareAcceptanceProven:result.hardwareAcceptanceProven}:result,null,2));}catch(e){console.error(String(e));process.exitCode=1;}}
module.exports={analyze,wallClock,defaultRoster};
