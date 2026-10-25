'use strict';
// Observe exact compiler bytes and file provenance. Business assertions unchanged.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),Module=require('node:module');
const root=process.env.YMCC_CONTROLLER_SOURCE_ROOT,trace=process.env.YMCC_CONTROLLER_TRACE_ROOT;
if(!root||!trace||!path.isAbsolute(root)||!path.isAbsolute(trace))throw Error('CONTROLLER_UI_OBSERVER_ARGUMENTS');
fs.mkdirSync(trace,{recursive:true});
const originalRead=fs.readFileSync.bind(fs),load=Module._load,sources=new Map(),tools=new Map(),wrapped=new WeakMap();
const sha=data=>crypto.createHash('sha256').update(data).digest('hex');
function record(file,data){const full=path.resolve(file),rel=path.relative(root,full);if(!rel.startsWith('..')&&!path.isAbsolute(rel)&&!rel.split(path.sep).includes('node_modules'))sources.set(full,sha(data));}
fs.readFileSync=function(file,...args){const data=originalRead(file,...args);if(typeof file==='string'&&path.isAbsolute(file))record(file,Buffer.isBuffer(data)?data:Buffer.from(data,typeof args[0]==='string'?args[0]:'utf8'));return data;};
Module._load=function(name,parent,isMain){
 let resolved;try{resolved=Module._resolveFilename(name,parent,isMain);if(path.isAbsolute(resolved)&&fs.statSync(resolved).isFile())tools.set(resolved,sha(originalRead(resolved)));}catch{}
 const actual=load.apply(this,arguments);if(name!=='esbuild')return actual;if(wrapped.has(actual))return wrapped.get(actual);
 const observer={...actual,async build(options){const result=await actual.build({...options,metafile:true});
  for(let i=0;i<result.outputFiles.length;i++)fs.writeFileSync(path.join(trace,`output-${i}.js`),result.outputFiles[i].contents);
  for(const name of Object.keys(result.metafile.inputs)){if(name.startsWith('<')||name.startsWith('fixture:'))continue;const file=path.resolve(options.absWorkingDir??process.cwd(),name);if(fs.existsSync(file)&&fs.statSync(file).isFile())record(file,originalRead(file));}
  fs.writeFileSync(path.join(trace,'build.json'),JSON.stringify({esbuildVersion:actual.version,outputFiles:result.outputFiles.length,metafile:result.metafile},null,2));return result;
 }};wrapped.set(actual,observer);return observer;
};
process.on('exit',()=>{
 fs.writeFileSync(path.join(trace,'observed-inputs.json'),JSON.stringify({schemaVersion:1,sources:[...sources].map(([path,sha256])=>({path,sha256})),tools:[...tools].map(([path,sha256])=>({path,sha256}))},null,2));
});
