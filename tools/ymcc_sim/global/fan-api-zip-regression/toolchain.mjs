/** Exact local tools; no PATH search, download or installation. */
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
export const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
export const hashFile = file => sha256(fs.readFileSync(file));
export function toolRow(file,role) {
 const actual=fs.realpathSync(file),stat=fs.statSync(actual);
 if(!stat.isFile()) throw Error('TOOL_NOT_REGULAR_FILE');
 return {path:actual,bytes:stat.size,sha256:hashFile(actual),role};
}
export function discoverTools(repoRoot,pythonPath) {
 const require=createRequire(path.join(repoRoot,'package.json'));
 const python=pythonPath??process.env.YMCC_SIM_PYTHON??path.join(os.homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
 if(typeof python!=='string'||!path.isAbsolute(python)||/[\0\r\n]/.test(python)||/(?:^|[\\/])WindowsApps(?:[\\/]|$)/i.test(python)||!/^python(?:3(?:\.\d+)?)?(?:\.exe)?$/i.test(path.basename(python)))throw Error('EXPLICIT_EXISTING_PYTHON_REQUIRED');
 if(/(?:^|[\\/])WindowsApps(?:[\\/]|$)/i.test(fs.realpathSync(python)))throw Error('PYTHON_ALIAS_NOT_ALLOWED');
 const rows=[toolRow(process.execPath,'executed-node'),toolRow(python,'executed-python')];
 const packages={};
 for(const name of ['typescript','vue','esbuild']) {
  try {
   const main=require.resolve(name),metadata=require.resolve(name+'/package.json'),version=JSON.parse(fs.readFileSync(metadata,'utf8')).version;
   const role=name==='esbuild'?'identified-not-executed':'actual-fixture-tool';
   packages[name]={version,main:fs.realpathSync(main),metadata:fs.realpathSync(metadata),executed:name!=='esbuild'};
   rows.push(toolRow(main,role),toolRow(metadata,role+'-metadata'));
   if(name==='esbuild') {
    try {const binary=createRequire(main).resolve('@esbuild/win32-x64/esbuild.exe');rows.push(toolRow(binary,'identified-not-executed-esbuild-native'));packages[name].native=fs.realpathSync(binary);}catch{packages[name].native=null;}
   }
  } catch(error) {
   if(name!=='esbuild')throw Error('REQUIRED_FIXTURE_TOOL_UNAVAILABLE:'+name);
   packages.esbuild={executed:false,available:false};
  }
 }
 return {node:fs.realpathSync(process.execPath),nodeVersion:process.version,python:fs.realpathSync(python),packages,rows};
}
export function loadedCommonJsTools(repoRoot) {
 const require=createRequire(path.join(repoRoot,'package.json'));
 return Object.keys(require.cache).filter(file=>/[\\/]node_modules[\\/]/i.test(file)).sort().map(file=>toolRow(file,'loaded-commonjs-tool'));
}
export function verifyToolRows(rows) {
 return rows.filter(row=>{try{return hashFile(row.path)!==row.sha256;}catch{return true;}}).map(row=>row.path);
}
