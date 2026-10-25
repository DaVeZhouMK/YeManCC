'use strict';
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module'),child=require('node:child_process');
const {makePaths}=require('./paths.cjs');
const caseRoot=path.resolve(process.env.YMCC_TDP_DISPLAY_ROOT),sourceRoot=path.resolve(process.cwd());
const tool=JSON.parse(fs.readFileSync(process.env.YMCC_TDP_DISPLAY_PROFILE,'utf8'));
const mapping=makePaths(caseRoot,sourceRoot),native={resolve:path.resolve.bind(path),read:fs.readFileSync.bind(fs),write:fs.writeFileSync.bind(fs),mkdir:fs.mkdirSync.bind(fs),exists:fs.existsSync.bind(fs),realpath:fs.realpathSync.bind(fs),spawn:child.spawn};
const observation={outputRedirects:0,denied:[],compilerProcesses:0,networkAttempts:0,builds:[]};
process.once('exit',code=>{native.write(path.join(caseRoot,'boundary-observation.json'),JSON.stringify({...observation,exitCode:code,sourceRoot,legacyOutputNotTouched:mapping.legacy,privateOutput:mapping.output},null,2));});
native.mkdir(mapping.output,{recursive:true});
const readRoots=[caseRoot,sourceRoot,path.dirname(process.execPath),tool.esbuildPackageRoot,path.dirname(tool.esbuildExecutable)];
function redirected(file){const result=mapping.redirect(file);if(result.redirected)observation.outputRedirects++;return result.path;}
function target(file,writing){
 if(typeof file!=='string'&&!(file instanceof URL))throw Error('TDP_DISPLAY_PATH_TYPE');
 const requested=file instanceof URL?require('node:url').fileURLToPath(file):file,actual=redirected(requested);
 if(writing&&!mapping.inside(caseRoot,actual)){observation.denied.push('WRITE_OUTSIDE_CASE');throw Error('TDP_DISPLAY_WRITE_OUTSIDE_CASE');}
 if(!writing&&!readRoots.some(root=>mapping.inside(root,actual))){observation.denied.push('READ_OUTSIDE_REVIEWED_ROOTS');throw Error('TDP_DISPLAY_READ_OUTSIDE_REVIEWED_ROOTS');}
 let existing=actual;while(!native.exists(existing)&&existing!==path.dirname(existing))existing=path.dirname(existing);
 const resolved=native.realpath(existing);
 if(writing&&!mapping.inside(native.realpath(caseRoot),resolved)){observation.denied.push('WRITE_REPARSE_ESCAPE');throw Error('TDP_DISPLAY_REPARSE_ESCAPE');}
 if(!writing&&!readRoots.some(root=>mapping.inside(native.realpath(root),resolved))){observation.denied.push('READ_REPARSE_ESCAPE');throw Error('TDP_DISPLAY_READ_REPARSE_ESCAPE');}
 return actual;
}
// Never use path.relative within this hook: its Win32 implementation calls
// the replaced public path.resolve and would recurse before test execution.
path.resolve=function(...args){return redirected(native.resolve(...args));};
for(const[name,writing]of[['readFileSync',false],['writeFileSync',true],['mkdirSync',true],['existsSync',false],['statSync',false],['readdirSync',false]]){const original=fs[name].bind(fs);fs[name]=function(file,...args){return original(target(file,writing),...args);};}
const expected=native.realpath(tool.esbuildExecutable);
child.spawn=function(program,args,options){if(native.realpath(program)!==expected||JSON.stringify(args)!==JSON.stringify(['--service='+tool.esbuildVersion,'--ping'])){observation.denied.push('UNREVIEWED_PROCESS');throw Error('TDP_DISPLAY_PROCESS_FORBIDDEN');}observation.compilerProcesses++;return native.spawn.call(this,program,args,options);};
for(const name of ['spawnSync','exec','execSync','execFile','execFileSync','fork'])child[name]=function(){observation.denied.push('UNREVIEWED_PROCESS');throw Error('TDP_DISPLAY_PROCESS_FORBIDDEN');};
function noNetwork(){observation.networkAttempts++;throw Error('TDP_DISPLAY_NETWORK_FORBIDDEN');}
for(const name of ['node:http','node:https']){const api=require(name);api.request=noNetwork;api.get=noNetwork;}require('node:net').Socket.prototype.connect=noNetwork;globalThis.fetch=noNetwork;
const originalLoad=Module._load,wrappers=new WeakMap();let sequence=0;
Module._load=function(name,parent,isMain){const actual=originalLoad.apply(this,arguments);if(name!=='esbuild')return actual;if(wrappers.has(actual))return wrappers.get(actual);
 const wrapper={...actual,async build(options){const result=await actual.build({...options,metafile:true}),id=String(++sequence).padStart(3,'0');
  for(let index=0;index<result.outputFiles.length;index++)native.write(path.join(caseRoot,'build-'+id+'-'+index+'.mjs'),result.outputFiles[index].contents);
  native.write(path.join(caseRoot,'build-'+id+'.json'),JSON.stringify({workingDirectory:options.absWorkingDir??process.cwd(),options,metafile:result.metafile,outputFiles:result.outputFiles.length},null,2));observation.builds.push({id,outputs:result.outputFiles.length});return result;
 }};wrappers.set(actual,wrapper);return wrapper;
};
require('node:module').syncBuiltinESMExports();
