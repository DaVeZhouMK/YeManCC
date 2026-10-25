import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const cfg=JSON.parse(fs.readFileSync(process.env.YMCC_STEAM_DRAFT_OBSERVER_CONFIG,'utf8'));
const producer=pathToFileURL(path.resolve(cfg.producer)).href;
const observer=new URL('./observe.cjs',import.meta.url).href;
// Only the original producer's esbuild import is observed. All actual compiler
// code and native protocol still execute the exact declared package/binary.
export async function resolve(specifier,context,nextResolve){
 if(specifier==='esbuild'&&context.parentURL===producer)return {url:observer,shortCircuit:true};
 return nextResolve(specifier,context);
}
