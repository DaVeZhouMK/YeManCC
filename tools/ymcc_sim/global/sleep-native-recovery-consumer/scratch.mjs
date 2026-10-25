import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
const markerName='.ymcc-sleep-owner.json';
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const hash=file=>digest(fs.readFileSync(file));
const need=(ok,code)=>{if(!ok)throw Error(code);};
const inside=(base,file)=>{const rel=path.relative(path.resolve(base),path.resolve(file));return !path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
function noReparse(file){for(let current=path.resolve(file);;){const stat=fs.lstatSync(current);need(!stat.isSymbolicLink(),'SLEEP_SCRATCH_REPARSE');const parent=path.dirname(current);if(parent===current)break;current=parent;}}
const key=(evidence,scratch,nonce)=>digest(path.resolve(evidence)+'\0'+path.resolve(scratch)+'\0'+nonce);
export function verifyScratch(record,evidenceRoot){
 need(record?.schemaVersion===1&&record.kind==='owned-sleep-native-execution'&&path.isAbsolute(evidenceRoot),'SLEEP_SCRATCH_SCHEMA');
 need(['root','tempBase','evidenceRoot'].every(name=>typeof record[name]==='string'&&path.isAbsolute(record[name])&&!/[\0\r\n]/.test(record[name]))&&/^[a-f0-9]{32}$/.test(record.nonce??'')&&/^[a-f0-9]{64}$/.test(record.markerSha256??''),'SLEEP_SCRATCH_SCHEMA');
 const tempBase=fs.realpathSync(os.tmpdir()),root=path.resolve(record.root);noReparse(root);noReparse(evidenceRoot);
 need(path.resolve(evidenceRoot)===path.resolve(record.evidenceRoot)&&fs.realpathSync(record.tempBase)===tempBase&&path.dirname(root)===tempBase&&/^ymcc-sr-[A-Za-z0-9]{6}$/.test(path.basename(root))&&fs.realpathSync(root)===root&&fs.lstatSync(root).isDirectory()&&record.ownershipKey===key(evidenceRoot,root,record.nonce),'SLEEP_SCRATCH_OWNERSHIP');
 const marker=path.join(root,markerName);noReparse(marker);need(fs.lstatSync(marker).isFile(),'SLEEP_SCRATCH_MARKER');const bytes=fs.readFileSync(marker);let value;try{value=JSON.parse(bytes);}catch{throw Error('SLEEP_SCRATCH_MARKER');}
 need(digest(bytes)===record.markerSha256&&value.schemaVersion===1&&value.kind===record.kind&&value.nonce===record.nonce&&value.root===root&&value.evidenceRoot===path.resolve(evidenceRoot),'SLEEP_SCRATCH_MARKER');return root;
}
export function allocateScratch(evidenceRoot){
 need(path.isAbsolute(evidenceRoot)&&fs.lstatSync(evidenceRoot).isDirectory(),'SLEEP_SCRATCH_CASE_REQUIRED');noReparse(evidenceRoot);const tempBase=fs.realpathSync(os.tmpdir());noReparse(tempBase);
 const root=fs.realpathSync(fs.mkdtempSync(path.join(tempBase,'ymcc-sr-')));need(root.length<=100&&/^[\x20-\x7e]+$/.test(root),'SLEEP_SCRATCH_SHORT_ASCII_REQUIRED');const nonce=crypto.randomBytes(16).toString('hex'),bytes=Buffer.from(JSON.stringify({schemaVersion:1,kind:'owned-sleep-native-execution',root,nonce,evidenceRoot:path.resolve(evidenceRoot)})+'\n');fs.writeFileSync(path.join(root,markerName),bytes,{flag:'wx'});const record={schemaVersion:1,kind:'owned-sleep-native-execution',root,tempBase,evidenceRoot:path.resolve(evidenceRoot),nonce,markerSha256:digest(bytes),ownershipKey:key(evidenceRoot,root,nonce)};verifyScratch(record,evidenceRoot);return record;
}
export function scratchFiles(record,evidenceRoot){
 const root=verifyScratch(record,evidenceRoot),files=[];
 function visit(dir){noReparse(dir);for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);need(inside(root,file),'SLEEP_SCRATCH_FILE_ESCAPE');noReparse(file);if(entry.isDirectory())visit(file);else{need(entry.isFile(),'SLEEP_SCRATCH_FILE_TYPE');files.push(file);}need(files.length<=12000,'SLEEP_SCRATCH_FILE_LIMIT');}}visit(root);return files.sort();
}
export function archiveScratch(record,evidenceRoot){
 const root=verifyScratch(record,evidenceRoot),destination=path.join(evidenceRoot,'execution');need(!fs.existsSync(destination),'SLEEP_ARCHIVE_MUST_BE_FRESH');fs.mkdirSync(destination);const rows=[];
 for(const original of scratchFiles(record,evidenceRoot)){const relative=path.relative(root,original),archived=path.join(destination,relative);need(inside(destination,archived),'SLEEP_ARCHIVE_FILE_ESCAPE');fs.mkdirSync(path.dirname(archived),{recursive:true});noReparse(path.dirname(archived));const before=hash(original);fs.copyFileSync(original,archived,fs.constants.COPYFILE_EXCL);const copied=hash(archived);need(before===copied&&hash(original)===before,'SLEEP_ARCHIVE_BYTES_CHANGED');rows.push({relative,originalPath:original,archivedPath:archived,sha256:before,bytes:fs.statSync(archived).size});}
 return {schemaVersion:1,kind:'sleep-native-execution-byte-archive',evidenceRoot:path.resolve(evidenceRoot),originalRoot:root,archiveRoot:destination,markerSha256:record.markerSha256,originalContentsRewritten:false,files:rows};
}
export function cleanupScratch(record,evidenceRoot,mapping){
 const root=verifyScratch(record,evidenceRoot);need(mapping?.originalRoot===root&&mapping.evidenceRoot===path.resolve(evidenceRoot)&&mapping.markerSha256===record.markerSha256,'SLEEP_CLEANUP_ARCHIVE_BINDING');const current=scratchFiles(record,evidenceRoot);need(current.length===mapping.files.length,'SLEEP_CLEANUP_MEMBERSHIP_CHANGED');
 for(const file of current){const row=mapping.files.find(x=>x.originalPath===file);need(row&&inside(path.join(evidenceRoot,'execution'),row.archivedPath),'SLEEP_CLEANUP_ARCHIVE_MISMATCH');noReparse(row.archivedPath);need(hash(file)===row.sha256&&hash(row.archivedPath)===row.sha256,'SLEEP_CLEANUP_ARCHIVE_MISMATCH');}
 // Absolute target, exact nonce owner, no reparse children, complete byte archive checked above.
 need(path.dirname(root)===fs.realpathSync(os.tmpdir())&&fs.realpathSync(root)===root,'SLEEP_CLEANUP_TARGET');fs.rmSync(root,{recursive:true,force:false,maxRetries:2,retryDelay:50});need(!fs.existsSync(root),'SLEEP_SCRATCH_CLEANUP_FAILED');return {root,markerSha256:record.markerSha256,verifiedOwnerAndAllArchivedBytes:true,cleaned:true};
}
