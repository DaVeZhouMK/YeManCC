import fs from 'node:fs';import path from 'node:path';
function receipt(file){if(!fs.existsSync(file))return null;try{return JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));}catch(error){return {invalid:true,error:error.message};}}
export function inspectCurrentStage(run,{negative=false}={}){
 const stages=[];let firstFailure=null;
 for(const label of ['old-off','old-on','fixed-off',...(negative?[]:['fixed-on'])]){
  const buildFile=path.join(run,label+'-build.command.json'),executeFile=path.join(run,label+'-execute.command.json');
  const build=receipt(buildFile),execute=receipt(executeFile);
  const buildState=!build?'NOT_REACHED':build.invalid?'INVALID_RECEIPT':build.timedOut?'TIMEOUT':build.error||build.signal?'PROCESS_ERROR':build.status===0?'PASS':'FAILED';
  const executeState=!execute?'NOT_REACHED':execute.invalid?'INVALID_RECEIPT':execute.timedOut?'TIMEOUT':execute.error||execute.signal?'PROCESS_ERROR':'REACHED';
  const stage={label,build:{state:buildState,path:buildFile,receipt:build,stdout:path.join(run,label+'-build.stdout.txt'),stderr:path.join(run,label+'-build.stderr.txt')},execute:{state:executeState,path:executeFile,receipt:execute}};
  stages.push(stage);
  if(!firstFailure&&build&&buildState!=='PASS')firstFailure={code:buildState==='TIMEOUT'?'ORIGINAL_XINPUT_BUILD_TIMEOUT':'ORIGINAL_XINPUT_BUILD_FAILED',label,phase:'build',status:build.status??null,error:build.error??null,signal:build.signal??null,evidence:stage.build};
  if(!firstFailure&&buildState==='PASS'&&execute&&['INVALID_RECEIPT','TIMEOUT','PROCESS_ERROR'].includes(executeState))firstFailure={code:executeState==='TIMEOUT'?'ORIGINAL_XINPUT_EXECUTION_TIMEOUT':'ORIGINAL_XINPUT_EXECUTION_PROCESS_ERROR',label,phase:'execute',evidence:stage.execute};
  if(!firstFailure&&buildState==='PASS'&&!execute)firstFailure={code:'ORIGINAL_XINPUT_EXECUTION_NOT_REACHED',label,phase:'execute',evidence:stage.execute};
 }
 if(!firstFailure){const absent=stages.find(stage=>stage.build.state==='NOT_REACHED');if(absent)firstFailure={code:'ORIGINAL_XINPUT_BUILD_NOT_REACHED',label:absent.label,phase:'build',evidence:absent.build};}
 return {schemaVersion:1,status:firstFailure?'FAIL':'RECORDED',firstFailure,stages};
}
export function requireCurrentStageRecorded(run,options){
 const evidence=inspectCurrentStage(run,options);
 if(evidence.firstFailure){const failure=evidence.firstFailure;throw Object.assign(Error(failure.code+':'+failure.label+(failure.status===null||failure.status===undefined?'':':'+failure.status)),{code:failure.code,stageFailure:failure,stageEvidence:evidence});}
 return evidence;
}
