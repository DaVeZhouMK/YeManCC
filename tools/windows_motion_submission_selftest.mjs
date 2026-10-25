import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
// Source-exact Windows motion frame handoff and final admission. All IO inert.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';import {spawnSync} from 'node:child_process';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {closeBrace} from './fixtures/hc_remaining_model_capabilities/expectations.mjs';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..'),workspace=path.resolve(repo,'../../..');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/WindowsSubmission');
const opt={},args=process.argv.slice(2);for(let i=0;i<args.length;i++){
 if(['--expected-before-failure','--mutation-checks'].includes(args[i]))opt[args[i].slice(2)]=true;
 else if(['--source','--out','--vcvars'].includes(args[i])&&args[i+1])opt[args[i].slice(2)]=args[++i];else throw Error(`Invalid option ${args[i]}`);
}
const out=path.resolve(opt.out||path.join(root,new Date().toISOString().replace(/[:.]/g,'-'))),rel=path.relative(root,out);
if(!rel||rel==='..'||rel.startsWith(`..${path.sep}`)||path.isAbsolute(rel))throw Error('Use a WindowsSubmission child');
if(fs.existsSync(out)&&fs.readdirSync(out).length)throw Error('Use a fresh output directory');fs.mkdirSync(out,{recursive:true});
const sha=x=>createHash('sha256').update(x).digest('hex');
const report={schema:1,status:'running',sourceBindings:[],configurations:[],mutations:[],newProductDlls:[],physicalAcceptance:false,deviceIo:false,hostExecuted:false};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(report,null,2)+'\n');save();
function run(exe,argv,log,env=process.env,cwd=out){const r=spawnSync(exe,argv,{cwd,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:8*1024*1024});const p=path.join(out,log);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,(r.stdout||'')+(r.stderr||'')+(r.error?.message||''));if(r.error)throw r.error;return r;}
function need(r,label){if(r.status!==0)throw Error(`${label}: exit ${r.status}`);return r.stdout;}
function once(s,a,b){if(s.split(a).length!==2)throw Error(`Nonunique mutation ${a}`);return s.replace(a,b);}
try{
 const sourcePath=path.resolve(opt.source||path.join(repo,'native/main.cpp')),source=fs.readFileSync(sourcePath,'utf8').replace(/\r\n/g,'\n');
 const extractOne=(name,kind='function')=>{const e=extract(source,name,kind);report.sourceBindings.push({...e,code:undefined});return e.code;};
 const frame=extractOne('InputFrameSubmission','struct');
 const wired=frame.includes('windowsMotion');
 const scope=wired?extractOne('WindowsMotionSubmissionScope','struct'):`struct WindowsMotionSubmissionScope {const wm::Stamp* previous;explicit WindowsMotionSubmissionScope(const wm::Stamp* next):previous(t_windowsMotionSubmission){t_windowsMotionSubmission=next;}~WindowsMotionSubmissionScope(){t_windowsMotionSubmission=previous;}};`;
 const helper=wired?extractOne('inputHostWindowsMotionSubmissionCurrent'):'';
 const guards=['inputHostFrameSubmissionMayIssue','inputHostFrameSubmissionRequestCurrent'].map(n=>extractOne(n)).join('\n\n');
 const producer=wired?source.match(/        const auto windowsMotionSource =[\s\S]*?        const WindowsMotionSubmissionScope windowsSubmission\([\s\S]*?\);/)?.[0]:
 `const auto windowsMotionSource = !controllerMotionRoute && gyroSubmitAllowed ? wm::retain(windowsMotionSensorOwner,gyroBindingGeneration) : wm::Stamp{}; const WindowsMotionSubmissionScope windowsSubmission(!controllerMotionRoute&&gyroSubmitAllowed?&windowsMotionSource:nullptr);`;
 if(!producer)throw Error('Actual producer scope missing');
 const copy=wired?source.match(/^    frameSubmission\.windowsMotion=t_windowsMotionSubmission;$/m)?.[0]:'frameSubmission.windowsMotion=t_windowsMotionSubmission;';
 if(!copy)throw Error('Actual frame stamp handoff missing');
 if(wired){
   const submit=extract(source,'inputHostSubmitPad').code;
   if(!submit.includes('if (!inputHostWindowsMotionSubmissionCurrent(frameSubmission))'))throw Error('Pre-assembly Windows guard missing');
   if(!submit.includes('InputFrameSubmissionScope submissionScope((t_commonFrameSource || t_windowsMotionSubmission)?&frameSubmission:nullptr)'))throw Error('Window-only source must still establish final frame scope');
   if(source.includes('static WindowsSensorView inputCaptureWindowsSensorViewSnapshot()')){if(!producer.includes('windowsSensorView.submission')||!source.includes('result.capture=ymcc::windows_motion::retain(owner,generation);'))throw Error('Producer not tied to coherent source snapshot');}
   else if(!source.includes('const auto windowsMotionSensorOwner = g_inputCaptureSink;')||!source.includes('if (windowsMotionSensorOwner->gyroOk)'))throw Error('Stamp source not tied to actual retained snapshot owner');
   report.sourceBindings.push({symbol:'WindowsMotionProducer',code:undefined,sha256:sha(producer)},{symbol:'WindowsMotionFrameCopy',sha256:sha(copy)});
 }
 const assemblyStart=wired?source.indexOf('        if (!inputHostWindowsMotionSubmissionCurrent(frameSubmission)) {'):-1;
 const assembly=wired?source.slice(assemblyStart,closeBrace(source,source.indexOf('{',assemblyStart))+1):'';
 if(wired)report.sourceBindings.push({symbol:'WindowsMotionBeforeAssembly',sha256:sha(assembly)});
 const fixture=path.join(repo,'tools/fixtures/windows_motion_submission'),template=fs.readFileSync(path.join(fixture,'harness.cpp'),'utf8');
 const headers=['windows_motion_submission.h','input_frame_submission.h','virtual_output_status.h','legion_motion_source.h'];
 const inputs=[path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),runner,path.join(fixture,'harness.cpp'),path.join(fixture,'README.md'),...headers.map(n=>path.join(repo,'native',n)),path.join(repo,'deps/json/json.hpp')];
 report.inputs=inputs.map(p=>({path:p,sha256:sha(fs.readFileSync(p))}));
 report.source={path:sourcePath,normalizedSha256:sha(source),sourceLineEndingsNormalizedOnly:true};report.producerWired=wired;
 report.scope='Actual frame/request/last-issue functions, actual retained source header, source handoff/scope statements after integration. Ambient Host/Legion context and pipe write effects are memory seams; full BUS/pipe/Host and OS races not executed.';save();
 const generated=`static constexpr bool producerWired=${wired};\nstatic constexpr bool publicationWired=${source.includes("currentForOutput")};\n`+template.replace('// @@FRAME_TYPE@@',frame).replace('// @@WINDOWS_SCOPE@@',scope)
   .replace('// @@WINDOWS_HELPER@@',helper).replace('// @@GUARDS@@',guards).replace('// @@ORIGIN_COPY@@',copy).replace('// @@PRODUCER_SCOPE@@',producer).replace('// @@BEFORE_ASSEMBLY@@',assembly).replace('// @@PRODUCER_VIEW_BOUNDARY@@',producer.includes('windowsSensorView.submission')?'struct FixtureView {wm::Stamp submission;}; const FixtureView windowsSensorView{wm::retain(windowsMotionSensorOwner,gyroBindingGeneration)};':'');
 let vcvars=opt.vcvars;if(!vcvars){const p=path.join(process.env['ProgramFiles(x86)'],'Microsoft Visual Studio/Installer/vswhere.exe');const installation=need(run(p,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'vswhere.log'),'VS discovery').trim();vcvars=path.join(installation,'VC/Auxiliary/Build/vcvars64.bat');}
 if(!fs.existsSync(vcvars)||/["%\r\n]/.test(vcvars))throw Error('Invalid vcvars');const envFile=path.join(out,'environment.cmd');fs.writeFileSync(envFile,calibrationToolchainPreamble(vcvars)+'set\r\n');
 const prep=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',envFile],{windowsHide:true,encoding:'utf8',timeout:30000});if(prep.status!==0)throw Error('Compiler setup failed');
 const env={...process.env};for(const row of prep.stdout.split(/\r?\n/)){const i=row.indexOf('=');if(i>0){const key=row.slice(0,i),existing=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[existing||key]=row.slice(i+1);}}
 const headerBytes=Object.fromEntries(headers.map(n=>[n,fs.readFileSync(path.join(repo,'native',n),'utf8')]));
 const configs=[['Release',['/O2','/DNDEBUG','/MD']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd']]];
 function execute(label,cpp,hs,expectedFailure=false,mutant=false){
   const folder=path.join(out,label);fs.mkdirSync(folder,{recursive:true});fs.writeFileSync(path.join(folder,'test.cpp'),cpp);
   for(const[n,text]of Object.entries(hs))fs.writeFileSync(path.join(folder,n),text);
   const results=[];for(const[name,flags]of configs){const dir=path.join(folder,name);fs.mkdirSync(dir);const exe=path.join(dir,'test.exe');
    need(run('cl.exe',['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/permissive-',...flags,`/I${folder}`,`/I${path.join(repo,'deps/json')}`,path.join(folder,'test.cpp'),`/Fo${path.join(dir,'test.obj')}`,`/Fd${path.join(dir,'test.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'],`${label}/${name}/compile.log`,env,dir),'Compile '+label+'/'+name);
    const r=run(exe,[],`${label}/${name}/run.log`,env,dir);let d;try{d=JSON.parse(r.stdout.trim());}catch{throw Error('No JSON report '+label+'/'+name);}
    const oldFailure=d.failureGroups?.['source-stale-issue']>0&&Object.keys(d.failureGroups).every(g=>g.startsWith('source-stale')||g==='producer-missing'||g==='neutral-context');
    const accepted=expectedFailure?r.status===1&&d.status==='fail'&&(mutant||oldFailure):r.status===0&&d.status==='pass';
    results.push({name,accepted,exitCode:r.status,report:d,exeSha256:sha(fs.readFileSync(exe))});console.log(`${label}/${name}: ${accepted?(expectedFailure?'EXPECTED FAILURE':'PASS'):'FAIL'} (${d.checks} checks, ${d.failureCount} failures)`);if(!accepted)throw Error('Unexpected semantic result '+label+'/'+name);
   }return results;
 }
 report.configurations=execute('current',generated,headerBytes,!!opt['expected-before-failure']);save();
 if(opt['mutation-checks']){
   if(!wired||opt['expected-before-failure'])throw Error('Mutate integrated source only');
   const variants=[
    ['no-final-issue-guard',()=>({code:once(generated,'if (verdict==ymcc::input::frameSubmission::Verdict::Allowed &&\n        !inputHostWindowsMotionSubmissionCurrent(*frame)) return false;','if (false) return false;'),headers:headerBytes})],
    ['no-request-guard',()=>({code:once(generated,'if (verdict==ymcc::input::frameSubmission::Verdict::Allowed &&\n        !inputHostWindowsMotionSubmissionCurrent(frame)) {','if (false) {'),headers:headerBytes})],
    ['recapture-binding-at-submit',()=>({code:once(generated,'ymcc::windows_motion::retain(windowsMotionSensorOwner,gyroBindingGeneration)','ymcc::windows_motion::retain(windowsMotionSensorOwner,windowsMotionSensorOwner ? windowsMotionSensorOwner->activeBindingGeneration.load() : 0)'),headers:headerBytes})],
   ];
   for(const[name,make]of variants){const v=make();const configurations=execute('mutations/'+name,v.code,v.headers,true,true);report.mutations.push({name,detected:configurations.every(c=>c.accepted),configurations});save();}
 }
 const finish=fs.readFileSync(sourcePath,'utf8').replace(/\r\n/g,'\n');report.relevantSourceUnchangedAtFinish=[frame,...['inputHostFrameSubmissionMayIssue','inputHostFrameSubmissionRequestCurrent'].map(n=>extract(source,n).code),...(wired?[scope,helper,producer,copy,assembly]:[])].every(s=>finish.includes(s));
 report.inputsUnchangedAtFinish=report.inputs.every(i=>sha(fs.readFileSync(i.path))===i.sha256);if(!report.relevantSourceUnchangedAtFinish||!report.inputsUnchangedAtFinish)throw Error('Relevant source changed during test');
 report.status=opt['expected-before-failure']?'expected-before-failure':'pass';report.completedAt=new Date().toISOString();save();console.log('Evidence: '+out);
}catch(error){report.status='error';report.error=error.stack||String(error);save();throw error;}
