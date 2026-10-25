// Actual production OEM consumers against memory IO boundaries. No Host or devices.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';import {spawnSync} from 'node:child_process';
import {generateOemConsumers} from './fixtures/unified_oem_input/generate.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
if(args.length!==2||args[0]!=='--out')throw Error('Usage: --out fresh-evidence-directory');
const out=path.resolve(args[1]),rel=path.relative(repo,out);if(!rel||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep)))throw Error('EVIDENCE_OUTSIDE_REPO_REQUIRED');
if(fs.existsSync(out))throw Error('FRESH_EVIDENCE_REQUIRED');fs.mkdirSync(out,{recursive:true});
const sha=bytes=>createHash('sha256').update(bytes).digest('hex'),write=(p,x)=>fs.writeFileSync(p,JSON.stringify(x,null,2)+'\n');
const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
const environmentFile=path.join(out,'compiler-env.cmd');fs.writeFileSync(environmentFile,calibrationToolchainPreamble(vcvars)+'set PATH\r\nset INCLUDE\r\nset LIB\r\nexit /b 0\r\n');
const envRun=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',environmentFile],{windowsHide:true,encoding:'utf8',timeout:30000});if(envRun.status!==0)throw Error('INSTALLED_COMPILER_SETUP_FAILED');
const compilerEnv={...process.env};for(const line of envRun.stdout.split(/\r?\n/)){const i=line.indexOf('=');if(i>0){const key=line.slice(0,i),current=Object.keys(compilerEnv).find(k=>k.toLowerCase()===key.toLowerCase());compilerEnv[current||key]=line.slice(i+1);}}
const baselinePath=path.join(out,'positive'),baseline=generateOemConsumers({repo,out:baselinePath}),results=[];
const original=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8');
function change(source,before,after){if(!source.includes(before)&&before.includes('\n')){before=before.replaceAll('\n','\r\n');after=after.replaceAll('\n','\r\n');}if(source.split(before).length!==2)throw Error('MUTATION_ANCHOR_NOT_UNIQUE:'+before);return source.replace(before,()=>after);}
function execute(name,root,configuration,expectedFailure){
 const flags=configuration==='Release'?['/O2','/DNDEBUG','/MD']:['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd'];
 const target=path.join(root,configuration);fs.mkdirSync(target,{recursive:true});const exe=path.join(target,'consumer.exe');
 const build=spawnSync('cl.exe',['/nologo','/std:c++17','/EHsc','/utf-8','/W4',...flags,path.join(root,'actual-oem-consumers.cpp'),'/Fo'+path.join(target,'consumer.obj'),'/Fd'+path.join(target,'consumer.pdb'),'/Fe'+exe,'/link','/INCREMENTAL:NO'],{cwd:target,env:compilerEnv,windowsHide:true,encoding:'utf8',timeout:90000,maxBuffer:8*1024*1024});
 fs.writeFileSync(path.join(target,'compile.log'),(build.stdout||'')+(build.stderr||'')+(build.error?.message||''));if(build.status!==0)throw Error('COMPILATION_FAILED_NOT_SEMANTIC:'+name+':'+configuration);
 const run=spawnSync(exe,[],{cwd:target,env:compilerEnv,windowsHide:true,encoding:'utf8',timeout:30000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(target,'run.log'),(run.stdout||'')+(run.stderr||''));
 let report;try{report=JSON.parse(run.stdout.trim());}catch{throw Error('CONSUMER_TERMINATION_SUMMARY_MISSING:'+name);}
 const valid=expectedFailure?run.status===1&&report.status==='FAIL'&&report.error===expectedFailure:run.status===0&&report.status==='PASS'&&report.completeProductionGate&&report.productionQuery&&report.productionHook&&report.selections===baseline.selections&&report.hardwareIo===false;
 if(!valid)throw Error('CONSUMER_ASSERTION_NOT_EXPECTED:'+name+':'+JSON.stringify(report));
 const result={name,configuration,compileExitCode:build.status,exitCode:run.status,status:expectedFailure?'EXPECTED_REJECTION':'PASS',report,executableSha256:sha(fs.readFileSync(exe))};results.push(result);return result;
}
try{
 for(const config of ['Release','Checked'])execute('current',baselinePath,config);
 const faults=[
  ['discard-unified-profile','return oemUnifiedInputBindingFor(family, cls).profileId;','return static_cast<int>(OemChordProfileId::None);','full gate consumes unified canonical profile'],
  ['raw-alias-capture-predicate','const std::string consumerClass = binding.model ? std::string(binding.model->consumerClass) : g_machineIdentity.deviceClass;','const std::string consumerClass = g_machineIdentity.deviceClass;','query aliases retain complete canonical capture predicates and routes'],
  ['lost-profile-epoch','g_oemShortcutEdgeEpoch.fetch_add(1, std::memory_order_acq_rel);\n    const bool chordCapable','g_oemShortcutEdgeEpoch.fetch_add(0, std::memory_order_acq_rel);\n    const bool chordCapable','full gate keeps profile-change epoch'],
  ['cached-live-interface','const bool gpdIfaceReady = g_gpdBackIfaceReady.load(std::memory_order_acquire);','const bool gpdIfaceReady = true;','same cached model must recheck live GPD interface'],
  ['release-other-source','g_oemKbDownRaw.store(0, std::memory_order_release);\n    oemBackHookRecompute();','g_oemKbDownRaw.store(0, std::memory_order_release);\n    g_oemKbDown = 0;\n    oemBackHookRecompute();','raw source return preserves hook lane'],
  ['bypass-pressed-consumer','const OemCatalogRef catalog = oemBoundCatalogForFamily(g_machineIdentity.family, g_machineIdentity.deviceClass);','const OemCatalogRef catalog{nullptr,0};','pressed enumeration reaches every original canonical key'],
 ];
 for(const [name,before,after,expected]of faults){const root=path.join(out,name),snapshot=path.join(out,name+'.cpp');fs.writeFileSync(snapshot,change(original,before,after));generateOemConsumers({repo,out:root,sourcePath:snapshot});for(const config of ['Release','Checked'])execute(name,root,config,expected);}
 const sourceIdentity=baseline.inputs.map(row=>({...row,currentSha256:sha(fs.readFileSync(row.path))}));if(sourceIdentity.some(r=>r.sha256!==r.currentSha256))throw Error('SOURCE_CHANGED_DURING_RUN');
 const outputTree=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(entry=>{if(entry.isSymbolicLink())throw Error('OUTPUT_LINK_NOT_ADMITTED');const file=path.join(dir,entry.name);return entry.isDirectory()?outputTree(file):[file];});
 const sourceHashes=[...sourceIdentity.map(r=>({path:r.file,sha256:r.sha256})),...outputTree(out).map(file=>({path:file,sha256:sha(fs.readFileSync(file))}))];
 const result={schemaVersion:1,status:'PASS',sourceHashes,generatedAt:new Date().toISOString(),hardwareIo:false,registeredProductConsumers:true,sourceIdentity,scope:baseline.scope,results,summary:{configurations:2,selectionsPerConfiguration:baseline.selections,checks:results.filter(r=>r.name==='current').map(r=>r.report.checks),compiledFaults:faults.length,negativeExecutions:faults.length*2},limitations:['Hook installation, registry/device identity and log sinks are memory IO boundaries.','No full native binary, Host startup, hardware, Windows sleep or physical source transport acceptance.']};write(path.join(out,'result.json'),result);console.log(JSON.stringify({status:result.status,summary:result.summary}));console.log('OEM_INPUT_CONSUMERS_COMPLETED configurations=2 faults='+faults.length);
}catch(error){write(path.join(out,'result.json'),{status:'FAIL',error:error.message,results,hardwareIo:false});console.error(error.message);process.exitCode=1;}
