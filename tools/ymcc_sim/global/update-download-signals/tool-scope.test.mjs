import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {runUpdateDownloadRegressions} from './index.mjs';
import {verifyUpdateToolEvidence,lockBundledNodeTools} from './tool-scope.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const evidence=process.env.YMCC_UPDATE_TOOL_TEST_ROOT;
assert(evidence&&path.isAbsolute(evidence)&&!fs.existsSync(evidence),'Explicit fresh owned evidence root required');
const within=(root,file)=>{const r=path.relative(path.resolve(root),path.resolve(file));return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
assert(!within(repo,evidence),'Evidence must stay outside product source');
fs.mkdirSync(evidence,{recursive:true});
let positive,worker,authority,own;
test.before(async()=>{own=path.join(evidence,'original-consumer');positive=await runUpdateDownloadRegressions({repoRoot:repo,evidenceDir:own});fs.writeFileSync(path.join(evidence,'parent-result-reference.json'),JSON.stringify({file:path.join(own,'result.json'),status:positive.status,scope:'Unchanged original business assertions; full tool-authority maintenance is separate.'},null,2));assert.equal(positive.status,'PASS',JSON.stringify(positive.failure));worker=JSON.parse(fs.readFileSync(path.join(own,'worker-result.json'),'utf8'));authority=lockBundledNodeTools(repo);});
test('same original57 assertions,23 consumer checks and15 stability families execute with distinct source/tool identities',()=>{assert.equal(positive.originalTests.reduce((n,row)=>n+row.checks,0),57);assert.equal(positive.checks.length,23);assert.equal(positive.stabilityTests.length,15);assert(positive.actualCompiledInputs.every(row=>!row.path.includes('node_modules')&&!row.path.startsWith('../')));assert(worker.compiledToolInputs.length>0);assert.equal(verifyUpdateToolEvidence(worker,repo,own,authority).status,'PASS');});
for(const [name,mutate] of [
 ['missing declared tool',value=>value.toolArtifacts.pop()],
 ['extra declared tool',value=>value.toolArtifacts.push({path:path.join(evidence,'unreviewed-tool.js'),sha256:'a'.repeat(64),status:'OK'})],
 ['changed tool identity',value=>{value.toolArtifacts[0].sha256='a'.repeat(64);} ],
 ['different parent source root',value=>{value.bundledToolAuthority.repoRoot=path.join(evidence,'foreign-source');}],
 ['wrong bound graph identity',value=>{value.bundledToolAuthority.graphSha256='a'.repeat(64);}],
 ['observed undeclared tool',value=>{value.compiledToolInputs[0].path=path.join(evidence,'unreviewed-tool.js');}],
 ['duplicate observed tool',value=>value.compiledToolInputs.push(structuredClone(value.compiledToolInputs[0]))],
 ['external tool masquerading as product source',value=>value.compiledInputs.push({path:'../outside/node_modules/fake.js',sha256:'a'.repeat(64)})],
 ['missing compiled product source',value=>value.compiledInputs.pop()],
 ['changed compiled product digest',value=>{value.compiledInputs[0].sha256='a'.repeat(64);}],
 ['bundle escaping owned case',value=>{value.bundles[0].path='../entry.mjs';}],
 ['missing original bundle',value=>value.bundles.pop()],
 ['changed bundle digest',value=>{value.bundles[0].sha256='a'.repeat(64);}],
])test('nested compiler refuses '+name,()=>{const value=structuredClone(worker);mutate(value);assert.throws(()=>verifyUpdateToolEvidence(value,repo,own,authority));});
test('real produced bundle byte tampering is refused and the original exact bytes are restored',()=>{const file=path.join(own,worker.bundles[0].path),original=fs.readFileSync(file);try{fs.appendFileSync(file,'\n/* owned maintenance corruption */\n');assert.throws(()=>verifyUpdateToolEvidence(worker,repo,own,authority));}finally{fs.writeFileSync(file,original);}assert.equal(verifyUpdateToolEvidence(worker,repo,own,authority).status,'PASS');});

