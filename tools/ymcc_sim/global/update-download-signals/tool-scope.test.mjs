import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {verifyUpdateToolEvidence,lockBundledNodeTools,ownedUpdateOutputs} from './tool-scope.mjs';
import {bundledArtifacts} from '../bundled-node-tool-authority.mjs';
import {simulationCorePath} from '../release-host-environment.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const evidence=process.env.YMCC_UPDATE_TOOL_TEST_ROOT;
const within=(root,file)=>{const r=path.relative(path.resolve(root),path.resolve(file));return r===''||(!r.startsWith('..'+path.sep)&&!path.isAbsolute(r));};
const key=p=>process.platform==='win32'?path.resolve(p).toLowerCase():path.resolve(p);
const sha=b=>createHash('sha256').update(b).digest('hex');
const write=(file,data)=>fs.writeFileSync(file,data,{flag:'wx'});
const workspace=()=>simulationCorePath('Workspace');
const sameDir=(dir,original)=>{
 const now=fs.lstatSync(dir,{bigint:true});
 assert(now.isDirectory()&&!now.isSymbolicLink()&&key(fs.realpathSync.native(dir))===key(dir));
 if(original)assert(now.dev===original.dev&&now.ino===original.ino&&now.birthtimeNs===original.birthtimeNs);
 return now;
};
let positive,worker,authority,own,synthetic,workspaceIdentity;
const created=[];
function prepareVerifierInput(){
 workspaceIdentity=sameDir(workspace());
 own=fs.mkdtempSync(path.join(workspace(),'update-tool-verifier-'));synthetic=sameDir(own);
 authority=lockBundledNodeTools(repo);
 const source='tools/ymcc_sim/global/update-download-signals/tool-scope.mjs';
 const compiler=authority.tools.find(r=>key(r.path)===key(authority.compilerEntry));assert(compiler);
 const text=fs.readFileSync(path.join(repo,'tools/ymcc_sim/global/update-download-signals/worker.mjs'),'utf8');
 const recipes=[...text.matchAll(/const powerInputSource=`([^`]*)`;/g)];assert.equal(recipes.length,1);
 const recipe=recipes[0][1],inputs={[source]:{bytes:fs.statSync(path.join(repo,source)).size},[compiler.path]:{bytes:fs.statSync(compiler.path).size},'inert-power-input:shared-power-facts':{bytes:Buffer.byteLength(recipe)}};
 const bundles=Array.from({length:11},(_,i)=>{
  const base='inert-'+i,bytes='// Inert verifier bytes, not a compiled bundle.\n// '+i+'\n';
  for(const [name,data] of [[base+'.mjs',bytes],[base+'.metafile.json',JSON.stringify({inputs})]]){
   const file=path.join(own,name);write(file,data);created.push(file);
  }
  return {path:base+'.mjs',sha256:sha(bytes),esbuildVersion:authority.compilerVersion};
 });
 worker={bundledToolAuthority:structuredClone(authority),compiledToolInputs:[{...compiler}],compiledInputs:[{path:source,sha256:sha(fs.readFileSync(path.join(repo,source)))}],bundles,
  sharedPowerInputBoundary:{module:'powerLifecycleState',role:'inert snapshot/listener input',realSharedPowerSourceExecuted:false,sourceSha256:sha(recipe)},
  toolArtifacts:bundledArtifacts(authority,[],ownedUpdateOutputs(own))};
}
test.before(async()=>{
 if(evidence===undefined){prepareVerifierInput();return;}
 assert(evidence&&path.isAbsolute(evidence)&&!fs.existsSync(evidence),'Explicit fresh owned evidence root required');
 assert(!within(repo,evidence),'Evidence must stay outside product source');
 fs.mkdirSync(evidence,{recursive:true});
 const {runUpdateDownloadRegressions}=await import('./index.mjs');
 own=path.join(evidence,'original-consumer');positive=await runUpdateDownloadRegressions({repoRoot:repo,evidenceDir:own});
 write(path.join(evidence,'parent-result-reference.json'),JSON.stringify({file:path.join(own,'result.json'),status:positive.status,scope:'Unchanged original business assertions; full tool-authority maintenance is separate.'},null,2));
 assert.equal(positive.status,'PASS',JSON.stringify(positive.failure));
 worker=JSON.parse(fs.readFileSync(path.join(own,'worker-result.json'),'utf8'));authority=lockBundledNodeTools(repo);
});
test.after(()=>{
 if(!synthetic)return;
 sameDir(workspace(),workspaceIdentity);sameDir(own,synthetic);
 assert(key(path.dirname(own))===key(workspace())&&path.basename(own).startsWith('update-tool-verifier-'));
 for(const file of created){
  assert(key(path.dirname(file))===key(own)&&fs.lstatSync(file).isFile()&&key(fs.realpathSync.native(file))===key(file));
  fs.unlinkSync(file);
 }
 fs.rmdirSync(own);
});
test('inert receipt verifies the full tool, bundle and source identity contract without running the producer',{skip:evidence!==undefined},()=>{
 assert(worker.compiledToolInputs.length>0);assert.equal(verifyUpdateToolEvidence(worker,repo,own,authority).status,'PASS');
});
test('same original57 assertions,23 consumer checks and15 stability families execute with distinct source/tool identities',{skip:evidence===undefined?'Explicit live fixture root not requested':false},()=>{assert.equal(positive.originalTests.reduce((n,row)=>n+row.checks,0),57);assert.equal(positive.checks.length,23);assert.equal(positive.stabilityTests.length,15);assert(positive.actualCompiledInputs.every(row=>!row.path.includes('node_modules')&&!row.path.startsWith('../')));assert(worker.compiledToolInputs.length>0);assert.equal(verifyUpdateToolEvidence(worker,repo,own,authority).status,'PASS');});
for(const [name,code,mutate] of [
 ['missing declared tool','BUNDLED_NODE_TOOL_ARTIFACT_CLOSURE_MISSING_OR_EXTRA',value=>value.toolArtifacts.pop()],
 ['extra declared tool','BUNDLED_NODE_TOOL_ARTIFACT_CLOSURE_MISSING_OR_EXTRA',value=>value.toolArtifacts.push({path:path.join(own,'unreviewed-tool.js'),sha256:'a'.repeat(64),status:'OK'})],
 ['changed tool identity','BUNDLED_NODE_TOOL_ARTIFACT_CLOSURE_CHANGED',value=>{value.toolArtifacts[0].sha256='a'.repeat(64);} ],
 ['different parent source root','BUNDLED_NODE_RECEIPT_REPO_FORWARDING_REJECTED',value=>{value.bundledToolAuthority.repoRoot=path.join(own,'foreign-source');}],
 ['wrong bound graph identity','BUNDLED_NODE_AUTHORITY_IDENTITY_CHANGED',value=>{value.bundledToolAuthority.graphSha256='a'.repeat(64);}],
 ['observed undeclared tool','UPDATE_TOOL_OBSERVED_TOOL_OUTSIDE_GRAPH_OR_CHANGED',value=>{value.compiledToolInputs[0].path=path.join(own,'unreviewed-tool.js');}],
 ['duplicate observed tool','UPDATE_TOOL_OBSERVED_TOOL_DUPLICATE_OR_PATH',value=>value.compiledToolInputs.push(structuredClone(value.compiledToolInputs[0]))],
 ['external tool masquerading as product source','UPDATE_TOOL_METAFILE_SOURCE_CLOSURE_MISSING_EXTRA_OR_CHANGED',value=>value.compiledInputs.push({path:'../outside/node_modules/fake.js',sha256:'a'.repeat(64)})],
 ['missing compiled product source','UPDATE_TOOL_METAFILE_SOURCE_CLOSURE_MISSING_EXTRA_OR_CHANGED',value=>value.compiledInputs.pop()],
 ['changed compiled product digest','UPDATE_TOOL_METAFILE_SOURCE_CLOSURE_MISSING_EXTRA_OR_CHANGED',value=>{value.compiledInputs[0].sha256='a'.repeat(64);} ],
 ['bundle escaping owned case','UPDATE_TOOL_BUNDLE_RELATIVE_NAME_REQUIRED',value=>{value.bundles[0].path='../entry.mjs';}],
 ['missing original bundle','UPDATE_TOOL_ORIGINAL_BUNDLES_INCOMPLETE',value=>value.bundles.pop()],
 ['changed bundle digest','UPDATE_TOOL_BUNDLE_IDENTITY_CHANGED',value=>{value.bundles[0].sha256='a'.repeat(64);} ],
])test('nested compiler refuses '+name,()=>{const value=structuredClone(worker);mutate(value);assert.throws(()=>verifyUpdateToolEvidence(value,repo,own,authority),{message:code});});
test('owned bundle byte tampering is refused and the exact bytes are restored',()=>{const file=path.join(own,worker.bundles[0].path),original=fs.readFileSync(file);try{fs.appendFileSync(file,'\n/* owned maintenance corruption */\n');assert.throws(()=>verifyUpdateToolEvidence(worker,repo,own,authority),{message:'UPDATE_TOOL_BUNDLE_IDENTITY_CHANGED'});}finally{fs.writeFileSync(file,original);}assert.equal(verifyUpdateToolEvidence(worker,repo,own,authority).status,'PASS');});

function withMetafileReplacement(useReplacementPin,run){
 assert(synthetic);sameDir(own,synthetic);
 const file=path.join(own,worker.bundles[0].path.replace(/\.mjs$/,'.metafile.json'));
 const read=fs.readFileSync,original=read(file),changed=JSON.parse(original);
 changed.inputs['package.json']={bytes:fs.statSync(path.join(repo,'package.json')).size};
 const replacement=Buffer.from(JSON.stringify(changed)),value=structuredClone(worker);
 const pin=value.toolArtifacts.find(row=>row.path===file);assert.equal(pin?.sha256,sha(original));
 if(useReplacementPin)pin.sha256=sha(replacement);
 let replacements=0;
 fs.readFileSync=(target,...args)=>{
  const bytes=read(target,...args);
  if(target===file&&!replacements){fs.writeFileSync(file,replacement);replacements++;}
  return bytes;
 };
 try{run(value);}finally{fs.readFileSync=read;fs.writeFileSync(file,original);}
 assert.equal(replacements,1);
}
test('metafile cannot claim a hash for bytes the verifier did not parse',{skip:evidence!==undefined},()=>{
 withMetafileReplacement(true,value=>assert.throws(()=>verifyUpdateToolEvidence(value,repo,own,authority),{message:'BUNDLED_NODE_TOOL_ARTIFACT_CLOSURE_CHANGED'}));
});
test('independent artifact reread rejects a metafile changed after parsing',{skip:evidence!==undefined},()=>{
 withMetafileReplacement(false,value=>assert.throws(()=>verifyUpdateToolEvidence(value,repo,own,authority),{message:'UPDATE_TOOL_TOOL_ARTIFACT_CHANGED'}));
});
