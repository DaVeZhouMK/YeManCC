import fs from 'node:fs';import path from 'node:path';import{createHash}from'node:crypto';import{spawnSync}from'node:child_process';import{pathToFileURL,fileURLToPath}from'node:url';
const opt={};for(let i=2;i<process.argv.length;i+=2){if(!process.argv[i+1])throw Error('argument value required');opt[process.argv[i].slice(2)]=process.argv[i+1];}
const repo=path.resolve(opt.repo??path.join(path.dirname(fileURLToPath(import.meta.url)),'..')),reference=path.resolve(opt.reference??repo),out=path.resolve(opt.out??'');if(!opt.out||fs.existsSync(out))throw Error('fresh --out required');fs.mkdirSync(out,{recursive:true});
const sha=b=>createHash('sha256').update(b).digest('hex'),digest=p=>sha(fs.readFileSync(p));
const extractorFile=reference+'/tools/fixtures/legion_motion_consumer/extract_main.mjs',toolchainFile=reference+'/tools/fixtures/hc_reviewed_calibration/toolchain.mjs';
const{extract}=await import(pathToFileURL(extractorFile)),{calibrationToolchainPreamble}=await import(pathToFileURL(toolchainFile));
const source=fs.readFileSync(repo+'/native/main.cpp','utf8'),names=['hardwareWriteAllowed','currentPowerGeneration','beginHardwareWriteLease','endHardwareWriteLease','nativeYmccShortcutSubmit','nativeSecondaryDisplayRequest','nativeSecondaryDisplayKeyboardConsume','reg_secondary_display'];
function exactFunction(name){
 if(name!=='nativeYmccShortcutSubmit')return extract(source,name);
 const signature='OemProducerTokens producerTokens = {}) {',start=source.indexOf('static bool nativeYmccShortcutSubmit('),at=source.indexOf(signature,start);if(start<0||at<start||at>start+800)throw Error('queue signature changed');
 // Mask only the default initializer for the existing extractor; emit original source bytes unchanged.
 const masked=source.slice(0,at)+signature.replace('{}',' 0')+source.slice(at+signature.length),fragment=extract(masked,name),original=Buffer.from(source).subarray(fragment.startUtf8Byte,fragment.endUtf8ByteExclusive).toString('utf8');
 return {...fragment,code:original,sha256:sha(original),signatureDefaultInitializerMaskedOnlyForLocating:true};
}
const fragments=names.map(exactFunction);fragments.splice(4,0,extract(source,'HardwareWriteLease','struct'));
const evalBody=extract(source,'nativeKeyboardShortcutsEvaluate').code;
if(!(evalBody.indexOf('nativeSecondaryDisplayKeyboardConsume(')>=0&&evalBody.indexOf('nativeSecondaryDisplayKeyboardConsume(')<evalBody.indexOf('if (g_nativeKeyboardShortcuts.empty())')))throw Error('dedicated OEM consumer must run before empty-rule return');
for(const op of ['get','set','toggle','release'])if(!source.includes('cmd == "model.secondaryDisplay.'+op+'"'))throw Error('secondary API missing existing async classification');
if(!source.includes('    reg_secondary_display();'))throw Error('registration not invoked');
const fixture=repo+'/tools/fixtures/hc_secondary_display_consumer/probe.cpp';let code=fs.readFileSync(fixture,'utf8');if(code.split('@@PRODUCTION@@').length!==2)throw Error('single production marker required');code=code.replace('@@PRODUCTION@@',fragments.map(x=>x.code).join('\n\n'));
fs.writeFileSync(out+'/probe.cpp',code);fs.writeFileSync(out+'/source-fragments.json',JSON.stringify({fragments,callerSha256:sha(evalBody),bodiesRewritten:false},null,2));
const inputs=[repo+'/native/main.cpp',repo+'/native/secondary_display_client.h',fixture,fileURLToPath(import.meta.url),extractorFile,toolchainFile];const before=inputs.map(file=>({file,sha256:digest(file)}));const configurations=[];
function run(label,exe,args,cwd){const r=spawnSync(exe,args,{cwd,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:8*1024*1024});fs.writeFileSync(out+'/'+label+'.stdout.log',r.stdout??'');fs.writeFileSync(out+'/'+label+'.stderr.log',r.stderr??'');if(r.error)throw r.error;return r;}
for(const mode of (opt.profiles??'Release,Checked').split(',')){
 if(!['Release','Checked'].includes(mode))throw Error('profile');const dir=out+'/'+mode;fs.mkdirSync(dir);const flags=mode==='Release'?'/O2 /DNDEBUG /MD':'/Od /RTC1 /Zi /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MDd';
 fs.writeFileSync(dir+'/compile.cmd',calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+'cl /nologo /std:c++20 /EHsc /utf-8 /W4 /permissive- '+flags+' /I"'+repo+'/native" /I"'+reference+'/deps/json" "'+out+'/probe.cpp" /Fo"'+dir+'/probe.obj" /Fd"'+dir+'/probe.pdb" /Fe"'+dir+'/probe.exe" /sourceDependencies "'+dir+'/dependencies.json" /link /INCREMENTAL:NO\r\nexit /b %errorlevel%\r\n');
 const build=run(mode+'-compile','cmd.exe',['/d','/c',dir+'/compile.cmd'],dir);if(build.status!==0)throw Error('native compile failed: '+out+'/'+mode+'-compile.stdout.log');
 const result=run(mode+'-run',dir+'/probe.exe',[],dir);let report;try{report=JSON.parse(result.stdout.trim());}catch{throw Error('native result missing');}
 const deps=JSON.parse(fs.readFileSync(dir+'/dependencies.json','utf8').replace(/^\uFEFF/,''));
 configurations.push({mode,buildExit:build.status,exit:result.status,signal:result.signal,naturalExit:result.signal===null,report,dependencies:[deps.Data.Source,...deps.Data.Includes].map(file=>({file,sha256:digest(file)})),executableSha256:digest(dir+'/probe.exe')});console.log(JSON.stringify({mode,...report}));
}
const faults=[];
if(opt['skip-faults']!=='true'){
 const definitions=[
  {id:'held-key-repeat',file:'native/secondary_display_client.h',before:'const bool emit=armed&&!held;',after:'const bool emit=armed;',error:'held key repeated'},
  {id:'ignore-screen-mapping',file:'native/main.cpp',before:'for(const auto& rule:g_nativeKeyboardShortcuts)if(rule.requiredMask&kOemBitScreen){mapped=true;break;}',after:'for(const auto& rule:g_nativeKeyboardShortcuts)if(rule.requiredMask==0){mapped=true;break;}',error:'mapped shortcut triggered default'},
  {id:'skip-recheck-after-get',file:'native/main.cpp',before:'        if(!current())return;\n        (void)nativeSecondaryDisplayRequest("toggle"',after:'        (void)nativeSecondaryDisplayRequest("toggle"',error:'stale read authorized toggle'},
  {id:'accept-late-power-result',file:'native/secondary_display_client.h',before:'if(!current())return rejected("副屏请求结束时来源或电源会话已变化");',after:'if(current()&&false)return rejected("副屏请求结束时来源或电源会话已变化");',error:'late power result accepted'}
 ];
 for(const definition of definitions){
  const dir=out+'/source-faults/'+definition.id,overlay=dir+'/source';fs.mkdirSync(overlay+'/native',{recursive:true});fs.mkdirSync(overlay+'/tools/fixtures/hc_secondary_display_consumer',{recursive:true});
  for(const name of ['native/main.cpp','native/secondary_display_client.h','tools/fixtures/hc_secondary_display_consumer/probe.cpp'])fs.copyFileSync(repo+'/'+name,overlay+'/'+name);
  const file=overlay+'/'+definition.file,original=fs.readFileSync(file,'utf8'),needle=definition.before.replaceAll('\\n','\n');if(original.split(needle).length!==2)throw Error('nonunique native fault anchor '+definition.id);fs.writeFileSync(file,original.replace(needle,definition.after));
  const execution=run('fault-'+definition.id,process.execPath,[fileURLToPath(import.meta.url),'--repo',overlay,'--reference',reference,'--out',dir+'/execution','--profiles','Release','--skip-faults','true'],dir);
  const record=JSON.parse(fs.readFileSync(dir+'/execution/result.json','utf8')),first=record.configurations[0];
  const rejected=execution.status===1&&first?.buildExit===0&&first?.report.status==='FAIL'&&first.report.error===definition.error;
  faults.push({id:definition.id,compiled:first?.buildExit===0,rejected,exitCode:execution.status,signal:execution.signal,actualError:first?.report.error,expectedError:definition.error,source:definition.file,originalSha256:sha(original),faultSha256:digest(file)});
  console.log(JSON.stringify({sourceFault:definition.id,rejected,error:first?.report.error}));if(!rejected)throw Error('native semantic fault escaped '+definition.id);
 }
}
const drift=before.filter(r=>digest(r.file)!==r.sha256);const result={schemaVersion:1,status:configurations.every(c=>c.exit===0&&c.report.status==='PASS')&&!drift.length?'PASS':'FAIL',sourceInputs:before,fragments:fragments.map(({code,...x})=>x),configurations,faults,drift,actualMainFunctionsExtracted:true,fullProductExecuted:false,physicalAcceptance:false,wholeProcessCpuReduction:null};fs.writeFileSync(out+'/result.json',JSON.stringify(result,null,2)+'\n');if(result.status!=='PASS')process.exitCode=1;