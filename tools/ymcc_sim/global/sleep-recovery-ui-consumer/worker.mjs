import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {scratchFor} from '../fan-monitor-browser-consumer/scratch.mjs';
import {need,faultSource,verifyOriginalReport,verifyBoundary,verifyAssertionFailure} from './contract.mjs';
const root=path.resolve(process.argv[2]),input=path.join(root,'input-sources'),module='tools/ymcc_sim/global/sleep-recovery-ui-consumer',shared='tools/ymcc_sim/global/fan-monitor-browser-consumer';
const read=file=>JSON.parse(fs.readFileSync(file,'utf8')),write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n'),sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const spec=read(path.join(input,module,'spec.json')),tool=read(path.join(input,shared,'toolchain-inputs.json')),ledger=[];
function inspect(dir,{expectedPages}){
 const observation=read(path.join(dir,'boundary-observation.json')),trace=read(path.join(dir,'ui-observation.json')),summary=verifyBoundary(observation,trace,{expectedPages});
 const pins=new Map(tool.files.map(row=>[fs.realpathSync(row.path).toLowerCase(),row.sha256]));
 need(observation.runtimeFiles.length>0&&observation.runtimeFiles.every(row=>pins.get(row.path.toLowerCase())===row.sha256),'SLEEP_UI_UNPINNED_RUNTIME');
 const build=read(path.join(dir,'build.json'));
 const compilerInputs=Object.keys(build.metafile.inputs).map(file=>/^(?:\.\.[\\/]|[A-Za-z]:)/.test(file)?fs.realpathSync(path.resolve(dir,'source-repo',file)).replaceAll('\\','/'):file).sort();
 need(JSON.stringify(compilerInputs)===JSON.stringify(spec.buildInputs),'SLEEP_UI_COMPILE_GRAPH_CHANGED');
 need(Object.values(build.metafile.outputs).every(row=>row.imports.length===0),'SLEEP_UI_EXTERNAL_COMPILED_IMPORT');
 const program=path.join(dir,'executed-program.js'),programSha256=sha(program);
 need(programSha256===sha(path.join(dir,'unobserved-rebuild.js'))&&build.observerPreservedProgramBytes===true&&build.rebuildExecuted===false,'SLEEP_UI_OBSERVER_PROGRAM_DRIFT');
 need(trace.served.some(row=>row.url==='/fixture.js')&&trace.served.filter(row=>row.url==='/fixture.js').every(row=>row.sha256===programSha256&&sha(row.file)===programSha256),'SLEEP_UI_SERVED_PROGRAM_DRIFT');
 for(const row of trace.served){need(sha(row.file)===row.sha256,'SLEEP_UI_SERVED_BYTES_CHANGED');if(row.url!=='/fixture.js')need(fs.readFileSync(row.file,'utf8').includes('<script src="/fixture.js"></script>'),'SLEEP_UI_SERVED_HTML_CONTRACT');}
 return {...summary,program,programSha256,compilerInputs:Object.keys(build.metafile.inputs).length,runtimeModules:observation.runtimeFiles.length,compileSucceeded:true,observerPreservedProgramBytes:true,rebuildExecuted:false,sourceReads:observation.sourceReads};
}
function execute(label,{fault=false}={}){
 const dir=path.join(root,label);fs.mkdirSync(dir);const scratch=scratchFor(read(path.join(root,'scratch-allocation.json')),root,fault?'negative-renderer':'positive-v2');
 for(const sub of ['t','local','output'])fs.mkdirSync(path.join(scratch,sub));
 const source=path.join(dir,'source-repo');fs.mkdirSync(source);
 for(const relative of spec.sources.filter(file=>!file.startsWith('tools/ymcc_sim/'))){const to=path.join(source,relative);fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(path.join(input,relative),to);}
 fs.mkdirSync(path.join(source,'node_modules'));
 for(const[name,actual]of [['esbuild',tool.esbuildPackageRoot],['vue',tool.vuePackageRoot]])fs.symlinkSync(actual,path.join(source,'node_modules',name),'junction');
 if(fault){const file=path.join(source,spec.fault.path),before=fs.readFileSync(file,'utf8'),after=faultSource(before,spec.fault);fs.writeFileSync(file,after);write(path.join(dir,'private-fault.json'),{...spec.fault,beforeSha256:crypto.createHash('sha256').update(before).digest('hex'),afterSha256:sha(file),productSourceChanged:false});}
 const original=path.join(source,spec.original);need(sha(original)===spec.originalSha256,'SLEEP_UI_ORIGINAL_CHANGED');
 const config=path.join(dir,'config.json');write(config,{caseRoot:dir,toolFile:path.join(input,shared,'toolchain-inputs.json'),readRoots:[input,...tool.runtimeReadRoots],snapshotRoot:input,scratchRoot:scratch});
 const env={...process.env,YMCC_FAN_BROWSER_BOUNDARY:config,YMCC_SLEEP_RECOVERY_UI_OUT:path.join(scratch,'output'),YMCC_UI_BROWSER_MODULE_DIR:tool.bundledPackagesRoot,YMCC_UI_BROWSER_EXE:tool.browserExecutable,ESBUILD_BINARY_PATH:tool.esbuildExecutable,TEMP:path.join(scratch,'t'),TMP:path.join(scratch,'t'),LOCALAPPDATA:path.join(scratch,'local')};
 delete env.NODE_OPTIONS;delete env.NODE_PATH;
 const args=['--require',path.join(input,module,'observe.cjs'),original],child=spawnSync(tool.nodeExecutable,args,{cwd:source,env,windowsHide:true,encoding:'utf8',timeout:30000,maxBuffer:4*1024*1024});
 fs.writeFileSync(path.join(dir,'stdout.log'),child.stdout??'');fs.writeFileSync(path.join(dir,'stderr.log'),child.stderr??'');
 const row={label,program:tool.nodeExecutable,args,exitCode:child.status,signal:child.signal,error:child.error?.code??null};ledger.push(row);write(path.join(root,'process-ledger.json'),ledger);
 need(Number.isInteger(row.exitCode)&&!row.signal&&!row.error,'SLEEP_UI_PROCESS_INCOMPLETE');
 const evidence=inspect(dir,{expectedPages:fault?1:2});
 fs.mkdirSync(path.join(dir,'original-output'));for(const entry of fs.readdirSync(path.join(scratch,'output'),{withFileTypes:true})){need(entry.isFile()&&!entry.isSymbolicLink(),'SLEEP_UI_ORIGINAL_OUTPUT_TYPE');fs.copyFileSync(path.join(scratch,'output',entry.name),path.join(dir,'original-output',entry.name));}
 if(fault){need(!fs.existsSync(path.join(dir,'original-output/result.json')),'SLEEP_UI_FAULT_FALSE_ENDPOINT');return {...verifyAssertionFailure(row,child.stderr??'',spec),...evidence,exitCode:row.exitCode};}
 need(row.exitCode===0,'SLEEP_UI_ORIGINAL_FAILURE');
 const report=read(path.join(dir,'original-output/result.json')),stdoutReport=JSON.parse((child.stdout??'').trim());need(JSON.stringify(stdoutReport)===JSON.stringify(report),'SLEEP_UI_FINAL_REPORT_STDOUT_DRIFT');
 const result=verifyOriginalReport(report,spec);
 for(const file of spec.screenshots){const bytes=fs.readFileSync(path.join(dir,'original-output',file));need(bytes.length>1000&&bytes.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10])),'SLEEP_UI_SCREENSHOT_INCOMPLETE');}
 return {...result,...evidence,exitCode:row.exitCode,reportPath:path.join(dir,'original-output/result.json'),screenshots:spec.screenshots.map(file=>({path:path.join(dir,'original-output',file),sha256:sha(path.join(dir,'original-output',file))}))};
}
try{
 const positive=execute('positive-original');write(path.join(root,'positive-result.json'),positive);
 const negative=execute('negative-stale-status',{fault:true});
 const result={status:'PASS',passed:true,positive,negative,reviewedScriptAndBusinessAssertionsUnchanged:true,productChanges:0,physicalAcceptance:false};write(path.join(root,'worker-result.json'),result);console.log(JSON.stringify({status:result.status,checks:positive.checks,faultLine:negative.line}));
}catch(error){write(path.join(root,'worker-failure.json'),{status:'FAIL',message:error.message,ledger,originalFailuresPreserved:true});console.error(error.stack);process.exitCode=2;}
