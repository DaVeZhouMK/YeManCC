import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';

export const NETSTANDARD20='netstandard, Version=2.0.0.0, Culture=neutral, PublicKeyToken=cc7b13ffcd2ddd51';
export const NETSTANDARD21='netstandard, Version=2.1.0.0, Culture=neutral, PublicKeyToken=cc7b13ffcd2ddd51';
export const NETSTANDARD_SHA='ce09cef0d758196f2467f320d840c03202453efe9fb1920dddd24ea8277bbbb3';
const DECL=/AUTHORITY_FRAMEWORK_VERSION_PAIR_DECLARATION_MISMATCH/;
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const pin=p=>({path:p,sha256:hash(p)});
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const clone=p=>structuredClone(p);
function pinnedFile(value){const p=typeof value==='string'?{path:value}:value;assert.ok(p&&typeof p.path==='string'&&path.win32.isAbsolute(p.path),'ABSOLUTE_INPUT_PATH_REQUIRED');const actual=pin(p.path);if(p.sha256)assert.equal(actual.sha256,p.sha256,'INPUT_PIN_CHANGED '+p.path);return actual;}
export function alterFacadeProviderAndDefault(p,edit){
 const a=p.exactAliases.find(a=>a.requestedIdentity===NETSTANDARD20),old=clone(a.provider);assert.equal(a.requestedIdentity,NETSTANDARD20);edit(a.provider);
 const provider=p.contexts[0].managed.find(x=>x.identity===old.identity);assert.ok(provider);Object.assign(provider,a.provider);
 for(const c of p.contexts)for(const r of c.requests)if(r.requestedIdentity===old.identity){r.requestedIdentity=a.provider.identity;r.targetPath=a.provider.path;}
 for(const c of p.contexts)for(const r of c.requests)if(r.requestedIdentity===a.requestedIdentity)r.targetPath=a.provider.path;
}
export function changeFacadeRequestedVersion(p,version){
 const a=p.exactAliases.find(a=>a.requestedIdentity===NETSTANDARD20),old=a.requestedIdentity;assert.equal(old,NETSTANDARD20);a.requestedIdentity=old.replace('2.0.0.0',version);
 for(const c of p.contexts)for(const r of c.requests)if(r.requestedIdentity===old)r.requestedIdentity=a.requestedIdentity;
}
export function createNetstandardFacadeModelCases(basePolicy){
 const policy=clone(basePolicy),provider=policy.exactAliases.find(a=>a.requestedIdentity===NETSTANDARD20)?.provider;
 assert.equal(policy.schemaVersion,5);assert.equal(policy.exactAliases.find(a=>a.requestedIdentity===NETSTANDARD20)?.requestedIdentity,NETSTANDARD20);assert.equal(provider.identity,NETSTANDARD21);assert.equal(provider.sha256,NETSTANDARD_SHA);
 const rows=[];const add=(name,edit,pattern=DECL)=>{const p=clone(policy);edit(p);rows.push({name,policy:p,pattern:pattern.source});};
 rows.push({name:'exact reviewed netstandard20 to21 pair with existing four aliases accepted',policy:clone(policy),pattern:null});
 add('facade requested minor2.0.1 not a range',p=>changeFacadeRequestedVersion(p,'2.0.1.0'));
 add('facade older1.0 is not automatically allowed',p=>changeFacadeRequestedVersion(p,'1.0.0.0'));
 add('facade provider2.2 refused',p=>alterFacadeProviderAndDefault(p,a=>a.identity=a.identity.replace('2.1.0.0','2.2.0.0')));
 add('facade provider10 not generic framework exception',p=>alterFacadeProviderAndDefault(p,a=>a.identity=a.identity.replace('2.1.0.0','10.0.0.0')));
 add('facade exact wrongsha refused even copied into default pin',p=>alterFacadeProviderAndDefault(p,a=>a.sha256='0'.repeat(64)));
 add('facade wrong path refused even copied into default',p=>alterFacadeProviderAndDefault(p,a=>a.path=a.path.replace('netstandard.dll','netstandard-other.dll')));
 add('facade wrong token refused',p=>{p.exactAliases.find(a=>a.requestedIdentity===NETSTANDARD20).requestedIdentity=p.exactAliases.find(a=>a.requestedIdentity===NETSTANDARD20).requestedIdentity.replace('cc7b13ffcd2ddd51','b03f5f7f11d50a3a');});
 add('facade role infrastructure refused',p=>alterFacadeProviderAndDefault(p,a=>a.role='infrastructure'));
 add('facade custom provider clone refused',p=>p.contexts[1].managed.push(clone(provider)),/AUTHORITY_ALIAS_CUSTOM_PROVIDER_FORBIDDEN/);
 add('facade missing constructor route refused',p=>{const req=p.exactAliases.find(a=>a.requestedIdentity===NETSTANDARD20).requestedIdentity;p.contexts[1].requests=p.contexts[1].requests.filter(r=>r.requestedIdentity!==req);},/AUTHORITY_ALIAS_EXACT_ROUTE_REQUIRED/);
 add('facade missing default provider pin refused',p=>{p.contexts[0].managed=p.contexts[0].managed.filter(r=>r.identity!==provider.identity);},/AUTHORITY_REQUEST_UNPINNED|AUTHORITY_FRAMEWORK_ALIAS_PROVIDER_NOT_DEFAULT_PIN/);
 add('unrelated framework provider2.1 remains refused',p=>{const a=p.exactAliases.find(a=>a.provider.identity.startsWith('System.Runtime, ')),original=a.provider.identity;a.provider.identity=a.provider.identity.replace('10.0.0.0','2.1.0.0');p.contexts[0].managed.find(x=>x.identity===original).identity=a.provider.identity;},/AUTHORITY_REQUEST_BINDING_IDENTITY_OR_PATH|AUTHORITY_FRAMEWORK_VERSION_PAIR_DECLARATION_MISMATCH|AUTHORITY_FRAMEWORK_ALIAS_PROVIDER_NOT_DEFAULT_PIN/);
 assert.equal(rows.length,13);return rows;
}
export async function runNetstandardFacadeContractTests({basePolicy,sourceBefore,sourceAfter,outDir}){
 assert.ok(typeof outDir==='string'&&path.win32.isAbsolute(outDir),'ABSOLUTE_FRESH_OUTPUT_REQUIRED');assert.equal(fs.existsSync(outDir),false,'FRESH_OUTPUT_REQUIRED');
 const policyPin=typeof basePolicy==='string'||basePolicy?.path?pinnedFile(basePolicy):null,policy=policyPin?read(policyPin.path):clone(basePolicy);
 const before=pinnedFile(sourceBefore),after=pinnedFile(sourceAfter),modelPin=pin(path.join(path.dirname(after.path),'exact-framework-version-pairs-contract.mjs'));
 const model=await import(pathToFileURL(modelPin.path).href);fs.mkdirSync(outDir,{recursive:true});const tests=[];
 function gate(name,run){try{run();tests.push({name,status:'PASS'});}catch(e){tests.push({name,status:'FAIL',message:e.message,stack:e.stack});}}
 const cases=createNetstandardFacadeModelCases(policy);const expectedManaged=policy.contexts.map(c=>c.managed.length),expectedProviders=new Set(policy.exactAliases.map(a=>a.provider.identity)).size;
 for(const c of cases)gate(c.name,()=>{if(c.pattern)assert.throws(()=>model.validateAliasPolicyModel(c.policy),new RegExp(c.pattern));else{model.validateAliasPolicyModel(c.policy);assert.deepEqual(c.policy.contexts.map(c=>c.managed.length),expectedManaged);assert.equal(model.schema5ProviderCatalog(c.policy).length,expectedProviders);}});
 gate('source exact branch only retains prior public lines',()=>{const old=fs.readFileSync(before.path,'utf8'),next=fs.readFileSync(after.path,'utf8'),pub=t=>t.split(/\r?\n/).filter(x=>/^\s*public /.test(x));assert.deepEqual(pub(old),pub(next));assert.equal(next.match(/bool exactNetstandardFacade=/g)?.length,1);});
 for(const p of [policyPin,before,after,modelPin].filter(Boolean))assert.equal(hash(p.path),p.sha256,'INPUT_CHANGED '+p.path);
 const result={schemaVersion:1,status:tests.every(r=>r.status==='PASS')?'PASS_EXACT_NETSTANDARD_FACADE_MODEL_CONTROLS':'FAIL',passed:tests.filter(r=>r.status==='PASS').length,total:tests.length,tests,inputs:{basePolicy:policyPin,sourceBefore:before,sourceAfter:after,contract:modelPin},modelOnly:true,productExecutions:0,CLRProcesses:0,notRuntimeEvidence:true};
 fs.writeFileSync(path.join(outDir,'NETSTANDARD-FACADE-FINITE-GATES.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
 assert.equal(result.total,14);assert.equal(result.passed,14,JSON.stringify(tests.filter(r=>r.status==='FAIL')));return result;
}
if(process.argv[1]&&pathToFileURL(path.resolve(process.argv[1])).href===import.meta.url){
 const args=process.argv.slice(2);assert.equal(args.length,8,'EXACT_FOUR_INPUT_OPTIONS_REQUIRED');const expected=['--base-policy','--source-before','--source-after','--out-dir'];const values={};for(let i=0;i<4;i++){assert.equal(args[i*2],expected[i]);values[expected[i]]=args[i*2+1];}
 const result=await runNetstandardFacadeContractTests({basePolicy:values['--base-policy'],sourceBefore:values['--source-before'],sourceAfter:values['--source-after'],outDir:values['--out-dir']});console.log(JSON.stringify({status:result.status,passed:result.passed,total:result.total,CLRProcesses:0}));
}
