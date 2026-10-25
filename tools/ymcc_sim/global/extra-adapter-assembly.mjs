/** Registration declarations only. Execution/audit retain their original source,
 * artifact, completion and lifecycle gates. No tool verifier is memoized globally. */
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
const digest=value=>createHash('sha256').update(value).digest('hex');
const cache=new Map(),original=Object.fromEntries(['readFileSync','existsSync','readdirSync','statSync','lstatSync','realpathSync','accessSync'].map(name=>[name,fs[name]]));
const nativeRealpath=fs.realpathSync.native;
const environment=()=>JSON.stringify([process.execPath,process.cwd(),Object.entries(process.env).sort(([a],[b])=>a.localeCompare(b))]);
function observation(method,args){try{const value=original[method](...args);if(method==='readFileSync')return{ok:true,sha256:digest(value)};if(method==='statSync'||method==='lstatSync')return{ok:true,identity:[String(value.dev),String(value.ino),String(value.size),String(value.mtimeMs),String(value.ctimeMs),value.isFile(),value.isDirectory(),value.isSymbolicLink()]};if(method==='readdirSync')return{ok:true,entries:value.map(row=>typeof row==='string'?row:{name:row.name,file:row.isFile(),directory:row.isDirectory(),link:row.isSymbolicLink()})};return{ok:true,value};}catch(error){return{ok:false,code:error.code};}}
function key(method,args){return method+'|'+JSON.stringify(args);}
export function observeRegistration(factory){const observations=new Map();try{for(const method of Object.keys(original)){const implementation=original[method];fs[method]=function(...args){let result,error;try{result=implementation.apply(this,args);}catch(e){error=e;}let value;if(error)value={ok:false,code:error.code};else if(method==='readFileSync')value={ok:true,sha256:digest(result)};else if(method==='statSync'||method==='lstatSync')value={ok:true,identity:[String(result.dev),String(result.ino),String(result.size),String(result.mtimeMs),String(result.ctimeMs),result.isFile(),result.isDirectory(),result.isSymbolicLink()]};else if(method==='readdirSync')value={ok:true,entries:result.map(row=>typeof row==='string'?row:{name:row.name,file:row.isFile(),directory:row.isDirectory(),link:row.isSymbolicLink()})};else value={ok:true,value:result};observations.set(key(method,args),{method,args,value});if(error)throw error;return result;};if(method==='realpathSync')fs[method].native=nativeRealpath;}
 const rows=factory();return{rows,observations:[...observations.values()]};}finally{for(const [method,fn]of Object.entries(original))fs[method]=fn;fs.realpathSync.native=nativeRealpath;}}
function unchanged(entry,shared){for(const row of entry.observations){const id=key(row.method,row.args);if(!shared.has(id))shared.set(id,observation(row.method,row.args));if(JSON.stringify(shared.get(id))!==JSON.stringify(row.value))return false;}return true;}
export function assembleExtraAdapters(factories,index,{repoRoot,selectedIds,auditReceiptIds,indexCurrent=true}={}){
 const selection=Array.isArray(selectedIds)&&selectedIds.length?new Set(selectedIds):null,audit=Array.isArray(auditReceiptIds)?new Set(auditReceiptIds):null,env=environment(),shared=new Map(),result=[];
 for(let i=0;i<factories.length;i++){
  const identity=index.groups[i];
  // Missing registrations need identity only. Any supplied execution, explicit
  // selection, or declared lifecycle capability is materialized by its getter.
  const wanted=!indexCurrent||(!selection&&!audit)||identity.rows.some(row=>selection?.has(row.id)||audit?.has(row.id)||(!selection&&audit&&row.lifecycleDeclared));
  if(!wanted){if(audit&&!selection)result.push(...identity.rows.map(({lifecycleDeclared,...row})=>({...row,registrationIdentityOnly:true})));continue;}
  const id=path.resolve(repoRoot)+'|'+i+'|'+JSON.stringify(selectedIds??null),prior=cache.get(id);let rows;
  if(prior&&prior.env===env&&unchanged(prior,shared))rows=structuredClone(prior.rows);
  else{const captured=observeRegistration(factories[i]);cache.set(id,{...captured,env,rows:structuredClone(captured.rows)});rows=captured.rows;}
  if(indexCurrent){const actual=rows.map(row=>({id:row.id,domain:row.domain,profiles:row.profiles,lifecycleDeclared:row.lifecycleContract!==undefined}));if(JSON.stringify(actual)!==JSON.stringify(identity.rows))throw Error('EXTRA_REGISTRATION_INDEX_CHANGED:'+i);}
  result.push(...rows);
 }
 return selection&&indexCurrent?result.filter(row=>selection.has(row.id)):result;
}
export function registrationIndexCurrent(index,repoRoot){return index.sources.every(row=>{try{return digest(original.readFileSync(path.join(repoRoot,row.path)))===row.sha256;}catch{return false;}});}
