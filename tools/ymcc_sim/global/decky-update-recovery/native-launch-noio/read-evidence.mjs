import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.dirname(fileURLToPath(import.meta.url));
export const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
export function record(file,kind,detail={}){const p=path.resolve(file),bytes=fs.readFileSync(p);fs.appendFileSync(path.join(root,'READ-EVIDENCE.jsonl'),JSON.stringify({time:new Date().toISOString(),path:p,sha256:sha(bytes),bytes:bytes.length,kind,...detail})+'\n');return bytes;}
const [mode,file,a,b]=process.argv.slice(2);
if(mode==='read'){const data=record(file,'source-excerpt',{start:Number(a)||1,end:Number(b)||null});const lines=data.toString('utf8').split('\n');for(let i=(Number(a)||1)-1;i<Math.min(Number(b)||lines.length,lines.length);i++)console.log(`${i+1}: ${lines[i]}`);}
if(mode==='json'){const obj=JSON.parse(record(file,'json-inspection'));console.log(JSON.stringify(obj,(k,v)=>Array.isArray(v)&&v.length>40?{count:v.length,first:v.slice(0,2),last:v.slice(-1)}:v,2));}
if(mode==='hash')console.log(sha(record(file,'full-byte-hash')));
