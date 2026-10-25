import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools,verifyBundledToolScope,classifyBundledInput,bundledArtifacts} from '../bundled-node-tool-authority.mjs';
const need=(v,c)=>{if(!v)throw Error('TDP_SCOPE_'+c);};
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const hash=f=>sha(fs.readFileSync(f));
const key=f=>path.resolve(f).toLowerCase();
const inside=(r,f)=>{const p=path.relative(r,f);return p===''||(!path.isAbsolute(p)&&p!=='..'&&!p.startsWith('..'+path.sep));};
export const TDP_SCOPE_SOURCES=Object.freeze(['tools/ymcc_sim/global/tdp-consumer-signals/tool-scope.mjs',...BUNDLED_AUTHORITY_SOURCES]);
const external=p=>p.split(/[\\/]/).includes('node_modules');

// A reviewed byte pin is portable; its old absolute path remains provenance in
// toolchain-inputs.json. Explicit repoRelative aliases must stay inside the
// current physical dependency root, and every original file pin is rechecked.
export function resolveTdpToolchain(repo,reviewed){
 const root=fs.realpathSync(path.join(repo,'node_modules'));
 function resolve(relative,original){
  need(typeof relative==='string'&&relative.startsWith('node_modules/')&&!path.isAbsolute(relative)&&!relative.split(/[\\/]/).some(p=>p==='.'||p==='..'||p==='')&&!/[\0\r\n:]/.test(relative),'RELOCATED_TOOL_PATH');
  const normalized=original.replaceAll('\\','/');
  need(path.isAbsolute(original)&&normalized.endsWith('/'+relative),'RELOCATED_TOOL_DECLARATION');
  const target=fs.realpathSync(path.join(repo,relative));
  need(inside(root,target)&&key(root)!==key(target),'RELOCATED_TOOL_ESCAPE');
  return target;
 }
 const tool={...reviewed,files:reviewed.files.map(row=>({...row,path:row.repoRelative===undefined?row.path:resolve(row.repoRelative,row.path)}))};
 for(const field of ['esbuildModule','esbuildPackageRoot','esbuildExecutable'])if(reviewed[field+'RepoRelative']!==undefined)tool[field]=resolve(reviewed[field+'RepoRelative'],reviewed[field]);
 for(const row of tool.files)need(hash(row.path)===row.sha256,'RELOCATED_TOOL_BYTES');
 return tool;
}

export function tdpToolDeclaration(repo,declared,legacy){
 legacy=resolveTdpToolchain(repo,legacy);
 const authority=lockBundledNodeTools(repo),pins=new Map(authority.tools.map(r=>[key(r.path),r]));
 // Legacy physical package entries are retained as exact tool obligations. None are discarded.
 const toolDeclarations=declared.filter(external).map(relative=>{
  need(relative.startsWith('node_modules/')&&!relative.split(/[\\/]/).includes('..'),'DECLARATION_PATH');
  const file=fs.realpathSync(path.join(authority.nodeModulesRealRoot,relative.slice('node_modules/'.length))),pin=pins.get(key(file));
  need(pin&&hash(file)===pin.sha256,'DECLARED_TOOL_NOT_BOUND:'+relative);
  return {declared:relative,...pin};
 });
 for(const row of legacy.files){need(hash(row.path)===row.sha256,'LEGACY_TOOL_CHANGED:'+row.path);if(external(row.path))need(pins.get(key(row.path))?.sha256===row.sha256,'LEGACY_TOOL_NOT_IN_AUTHORITY');}
 need(key(legacy.esbuildModule)===key(authority.compilerEntry)&&key(legacy.esbuildExecutable)===key(authority.compilerBinary),'COMPILER_IDENTITY');
 const tools=bundledArtifacts(authority,legacy.files,[],3072);
 return {authority,tools,toolDeclarations,sources:[...new Set([...declared.filter(p=>!external(p)),...TDP_SCOPE_SOURCES])]};
}
export function beginTdpScope(repo,declared,legacy){
 const scope=tdpToolDeclaration(repo,declared,legacy);scope.repo=fs.realpathSync(repo);scope.reads=[];scope.before=scope.tools.map(r=>({...r,observedSha256:hash(r.path)}));return scope;
}
export function observeTdpInput(scope,file,bytes,{label='compiler',faultOutput=null}={}){
 const result=classifyBundledInput(scope.repo,file,bytes,scope.authority);
 scope.reads.push({...result.row,kind:result.role==='tool'?'external-tool':'repo-source',role:label,executedBytesSha256:faultOutput?hash(faultOutput):sha(bytes),faultOutput});return result;
}
export function tdpLogicalInput(scope,file){
 const real=fs.realpathSync(file);
 if(inside(scope.authority.nodeModulesRealRoot,real)){
  need(scope.authority.tools.some(r=>key(r.path)===key(real)&&r.sha256===hash(real)),'ACTUAL_GRAPH_TOOL_UNBOUND');
  return 'node_modules/'+path.relative(scope.authority.nodeModulesRealRoot,real).replaceAll('\\','/');
 }
 need(inside(scope.repo,real)&&!external(real),'ACTUAL_GRAPH_SOURCE_ESCAPE');return path.relative(scope.repo,real).replaceAll('\\','/');
}
export function finishTdpScope(scope){
 verifyBundledToolScope(scope.authority,scope.repo,{limit:3072});
 for(const row of scope.tools)need(hash(row.path)===row.sha256,'TOOL_CHANGED_AFTER:'+row.path);
 return {schemaVersion:1,status:'PASS',authority:scope.authority,staticArtifactInputs:scope.tools,legacyPhysicalToolDeclarations:scope.toolDeclarations,before:scope.before,after:scope.tools.map(r=>({...r,observedSha256:hash(r.path)})),actualCompilerConsumerReads:scope.reads,meaning:'Static full package authority, actual compiler reads and validation rehashes are separate. No OS loader graph or hardware acceptance is claimed.'};
}
export function writeTdpOutputEvidence(root,files){
 const real=fs.realpathSync(root);const rows=files.map(file=>{
  const actual=fs.realpathSync(file);need(inside(real,actual)&&inside(path.resolve(root),path.resolve(file))&&!external(file),'OUTPUT_OUTSIDE_CASE');
  let role='owned-consumer-evidence';if(file.includes('input-sources')||file.includes('source-repo'))role='owned-source-mirror';if(/fault-source|negative/.test(file))role='owned-negative-case';if(/program\.cjs|build-.*\.mjs|rebuild|under-test\.mjs|\.marshal$/.test(file))role='owned-compiler-output';
  return {path:path.resolve(file),sha256:hash(file),kind:'generated-output',role};
 });
 need(rows.length<=1500&&new Set(rows.map(r=>key(r.path))).size===rows.length,'OUTPUT_BUDGET_OR_DUPLICATE');
 fs.writeFileSync(path.join(root,'output-evidence.json'),JSON.stringify({schemaVersion:1,status:'PASS',passed:true,sourceHashes:rows,generatedOutputCount:rows.length,productSourceCount:0,meaning:'Owned generated files only; product sources are exclusively the repository-relative result.json sourceHashes. This index excludes itself.'},null,2)+'\n');
}
