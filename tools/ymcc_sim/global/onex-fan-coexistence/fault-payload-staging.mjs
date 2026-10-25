import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const need=(ok,code)=>{if(!ok)throw Error(code);};
const key=p=>path.resolve(p).toLowerCase();
const inside=(root,p)=>{const r=path.relative(root,p);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const sha=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
// The original Cecil mutators create Host.dll. Copying a sealed read-only
// Host first makes that write fail on Windows before the negative can run.
export function stageFaultPayload(payload,targetRoot){
 need(path.isAbsolute(payload??'')&&path.isAbsolute(targetRoot??''),'FAULT_PAYLOAD_ABSOLUTE_ROOTS_REQUIRED');
 const source=path.resolve(payload),target=path.resolve(targetRoot);
 need(!inside(source,target)&&!inside(target,source),'FAULT_PAYLOAD_OVERLAPPING_ROOTS');
 need(fs.existsSync(source)&&fs.lstatSync(source).isDirectory()&&!fs.lstatSync(source).isSymbolicLink()&&key(fs.realpathSync(source))===key(source),'FAULT_PAYLOAD_SOURCE_DIRECTORY_REQUIRED');
 need(!fs.existsSync(target),'FAULT_PAYLOAD_FRESH_TARGET_REQUIRED');
 const host=path.join(source,'YeManFanHost.dll'),output=path.join(target,'YeManFanHost.dll');
 need(fs.existsSync(host)&&fs.lstatSync(host).isFile()&&!fs.lstatSync(host).isSymbolicLink(),'FAULT_PAYLOAD_HOST_REQUIRED');
 const before=sha(host);let files=0;
 const walk=dir=>{for(const e of fs.readdirSync(dir,{withFileTypes:true})){need(!e.isSymbolicLink(),'FAULT_PAYLOAD_REPARSE_MEMBER');const p=path.join(dir,e.name);if(e.isDirectory())walk(p);else{need(e.isFile(),'FAULT_PAYLOAD_NONFILE_MEMBER');files++;}}};walk(source);
 fs.cpSync(source,target,{recursive:true,errorOnExist:true,force:false,filter:p=>key(p)!==key(host)});
 need(!fs.existsSync(output),'FAULT_PAYLOAD_MUTATOR_OUTPUT_ALREADY_EXISTS');need(sha(host)===before,'FAULT_PAYLOAD_SOURCE_HOST_CHANGED');
 return {schemaVersion:1,status:'FAULT_PAYLOAD_STAGED_OUTPUT_FRESH',source,target,sourceHostSha256:before,copiedFiles:files-1,mutatorOutput:output,targetExecuted:false};
}
