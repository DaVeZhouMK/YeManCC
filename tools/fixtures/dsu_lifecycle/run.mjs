// Exact current DSU source, installed toolchain, localhost only. Every invocation
// creates a new evidence subdirectory; failures and mutation logs are retained.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash,randomUUID} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {calibrationToolchainPreamble} from '../hc_reviewed_calibration/toolchain.mjs';
import {toolInputs} from '../../ymcc_sim/global/controller-signal-evidence/adapter.mjs';
import {consumerEnvironment} from '../../ymcc_sim/global/controller-signal-evidence/environment.mjs';
import {createOwnedWorkspace} from '../../ymcc_sim/global/controller-signal-evidence/owned-workspace.mjs';
const here=path.dirname(fileURLToPath(import.meta.url)),repo=path.resolve(here,'../../..');
const toolProbe=process.argv.includes('--tool-probe');
const positional=process.argv.slice(2).filter(v=>v!=='--tool-probe');
if(positional.length>1||positional.some(v=>v.startsWith('--')))throw Error('Expected output directory and optional --tool-probe');
const root=path.resolve(positional[0]||'G:/YeManCC-Work/Testing/YMCC/Evidence/VC-DsuOwner23');
const out=path.join(root,new Date().toISOString().replace(/[:.]/g,'-')+'-'+randomUUID().slice(0,8));
fs.mkdirSync(out,{recursive:true});
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const source=path.join(repo,'native/dsu_server.cpp'),fixture=path.join(here,'native.cpp');
const result={schema:1,status:'RUNNING',at:new Date().toISOString(),repo,evidence:out,sources:[],runs:[],limitations:[
    'Existing std::thread::join has no arbitrary blocking deadline. Queries can wait behind lifecycleMx and that join.',
    'Software-only DSU resource ownership proof. No main.cpp typed capture receipt integration, full app, installation, devices, drivers, services, real sleep or export.',
    'Private API failures are injected before calling real valid resources; no invalid real handle operations.',
    'Thread construction exception unwind is compiled but not forced. A hung worker or failed stop-event signal has no bounded-stop proof.',
    'Existing pinned HC motion/oracle runner is not rebuilt, repinned or replaced; wire, validator, 125Hz and mode policy acceptance are not claimed.'
]};
const receiptName=toolProbe?'TOOL-PROBE.json':'RESULT.json';
const save=()=>fs.writeFileSync(path.join(out,receiptName),JSON.stringify(result,null,2)+'\n');
function identity(p){const bytes=fs.readFileSync(p);return {path:p,bytes:bytes.length,sha256:sha(bytes)};}
const visited=new Set();
function bind(p){p=path.resolve(p);if(visited.has(p))return;visited.add(p);result.sources.push(identity(p));const text=fs.readFileSync(p,'utf8');for(const match of text.matchAll(/^\s*#include\s+"([^"\r\n]+)"/gm)){const nested=path.resolve(path.dirname(p),match[1]);if(fs.existsSync(nested))bind(nested);}}
function invoke(exe,args,cwd,log,env=process.env,timeout=60000){
    const started=Date.now(),r=spawnSync(exe,args,{cwd,env,windowsHide:true,timeout,maxBuffer:8*1024*1024});
    const bytes=Buffer.concat([r.stdout||Buffer.alloc(0),r.stderr||Buffer.alloc(0),Buffer.from(r.error?.stack||'')]);
    const rawLog=log+'.raw';fs.writeFileSync(rawLog,bytes);
    let text,outputEncoding='utf-8';
    try{text=new TextDecoder('utf-8',{fatal:true}).decode(bytes);}catch{outputEncoding='gbk';text=new TextDecoder('gbk').decode(bytes);}
    fs.writeFileSync(log,text);
    return {exe,args,cwd,exitCode:r.status,signal:r.signal,error:r.error?.message||null,elapsedMs:Date.now()-started,log,rawLog,outputEncoding,text};
}
function requireSuccess(run){if(run.exitCode!==0||run.error)throw Error('Command failed; retained log: '+run.log);}
try{
    bind(source);bind(path.join(repo,'native/dsu_server.h'));bind(fixture);bind(fileURLToPath(import.meta.url));
    bind(path.resolve(here,'../hc_reviewed_calibration/toolchain.mjs'));
    const tools=toolInputs(repo),primary=tools.primary;
    const workspace=createOwnedWorkspace({repo,output:out,primary}),owned=workspace.allocate('dsu-tools');
    const cleanEnv={...consumerEnvironment({ambient:process.env,tools,out,nodeExecutable:process.execPath}),TEMP:owned.temp,TMP:owned.temp};
    const toolIdentities=()=>Object.entries(primary).map(([role,p])=>{
        const actual=identity(p),declared=tools.files.find(v=>path.resolve(v.path).toLowerCase()===path.resolve(p).toLowerCase());
        if(!declared||actual.sha256!==declared.sha256)throw Error('DSU_PRIMARY_TOOL_IDENTITY:'+role);
        return {role,...actual};
    });
    result.primaryToolsBefore=toolIdentities();result.ownedWorkspace=owned;
    const head=invoke(primary.git,['rev-parse','HEAD'],repo,path.join(out,'git-head.log'),cleanEnv,10000);requireSuccess(head);
    result.gitProbe={...head,text:undefined};result.head=head.text.trim();
    if(!/^[a-f0-9]{40,64}$/.test(result.head))throw Error('DSU_GIT_HEAD_INVALID');save();
    const vcvars=primary.vcvars;
    const environmentFile=path.join(out,'environment.cmd');fs.writeFileSync(environmentFile,calibrationToolchainPreamble(vcvars)+'set\r\n');
    const configured=invoke(primary.commandProcessor,['/d','/c',environmentFile],out,path.join(out,'environment.log'),cleanEnv);requireSuccess(configured);
    const env={};for(const line of configured.text.split(/\r?\n/)){const i=line.indexOf('=');if(i>0)env[line.slice(0,i)]=line.slice(i+1);}
    env.VSLANG='1033';
    const selected=path.join(env.VCToolsInstallDir||'','bin/Hostx64/x64/cl.exe');
    if(path.resolve(selected).toLowerCase()!==path.resolve(primary.compiler).toLowerCase())throw Error('DSU_COMPILER_ENVIRONMENT_IDENTITY');
    if(env.WindowsSDKVersion.replace(/[\\/]$/,'')!==tools.sdkVersion)throw Error('DSU_SDK_ENVIRONMENT_IDENTITY');
    env.INCLUDE=tools.native.include.join(';');env.LIB=tools.native.lib.join(';');
    const toolchain=invoke(primary.compiler,['/Bv'],out,path.join(out,'toolchain.log'),env);
    if(toolchain.error||toolchain.signal||![0,2].includes(toolchain.exitCode)||!toolchain.text.toLowerCase().includes(primary.compiler.toLowerCase()))throw Error('DSU_COMPILER_VERSION_PROBE_FAILED');
    result.toolchain={vcvars,log:toolchain.log,environmentLog:configured.log,primaryTools:primary};
    if(toolProbe){
        result.probeOnly=true;result.productionAcceptance=false;
        const probe=path.join(repo,'tools/ymcc_sim/global/controller-signal-evidence/tool-probe.cpp');bind(probe);
        const cpp=path.join(owned.directory,'p.cpp'),obj=path.join(owned.directory,'p.obj'),exe=path.join(owned.directory,'p.exe');fs.copyFileSync(probe,cpp);
        const compile=invoke(primary.compiler,['/nologo','/std:c++17','/EHsc','/MT','/O2',cpp,'/Fo'+obj,'/Fe'+exe],owned.directory,path.join(out,'probe-build.log'),env);requireSuccess(compile);
        const runtime=invoke(exe,[],owned.directory,path.join(out,'probe-run.log'),env,10000);requireSuccess(runtime);
        if(runtime.text.trim()!=='PRIMARY_TOOL_PROBE_PASS')throw Error('DSU_COMPILER_RUNTIME_PROBE_FAILED');
        result.probe={compile:{...compile,text:undefined},runtime:{...runtime,text:undefined},binary:identity(exe),temp:owned.temp};
    }else{
    const base=fs.readFileSync(source,'utf8').replace(/\r\n/g,'\n');
    const variants=[
        {name:'public-Debug',configuration:'Debug',public:true},
        {name:'public-Release',configuration:'Release',public:true},
        {name:'private-Debug',configuration:'Debug'},
        {name:'private-Release',configuration:'Release'},
        {name:'negative-running-only',configuration:'Debug',needle:'return !running.load(std::memory_order_acquire)&&!serverThread.joinable()&&resourcesClosed();',replacement:'return !running.load(std::memory_order_acquire);',expectedCase:'running-false-with-exited-unjoined-thread'},
        {name:'negative-discard-socket-owner',configuration:'Debug',needle:'if(serverSocket!=INVALID_SOCKET&&closesocket(serverSocket)==0)serverSocket=INVALID_SOCKET;',replacement:'if(serverSocket!=INVALID_SOCKET){closesocket(serverSocket);serverSocket=INVALID_SOCKET;}',expectedCase:'stop-failure-retains-closesocket'}
    ];
    for(const variant of variants){
        const dir=path.join(out,variant.name);fs.mkdirSync(dir);const exe=path.join(dir,'consumer.exe');
        let inputs=[fixture];const defines=['/DYMCC_DSU_TEST'];
        if(variant.public){inputs.push(source);defines.push('/DYMCC_DSU_PUBLIC_CONSUMER');}
        if(variant.needle){
            if(base.split(variant.needle).length!==2)throw Error('Mutation must match current source exactly once: '+variant.name);
            const mutant=path.join(dir,'dsu_server.cpp');fs.writeFileSync(mutant,base.replace(variant.needle,variant.replacement));
            const wrapper=path.join(dir,'consumer.cpp');fs.writeFileSync(wrapper,'#define YMCC_DSU_IMPLEMENTATION '+JSON.stringify(mutant.replaceAll('\\','/'))+'\n#include '+JSON.stringify(fixture.replaceAll('\\','/'))+'\n');inputs=[wrapper];
            variant.mutation={source:identity(mutant),needle:variant.needle,replacement:variant.replacement};
        }
        const flags=variant.configuration==='Debug'?['/Od','/Zi','/MDd']:['/O2','/MD','/DNDEBUG'];
        const args=['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/permissive-',...flags,...defines,'/I'+path.join(repo,'native'),...inputs,'/Fo'+dir+path.sep,'/Fd'+path.join(dir,'consumer.pdb'),'/Fe'+exe,'/link','ws2_32.lib','/INCREMENTAL:NO'];
        const compile=invoke(primary.compiler,args,dir,path.join(dir,'compile.log'),env);const row={name:variant.name,configuration:variant.configuration,expected:variant.expectedCase?'ASSERTION_FAILURE':'PASS',compile:{...compile,text:undefined},mutation:variant.mutation};result.runs.push(row);save();requireSuccess(compile);
        row.binary=identity(exe);const runtime=invoke(exe,[],dir,path.join(dir,'run.log'),env,20000);row.runtime={...runtime,text:undefined};
        row.events=runtime.text.split(/\r?\n/).filter(line=>line.startsWith('{')).map(line=>JSON.parse(line));
        if(variant.expectedCase){
            const failed=row.events.find(event=>event.status==='FAIL');
            row.accepted=runtime.exitCode===1&&!runtime.error&&failed?.case===variant.expectedCase&&row.events.some(event=>event.rescueOwnedResourcesReleased===true);
        }else row.accepted=runtime.exitCode===0&&!runtime.error&&row.events.at(-1)?.status==='PASS';
        save();if(!row.accepted)throw Error('Unexpected consumer result; retained log: '+runtime.log);
        console.log(JSON.stringify({variant:row.name,accepted:row.accepted,summary:row.events.findLast(event=>event.status==='PASS'||event.status==='FAIL'),elapsedMs:runtime.elapsedMs}));
    }
    }
    result.primaryToolsAfter=toolIdentities();workspace.verify(owned);
    result.sourceIdentityAfter=result.sources.map(source=>({...identity(source.path),unchanged:identity(source.path).sha256===source.sha256}));
    if(result.sourceIdentityAfter.some(source=>!source.unchanged))throw Error('Source changed during test; no acceptance');
    result.status=toolProbe?'TOOL_PROBE_PASS':'PASS';
}catch(error){result.status='FAIL';result.error=error.stack;process.exitCode=1;}
result.completedAt=new Date().toISOString();save();console.log(JSON.stringify({status:result.status,result:path.join(out,receiptName),error:result.error}));
