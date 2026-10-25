/** Executable bootstrap; importing this module only defines its entry function. */
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {createInterface} from 'node:readline';import {fileURLToPath} from 'node:url';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const save=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
const need=(ok,why)=>{if(!ok)throw Error('CSS_APPEARANCE_BOOTSTRAP_'+why);};
const key=file=>path.resolve(file).toLowerCase();
export async function runCssAppearanceBootstrap([requestFile,expected,ownerPid]){
 need(/^[a-f0-9]{64}$/.test(expected||'')&&hash(requestFile)===expected,'REQUEST_PIN');
 const request=JSON.parse(fs.readFileSync(requestFile,'utf8'));need(process.ppid===Number(ownerPid),'EXACT_GUARDIAN_PARENT');
 const gate=createInterface({input:process.stdin});let timer;
 const nonce=await Promise.race([new Promise(resolve=>{gate.once('line',resolve);gate.once('close',()=>resolve('EOF'));}),new Promise(resolve=>timer=setTimeout(()=>resolve('TIMEOUT'),Math.min(5000,Math.max(1,request.runDeadlineMs-Date.now()))))]);clearTimeout(timer);gate.close();process.stdin.destroy();
 need(nonce===request.nonce,'STDIN_NONCE_REQUIRED');
 const assigned=JSON.parse(fs.readFileSync(path.join(request.work,'assigned.json'),'utf8'));need(assigned.childPid===process.pid&&assigned.helperPid===process.ppid&&assigned.jobAssigned===true&&assigned.killOnJobClose===true,'ASSIGN_BEFORE_GATE');
 const checked=()=>[...request.sources,...request.tools].filter(row=>{try{return hash(row.path)!==row.sha256;}catch{return true;}}).map(row=>row.path);
 const startedAt=new Date().toISOString(),started=Date.now();let sequenceStarted=false,raw=null;
 function copyCanonical(){
  if(!fs.existsSync(request.cssWork))return null;
  need(key(path.dirname(request.cssWork))===key(request.work)&&key(path.dirname(request.cssCanonical))===key(request.callerEvidence),'OWNED_CSS_ROOTS');
  need(!fs.existsSync(request.cssCanonical),'FRESH_CANONICAL');fs.mkdirSync(request.cssCanonical);const files=[];
  function visit(source,destination){for(const row of fs.readdirSync(source,{withFileTypes:true})){
   need(Date.now()<request.runDeadlineMs,'CANONICAL_COPY_DEADLINE');const from=path.join(source,row.name),to=path.join(destination,row.name);
   need(!row.isSymbolicLink()&&key(fs.realpathSync(from))===key(from),'NO_REPARSE_RAW');
   if(row.isDirectory()){fs.mkdirSync(to);visit(from,to);}else{need(row.isFile(),'RAW_FILE_REQUIRED');fs.copyFileSync(from,to,fs.constants.COPYFILE_EXCL);const sha256=hash(from);need(hash(to)===sha256,'CANONICAL_BYTES');files.push({source:from,path:to,sha256,bytes:fs.statSync(from).size});}
  }}
  visit(request.cssWork,request.cssCanonical);
  const map=path.join(request.callerEvidence,'CSS-RAW-MAP.json');save(map,{schemaVersion:1,source:request.cssWork,canonical:request.cssCanonical,byteExact:true,files});return {map:{path:map,sha256:hash(map)},canonical:request.cssCanonical,files};
 }
 try{
  need(checked().length===0,'SOURCE_OR_TOOL_CHANGED_BEFORE_EXECUTION');
  save(path.join(request.work,'bootstrap-started.json'),{pid:process.pid,ownerPid:process.ppid,parent:request.parent,startedAt,mode:request.mode,assignedBeforeNonce:true,requestSha256:expected});
  if(request.mode==='control-stall'){setInterval(()=>{},3600000);await new Promise(()=>{});}
  else if(request.mode==='control-normal')save(path.join(request.work,'bootstrap-result.json'),{status:'COMPLETED',mode:request.mode,suiteExecuted:false,sequenceStarted:false,pid:process.pid,finishedAt:new Date().toISOString(),sourceChanges:checked()});
  else{
   need(request.mode==='suite'&&key(request.driver)===key(path.join(request.repo,'tools/ymcc_sim/global/decky-update-recovery/css-appearance-sequence.mjs')),'EXACT_SEQUENCE_ONLY');
   const {runCssAppearanceSequence}=await import('./css-appearance-sequence.mjs');
   need(checked().length===0&&Date.now()<request.runDeadlineMs,'PINS_AND_DEADLINE_BEFORE_SEQUENCE');
   sequenceStarted=true;save(path.join(request.work,'sequence-started.json'),{pid:process.pid,startedAt:new Date().toISOString(),source:{path:request.driver,sha256:hash(request.driver)},work:request.cssWork,sharedDeadline:request.deadlineMs,runDeadline:request.runDeadlineMs});
   const result=runCssAppearanceSequence({repo:request.repo,work:request.cssWork,powershell:request.powershell.path,deadlineMs:request.runDeadlineMs});
   raw=copyCanonical();const changed=checked();need(changed.length===0,'SOURCE_OR_TOOL_CHANGED_AFTER_EXECUTION');
   save(path.join(request.work,'bootstrap-result.json'),{status:'COMPLETED',suiteExecuted:sequenceStarted,sequenceStarted,pid:process.pid,startedAt,elapsedMs:Date.now()-started,originalReport:result.report,stages:result.stages,originalCssBlock:result.originalCssBlock,raw,sourceChanges:changed});
  }
 }catch(error){
  let copyError=null;try{if(!raw&&!fs.existsSync(request.cssCanonical))raw=copyCanonical();}catch(e){copyError=e.stack;}
  save(path.join(request.work,'bootstrap-result.json'),{status:'FAIL',suiteExecuted:sequenceStarted,sequenceStarted,pid:process.pid,error:error.stack??String(error),copyError,raw,sourceChanges:checked()});process.exitCode=2;
 }
}
if(process.argv[1]&&key(fileURLToPath(import.meta.url))===key(process.argv[1]))await runCssAppearanceBootstrap(process.argv.slice(2));