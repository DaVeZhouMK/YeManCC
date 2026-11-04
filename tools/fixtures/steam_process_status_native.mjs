import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';import {fileURLToPath,pathToFileURL} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url)),sha=b=>createHash('sha256').update(b).digest('hex');
const requireState=(value,message)=>{if(!value)throw Error(message);};
const within=(base,file)=>{const p=path.relative(base,file);return p===''||(!path.isAbsolute(p)&&p!=='..'&&!p.startsWith('..'+path.sep));};
export function extractProcessStatus(main){const a=main.indexOf('    // ── proc.running：');requireState(a>=0,'MISSING_NATIVE_STATUS_ANCHOR');const z=main.indexOf('    // Exact executable lookup used by Fan Host startup recovery.',a);requireState(z>a,'MISSING_NATIVE_STATUS_BOUNDARY');return main.slice(a,z);}
export async function prepareProcessStatus({repoRoot,ownedRoot,outputDir,afterHandler}={}){
 repoRoot=fs.realpathSync(repoRoot);ownedRoot=fs.realpathSync(ownedRoot);const out=path.resolve(outputDir);requireState(within(ownedRoot,out)&&!within(repoRoot,out)&&!fs.existsSync(out),'FRESH_OWNED_OUTPUT_REQUIRED');
 const mainFile=path.join(repoRoot,'native/main.cpp'),main=fs.readFileSync(mainFile,'utf8'),a=main.indexOf('static std::string W2U('),z=main.indexOf('// 控制台子进程',a);requireState(a>=0&&z>a,'MISSING_VERBATIM_UTF8_HELPERS');const helpers=main.slice(a,z);
 const beforePath=path.join(here,'steam_process_status_before.inc'),beforeRaw=fs.readFileSync(beforePath),before=beforeRaw.toString('utf8');
 const frozenSha='f02b1f3cbb4b543f58229bd4cc4026c2a7baed893590fb2279c3263a6a57aef6';requireState(sha(beforeRaw)===frozenSha,'FROZEN_BEFORE_CHANGED');
 let after=extractProcessStatus(main);if(afterHandler){afterHandler=fs.realpathSync(afterHandler);requireState(within(ownedRoot,afterHandler),'PRIVATE_HANDLER_ROOT_REQUIRED');after=fs.readFileSync(afterHandler,'utf8');}
 requireState(after.includes('processInventory::read')&&after.includes('PROCESS_INVENTORY_UNAVAILABLE')&&!after.includes('CreateToolhelp32Snapshot'),'NEW_ACTUAL_OWNER_HANDLER_REQUIRED');
 const wrap=(ns,source)=>`namespace ${ns} {\nstatic std::function<json(const json&)> handler;\nstatic void ipc_on(const char* name,std::function<json(const json&)> fn){if(std::string(name)!="proc.running")throw std::runtime_error("unexpected native handler");handler=std::move(fn);}\nstatic void registration(){\n${source}\n}\n}\n`;
 const sources=helpers+wrap('proc_before',before)+wrap('proc_after',after),cpp=path.join(here,'steam_process_status_native.cpp'),ports=path.resolve(here,'process_inventory_ports.h');
 const {calibrationToolchainPreamble}=await import(pathToFileURL(path.join(repoRoot,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')));
 const toolsRoot=path.resolve(here,'../..'),batch=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+`set "VSLANG=1033"\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /Od /showIncludes /I"${toolsRoot}" /I"${repoRoot}" "fixture.cpp" /Fo"fixture.obj" /Fe"fixture.exe"\r\nexit /b %errorlevel%\r\n`;
 fs.mkdirSync(out);fs.writeFileSync(path.join(out,'fixture.cpp'),fs.readFileSync(cpp));fs.writeFileSync(path.join(out,'handlers.inc'),sources);fs.writeFileSync(path.join(out,'compile.cmd'),batch);
 const inputFiles=[cpp,ports,beforePath,fileURLToPath(import.meta.url),path.join(repoRoot,'native/process_inventory.h'),path.join(repoRoot,'native/fxsound_recovery_policy.h'),path.join(repoRoot,'deps/json/json.hpp'),path.join(repoRoot,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),...(afterHandler?[afterHandler]:[]),...['fixture.cpp','handlers.inc','compile.cmd'].map(f=>path.join(out,f))];
 const inputPins=inputFiles.map(file=>({file,bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))}));
 const prepared={schemaVersion:1,status:'PREPARED_NOT_EXECUTED',repoRoot,ownedRoot,out,inputPins,mainCapture:{file:mainFile,rawSha256:sha(Buffer.from(main)),helpersSha256:sha(Buffer.from(helpers)),handlerSha256:sha(Buffer.from(after)),scope:'Exact captured UTF helpers/native handler; unrelated whole-main changes are not inferred to change this TU'},commands:[{stage:'compile',program:process.env.ComSpec||'C:/Windows/System32/cmd.exe',args:['/d','/c',path.join(out,'compile.cmd')],expected:0},{stage:'before',program:path.join(out,'fixture.exe'),args:['--before'],expected:1},{stage:'after',program:path.join(out,'fixture.exe'),args:['--after'],expected:0}],realProcessQueries:false,fullProductTU:false};
 fs.writeFileSync(path.join(out,'prepare.json'),JSON.stringify(prepared,null,2)+'\n');return prepared;
}
export async function runProcessStatus(prepared,{ownerJob,runProcess}={}){
 requireState(ownerJob?.budgetMs===20000&&typeof ownerJob.remainingMs==='function','EXISTING_20S_OWNER_JOB_REQUIRED');const results=[];
 for(const command of prepared.commands){const result=await runProcess(command.program,command.args,{cwd:prepared.out,env:process.env,timeoutMs:ownerJob.remainingMs()});fs.writeFileSync(path.join(prepared.out,command.stage+'.stdout.log'),result.stdout||'');fs.writeFileSync(path.join(prepared.out,command.stage+'.stderr.log'),result.stderr||'');fs.writeFileSync(path.join(prepared.out,command.stage+'.process.json'),JSON.stringify(result,null,2)+'\n');results.push({stage:command.stage,...result});requireState(!result.timedOut&&!result.launchError&&!result.outputTruncated&&result.exitCode===command.expected,'NATIVE_STAGE_'+command.stage+'_FAILED');if(command.stage==='before')requireState((result.stderr||'').includes('BEFORE_UNKNOWN_REPORTED_ABSENT:'),'BEFORE_NOT_SEMANTIC_FAILURE');}
 for(const pin of prepared.inputPins)requireState(sha(fs.readFileSync(pin.file))===pin.sha256,'ACTUAL_SOURCE_DRIFT '+pin.file);
 const result={schemaVersion:1,status:'PASS_NATIVE_STATUS_SELECTED',actualOwner:true,actualHandler:true,results,inputPins:prepared.inputPins,fullProductTU:false,realProcessQueries:false,sourceClosureComplete:false};fs.writeFileSync(path.join(prepared.out,'result.json'),JSON.stringify(result,null,2)+'\n');return result;
}
export async function processStatusNativeCli(argv){
 const values={};const keys={'--owned-root':'ownedRoot','--out':'outputDir','--after-handler':'afterHandler','--budget-context':'budgetContext','--budget-nonce':'budgetNonce'};
 for(let i=0;i<argv.length;i++){if(argv[i]==='--process-status-native-only')continue;const key=keys[argv[i]];requireState(key&&argv[i+1]!==undefined,'UNEXPECTED_NATIVE_SELECTION_ARGUMENT');values[key]=argv[++i];}
 const repoRoot=fs.realpathSync(process.cwd());requireState(values.budgetContext&&values.budgetNonce,'INHERITED_BUDGET_CONTEXT_REQUIRED');
 const {readBudgetWorker}=await import(pathToFileURL(path.join(repoRoot,'tools/ymcc_sim/optimization/execution-budget.mjs')));
 const ownerJob=readBudgetWorker(values.budgetContext,values.budgetNonce,{repoRoot,coreRoot:values.ownedRoot});
 const {runProcess}=await import(pathToFileURL(path.join(repoRoot,'tools/ymcc_sim/global/runner.mjs')));
 const prepared=await prepareProcessStatus({repoRoot,...values});const result=await runProcessStatus(prepared,{ownerJob,runProcess});console.log(JSON.stringify({status:result.status,out:prepared.out}));
}
