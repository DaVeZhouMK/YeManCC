import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {createHash} from 'node:crypto';
import {BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools,classifyBundledInput,verifyBundledToolScope,verifyBundledImplementation} from '../bundled-node-tool-authority.mjs';
export const RADIAL_HOST_TOOL_SOURCES=Object.freeze([...BUNDLED_AUTHORITY_SOURCES,'tools/ymcc_sim/global/radial-host-tool-scope/index.mjs']);
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
const need=(ok,code)=>{if(!ok)throw Error('RADIAL_HOST_'+code);};
export function radialHostToolDeclaration(repoRoot,options={}){
 try{return {artifactInputs:lockBundledNodeTools(repoRoot,options).tools.map(row=>row.path)};}
 catch(error){return {artifactInputs:[],blockedReason:'RADIAL_HOST_TOOL_AUTHORITY_UNAVAILABLE: '+error.message};}
}
export function beginRadialHostTools(repoRoot,outputFile,options={}){
 const repo=fs.realpathSync(repoRoot);verifyBundledImplementation(repo);
 const authority=lockBundledNodeTools(repo,options),sourceHashes={};
 for(const relative of [...RADIAL_HOST_TOOL_SOURCES,'tools/radial_host_selftest.mjs'])sourceHashes[relative]=hash(path.join(repo,relative));
 const output=outputFile?path.resolve(outputFile):path.join(fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-radial-host-')),'action-host.json');
 return {repo,output,authority,sourceHashes,sources:new Map(),tools:new Map(),report:{authority,compiledArtifacts:[],toolDependencyInputs:[],scopeVerified:false}};
}
function observe(scope,file,contents){
 const classified=classifyBundledInput(scope.repo,file,contents,scope.authority);
 (classified.role==='tool'?scope.tools:scope.sources).set(classified.row.path,classified.row);
 return classified;
}
export function radialHostInputPlugin(scope){return {name:'radial-host-exact-input-scope',setup(build){
 build.onLoad({filter:/\.(?:[cm]?js|jsx|tsx?|json)$/,namespace:'file'},({path:file})=>{
  const contents=fs.readFileSync(file);observe(scope,file,contents);
  const extension=path.extname(file).slice(1);
  return {contents:contents.toString('utf8'),resolveDir:path.dirname(file),loader:['ts','tsx','jsx','json'].includes(extension)?extension:'js'};
 });
}};}
export function verifyRadialHostTools(scope){
 const proof=verifyBundledToolScope(scope.authority,scope.repo);
 for(const [relative,digest]of Object.entries(scope.sourceHashes))need(hash(path.join(scope.repo,relative))===digest,'SOURCE_CHANGED:'+relative);
 for(const row of scope.report.compiledArtifacts)need(hash(row.path)===row.sha256,'COMPILED_ARTIFACT_CHANGED:'+row.path);
 scope.report.scopeVerified=true;scope.report.toolDependencyInputs=[...scope.tools.values()];
 scope.report.sourceCount=Object.keys(scope.sourceHashes).length;
 return {graphSha256:proof.graphSha256,packageCount:proof.packageCount,fileCount:proof.fileCount,toolCount:proof.tools.length,toolAuthorityVerified:true};
}
export function completeRadialHostCompile(scope,compiled,virtualModules){
 const observed=new Map([...scope.sources.values(),...scope.tools.values()].map(row=>[key(path.isAbsolute(row.path)?row.path:path.join(scope.repo,row.path)),row.sha256]));
 for(const input of Object.keys(compiled.metafile.inputs)){
  if(input==='<stdin>')continue;
  if(input.startsWith('fixture:')){need(virtualModules.includes(input.slice(8)),'UNDECLARED_VIRTUAL_INPUT');continue;}
  const file=path.resolve(scope.repo,input),contents=fs.readFileSync(file),classified=classifyBundledInput(scope.repo,file,contents,scope.authority);
  need(observed.get(key(fs.realpathSync(file)))===classified.row.sha256,'METAFILE_INPUT_NOT_OBSERVED:'+input);
 }
 for(const row of scope.sources.values())scope.sourceHashes[row.path]=row.sha256;
 need(compiled.outputFiles.length===1,'EXACT_SINGLE_EXECUTED_BUNDLE');
 const artifacts=[{path:scope.output+'.bundle.cjs',bytes:compiled.outputFiles[0].contents,role:'actual executed nested bundle'},{path:scope.output+'.metafile.json',bytes:Buffer.from(JSON.stringify(compiled.metafile,null,2)+'\n'),role:'actual nested compiler metafile'}];
 for(const artifact of artifacts){fs.writeFileSync(artifact.path,artifact.bytes,{flag:'wx'});scope.report.compiledArtifacts.push({path:artifact.path,sha256:hash(artifact.path),role:artifact.role});}
 scope.report.bundleByteLength=compiled.outputFiles[0].contents.length;
 scope.report.afterCompileProof=verifyRadialHostTools(scope);
 return scope.report;
}
