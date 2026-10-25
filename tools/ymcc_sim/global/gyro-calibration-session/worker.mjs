import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const need=(condition,message)=>{if(!condition)throw Error(message);};
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const inside=(root,file)=>{const rel=path.relative(root,file);return rel===''||rel!=='..'&&!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel);};
export async function executeWorker(config){
 const root=fs.realpathSync(config.root),sourceRoot=fs.realpathSync(config.sourceRoot);
 const expectedReport=path.resolve(root,'Build/Validation/GyroVirtual/T11-E01-gyro-calibration-session-mock-20260904.json');
 const cwd=path.join(root,'cwd','deep');fs.mkdirSync(cwd,{recursive:true});
 need(inside(root,expectedReport)&&path.resolve(cwd,'../../Build/Validation/GyroVirtual/T11-E01-gyro-calibration-session-mock-20260904.json')===expectedReport,'ORIGINAL_RELATIVE_OUTPUT_NOT_CONFINED');
 process.env.ESBUILD_BINARY_PATH=config.esbuildBinary;
 const require=createRequire(config.esbuildMain),esbuild=require(config.esbuildMain),builds=[];
 async function bundle(entry,name){
  const input=path.join(sourceRoot,entry),outfile=path.join(root,name+'.mjs'),loaded=new Map();
  const result=await esbuild.build({entryPoints:[input],absWorkingDir:sourceRoot,tsconfig:path.join(sourceRoot,'tsconfig.json'),bundle:true,platform:'node',format:'esm',target:'node18',outfile,sourcemap:'external',metafile:true,logLevel:'silent',plugins:[{name:'record-actual-typescript-bytes',setup(build){build.onLoad({filter:/\.(?:[cm]?js|tsx?|json)$/},({path:file})=>{
   const actual=fs.realpathSync(file);need(inside(sourceRoot,actual),'BUILD_INPUT_OUTSIDE_PRIVATE_SOURCE');const bytes=fs.readFileSync(actual),relative=path.relative(sourceRoot,actual).replaceAll('\\','/');
   loaded.set(relative,{path:relative,sha256:hash(bytes),bytes:bytes.length});return{contents:bytes.toString('utf8'),resolveDir:path.dirname(actual),loader:path.extname(actual)==='.json'?'json':path.extname(actual)==='.ts'?'ts':'js'};
  });}}]});
  const inputs=Object.keys(result.metafile.inputs).map(relative=>path.relative(sourceRoot,path.resolve(sourceRoot,relative)).replaceAll('\\','/')).sort();
  need(JSON.stringify(inputs)===JSON.stringify([...loaded.keys()].sort()),'ESBUILD_METAFILE_DOES_NOT_MATCH_ACTUAL_READS');
  write(path.join(root,name+'-metafile.json'),result.metafile);
  const proof={entry,compilerVersion:esbuild.version,inputs:[...loaded.values()].sort((a,b)=>a.path.localeCompare(b.path)),metafilePath:path.join(root,name+'-metafile.json'),bundle:outfile,bundleSha256:hash(fs.readFileSync(outfile)),compileSucceeded:true};builds.push(proof);write(path.join(root,name+'-compile.json'),proof);return outfile;
 }
 function run(file,name){
  const result=spawnSync(process.execPath,[file],{cwd,env:process.env,windowsHide:true,encoding:'utf8',timeout:20000,maxBuffer:2*1024*1024});
  fs.writeFileSync(path.join(root,name+'-stdout.log'),result.stdout??'');fs.writeFileSync(path.join(root,name+'-stderr.log'),result.stderr??'');
  const proof={exitCode:result.status,signal:result.signal,timedOut:result.error?.code==='ETIMEDOUT',error:result.error?.message??null,cwd,stdout:result.stdout??'',stderr:result.stderr??''};write(path.join(root,name+'-process.json'),proof);return proof;
 }
 try{
  const entry=await bundle(config.producer,'original-session');
  const original=run(entry,'original');
  need(!original.signal&&!original.timedOut&&!original.error&&Number.isInteger(original.exitCode),'ORIGINAL_SESSION_DID_NOT_TERMINATE_NATURALLY');
  let report=null,supplemental=null;
  if(config.expectedFailure){
   need(original.exitCode===1&&original.stderr.includes('Error: '+config.expectedFailure)&&!original.stdout.includes('gyro calibration session selftest: PASS ('),'ACTUAL_SOURCE_FAULT_NOT_DETECTED_BY_ORIGINAL_ASSERTIONS');
   need(!fs.existsSync(expectedReport),'NEGATIVE_UNEXPECTEDLY_PRODUCED_FINAL_RECEIPT');
  }else{
   need(original.exitCode===0,'ORIGINAL_SESSION_FAILED');
   const terminal='gyro calibration session selftest: PASS ('+expectedReport+')';
   need(original.stdout.split(/\r?\n/).filter(line=>line===terminal).length===1,'ORIGINAL_SESSION_FINAL_LINE_MISSING_OR_DUPLICATED');
   need(fs.statSync(expectedReport).isFile()&&inside(root,fs.realpathSync(expectedReport)),'ORIGINAL_JSON_OUTSIDE_OWN_CASE');
   report=JSON.parse(fs.readFileSync(expectedReport,'utf8'));
   need(report.evidenceId==='T11-E01-GYRO-CALIBRATION-SESSION-MOCK-20260904'&&report.status==='MOCK-READY'&&report.systemMutation===false&&Array.isArray(report.trace)&&report.trace.length>0&&['confidenceEqualsOneOnly','timeoutCannotLockManual','movementAbortsCandidate','invalidOffsetRejected','oldGenerationRejected','oldCalibrationEpochRejected','missingAccelSafeStops','physicalBaseInputOutsideGyroGate'].every(key=>report.assertions?.[key]===true),'ORIGINAL_CALIBRATION_RECEIPT_NOT_COMPLETE');
   for(const event of ['calibrate-timeout','calibrate-lock','calibrate-aborted-motion','calibrate-offset-invalid','sample-stale','calibrate-provider-incomplete'])need(report.trace.some(row=>row.event===event),'ORIGINAL_REQUIRED_EVENT_MISSING:'+event);
   const exitEntry=await bundle(config.supplemental,'session-exit');const exited=run(exitEntry,'exit');
   need(exited.exitCode===0&&!exited.signal&&!exited.timedOut&&!exited.error,'ACTUAL_SESSION_EXIT_ASSERTIONS_FAILED');
   supplemental=JSON.parse(exited.stdout.trim());need(supplemental.status==='PASS'&&supplemental.scenarios===4&&supplemental.checks>=13,'SESSION_EXIT_COMPLETION_MISSING');
  }
  const tools=Object.keys(require.cache).filter(file=>path.isAbsolute(file)).map(file=>({path:file,sha256:hash(fs.readFileSync(file))}));
  const result={schemaVersion:1,status:'PASS',kind:config.expectedFailure?'ORIGINAL_ASSERTION_DETECTED_SOURCE_FAULT':'ORIGINAL_SESSION_AND_EXIT_COMPLETED',sourceRoot,builds,original,originalEvidencePath:config.expectedFailure?null:expectedReport,originalEvidenceSha256:config.expectedFailure?null:hash(fs.readFileSync(expectedReport)),originalReportStatus:report?.status??null,supplemental,observedCompilerModules:tools,expectedFailure:config.expectedFailure??null,hardwareIo:false};
  write(path.join(root,'worker-result.json'),result);return result;
 }finally{esbuild.stop();}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 if(process.argv.length!==4||process.argv[2]!=='--config')throw Error('--config required');
 executeWorker(JSON.parse(fs.readFileSync(process.argv[3],'utf8').replace(/^\uFEFF/,''))).then(result=>console.log(JSON.stringify({status:result.status,kind:result.kind,originalExitCode:result.original.exitCode})),error=>{console.error(error.stack??error.message);process.exitCode=2;});
}
