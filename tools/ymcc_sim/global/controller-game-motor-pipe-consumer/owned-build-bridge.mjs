import fs from 'node:fs';
import path from 'node:path';
import {createHash,randomUUID} from 'node:crypto';
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const inside=(root,file)=>{const r=path.relative(path.resolve(root),path.resolve(file));return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
const need=(value,code)=>{if(!value)throw Error(code);};
const key=file=>path.resolve(file).toLowerCase();
const json=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
function noReparse(file){for(let current=path.resolve(file);;current=path.dirname(current)){if(fs.existsSync(current))need(!fs.lstatSync(current).isSymbolicLink(),'BRIDGE_REPARSE_PATH');if(path.dirname(current)===current)break;}}
function regular(file){noReparse(file);need(fs.statSync(file).isFile(),'BRIDGE_REGULAR_FILE_REQUIRED');return file;}
function filesIn(root){
 const files=[];const visit=dir=>{for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);need(!entry.isSymbolicLink(),'BRIDGE_OUTPUT_REPARSE');if(entry.isDirectory())visit(file);else{need(entry.isFile(),'BRIDGE_OUTPUT_NOT_REGULAR');need(files.length<100000,'BRIDGE_OUTPUT_FILE_LIMIT');files.push(file);}}};visit(root);return files.sort();
}
/** Only the outer evidence location is bridged. The unchanged producer still
 * receives a fresh child below the real, configured WORKSPACE/Build directory. */
export function prepareOwnedBuildCase({evidenceDir,caseId,repo,workspace}){
 need(['controller.game-feedback-writer-consumer','controller.game-motor-private-pipe-consumer'].includes(caseId),'BRIDGE_KNOWN_EXISTING_LEAF_REQUIRED');
 need(typeof evidenceDir==='string'&&path.isAbsolute(evidenceDir)&&!/[\0\r\n]/.test(evidenceDir),'BRIDGE_ABSOLUTE_EVIDENCE_REQUIRED');
 const out=path.resolve(evidenceDir),repository=path.resolve(repo),buildRoot=path.resolve(workspace,'Build');noReparse(buildRoot);need(fs.statSync(buildRoot).isDirectory(),'BRIDGE_BUILD_ROOT_REQUIRED');
 const onD=path.parse(out).root.toLowerCase()==='d:\\';
 need(!out.startsWith('\\\\')&&(onD||inside(buildRoot,out))&&out!==path.parse(out).root&&!inside(repository,out)&&!inside(out,repository)&&!inside(path.resolve('C:/SOFT/YeMan'),out),'BRIDGE_EVIDENCE_MUST_BE_D_OR_OWNED_BUILD');
 noReparse(out);need(!fs.existsSync(out),'BRIDGE_FRESH_EVIDENCE_REQUIRED');need(fs.statSync(path.dirname(out)).isDirectory(),'BRIDGE_EVIDENCE_PARENT_REQUIRED');fs.mkdirSync(out);
 const container=fs.mkdtempSync(path.join(buildRoot,'ymcc-fb-')),executionRoot=path.join(container,'case');
 const owner={schemaVersion:1,kind:'ymcc-feedback-build-owner',nonce:randomUUID(),id:caseId,repo:repository,buildRoot:fs.realpathSync(buildRoot),container:fs.realpathSync(container),executionRoot,outerEvidenceRoot:fs.realpathSync(out),nodePid:process.pid,startedAt:new Date().toISOString()};
 const ownerFile=path.join(container,'owner.json');json(ownerFile,owner);
 const context={out,caseId,repo:repository,buildRoot:owner.buildRoot,container:owner.container,executionRoot,owner,ownerFile,ownerSha256:hash(ownerFile)};
 json(path.join(out,'execution-start.json'),{schemaVersion:1,status:'PREPARED_NOT_EXECUTED',owner,ownerFile,ownerSha256:context.ownerSha256,originalProducerRootMustBeFresh:true});
 return context;
}
export function verifyOwnedBuildCase(context){
 const {buildRoot,container,executionRoot,out,ownerFile,ownerSha256,owner}=context;
 noReparse(buildRoot);noReparse(container);noReparse(executionRoot);noReparse(out);regular(ownerFile);
 need(inside(buildRoot,container)&&container!==buildRoot&&path.basename(container).startsWith('ymcc-fb-')&&key(executionRoot)===key(path.join(container,'case'))&&hash(ownerFile)===ownerSha256,'BRIDGE_BUILD_OWNER_MISMATCH');
 need(JSON.stringify(JSON.parse(fs.readFileSync(ownerFile,'utf8')))===JSON.stringify(owner)&&owner.id===context.caseId&&key(owner.outerEvidenceRoot)===key(out)&&key(owner.repo)===key(context.repo),'BRIDGE_BUILD_OWNER_MISMATCH');
 if(fs.existsSync(executionRoot))need(fs.statSync(executionRoot).isDirectory()&&inside(fs.realpathSync(container),fs.realpathSync(executionRoot)),'BRIDGE_EXECUTION_ROOT_ESCAPED');
 return true;
}
function verifyOriginalSources(context,report){
 need(Array.isArray(report.sourceHashes)&&report.sourceHashes.length>0,'BRIDGE_ORIGINAL_SOURCE_CLOSURE_REQUIRED');
 const repositoryRows=[],observed=new Map();
 for(const row of report.sourceHashes){
  need(row&&typeof row.path==='string'&&/^[a-f0-9]{64}$/i.test(row.sha256??''),'BRIDGE_ORIGINAL_SOURCE_ROW_INVALID');
  const absolute=path.isAbsolute(row.path)?path.resolve(row.path):path.resolve(context.repo,row.path);
  need(inside(context.executionRoot,absolute)||inside(context.repo,absolute),'BRIDGE_UNDECLARED_EXTERNAL_SOURCE');regular(absolute);need(hash(absolute)===row.sha256.toLowerCase(),'BRIDGE_ORIGINAL_SOURCE_CHANGED');
  const previous=observed.get(key(absolute));need(!previous||previous===row.sha256.toLowerCase(),'BRIDGE_SOURCE_PIN_CONFLICT');observed.set(key(absolute),row.sha256.toLowerCase());
  if(inside(context.repo,absolute))repositoryRows.push({path:path.relative(context.repo,absolute).replaceAll('\\','/'),sha256:row.sha256.toLowerCase()});
 }
 const actualOwned=filesIn(context.executionRoot).filter(file=>key(file)!==key(path.join(context.executionRoot,'result.json'))).map(key).sort();
 const reportedOwned=[...observed.keys()].filter(file=>inside(context.executionRoot,file)).sort();
 need(JSON.stringify(actualOwned)===JSON.stringify(reportedOwned),'BRIDGE_ORIGINAL_OWNED_CLOSURE_DIFFERS');
 return repositoryRows;
}
/** Every copied payload keeps its real original location. No DLL, MVID, source
 * manifest, loaded-assembly row or producer report path is rewritten. */
function archiveProducer(context,{tolerateFailures=false}={}){
 const rows=[],errors=[];let files=[];
 try{if(fs.existsSync(context.executionRoot))files=filesIn(context.executionRoot);}catch(error){errors.push({code:error.code??error.message,phase:'enumerate'});}
 for(const actualPath of files){
  try{
   regular(actualPath);const relative=path.relative(context.executionRoot,actualPath),archivedPath=path.join(context.out,'producer-files',relative);
   need(inside(context.out,archivedPath),'BRIDGE_ARCHIVE_ESCAPED');const before=hash(actualPath);fs.mkdirSync(path.dirname(archivedPath),{recursive:true});noReparse(archivedPath);fs.copyFileSync(actualPath,archivedPath,fs.constants.COPYFILE_EXCL);
   need(hash(archivedPath)===before&&hash(actualPath)===before,'BRIDGE_ARCHIVE_BYTES_DIFFER');rows.push({actualPath,archivedPath,sha256:before,bytes:fs.statSync(archivedPath).size});
  }catch(error){errors.push({actualPath,code:error.code??error.message,phase:'archive',detail:String(error.message).slice(0,400)});if(!tolerateFailures)break;}
 }
 return {rows,errors,complete:errors.length===0};
}
function errorRecord(error){return{code:/^[A-Z_]+$/.test(error.message)?error.message:'BRIDGE_PRODUCER_ERROR',message:String(error.message).slice(0,1000),...(error.exitCode!==undefined?{exitCode:error.exitCode}:{}),...(error.code?{systemCode:error.code}:{})};}
/** Callback is fixed by each public wrapper to its existing full producer.
 * No CLI option can choose a producer, prototype, path or smaller case set. */
export async function runOwnedBuildBridge({evidenceDir,caseId,repo,workspace,produce}){
 const context=prepareOwnedBuildCase({evidenceDir,caseId,repo,workspace});
 let returned,original,originalResult,primary=null,repositoryRows=[],archive={rows:[],errors:[],complete:false};const secondary=[];
 try{
  verifyOwnedBuildCase(context);need(!fs.existsSync(context.executionRoot),'BRIDGE_FRESH_PRODUCER_ROOT_REQUIRED');returned=await produce(context.executionRoot);verifyOwnedBuildCase(context);
  const actualPath=regular(path.join(context.executionRoot,'result.json')),bytes=fs.readFileSync(actualPath);original=JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));
  need(JSON.stringify(original)===JSON.stringify(returned),'BRIDGE_RETURNED_RESULT_DIFFERS_FROM_ORIGINAL');
  need(original.id===caseId&&original.status==='PASS'&&original.passed===true,'BRIDGE_ORIGINAL_PRODUCER_NOT_PASS');
  need(original.sourceMode===undefined||original.sourceMode==='current-production-source','BRIDGE_FORMAL_SOURCE_REQUIRED');
  originalResult={actualPath,sha256:hash(actualPath),bytes:bytes.length};repositoryRows=verifyOriginalSources(context,original);
 }catch(error){primary=error;}
 try{
  verifyOwnedBuildCase(context);archive=archiveProducer(context,{tolerateFailures:primary!==null});
  if(archive.errors.length)throw Object.assign(Error('BRIDGE_ARCHIVE_INCOMPLETE'),{archiveErrors:archive.errors});
  if(!primary){verifyOriginalSources(context,original);for(const row of archive.rows)need(hash(row.actualPath)===row.sha256&&hash(row.archivedPath)===row.sha256,'BRIDGE_ARCHIVE_CHANGED_AFTER_COPY');}
 }catch(error){if(primary)secondary.push({...errorRecord(error),...(error.archiveErrors?{archiveErrors:error.archiveErrors}:{})});else primary=error;}
 // The raw producer result is byte-identical at this archived location. Its
 // actual G paths remain untouched, while generic D receipt audit uses copies.
 if(originalResult){const copied=archive.rows.find(row=>key(row.actualPath)===key(originalResult.actualPath));if(copied)originalResult={...originalResult,archivedPath:copied.archivedPath,archiveSha256:copied.sha256};else if(!primary)primary=Error('BRIDGE_ORIGINAL_RESULT_NOT_ARCHIVED');}
 const ownerCopy=path.join(context.out,'build-owner.json');try{fs.copyFileSync(context.ownerFile,ownerCopy,fs.constants.COPYFILE_EXCL);need(hash(ownerCopy)===context.ownerSha256,'BRIDGE_OWNER_COPY_CHANGED');}catch(error){if(primary)secondary.push(errorRecord(error));else primary=error;}
 const status=primary?(primary.exitCode===7?'BLOCKED':'FAIL'):'PASS';
 const bridge={schemaVersion:1,id:caseId,status,passed:status==='PASS',kind:'existing-feedback-owned-build-bridge',owner:context.owner,actualExecutionRoot:context.executionRoot,originalResult:originalResult??null,archivedFiles:archive.rows,closureComplete:archive.complete&&primary===null,actualLoadAndSourcePathsUnchanged:true,archivedBytesAreNotClaimedAsLoadedPaths:true,...(primary?{failure:errorRecord(primary)}:{}),secondaryFailures:secondary,archiveErrors:archive.errors,finishedAt:new Date().toISOString()};
 const manifest=path.join(context.out,'bridge.json');json(manifest,bridge);
 const sourceHashes=[...repositoryRows,...archive.rows.map(row=>({path:row.archivedPath,sha256:row.sha256})),...['execution-start.json','build-owner.json','bridge.json'].map(name=>path.join(context.out,name)).filter(fs.existsSync).map(file=>({path:file,sha256:hash(file)}))];
 const result={...(original??{schemaVersion:1,id:caseId}),status,passed:status==='PASS',kind:'existing-feedback-owned-build-bridge',sourceHashes,executionBridge:{manifest,sha256:hash(manifest),actualExecutionRoot:context.executionRoot,originalResult:originalResult??null,closureComplete:bridge.closureComplete,actualLoadAndSourcePathsUnchanged:true},...(primary?{failure:errorRecord(primary),secondaryFailures:secondary}:{})};
 json(path.join(context.out,'result.json'),result);
 if(primary){primary.bridgeEvidence={outerResult:path.join(context.out,'result.json'),manifest,actualExecutionRoot:context.executionRoot};throw primary;}
 return result;
}
