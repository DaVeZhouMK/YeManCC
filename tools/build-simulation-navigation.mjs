/** Rebuild only the standalone simulator's shared navigation prefix. */
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json'));
const {buildSync}=require('esbuild'),source=path.join(root,'src/gamepad/spatial.ts'),target=path.join(root,'public/performance-simulation/app.js');
const begin='// YMCC_SHARED_SPATIAL_BEGIN',end='// YMCC_SHARED_SPATIAL_END';
const result=buildSync({entryPoints:[source],absWorkingDir:root,bundle:true,write:false,format:'iife',globalName:'YmccSpatial',target:'es2020'}).outputFiles[0].text;
const original=fs.readFileSync(target,'utf8');let rest;
if(original.startsWith(begin)){const boundary=original.indexOf(end);if(boundary<0)throw Error('Shared navigation end marker missing');rest=original.slice(boundary+end.length).replace(/^\r?\n/,'');}
else {const match=original.match(/^var YmccSpatial = \(\(\) => \{[\s\S]*?^\}\)\(\);\r?\n/m);if(!match)throw Error('Reviewed standalone navigation prefix missing');rest=original.slice(match[0].length);}
fs.writeFileSync(target,begin+'\n'+result+end+'\n'+rest);
const manifestFile=path.join(root,'public/performance-simulation/manifest.json'),manifest=JSON.parse(fs.readFileSync(manifestFile,'utf8'));
const data=fs.readFileSync(target),entry=manifest.files.find(f=>f.name==='app.js');if(!entry)throw Error('Simulator app manifest entry missing');entry.bytes=data.length;entry.sha256=createHash('sha256').update(data).digest('hex');manifest.sharedSpatialNavigation={source:'src/gamepad/spatial.ts',sha256:createHash('sha256').update(fs.readFileSync(source)).digest('hex')};fs.writeFileSync(manifestFile,JSON.stringify(manifest,null,2)+'\n');
console.log('Simulator shared navigation rebuilt from production spatial.ts');
