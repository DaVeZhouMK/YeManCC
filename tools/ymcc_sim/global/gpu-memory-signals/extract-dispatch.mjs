import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export function extractGpuDispatch(repo,out){
 const file=path.join(repo,'native/main.cpp');const bytes=fs.readFileSync(file);const source=bytes.toString('utf8');
 const marker='static void reg_gpu_dedicated_memory() {';const start=source.indexOf(marker);if(start<0||source.indexOf(marker,start+1)>=0)throw Error('GPU_DISPATCH_START_AMBIGUOUS');
 const end=source.indexOf('\nstatic void reg_os()',start);if(end<0)throw Error('GPU_DISPATCH_END_MISSING');
 const snippet=source.slice(start,end);
 for(const token of ['"gpu.dedicatedMemory.get"','"gpu.dedicatedMemory.set"','runtimeRead(root,g_machineIdentity.deviceClass)','runtimeApply(root,g_machineIdentity.deviceClass,args)'])if(snippet.split(token).length!==2)throw Error('GPU_DISPATCH_CONTRACT_CHANGED:'+token);
 fs.mkdirSync(out,{recursive:true});const generated=path.join(out,'GpuDispatch.g.h');fs.writeFileSync(generated,snippet);
 const result={status:'PASS',source:'native/main.cpp',sourceSha256:sha(bytes),fragmentSha256:sha(Buffer.from(snippet)),generated,byteIdentical:true,meaning:'Current production registration function preserved; only machine identity, callback registry, data-directory and diagnostic sink boundaries are supplied by the test worker.'};
 fs.writeFileSync(path.join(out,'dispatch-source.json'),JSON.stringify(result,null,2));return result;
}
