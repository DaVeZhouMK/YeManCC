import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createCapturedSourceSnapshot} from './source-snapshot.mjs';
import {parseSnapshotArguments} from './create-source-snapshot.mjs';
export const CAPTURE_SOURCE_VERSION_HELP=`Capture a complete fixed source version from each file's same hashed descriptor.
node tools/ymcc_sim/global/capture-source-version.mjs --repo ABS_REPO --out ABS_FRESH_SIBLING
This never launches a simulator or program. It does not claim an atomic cross-file source time.
Changed current source still leaves the original creation FAIL. Only a complete independently
verified copy may be tested with verifyCapturedSourceSnapshot; it never approves changed primary.
Partial capture, changes during a descriptor read, overwrite or reparse traversal are refused.
The original strict create-source-snapshot.mjs entry remains available with its original behavior.
`;
export function runCapturedSourceVersionCli(args){
 const options=parseSnapshotArguments(args);
 if(options.mode==='help')return {exitCode:0,text:CAPTURE_SOURCE_VERSION_HELP};
 if(options.mode!=='create')throw Error('CAPTURE_SOURCE_VERSION_REQUIRES_FRESH_CREATE');
 const value=createCapturedSourceSnapshot(options);
 return {exitCode:value.status==='PASS'?0:2,result:{schemaVersion:value.schemaVersion,kind:value.kind,captureMode:value.captureMode,atomicSourceTreeClaimed:false,status:value.status,sourceRoot:value.sourceRoot,snapshotRoot:value.snapshotRoot,manifestFile:value.manifestFile,isCurrent:value.isCurrent,fileCount:value.files.length,copyVerifiedFiles:value.copyVerifiedFiles,snapshotCopyClosed:value.snapshotDiff?.equal===true&&value.copyVerifiedFiles===value.files.length,main:value.main,failure:value.failure,sourceApprovalGranted:false,executionPerformed:false}};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const value=runCapturedSourceVersionCli(process.argv.slice(2));if(value.text)process.stdout.write(value.text);else console.log(JSON.stringify(value.result));process.exitCode=value.exitCode;}
 catch(error){console.error(JSON.stringify({status:'FAIL',code:error.code??error.message,message:String(error.message).slice(0,500),sourceApprovalGranted:false,executionPerformed:false}));process.exitCode=3;}
}