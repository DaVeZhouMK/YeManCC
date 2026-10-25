import {resolveTdpToolchain,beginTdpScope,observeTdpInput,tdpLogicalInput,finishTdpScope,writeTdpOutputEvidence} from '../tdp-consumer-signals/tool-scope.mjs';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {executeOriginalBundle} from './vm-io.mjs';
import {MODULE,REPO,CASE_ID,moduleSources} from './adapter.mjs';
const digest=data=>crypto.createHash('sha256').update(data).digest('hex');
const hash=file=>digest(fs.readFileSync(file));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,data)=>fs.writeFileSync(file,JSON.stringify(data,null,2)+'\n');
const need=(condition,code)=>{if(!condition)throw Error(code);};
const inside=(root,target)=>{const rel=path.relative(root,target);return rel===''||(rel!=='..'&&!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel));};
function sourceFile(root,relative){need(typeof relative==='string'&&!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'),'TDP_SOURCE_PATH_INVALID');const actual=fs.realpathSync(path.join(root,relative));need(inside(fs.realpathSync(root),actual),'TDP_SOURCE_OUTSIDE_REPOSITORY');return actual;}
export function parseArgs(args){need(args.length===2&&args[0]==='--evidence-dir'&&typeof args[1]==='string'&&path.isAbsolute(args[1])&&!/[\0\r\n]/.test(args[1]),'TDP_ARGUMENTS_INVALID');const root=path.resolve(args[1]);need(!inside(REPO,root),'TDP_EVIDENCE_IN_SOURCE');need(!fs.existsSync(root),'TDP_FRESH_EVIDENCE_REQUIRED');return {root};}
export function verifyOriginalExecution(execution,spec,{negative=false}={}){
 need(execution&&execution.thrown===null&&execution.processExitObserved===true,'TDP_SCHEDULE_MAIN_DID_NOT_COMPLETE');
 need(execution.boundary&&execution.boundary.denied.length===0&&execution.boundary.processAttempts===0&&execution.boundary.networkAttempts===0,'TDP_SCHEDULE_IO_BOUNDARY_VIOLATION');
 const markers=spec.expectedMarkers.filter(marker=>execution.logs.includes(marker));
 if(negative)need(execution.exitCode===1&&markers.length===0&&execution.logs.some(line=>line.includes(spec.fault.expectedFailedCase)),'TDP_SCHEDULE_ORIGINAL_ASSERTION_NOT_CAUGHT');
 else need(execution.exitCode===0&&markers.length===spec.expectedMarkers.length,'TDP_SCHEDULE_ORIGINAL_NOT_PASS');
 return {completedMarkers:markers,failedNames:negative?[spec.fault.expectedFailedCase]:[],originalExitObserved:true,forbiddenProcessAttempts:0,networkRequests:0,virtualizedDefaultStorageCalls:execution.boundary.virtualizedCalls};
}
function walk(root){const files=[];function visit(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);need(!entry.isSymbolicLink(),'TDP_OUTPUT_REPARSE');if(entry.isDirectory())visit(file);else if(entry.isFile())files.push(file);else throw Error('TDP_OUTPUT_NONFILE');need(files.length<=1500,'TDP_OUTPUT_COUNT_LIMIT');}}visit(root);return files.sort();}
export async function runTdpConsumer(root,{repoRoot=REPO}={}){
 need(path.isAbsolute(root)&&!inside(repoRoot,root)&&!fs.existsSync(root),'TDP_FRESH_EXTERNAL_EVIDENCE_REQUIRED');
 const spec=read(path.join(repoRoot,MODULE,'spec.json')),tool=resolveTdpToolchain(repoRoot,read(path.join(repoRoot,MODULE,'toolchain-inputs.json')));
 need(spec.schemaVersion===1&&spec.expectedMarkers.length===3&&tool.schemaVersion===1&&tool.files.length===5,'TDP_PROFILE_INVALID');
 need(fs.realpathSync(process.execPath)===fs.realpathSync(tool.nodeExecutable),'TDP_NODE_IDENTITY');
 for(const file of tool.files)need(hash(file.path)===file.sha256,'TDP_TOOLCHAIN_IDENTITY');
 const scope=beginTdpScope(repoRoot,spec.sourceInputs,tool);
 const inputs=[...new Set([...moduleSources(),...scope.sources])].sort();
 const snapshot=inputs.map(relative=>({path:relative,sha256:hash(sourceFile(repoRoot,relative))}));
 fs.mkdirSync(root,{recursive:true});
 const frozen=new Map();
 for(const row of snapshot){const bytes=fs.readFileSync(sourceFile(repoRoot,row.path));need(digest(bytes)===row.sha256,'TDP_SOURCE_CHANGED_DURING_SNAPSHOT');const copy=path.join(root,'input-sources',row.path);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);frozen.set(fs.realpathSync(path.join(repoRoot,row.path)),{...row,bytes,copy});}
 const require=createRequire(path.join(repoRoot,'package.json'));let esbuild;const previousBinary=process.env.ESBUILD_BINARY_PATH;
 try{
  process.env.ESBUILD_BINARY_PATH=tool.esbuildExecutable;
  esbuild=require('esbuild');need(esbuild.version===tool.esbuildVersion,'TDP_COMPILER_VERSION');
  const executed=[];
  async function execute(label,negative){
   const dir=path.join(root,label);fs.mkdirSync(dir);
   let faultSource=null;
   const result=await esbuild.build({absWorkingDir:repoRoot,entryPoints:[spec.script],bundle:true,write:false,platform:'node',format:'cjs',target:'node22',metafile:true,logLevel:'silent',define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'frozen-original-inputs',setup(build){build.onLoad({filter:/\.(?:[cm]?js|tsx?|json)$/},args=>{
    const original=frozen.get(fs.realpathSync(args.path));const bytes=original?.bytes??fs.readFileSync(args.path);const classified=observeTdpInput(scope,args.path,bytes,{label});need(original||classified.role==='tool','TDP_UNREVIEWED_BUILD_SOURCE:'+args.path);let contents=bytes.toString('utf8');
    if(negative&&original?.path===spec.fault.path){need(contents.split(spec.fault.before).length===2,'TDP_FAULT_ANCHOR_CHANGED');contents=contents.replace(spec.fault.before,spec.fault.after);faultSource=path.join(dir,'fault-source.ts');fs.writeFileSync(faultSource,contents);scope.reads.at(-1).faultOutput=faultSource;scope.reads.at(-1).executedBytesSha256=hash(faultSource);}
    return {contents,loader:args.path.endsWith('.json')?'json':args.path.endsWith('.tsx')?'tsx':args.path.endsWith('.ts')?'ts':'js',resolveDir:path.dirname(args.path)};
   });}}]});
   need(result.outputFiles.length===1,'TDP_COMPILER_OUTPUT_COUNT');
   const actual=Object.keys(result.metafile.inputs).map(file=>tdpLogicalInput(scope,path.resolve(repoRoot,file))).sort();
   need(JSON.stringify(actual)===JSON.stringify([...spec.buildInputs].sort()),'TDP_EXECUTED_IMPORT_GRAPH_CHANGED');
   const program=path.join(dir,'program.cjs'),metafile=path.join(dir,'metafile.json');fs.writeFileSync(program,result.outputFiles[0].contents);write(metafile,result.metafile);
   const execution=await executeOriginalBundle(program,path.join(dir,'scratch'));
   write(path.join(dir,'execution.json'),{...execution,report:undefined});fs.writeFileSync(path.join(dir,'stdout.log'),execution.logs.join('\n')+'\n');
   const verified=verifyOriginalExecution(execution,spec,{negative});
   need(!negative||faultSource,'TDP_FAULT_SOURCE_NOT_EXECUTED');
   const record={label,...verified,program,programSha256:hash(program),metafile,metafileSha256:hash(metafile),faultSource,faultSourceSha256:faultSource?hash(faultSource):null,originalScriptSha256:snapshot.find(row=>row.path===spec.script).sha256};
   executed.push(record);return record;
  }
  const positive=await execute('positive',false),negative=await execute('negative-tdp-feedback',true);
  const drift=snapshot.filter(row=>hash(sourceFile(repoRoot,row.path))!==row.sha256||hash(path.join(root,'input-sources',row.path))!==row.sha256);
  need(drift.length===0,'TDP_SOURCE_CHANGED_DURING_RUN');for(const file of tool.files)need(hash(file.path)===file.sha256,'TDP_TOOLCHAIN_CHANGED_DURING_RUN');
  write(path.join(root,'actual-fault.json'),{path:spec.fault.path,before:spec.fault.before,after:spec.fault.after,expectedFailedCase:spec.fault.expectedFailedCase,actualFailedNames:negative.failedNames,originalAssertionsUnchanged:true,currentSourceDeliberateFault:true,historicalFailure:false});
  write(path.join(root,'tool-scope.json'),finishTdpScope(scope));
  const sourceHashes=[...snapshot];
  const report={schemaVersion:1,status:'PASS',passed:true,id:CASE_ID,kind:'tdp-original-schedule-consumer',positive,negative,sourceHashes,executedProductModules:['src/bridge/performanceSchedule.ts','src/bridge/autofloat.ts','src/bridge/cpuProfiles.ts','src/bridge/yeman.ts','src/bridge/settingsRepository.ts','src/bridge/frameRateLimits.ts'],inputConsumerStateFeedback:{input:'Unchanged original CPU/TDP schedule suite and original in-memory IPC responders',consumer:'Actual schedule, automatic CPU bank, settings ownership and TDP consumers',state:'Original AC/DC ownership, optional TDP failure, G3E range and CPU isolation assertions',feedback:'Original three completion markers and actual caught original assertion on the mutated consumer'},sourceChanges:[],compatibility:{format:'CommonJS with unchanged original TS source',newHcVersionAssumed:false,totalOemCatalogBinding:'NOT_CLAIMED'},boundary:{ipc:'Unchanged original in-memory responders',filesystem:'Fresh scratch plus explicit default-install namespace mapped into that same scratch; no installed/user file access',externalProcesses:0,networkRequests:0,userStorageRead:false,productMainStarted:false,physicalPowerAcceptance:false},remainingGaps:['Native TDP server/CLI binaries and actual OEM readback not executed','OEMCPU/OEMGPU unified catalog binding remains a separate pending domain','Whole device effective CPU/GPU power and installed version are not accepted by frontend tests'],fullGoalComplete:false};
  write(path.join(root,'result.json'),report);writeTdpOutputEvidence(root,walk(root));return report;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',code:error.message,originalFailurePreserved:true});throw error;}
 finally{esbuild?.stop();if(previousBinary===undefined)delete process.env.ESBUILD_BINARY_PATH;else process.env.ESBUILD_BINARY_PATH=previousBinary;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const {root}=parseArgs(process.argv.slice(2)),result=await runTdpConsumer(root);console.log(JSON.stringify({id:CASE_ID,status:result.status,completionMarkers:result.positive.completedMarkers.length,negativeDetected:result.negative.failedNames,sourceHashes:result.sourceHashes.length}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
