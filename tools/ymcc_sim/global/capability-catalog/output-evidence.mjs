import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {getCapabilityCatalogAdapters} from './adapter.mjs';

export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const FILES=Object.freeze({
 'fan.unified-capability-dispatch':{report:'result.json',assemblies:['YeManFanHost','YeManFanBackend','HcCapabilityCatalogSelfTest','HcCapabilityHotPathSelfTest'],host:'build/bin/YeManFanHost/release_win-x64/YeManFanHost.dll'},
 'fan.unified-capability-roundtrip':{report:'actual-roundtrip-results.json',assemblies:['FanCapabilityRoundtrip','YeManFanBackend']}
});
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export const hash=file=>digest(fs.readFileSync(file));
const maxJsonBytes=16*1024*1024;
function readJsonBytes(file){
 if(fs.statSync(file).size>maxJsonBytes)throw Error('CATALOG_JSON_TOO_LARGE');
 const bytes=fs.readFileSync(file);
 if(bytes.length>maxJsonBytes)throw Error('CATALOG_JSON_TOO_LARGE');
 return bytes;
}
const parseJson=bytes=>JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));
const read=file=>parseJson(readJsonBytes(file));
const need=(value,code)=>{if(!value)throw Error(code);};
export function inside(root,file){const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));}
export function evidenceRoot(value){need(typeof value==='string'&&path.isAbsolute(value),'CATALOG_ABSOLUTE_EVIDENCE_ROOT_REQUIRED');const root=path.resolve(value);need(!inside(REPO,root)&&!inside(root,REPO),'CATALOG_EVIDENCE_REPOSITORY_BOUNDARY');for(let p=root;;){if(fs.existsSync(p))need(!fs.lstatSync(p).isSymbolicLink(),'CATALOG_REPARSE_EVIDENCE');const parent=path.dirname(p);if(parent===p)break;p=parent;}need(fs.statSync(root).isDirectory(),'CATALOG_EVIDENCE_DIRECTORY_REQUIRED');return root;}
function walk(root){const rows=[];function visit(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name),stat=fs.lstatSync(file);need(!stat.isSymbolicLink(),'CATALOG_REPARSE_OUTPUT');if(stat.isDirectory())visit(file);else{need(stat.isFile(),'CATALOG_NONFILE_OUTPUT');need(rows.length<12000,'CATALOG_OUTPUT_FILE_LIMIT');rows.push(file);}}}visit(root);return rows.sort();}
function sdkIdentity(){const spec=read(path.join(REPO,'tools/ymcc_sim/global/capability-catalog/toolchain-inputs.json')),call=spawnSync('dotnet',['--version'],{cwd:REPO,windowsHide:true,encoding:'utf8',timeout:15000,maxBuffer:128*1024});need(!call.error&&call.status===0&&call.stdout.trim()===spec.sdkVersion,'CATALOG_SDK_IDENTITY_CHANGED');return spec;}
function inputPaths(caseId){const adapter=getCapabilityCatalogAdapters(REPO).find(row=>row.id===caseId);need(adapter&&FILES[caseId],'CATALOG_CASE_UNKNOWN');const spec=sdkIdentity();const sources=[...new Set([adapter.path,...adapter.evidenceInputs])].map(file=>path.resolve(REPO,file)),artifacts=[...new Set(adapter.artifactInputs.map(file=>path.resolve(file)))];for(const row of spec.roslynReferences){const file=path.join(process.env.ProgramFiles??'C:/Program Files','dotnet','sdk',spec.sdkVersion,'Roslyn','bincore',row.file);need(hash(file)===row.sha256,'CATALOG_SDK_REFERENCE_CHANGED');need(artifacts.includes(path.resolve(file)),'CATALOG_SDK_REFERENCE_NOT_DECLARED');}return{sources,artifacts};}
function verifyInputs(snapshot,root,caseId,current){need(snapshot.schemaVersion===1&&snapshot.caseId===caseId&&path.resolve(snapshot.evidenceRoot)===root,'CATALOG_INPUT_SNAPSHOT_MISMATCH');const expected=[...current.sources,...current.artifacts].map(file=>path.resolve(file).toLowerCase()).sort();need(JSON.stringify(expected)===JSON.stringify(snapshot.inputs.map(row=>path.resolve(row.path).toLowerCase()).sort()),'CATALOG_INPUT_MEMBERSHIP_CHANGED');for(const row of snapshot.inputs){need(hash(row.path)===row.sha256,'CATALOG_INPUT_CHANGED');if(row.snapshotPath)need(hash(row.snapshotPath)===row.sha256,'CATALOG_INPUT_SNAPSHOT_COPY_CHANGED');}return snapshot.inputs;}
export function capturePaths(caseId,evidenceDir,paths){const root=evidenceRoot(evidenceDir),checkpoint=path.join(root,'catalog-input-snapshot.json');need(!fs.existsSync(checkpoint),'CATALOG_INPUT_SNAPSHOT_ALREADY_EXISTS');const inputs=[];for(const[kind,files]of Object.entries(paths))for(const file of files){need(fs.statSync(file).isFile()&&!fs.lstatSync(file).isSymbolicLink(),'CATALOG_INPUT_NOT_REGULAR');const sha256=hash(file);let receiptPath;if(inside(REPO,file))receiptPath=path.relative(REPO,file).replaceAll('\\','/');else{receiptPath=path.join(root,'external-inputs',sha256.slice(0,16)+'-'+path.basename(file));fs.mkdirSync(path.dirname(receiptPath),{recursive:true});if(!fs.existsSync(receiptPath))fs.copyFileSync(file,receiptPath,fs.constants.COPYFILE_EXCL);need(hash(receiptPath)===sha256,'CATALOG_EXTERNAL_COPY_DIFFERS');}let snapshotPath=inside(REPO,file)&&kind==='sources'?path.join(root,'input-sources',path.relative(REPO,file)):!inside(REPO,file)?receiptPath:null;if(snapshotPath&&snapshotPath!==receiptPath){fs.mkdirSync(path.dirname(snapshotPath),{recursive:true});fs.copyFileSync(file,snapshotPath,fs.constants.COPYFILE_EXCL);need(hash(snapshotPath)===sha256,'CATALOG_SOURCE_COPY_DIFFERS');}inputs.push({kind,path:file,receiptPath,snapshotPath,sha256});}const snapshot={schemaVersion:1,caseId,evidenceRoot:root,capturedAt:new Date().toISOString(),inputs,artifactMeaning:'All declared possible sibling DLLs and explicit Roslyn references; do not infer the subset actually loaded.'};fs.writeFileSync(checkpoint,JSON.stringify(snapshot,null,2)+'\n');return snapshot;}
function requireGeneratedArtifacts(root,files,contract){
 const build=path.join(root,'build');
 for(const name of contract.assemblies)for(const suffix of ['.dll','.pdb'])need(files.some(file=>inside(build,file)&&path.basename(file)===name+suffix),'CATALOG_REQUIRED_GENERATED_ARTIFACT_MISSING');
}
export function sealPaths(caseId,evidenceDir,current){const root=evidenceRoot(evidenceDir),contract=FILES[caseId];need(contract,'CATALOG_CASE_UNKNOWN');
 const checkpoint=path.join(root,'catalog-input-snapshot.json'),snapshotBytes=readJsonBytes(checkpoint),snapshot=parseJson(snapshotBytes);const inputs=verifyInputs(snapshot,root,caseId,current);
 const reportFile=path.join(root,contract.report),reportBytes=readJsonBytes(reportFile),raw=parseJson(reportBytes);need((raw.status==='PASS'||raw.status==='passed')&&Number.isSafeInteger(raw.checks)&&raw.checks>0,'CATALOG_ORIGINAL_REPORT_NOT_PASSED');if(caseId==='fan.unified-capability-dispatch')need(Array.isArray(raw.failures)&&raw.failures.length===0&&typeof raw.hostSha256==='string'&&hash(path.join(root,contract.host))===raw.hostSha256.toLowerCase(),'CATALOG_CONSUMED_HOST_IDENTITY_DIFFERS');else need(raw.passed===true&&raw.binaryStatus==='passed-actual-locked-dll'&&raw.shippingOneXStatus==='passed-actual-shipping-one-x-contract','CATALOG_ROUNDTRIP_ORIGINAL_NOT_COMPLETE');
 requireGeneratedArtifacts(root,walk(root),contract);const original=path.join(root,'consumer-original-report.json');need(!fs.existsSync(original),'CATALOG_ORIGINAL_REPORT_ALREADY_ARCHIVED');fs.copyFileSync(reportFile,original,fs.constants.COPYFILE_EXCL);
 const files=walk(root).filter(file=>path.resolve(file)!==path.resolve(reportFile));
 requireGeneratedArtifacts(root,files,contract);
 const rows=new Map();for(const row of inputs)rows.set(row.receiptPath,{path:row.receiptPath,sha256:row.sha256});
 let originalReport,inputSnapshot;
 for(const file of files){const row={path:file,sha256:hash(file)};rows.set(file,row);if(file===original)originalReport=row;if(file===checkpoint)inputSnapshot=row;}
 // Bind the independently read output rows to the bytes used for validation.
 need(originalReport?.sha256===digest(reportBytes),'CATALOG_ORIGINAL_REPORT_CHANGED_DURING_SEAL');
 need(inputSnapshot?.sha256===digest(snapshotBytes),'CATALOG_INPUT_SNAPSHOT_CHANGED_DURING_SEAL');
 const generated=new Set(files),outputSha=file=>generated.has(file)?rows.get(file)?.sha256:undefined;
 if(contract.host)need(outputSha(path.join(root,contract.host))===raw.hostSha256.toLowerCase(),'CATALOG_CONSUMED_HOST_CHANGED_DURING_SEAL');
 for(const row of inputs)if(row.snapshotPath)need(outputSha(path.resolve(row.snapshotPath))===row.sha256,'CATALOG_INPUT_COPY_CHANGED_DURING_SEAL');
 const closure={schemaVersion:1,caseId,originalReport,inputSnapshot,ownedOutputFiles:files.length,sourceInputs:inputs.filter(row=>row.kind==='sources').length,artifactInputs:inputs.filter(row=>row.kind==='artifacts').length,hostExecutedBySealer:false,hardwareIo:false};const result={...raw,sourceHashes:[...rows.values()],evidenceClosure:closure};fs.writeFileSync(reportFile,JSON.stringify(result,null,2)+'\n');return result;}
export const captureInputs=(caseId,root)=>capturePaths(caseId,root,inputPaths(caseId));
export const sealOutputs=(caseId,root)=>sealPaths(caseId,root,inputPaths(caseId));
function argumentsFor(args){need(args.length===5&&['capture','seal'].includes(args[0])&&args[1]==='--case'&&args[3]==='--evidence-dir','CATALOG_EVIDENCE_ARGUMENTS_INVALID');return{mode:args[0],caseId:args[2],root:args[4]};}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{need(process.env.YMCC_SIM_GLOBAL_RUN==='1','CATALOG_GLOBAL_OWNED_JOB_REQUIRED');const options=argumentsFor(process.argv.slice(2)),result=options.mode==='capture'?captureInputs(options.caseId,options.root):sealOutputs(options.caseId,options.root);console.log(JSON.stringify({status:options.mode==='capture'?'CAPTURED':'SEALED',caseId:options.caseId,sourceHashes:result.sourceHashes?.length,closure:result.evidenceClosure}));}catch(error){console.error(error.message);process.exitCode=2;}}
