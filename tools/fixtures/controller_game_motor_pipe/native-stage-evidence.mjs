// Retain actual compiler terminal evidence before a fixture/outer step can throw.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const within=(root,file)=>{const r=path.relative(path.resolve(root),path.resolve(file));return r!==''&&!r.startsWith('..')&&!path.isAbsolute(r);};
export function recordNativeCompile({root,dir,mode,command,process:child,startedAt}){
 const log=path.join(dir,'compile.log');
 const value={schemaVersion:1,kind:'feedback-native-compile-terminal',stage:'compile',configuration:mode,pid:child.pid??null,command,cwd:dir,exitCode:child.status??null,signal:child.signal??null,error:child.error?{message:child.error.message,code:child.error.code??null}:null,natural:!child.error&&!child.signal&&Number.isInteger(child.status),elapsedMs:Date.now()-startedAt,log:{path:log,sha256:hash(log)},consumerExecutableReached:false,semanticRejection:false};
 const file=path.join(dir,'compile-receipt.json');fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
 if(child.error||child.signal||child.status!==0){const failure={...value,status:'FAIL',primaryError:'NATIVE_COMPILATION_FAILED:'+mode+':exit='+String(child.status)+':'+log,compileReceipt:{path:file,sha256:hash(file)},naturalBusinessStage:{reached:false,reason:'compile did not succeed'}};fs.writeFileSync(path.join(root,'native-stage-failure.json'),JSON.stringify(failure,null,2)+'\n',{flag:'wx'});const error=Error(failure.primaryError);error.stageFailure=failure;throw error;}
 return value;
}
export function requireOwnedStep(root,name,child,args){
 if(child.exitCode===0&&!child.timedOut&&!child.launchError&&!child.signal&&!child.outputTruncated)return;
 const outerStepError='OWNED_STEP_NOT_COMPLETED:'+name,terminal={name,pid:child.pid??null,exitCode:child.exitCode??null,signal:child.signal??null,timedOut:child.timedOut??false,launchError:child.launchError??null,outputTruncated:child.outputTruncated??false};
 let actual=null;const secondaryErrors=[],at=args.indexOf('--out');
 if(at>=0&&typeof args[at+1]==='string'){const file=path.resolve(args[at+1],'native-stage-failure.json');if(within(root,file)&&fs.existsSync(file))try{const stat=fs.statSync(file);if(stat.size>2*1024*1024)throw Error('failure receipt exceeds limit');const r=JSON.parse(fs.readFileSync(file,'utf8'));if(r.kind!=='feedback-native-compile-terminal'||r.status!=='FAIL'||r.semanticRejection!==false)throw Error('invalid native failure receipt');actual={...r,receipt:{path:file,sha256:hash(file)}};}catch(e){secondaryErrors.push({path:file,error:String(e)});}}
 const stderr=String(child.stderr??'');const error=Error(actual?.primaryError??outerStepError);error.stageFailure={primary:actual,outerStepError,terminal,originalStderr:stderr,secondaryErrors,compilationFailureCountedAsNegative:false};throw error;
}
