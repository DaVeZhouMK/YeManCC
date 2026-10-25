import fs from 'node:fs';
import path from 'node:path';
import childProcess from 'node:child_process';
import {createRequire} from 'node:module';
import {authorizeInputFile,fileHash,digest,need,fileKey} from './inputs.mjs';
import {save} from './identity.mjs';
import {stopInputCompilerServices} from './service-lifecycle.mjs';

async function compile(session,entry) {
  const {repoRoot,declaration,directory}=session,tool=declaration.tool;
  const require=createRequire(path.join(repoRoot,'package.json'));
  const requireBefore=new Set(Object.keys(require.cache)),esbuild=require('esbuild');
  const actual=new Map(),services=[],serviceChildren=[],terminals=[];
  const originalSpawn=childProcess.spawn,oldBinary=process.env.ESBUILD_BINARY_PATH;
  need(fileKey(fs.realpathSync(require.resolve('esbuild')))===fileKey(fs.realpathSync(tool.esbuildModule)),'INPUT_ESBUILD_MODULE_NOT_DECLARED');
  authorizeInputFile(declaration,tool.esbuildModule,'esbuild-node-module');
  process.env.ESBUILD_BINARY_PATH=tool.esbuildExecutable;
  childProcess.spawn=function(executable,args,options) {
    need(fileKey(fs.realpathSync(executable))===fileKey(fs.realpathSync(tool.esbuildExecutable)),'INPUT_UNDECLARED_ESBUILD_SUBPROCESS');
    authorizeInputFile(declaration,executable,'actual-esbuild-subprocess');
    const child=originalSpawn.call(this,executable,args,options);
    const record={path:executable,sha256:fileHash(executable),args,pid:child.pid,startedAt:new Date().toISOString(),closeObserved:false};
    services.push(record);serviceChildren.push(child);
    terminals.push(new Promise(resolve=>{
      child.once('error',error=>{record.error={code:error.code??null,message:error.message};});
      child.once('close',(exitCode,signal)=>{Object.assign(record,{closeObserved:true,endedAt:new Date().toISOString(),exitCode,signal});resolve();});
    }));
    return child;
  };
  const entryFile=path.join(directory,'input-signals-entry.ts');fs.writeFileSync(entryFile,entry);
  let result,failure;
  try {
    result=await esbuild.build({
      stdin:{contents:entry,resolveDir:repoRoot,sourcefile:'input-signals-entry.ts',loader:'ts'},
      absWorkingDir:repoRoot,bundle:true,write:false,platform:'browser',format:'iife',
      globalName:'YMCCInputSignals',target:'es2022',metafile:true,logLevel:'silent',
      define:{'process.env.NODE_ENV':'"production"'},
      plugins:[{name:'declared-actual-input-source-bytes',setup(build){
        build.onLoad({filter:/\.(?:[cm]?[jt]sx?|json)$/},args=>{
          const row=authorizeInputFile(declaration,args.path,'actual-esbuild-read');
          const contents=fs.readFileSync(args.path);
          need(digest(contents)===row.sha256,'INPUT_ESBUILD_READ_DRIFT');
          actual.set(fileKey(fs.realpathSync(args.path)),row);
          return {contents,loader:/\.json$/.test(args.path)?'json':/\.[cm]?tsx?$/.test(args.path)?'ts':'js',resolveDir:path.dirname(args.path)};
        });
      }}]
    });
  } catch(error) {
    failure=error;
  } finally {
    try{await stopInputCompilerServices(serviceChildren,()=>esbuild.stop(),terminals);}
    finally{childProcess.spawn=originalSpawn;if(oldBinary===undefined)delete process.env.ESBUILD_BINARY_PATH;else process.env.ESBUILD_BINARY_PATH=oldBinary;}
  }
  if(failure){save(path.join(directory,'product-compiler-failure.json'),{message:failure.message,entry:{path:entryFile,sha256:fileHash(entryFile)},actualByteReads:[...actual.values()],esbuildServices:services});throw failure;}
  const bundle=path.join(directory,'product.iife.js'),metafile=path.join(directory,'metafile.json');
  fs.writeFileSync(bundle,result.outputFiles[0].text);save(metafile,result.metafile);
  need(services.length===1&&services.every(row=>row.pid&&row.closeObserved&&!row.error),'INPUT_ESBUILD_SERVICE_EVIDENCE_MISSING');
  const observed=[];
  for(const file of Object.keys(require.cache).filter(file=>!requireBefore.has(file))) {
    if(file.includes('node_modules'))observed.push(authorizeInputFile(declaration,file,'observed-cjs-cache-addition'));
  }
  const metafileInputs=[];
  for(const item of Object.keys(result.metafile.inputs)) {
    if(item==='input-signals-entry.ts')continue;
    const file=path.isAbsolute(item)?item:path.resolve(repoRoot,item);
    metafileInputs.push(authorizeInputFile(declaration,file,'actual-esbuild-metafile-input'));
  }
  const sources=metafileInputs.filter(row=>!path.isAbsolute(row.path)).map(row=>({path:row.path,sha256:row.sha256}));
  const identity={schemaVersion:1,status:'PASS',entry:{path:entryFile,sha256:fileHash(entryFile)},
    bundle:{path:bundle,sha256:fileHash(bundle)},metafile:{path:metafile,sha256:fileHash(metafile)},
    metafileInputs,actualByteReads:[...actual.values()],observedCjsCacheAdditions:observed,esbuildServices:services,
    declaredToolFilesAreNotLoadedGraph:true,
    scope:'Compiler byte/metafile and Node cache observations; package metadata/configuration are separately declared. Not all OS modules or all product branches.'};
  save(path.join(directory,'product-compiler-inputs.json'),identity);
  return {code:result.outputFiles[0].text,sources,identity};
}
export async function compileInputProduct(session,entry) {
  session.productPromise??=compile(session,entry);
  session.product=await session.productPromise;
  return session.product;
}
