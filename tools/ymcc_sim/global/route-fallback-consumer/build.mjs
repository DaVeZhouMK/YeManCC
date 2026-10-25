import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {MODULE, PRODUCT, hash, sha, need, write} from './contract.mjs';
export async function buildConsumer(root,out,tools,fault=null) {
  fs.mkdirSync(out,{recursive:true});
  const productFile=path.join(root,PRODUCT),ports=path.join(root,MODULE,'memory-ports.ts');
  const bytes=fs.readFileSync(productFile),original=bytes.toString('utf8');
  let compiled=original;
  if(fault){const from=original.includes('\r\n')?fault.from.replaceAll('\n','\r\n'):fault.from;
    need(original.split(from).length===2,'FAULT_ANCHOR_NOT_UNIQUE:'+fault.id);
    compiled=original.replace(from,fault.to);}
  const observed=[];
  const esbuild=createRequire(import.meta.url)(tools.authority.compilerEntry);
  const result=await esbuild.build({absWorkingDir:root,entryPoints:[productFile],bundle:true,write:false,
    platform:'browser',format:'iife',globalName:'RouteFallbackProduct',target:'es2022',metafile:true,
    sourcemap:'external',outfile:path.join(out,'consumer.js'),inject:[ports],logLevel:'silent',
    plugins:[{name:'route-fallback-memory-terminals',setup(build){
      build.onResolve({filter:/^\.\.\/bridge\/(api|settingsRepository)$/},args=>{
        need(path.resolve(args.importer)===productFile,'UNEXPECTED_BOUNDARY_IMPORTER');
        return {path:ports};
      });
      build.onLoad({filter:/routeFallback\.ts$/},args=>{
        need(path.resolve(args.path)===productFile,'UNEXPECTED_PRODUCT');
        observed.push({path:PRODUCT,rawSha256:sha(bytes),compiledSha256:sha(compiled),mutated:Boolean(fault)});
        return{contents:compiled,loader:'ts',resolveDir:path.dirname(productFile)};
      });
    }}]});
  const outputs=[];
  for(const file of result.outputFiles){fs.writeFileSync(file.path,file.contents);outputs.push({path:file.path,sha256:hash(file.path)});}
  const inputPaths=Object.keys(result.metafile.inputs);
  need(inputPaths.length===2&&inputPaths.includes(PRODUCT)&&inputPaths.includes(MODULE+'/memory-ports.ts'),'UNEXPECTED_COMPILER_INPUTS');
  const report={status:'COMPILED_REAL_PRODUCT_SOURCE',product:observed,compilerInputs:inputPaths,
    compiler:{entry:tools.authority.compilerEntry,binary:tools.authority.compilerBinary,version:esbuild.version},
    outputs,fault,boundary:'Only imported dialog/reset ports and injected window proxy; document/HTMLElement/event/focus/click semantics are native Chromium DOM. Product normal bytes unchanged.',
    esbuildMetafile:result.metafile};
  write(path.join(out,'build.json'),report);
  return report;
}
