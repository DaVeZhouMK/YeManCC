import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),source=path.join(root,'native/decky_sidebar_health.js'),header=path.join(root,'native/decky_sidebar_health_script.h');
export function healthHeader(){
 const text=fs.readFileSync(source,'utf8').replace(/\r\n/g,'\n');
 if(text.includes(')YMCCHEALTH"'))throw Error('health script delimiter');
 // MSVC bounds each literal. Adjacent chunks retain the original script bytes.
 const chunks=[];let chunk='';
 for(const line of text.match(/[^\n]*\n|[^\n]+$/g)||[]){
  if(Buffer.byteLength(line,'utf8')>8192)throw Error('Health script source line exceeds literal chunk budget');
  if(Buffer.byteLength(chunk+line,'utf8')>8192){chunks.push(chunk);chunk='';}
  chunk+=line;
 }
 if(chunk||!chunks.length)chunks.push(chunk);
 const literals=chunks.map(value=>'R"YMCCHEALTH('+value+')YMCCHEALTH"').join('\n');
 return '#pragma once\n// Generated from decky_sidebar_health.js by tools/decky_sidebar_health_embed.mjs.\nnamespace ymcc::deckymirror {\ninline const char* healthScript(){return '+literals+';}\n}\n';
}
export function verifyHealthHeader(){if(!fs.existsSync(header)||fs.readFileSync(header,'utf8').replace(/\r\n/g,'\n')!==healthHeader())throw Error('Regenerate Decky health script header before building');}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){if(process.argv.includes('--check'))verifyHealthHeader();else fs.writeFileSync(header,healthHeader());}
