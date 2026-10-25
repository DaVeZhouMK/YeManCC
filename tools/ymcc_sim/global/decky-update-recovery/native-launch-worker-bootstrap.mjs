import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawn} from 'node:child_process';import {createInterface} from 'node:readline';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const save=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
const need=(ok,why)=>{if(!ok)throw Error('NATIVE_LAUNCH_BOOTSTRAP_'+why);};
const [requestFile,expected,ownerPid]=process.argv.slice(2);need(/^[a-f0-9]{64}$/.test(expected||'')&&hash(requestFile)===expected,'REQUEST_PIN');
const request=JSON.parse(fs.readFileSync(requestFile,'utf8'));need(process.ppid===Number(ownerPid),'EXACT_HELPER_PARENT');
const gate=createInterface({input:process.stdin});
let nonceTimer;
const nonce=await Promise.race([new Promise(resolve=>{gate.once('line',resolve);gate.once('close',()=>resolve('EOF'));}),new Promise(resolve=>nonceTimer=setTimeout(()=>resolve('TIMEOUT'),Math.min(5000,Math.max(1,request.runDeadlineMs-Date.now()))))]);clearTimeout(nonceTimer);gate.close();process.stdin.destroy();
need(nonce===request.nonce,'STDIN_NONCE_REQUIRED');
const assigned=JSON.parse(fs.readFileSync(path.join(request.work,'assigned.json'),'utf8'));need(assigned.childPid===process.pid&&assigned.helperPid===process.ppid&&assigned.jobAssigned===true&&assigned.killOnJobClose===true,'ASSIGN_BEFORE_GATE');
const checked=()=>[...request.sources,...request.tools].filter(row=>{try{return hash(row.path)!==row.sha256;}catch{return true;}}).map(row=>row.path);
const startedAt=new Date().toISOString(),started=Date.now();let driver=null,driverStarted=false;
try{
 need(checked().length===0,'SOURCE_OR_TOOL_CHANGED_BEFORE_EXECUTION');
 save(path.join(request.work,'bootstrap-started.json'),{pid:process.pid,ownerPid:process.ppid,parent:request.parent,startedAt,mode:request.mode,assignedBeforeNonce:true,requestSha256:expected});
 if(request.mode==='control-stall'){
  // Deliberately noncooperative inert control, always bounded by its exact owner.
  setInterval(()=>{},3600000);await new Promise(()=>{});
 }else if(request.mode==='control-normal'){
  save(path.join(request.work,'bootstrap-result.json'),{status:'COMPLETED',mode:request.mode,suiteExecuted:false,driverStarted:false,pid:process.pid,finishedAt:new Date().toISOString(),sourceChanges:checked()});
 }else{
  need(request.mode==='suite'&&request.driver===path.join(request.repo,'tools/steam_native_launch_regression.mjs'),'ORIGINAL_DRIVER_ONLY');
  fs.mkdirSync(path.join(request.work,'noio'));const stdout=path.join(request.work,'driver.stdout.log'),stderr=path.join(request.work,'driver.stderr.log'),out=fs.openSync(stdout,'wx'),err=fs.openSync(stderr,'wx');
  try{driver=spawn(request.node.path,[request.driver,...request.driverArgs],{cwd:request.repo,windowsHide:true,stdio:['ignore',out,err],env:{...process.env,TEMP:path.join(request.work,'t'),TMP:path.join(request.work,'t')}});}finally{fs.closeSync(out);fs.closeSync(err);}
  await new Promise((resolve,reject)=>{driver.once('spawn',()=>{driverStarted=true;resolve();});driver.once('error',reject);});
  save(path.join(request.work,'driver-started.json'),{pid:driver.pid,startedAt:new Date().toISOString(),program:request.node,args:[request.driver,...request.driverArgs],cwd:request.repo,withinAssignedJob:true});
  const exit=await new Promise(resolve=>{driver.once('error',error=>resolve({code:null,error:error.message}));driver.once('close',(code,signal)=>resolve({code,signal,exitObserved:true}));});
  need(exit.code===0&&!exit.signal&&!exit.error,'ORIGINAL_DRIVER_NONZERO:'+JSON.stringify(exit));need(Date.now()<request.runDeadlineMs,'SHARED_DEADLINE');
  const rawFile=path.join(request.work,'noio/native-manual-launch/result.json'),result=JSON.parse(fs.readFileSync(rawFile,'utf8'));
  need(result.ok===true&&result.positiveCases===36&&result.negative.length===4&&result.negative.every(r=>r.detected),'ORIGINAL_36_AND_4_RESULT');
  const js=['steam_launch_admission_selftest.mjs.json','steam_linked_launch_selftest.mjs.json'].map(name=>{const file=path.join(path.dirname(rawFile),name);need(fs.existsSync(file),'ORIGINAL_JS_RAW_REQUIRED');return{path:file,sha256:hash(file)};});
  const changed=checked();need(changed.length===0,'SOURCE_OR_TOOL_CHANGED_AFTER_EXECUTION');
  save(path.join(request.work,'bootstrap-result.json'),{status:'COMPLETED',suiteExecuted:driverStarted,driverStarted,pid:process.pid,startedAt,elapsedMs:Date.now()-started,driver:{pid:driver.pid,exit},originalResult:result,raw:{result:{path:rawFile,sha256:hash(rawFile)},js,stdout:{path:stdout,sha256:hash(stdout)},stderr:{path:stderr,sha256:hash(stderr)}},sourceChanges:changed});
 }
}catch(error){save(path.join(request.work,'bootstrap-result.json'),{status:'FAIL',suiteExecuted:driverStarted,driverStarted,pid:process.pid,error:error.stack??String(error),sourceChanges:checked(),driverPid:driver?.pid??null});process.exitCode=2;}