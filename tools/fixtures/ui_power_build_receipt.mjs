import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';

export function observeUiPowerBuilds(root){
 const require=createRequire(path.join(root,'package.json'));
 const {build}=require('esbuild');
 const graphs=[],externalImports=[];
 let compilerEntry=null,compilerExecutable=null;
 try{
  compilerEntry=fs.realpathSync(require.resolve('esbuild'));
  if(process.platform==='win32'&&process.arch==='x64'&&!process.env.ESBUILD_BINARY_PATH){
   compilerExecutable=fs.realpathSync(createRequire(compilerEntry).resolve('@esbuild/win32-x64/esbuild.exe'));
  }
 }catch{/* Allow original execution; an incomplete tool observation cannot be reused. */}
 return {
  async build(options){
   const result=await build({...options,metafile:true});
   graphs.push(Object.keys(result.metafile.inputs).map(p=>p.replaceAll('\\','/')).sort());
   externalImports.push(Object.values(result.metafile.outputs).flatMap(output=>output.imports.filter(input=>input.external).map(input=>input.path)).sort());
   return result;
  },
  receipt:()=>({graphs,externalImports,compilerEntry,compilerExecutable,nodeExecutable:process.execPath}),
 };
}
