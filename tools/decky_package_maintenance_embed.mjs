import fs from 'node:fs';import path from 'node:path';import{fileURLToPath}from'node:url';
const root=path.resolve(import.meta.dirname,'..'),source=path.join(root,'native/decky_package_maintenance.ps1'),header=path.join(root,'native/decky_package_maintenance_script.h');
export function maintenanceHeader(){
  const ps=fs.readFileSync(source,'utf8').replace(/^\uFEFF/,'').replace(/\r\n/g,'\n');
  if(ps.includes(')YMCCDECKYPKG"'))throw Error('Package maintenance delimiter collision');
  // MSVC limits a single literal token to 16KiB. Split at complete source lines
  // into <=8KiB UTF-8 chunks; adjacent literals preserve the exact script bytes.
  const chunks=[];let chunk='';
  for(const line of ps.match(/[^\n]*\n|[^\n]+$/g)||[]){
    if(Buffer.byteLength(line,'utf8')>8192)throw Error('Package maintenance source line exceeds literal chunk budget');
    if(Buffer.byteLength(chunk+line,'utf8')>8192){chunks.push(chunk);chunk='';}
    chunk+=line;
  }
  if(chunk||!chunks.length)chunks.push(chunk);
  const literals=chunks.map(value=>'R"YMCCDECKYPKG('+value+')YMCCDECKYPKG"').join('\n');
  return '#pragma once\n// Generated from decky_package_maintenance.ps1 by tools/decky_package_maintenance_embed.mjs.\nnamespace ymcc::deckypackage {\ninline const char* maintenanceScript(){return '+literals+';}\n}\n';
}
export function verifyMaintenanceHeader(){if(!fs.existsSync(header)||fs.readFileSync(header,'utf8').replace(/\r\n/g,'\n')!==maintenanceHeader())throw Error('Decky package maintenance source/header differ; regenerate before building');}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){if(process.argv.includes('--check'))verifyMaintenanceHeader();else fs.writeFileSync(header,maintenanceHeader());}
