// Maintenance context only. The existing declared positive pin remains authoritative.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {inputs} from './adapter.mjs';
import {R3_LAYOUT,fileHash,resolveR3ManifestPin,verifyManagedSourceRows,verifyManagedBuilderAdapters} from './r3-source-equivalence.mjs';
export const MAINTENANCE_REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const need=(ok,reason)=>{if(!ok)throw Error('R3_MAINTENANCE_'+reason);};
const readable=(file,role)=>{need(typeof file==='string'&&file.length>0&&path.isAbsolute(file),role+'_ABSOLUTE_PATH_REQUIRED');need(fs.existsSync(file)&&fs.statSync(file).isFile(),role+'_FILE_MISSING');};
const parse=(file,role)=>{try{return JSON.parse(fs.readFileSync(file,'utf8'));}catch(cause){throw Error('R3_MAINTENANCE_'+role+'_INVALID_JSON',{cause});}};
export function resolveEquivalenceContext({manifestPath=process.env.VC_R3_EQUIVALENCE_MANIFEST,foreignRepo=process.env.VC_R3_EQUIVALENCE_REPO??R3_LAYOUT.sourceRoot}={}){
 let context;
 try{context=inputs('positive');}catch(cause){throw Error('R3_MAINTENANCE_DECLARED_POSITIVE_UNAVAILABLE: '+cause.message,{cause});}
 const expected=context.managed.manifest,pin=context.native.value.managedPrerequisite;
 // An explicit override selects the same pinned manifest; it never searches for a DLL.
 const manifest=manifestPath===undefined?expected:manifestPath;
 readable(manifest,'EQUIVALENCE_MANIFEST');
 need(path.resolve(manifest)===path.resolve(expected),'EQUIVALENCE_MANIFEST_MUST_MATCH_DECLARED_POSITIVE');
 need(resolveR3ManifestPin(pin)===expected&&fileHash(manifest)===pin.sha256,'EQUIVALENCE_MANIFEST_IDENTITY_MISMATCH');
 const value=parse(manifest,'EQUIVALENCE_MANIFEST');
 const mapped=verifyManagedSourceRows(value,MAINTENANCE_REPO);
 verifyManagedBuilderAdapters(value,MAINTENANCE_REPO);
 need(typeof foreignRepo==='string'&&foreignRepo.length>0&&path.isAbsolute(foreignRepo)&&fs.existsSync(foreignRepo)&&fs.statSync(foreignRepo).isDirectory(),'FOREIGN_SOURCE_ROOT_REQUIRED');
 const foreign=path.resolve(foreignRepo);
 verifyManagedSourceRows(value,foreign);
 return {manifest:path.resolve(manifest),repo:MAINTENANCE_REPO,value,foreign,summary:{selection:manifestPath===undefined?'declared-positive-pin':'explicit-same-positive-pin',manifest:path.resolve(manifest),manifestSha256:pin.sha256,currentManagedSourceRoot:MAINTENANCE_REPO,foreignManagedSourceRoot:foreign,exactMembers:mapped.length,wholeCurrentNativeSourceAccepted:false}};
}
const historicalShapes=Object.freeze({current:{checks:2175,cases:21,failures:2,status:'FAIL'},old:{checks:2182,cases:21,failures:13,status:'FAIL'},normal:{checks:921,cases:43,failures:0,status:'PASS'}});
export function resolveHistoricalContext(configPath){
 readable(configPath,'HISTORY_CONFIG');
 const config=parse(configPath,'HISTORY_CONFIG');
 need(config?.schemaVersion===1&&config.receipts&&Object.keys(config.receipts).sort().join(',')==='current,normal,old','EXACT_THREE_HISTORICAL_ROLES_REQUIRED');
 const env={},identities=[];
 for(const [role,shape] of Object.entries(historicalShapes)){
  const row=config.receipts[role];readable(row?.path,'HISTORY_'+role.toUpperCase());
  need(typeof row.sha256==='string'&&/^[a-f0-9]{64}$/.test(row.sha256)&&fileHash(row.path)===row.sha256,'HISTORY_'+role.toUpperCase()+'_IDENTITY_MISMATCH');
  const raw=parse(row.path,'HISTORY_'+role.toUpperCase());
  need(raw.checks===shape.checks&&raw.cases?.length===shape.cases&&raw.failures?.length===shape.failures&&raw.status===shape.status&&raw.forbiddenIo===0&&raw.fullTranslationUnit===true&&raw.realDeviceIo===false&&raw.normalProductMainExecuted===false,'HISTORY_'+role.toUpperCase()+'_EXACT_SCOPE_REQUIRED');
  env[{current:'VC_R3_TEST_CURRENT',old:'VC_R3_TEST_OLD',normal:'VC_R3_TEST_NORMAL'}[role]]=path.resolve(row.path);
  identities.push({role,path:path.resolve(row.path),sha256:row.sha256,...shape,scope:'historical-only-not-current-native-acceptance'});
 }
 return {env,identities,config:{path:path.resolve(configPath),sha256:fileHash(configPath)}};
}