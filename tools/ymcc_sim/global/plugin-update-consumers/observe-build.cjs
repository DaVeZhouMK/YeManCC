// Observe exact compiler output and optional original uncaught assertion. Never suppress or replace the exception.
'use strict';
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module');
const trace=process.env.YMCC_PLUGIN_TRACE_ROOT,sourceRoot=process.env.YMCC_PLUGIN_SOURCE_ROOT;
if(!trace||!sourceRoot||!path.isAbsolute(trace)||!path.isAbsolute(sourceRoot))throw Error('PLUGIN_BUILD_OBSERVER_ARGUMENTS');
fs.mkdirSync(trace,{recursive:true});
const original=Module._load,wrappers=new WeakMap();let sequence=0;
Module._load=function(name,parent,isMain){const actual=original.apply(this,arguments);if(name!=='esbuild')return actual;if(wrappers.has(actual))return wrappers.get(actual);
 const value={...actual,async build(options){const result=await actual.build({...options,metafile:true}),id=String(++sequence).padStart(3,'0');
  for(let n=0;n<result.outputFiles.length;n++)fs.writeFileSync(path.join(trace,`${id}-${n}.mjs`),result.outputFiles[n].contents);
  fs.writeFileSync(path.join(trace,`${id}.json`),JSON.stringify({schemaVersion:1,esbuildVersion:actual.version,sourceRoot,workingDirectory:options.absWorkingDir??process.cwd(),options,metafile:result.metafile,outputFiles:result.outputFiles.length},null,2));return result;
 }};wrappers.set(actual,value);return value;
};

// An uncaughtExceptionMonitor observer does not install an exception handler or change Node's exit.
if(process.env.YMCC_PLUGIN_FAILURE_FILE){
 const crypto=require('node:crypto'),failureFile=process.env.YMCC_PLUGIN_FAILURE_FILE,script=process.env.YMCC_PLUGIN_ASSERTION_SCRIPT;
 const inside=(root,file)=>{const relative=path.relative(root,file);return relative!==''&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};
 if(!path.isAbsolute(failureFile)||!script||!path.isAbsolute(script)||!inside(path.dirname(trace),failureFile)||!inside(sourceRoot,script))throw Error('PLUGIN_ASSERTION_OBSERVER_PATH_INVALID');
 const describe=value=>typeof value==='function'?{type:'function',name:value.name}:['string','boolean','number'].includes(typeof value)||value===null?{type:value===null?'null':typeof value,value}:{type:typeof value};
 process.on('uncaughtExceptionMonitor',error=>{
  const record={schemaVersion:1,observedBy:'uncaughtExceptionMonitor',exceptionUnmodified:true,script:path.resolve(script),scriptSha256:crypto.createHash('sha256').update(fs.readFileSync(script)).digest('hex'),name:error?.name??null,code:error?.code??null,message:String(error?.message??'').slice(0,4096),stack:String(error?.stack??'').slice(0,65536),assertion:{operator:error?.operator??null,actual:describe(error?.actual),expected:describe(error?.expected)}};
  fs.writeFileSync(failureFile,JSON.stringify(record,null,2)+'\n',{flag:'wx'});
 });
}
