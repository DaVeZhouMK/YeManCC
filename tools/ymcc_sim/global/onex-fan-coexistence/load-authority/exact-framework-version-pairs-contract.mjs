import path from 'node:path';
import assert from 'node:assert/strict';

export const INTEROP8='System.Runtime.InteropServices, Version=8.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a';
export const INTEROP10_PROVIDER=Object.freeze({path:'C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.11\\System.Runtime.InteropServices.dll'.replaceAll('\\\\','\\'),sha256:'9d0621f1e573bd20525287533bea24ec7824813eada7820a0c82252ede072ab9',identity:'System.Runtime.InteropServices, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',role:'framework'});
const full=p=>path.win32.resolve(p).toUpperCase(),same=(a,b)=>full(a)===full(b),abs=p=>path.win32.isAbsolute(p)&&/^[A-Za-z]:[\\/]/.test(p),equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
function need(v,code){if(!v)throw new Error(code);}
function exactProvider(p){return p&&p.identity===INTEROP10_PROVIDER.identity&&p.sha256===INTEROP10_PROVIDER.sha256&&p.role==='framework'&&abs(p.path)&&same(p.path,INTEROP10_PROVIDER.path);}
function maps(policy){const contexts=new Map(),paths=new Map(),requests=new Map();for(const c of policy.contexts){need(!contexts.has(c.id),'AUTHORITY_CONTEXT_SHAPE');contexts.set(c.id,c);const p=new Map(),names=new Set(),r=new Map();for(const pin of c.managed){const name=pin.identity.split(',')[0].toUpperCase();need(!p.has(full(pin.path))&&!names.has(name),'AUTHORITY_DUPLICATE_CONTEXT_PIN');p.set(full(pin.path),pin);names.add(name);}for(const b of c.requests){need(!r.has(b.requestedIdentity),'AUTHORITY_DUPLICATE_REQUEST');r.set(b.requestedIdentity,b);}paths.set(c.id,p);requests.set(c.id,r);}need(contexts.get('Default')?.kind==='default'&&policy.contexts.filter(c=>c.kind==='default').length===1,'AUTHORITY_DEFAULT_SHAPE');return {contexts,paths,requests};}
function isAliasScope(c,policy,model){if(c.id==='Default')return c.kind==='default';const entry=model.paths.get('Default').get(full(policy.entry)),reg=c.registrationAssembly;if(c.kind!=='custom'||!reg||!entry||entry.identity!=='OneXFanCoexistence, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'||reg.identity!==entry.identity||reg.sha256!==entry.sha256||!same(reg.path,entry.path))return false;if(c.id==='ConstructorContext')return c.registrationType==='Harness+ConstructorContext';const prefix='Harness.ProductContext|';if(c.registrationType!=='Harness+ProductContext'||!c.id.startsWith(prefix))return false;const root=c.id.slice(prefix.length);return abs(root)&&root===full(root).replace(/[\\/]$/,'')&&c.managed.every(p=>full(path.win32.dirname(p.path))===root);}
function declaration(policy,model){if(policy.schemaVersion===6){joinedDefaultVersionPairDeclaration(policy,model);return null;}if(policy.schemaVersion===5){versionPairDeclaration(policy,model);return null;}if(policy.schemaVersion===4){frameworkDeclaration(policy,model);return null;}if(policy.schemaVersion===2){need(policy.exactAliases==null,'AUTHORITY_ALIAS_SCHEMA3_REQUIRED');return null;}need(Array.isArray(policy.exactAliases)&&policy.exactAliases.length===1,'AUTHORITY_ALIAS_EXACTLY_ONE_REQUIRED');const a=policy.exactAliases[0];need(a&&a.requestedIdentity===INTEROP8&&a.targetContext==='Default'&&exactProvider(a.provider),'AUTHORITY_ALIAS_DECLARATION_MISMATCH');need(policy.contexts.length===3&&Array.isArray(a.requestingContexts)&&a.requestingContexts.length===3&&new Set(a.requestingContexts).size===3&&policy.contexts.every(c=>isAliasScope(c,policy,model)&&a.requestingContexts.includes(c.id))&&model.contexts.has('ConstructorContext')&&policy.contexts.filter(c=>c.registrationType==='Harness+ProductContext').length===1,'AUTHORITY_ALIAS_CONTEXT_SET_MISMATCH');need(exactProvider(model.paths.get('Default').get(full(a.provider.path))),'AUTHORITY_ALIAS_PROVIDER_NOT_DEFAULT_PIN');for(const c of policy.contexts){const r=model.requests.get(c.id).get(INTEROP8);need(r&&r.targetContext==='Default'&&abs(r.targetPath)&&same(r.targetPath,INTEROP10_PROVIDER.path),'AUTHORITY_ALIAS_EXACT_ROUTE_REQUIRED');need(c.id==='Default'||!c.managed.some(p=>p.identity.split(',')[0].toUpperCase()==='SYSTEM.RUNTIME.INTEROPSERVICES'),'AUTHORITY_ALIAS_CUSTOM_PROVIDER_FORBIDDEN');}return a;}
// JSON contract tests only. These functions do not hash/load a DLL or run CLR.
export function validateRequestBindingModel(policy,{requester,target,targetPath,requestedIdentity,pin},model=maps(policy),refusal='AUTHORITY_REQUEST_IDENTITY_MISMATCH'){
 if(policy.schemaVersion===6){joinedDefaultVersionPairDeclaration(policy,model);need(requester==='Default'&&target==='Default'&&abs(targetPath)&&providerSame(model.paths.get('Default').get(full(targetPath)),pin),refusal);if(requestedIdentity===pin.identity)return 'EXACT_IDENTITY';const a=policy.exactAliases.find(a=>a.requestedIdentity===requestedIdentity);need(a&&same(targetPath,a.provider.path)&&providerSame(pin,a.provider)&&pin.role==='framework',refusal);return 'EXACT_DECLARED_JOINED_DEFAULT_VERSION_PAIR';}
 if(requestedIdentity===pin.identity)return 'EXACT_IDENTITY';
 if(policy.schemaVersion===5){versionPairDeclaration(policy,model);const a=policy.exactAliases.find(a=>a.requestedIdentity===requestedIdentity);need(a&&target==='Default'&&a.requestingContexts.includes(requester)&&abs(targetPath)&&same(targetPath,a.provider.path)&&providerSame(pin,a.provider),refusal);return 'EXACT_DECLARED_FRAMEWORK_VERSION_PAIR';}
 if(policy.schemaVersion===4){frameworkDeclaration(policy,model);const a=policy.exactAliases.find(a=>a.requestedIdentity===requestedIdentity);need(a&&target==='Default'&&a.requestingContexts.includes(requester)&&abs(targetPath)&&same(targetPath,a.provider.path)&&providerSame(pin,a.provider),refusal);return 'EXACT_DECLARED_FRAMEWORK_ALIAS';}
 need(policy.schemaVersion===3,refusal);const a=declaration(policy,model);
 need(requestedIdentity===INTEROP8&&target==='Default'&&a.requestingContexts.includes(requester)&&abs(targetPath)&&same(targetPath,INTEROP10_PROVIDER.path)&&exactProvider(pin),refusal);return 'EXACT_INTEROP8_TO10_ALIAS';
}
export function validateAliasPolicyModel(policy){
 need(policy.schemaVersion===2||policy.schemaVersion===3||policy.schemaVersion===4||policy.schemaVersion===5||policy.schemaVersion===6,'AUTHORITY_POLICY_SHAPE');const model=maps(policy);declaration(policy,model);
 for(const c of policy.contexts)for(const r of c.requests){need(r.targetContext===c.id||r.targetContext==='Default','AUTHORITY_CROSS_CUSTOM_EXPORT_FORBIDDEN');const p=model.paths.get(r.targetContext)?.get(full(r.targetPath));need(p,'AUTHORITY_REQUEST_BINDING_IDENTITY_OR_PATH');validateRequestBindingModel(policy,{requester:c.id,target:r.targetContext,targetPath:r.targetPath,requestedIdentity:r.requestedIdentity,pin:p},model,'AUTHORITY_REQUEST_BINDING_IDENTITY_OR_PATH');}
 return {status:policy.schemaVersion===2?'PASS_SCHEMA2_EXACT_IDENTITY_MODEL':policy.schemaVersion===3?'PASS_SCHEMA3_EXACT_INTEROP_ALIAS_MODEL':policy.schemaVersion===4?'PASS_SCHEMA4_EXACT_FRAMEWORK_ALIAS_TABLE_MODEL':policy.schemaVersion===5?'PASS_SCHEMA5_EXACT_FRAMEWORK_VERSION_PAIRS_MODEL':'PASS_SCHEMA6_JOINED_DEFAULT_VERSION_PAIRS_MODEL',managed:policy.contexts.map(c=>c.managed.length),clrTested:false,compileTested:false};
}
export function createSchema3AliasModel(basePolicy){
 need(basePolicy.schemaVersion===2&&basePolicy.exactAliases==null,'EXPECTED_SCHEMA2_BASE');validateAliasPolicyModel(basePolicy);
 const p=structuredClone(basePolicy);p.schemaVersion=3;p.exactAliases=[{requestedIdentity:INTEROP8,targetContext:'Default',provider:{...INTEROP10_PROVIDER},requestingContexts:p.contexts.map(c=>c.id)}];
 for(const c of p.contexts)c.requests.push({requestedIdentity:INTEROP8,targetContext:'Default',targetPath:INTEROP10_PROVIDER.path});
 validateAliasPolicyModel(p);return p;
}
export function assertOnlySchema3AliasDelta(basePolicy,modelPolicy){
 validateAliasPolicyModel(basePolicy);validateAliasPolicyModel(modelPolicy);const restored=structuredClone(modelPolicy);restored.schemaVersion=2;delete restored.exactAliases;for(const c of restored.contexts)c.requests=c.requests.filter(r=>r.requestedIdentity!==INTEROP8);assert.deepEqual(restored,basePolicy);return {status:'PASS_ONLY_SCHEMA3_PLUS_EXACT_ALIAS_DELTA',managedPinsAdded:0,requestsAdded:3,source44BaseNotResigned:true};
}
export function assessProviderResultModel(providerPin,result){need(exactProvider(providerPin),'AUTHORITY_ALIAS_DECLARATION_MISMATCH');need(result.identity===providerPin.identity&&same(result.path,providerPin.path)&&result.context==='Default'&&result.sha256===providerPin.sha256,'AUTHORITY_CLR_RESULT_MISMATCH');return {status:'PASS_PROVIDER_PIN_RESULT_MODEL',clrTested:false};}
// Schema4 is a bounded declaration table, not implicit CLR version unification.
export const FRAMEWORK_ROOT='C:/Program Files/dotnet/shared/Microsoft.NETCore.App/10.0.11';
const fullIdentity=/^([^,]+), Version=(\d+)\.(\d+)\.(\d+)\.(\d+), Culture=([^,]+), PublicKeyToken=(null|[a-f0-9]{16})$/;
function identity(value){const m=typeof value==='string'&&fullIdentity.exec(value);return m&&!m.slice(2,6).some(v=>String(Number(v))!==v)?{name:m[1],version:m.slice(2,6).map(Number),culture:m[6],token:m[7]}:null;}
function providerSame(a,b){return a&&b&&a.identity===b.identity&&a.sha256===b.sha256&&a.role===b.role&&same(a.path,b.path);}
function exactFrameworkProvider(a){
 if(!a?.provider||a.targetContext!=='Default')return false;const p=a.provider,provider=identity(p.identity),request=identity(a.requestedIdentity);
 return p.role==='framework'&&abs(p.path)&&/^[a-f0-9]{64}$/.test(p.sha256??'')&&provider&&request&&provider.version.join('.')==='10.0.0.0'&&request.version.join('.')==='8.0.0.0'&&provider.name===request.name&&provider.culture===request.culture&&provider.token===request.token&&!/[<>:"/\\|?*]/.test(provider.name)&&![...provider.name].some(c=>c.charCodeAt(0)<32)&&full(path.win32.dirname(p.path))===full(FRAMEWORK_ROOT)&&same(p.path,path.win32.join(FRAMEWORK_ROOT,provider.name+'.dll'));
}
function frameworkDeclaration(policy,model){
 const aliases=policy.exactAliases;need(Array.isArray(aliases)&&aliases.length>=1&&aliases.length<=16,'AUTHORITY_FRAMEWORK_ALIAS_TABLE_SIZE');
 need(policy.contexts.length===3&&policy.contexts.every(c=>isAliasScope(c,policy,model))&&model.contexts.has('ConstructorContext')&&policy.contexts.filter(c=>c.registrationType==='Harness+ProductContext').length===1,'AUTHORITY_ALIAS_CONTEXT_SET_MISMATCH');
 const names=new Set(),providerIdentities=new Set(),providerPaths=new Set();
 for(const a of aliases){need(exactFrameworkProvider(a),'AUTHORITY_FRAMEWORK_ALIAS_DECLARATION_MISMATCH');need(!names.has(a.requestedIdentity)&&!providerIdentities.has(a.provider.identity)&&!providerPaths.has(full(a.provider.path)),'AUTHORITY_FRAMEWORK_ALIAS_DUPLICATE');names.add(a.requestedIdentity);providerIdentities.add(a.provider.identity);providerPaths.add(full(a.provider.path));
  need(Array.isArray(a.requestingContexts)&&a.requestingContexts.length===3&&new Set(a.requestingContexts).size===3&&policy.contexts.every(c=>a.requestingContexts.includes(c.id)),'AUTHORITY_ALIAS_CONTEXT_SET_MISMATCH');
  need(providerSame(model.paths.get('Default').get(full(a.provider.path)),a.provider),'AUTHORITY_FRAMEWORK_ALIAS_PROVIDER_NOT_DEFAULT_PIN');
  for(const c of policy.contexts){const route=model.requests.get(c.id).get(a.requestedIdentity);need(route&&route.targetContext==='Default'&&abs(route.targetPath)&&same(route.targetPath,a.provider.path),'AUTHORITY_ALIAS_EXACT_ROUTE_REQUIRED');need(c.id==='Default'||!c.managed.some(p=>p.identity.split(',')[0].toUpperCase()===identity(a.provider.identity).name.toUpperCase()),'AUTHORITY_ALIAS_CUSTOM_PROVIDER_FORBIDDEN');}
 }
}
export function createSchema4AliasModel(basePolicy,providers){
 need(basePolicy.schemaVersion===2||basePolicy.schemaVersion===3,'EXPECTED_SCHEMA2_OR3_BASE');validateAliasPolicyModel(basePolicy);need(Array.isArray(providers)&&providers.length>0&&providers.length<=16,'AUTHORITY_FRAMEWORK_ALIAS_TABLE_SIZE');
 const p=structuredClone(basePolicy),oldAliases=new Set((p.exactAliases??[]).map(a=>a.requestedIdentity));for(const c of p.contexts)c.requests=c.requests.filter(r=>!oldAliases.has(r.requestedIdentity));p.schemaVersion=4;
 p.exactAliases=providers.map(provider=>{const id=identity(provider.identity);need(id&&id.version.join('.')==='10.0.0.0','EXPECTED_EXACT_PROVIDER10');return {requestedIdentity:provider.identity.replace(', Version=10.0.0.0,',', Version=8.0.0.0,'),targetContext:'Default',provider:structuredClone(provider),requestingContexts:p.contexts.map(c=>c.id)};});
 for(const a of p.exactAliases)for(const c of p.contexts)c.requests.push({requestedIdentity:a.requestedIdentity,targetContext:'Default',targetPath:a.provider.path});validateAliasPolicyModel(p);return p;
}
export function assertSchema3To4OnlyAliasDelta(base,p){
 validateAliasPolicyModel(base);validateAliasPolicyModel(p);need(base.schemaVersion===3&&p.schemaVersion===4,'EXPECTED_SCHEMA3_TO4');const strip=v=>{const c=structuredClone(v),names=new Set((c.exactAliases??[]).map(a=>a.requestedIdentity));for(const scope of c.contexts)scope.requests=scope.requests.filter(r=>!names.has(r.requestedIdentity));delete c.exactAliases;c.schemaVersion=2;return c;};assert.deepEqual(strip(p),strip(base));return {status:'PASS_SCHEMA4_EXPLICIT_TABLE_NO_MANAGED_BASE_CHANGE',baseSchema:3,candidateSchema:4,aliases:p.exactAliases.length,managed:p.contexts.map(c=>c.managed.length),compileTested:false,clrTested:false};
}

// Schema5 accepts complete explicitly selected version pairs only.
function versionLess(a,b){for(let i=0;i<4;i++){if(a[i]<b[i])return true;if(a[i]>b[i])return false;}return false;}
function exactVersionPair(a){
 if(!a?.provider||a.targetContext!=='Default')return false;const p=a.provider,provider=identity(p.identity),request=identity(a.requestedIdentity);const exactNetstandardFacade=p.identity==='netstandard, Version=2.1.0.0, Culture=neutral, PublicKeyToken=cc7b13ffcd2ddd51'&&a.requestedIdentity==='netstandard, Version=2.0.0.0, Culture=neutral, PublicKeyToken=cc7b13ffcd2ddd51'&&p.sha256==='ce09cef0d758196f2467f320d840c03202453efe9fb1920dddd24ea8277bbbb3'&&same(p.path,path.win32.join(FRAMEWORK_ROOT,'netstandard.dll'));
 return p.role==='framework'&&abs(p.path)&&/^[a-f0-9]{64}$/.test(p.sha256??'')&&provider&&request&&(provider.name.toLowerCase()==='netstandard'?exactNetstandardFacade:provider.version.join('.')==='10.0.0.0')&&request.version.every(v=>Number.isInteger(v)&&v>=0&&v<=65535)&&versionLess(request.version,provider.version)&&provider.name===request.name&&provider.culture===request.culture&&provider.token===request.token&&!/[<>:"/\\|?*]/.test(provider.name)&&![...provider.name].some(c=>c.charCodeAt(0)<32)&&full(path.win32.dirname(p.path))===full(FRAMEWORK_ROOT)&&same(p.path,path.win32.join(FRAMEWORK_ROOT,provider.name+'.dll'));
}
function versionPairDeclaration(policy,model){
 const aliases=policy.exactAliases;need(Array.isArray(aliases)&&aliases.length>=1&&aliases.length<=16,'AUTHORITY_FRAMEWORK_ALIAS_TABLE_SIZE');
 need(policy.contexts.length===3&&policy.contexts.every(c=>isAliasScope(c,policy,model))&&model.contexts.has('ConstructorContext')&&policy.contexts.filter(c=>c.registrationType==='Harness+ProductContext').length===1,'AUTHORITY_ALIAS_CONTEXT_SET_MISMATCH');
 const requestsSeen=new Set(),providers=new Map(),providerPaths=new Map();
 for(const a of aliases){need(exactVersionPair(a),'AUTHORITY_FRAMEWORK_VERSION_PAIR_DECLARATION_MISMATCH');need(!requestsSeen.has(a.requestedIdentity),'AUTHORITY_FRAMEWORK_VERSION_PAIR_DUPLICATE_REQUEST');requestsSeen.add(a.requestedIdentity);
  need((!providers.has(a.provider.identity)||providerSame(providers.get(a.provider.identity),a.provider))&&(!providerPaths.has(full(a.provider.path))||providerSame(providerPaths.get(full(a.provider.path)),a.provider)),'AUTHORITY_FRAMEWORK_VERSION_PAIR_PROVIDER_CONFLICT');providers.set(a.provider.identity,a.provider);providerPaths.set(full(a.provider.path),a.provider);
  need(Array.isArray(a.requestingContexts)&&a.requestingContexts.length===3&&new Set(a.requestingContexts).size===3&&policy.contexts.every(c=>a.requestingContexts.includes(c.id)),'AUTHORITY_ALIAS_CONTEXT_SET_MISMATCH');need(providerSame(model.paths.get('Default').get(full(a.provider.path)),a.provider)&&a.provider.role==='framework','AUTHORITY_FRAMEWORK_ALIAS_PROVIDER_NOT_DEFAULT_PIN');
  for(const c of policy.contexts){const route=model.requests.get(c.id).get(a.requestedIdentity);need(route&&route.targetContext==='Default'&&abs(route.targetPath)&&same(route.targetPath,a.provider.path),'AUTHORITY_ALIAS_EXACT_ROUTE_REQUIRED');need(c.id==='Default'||!c.managed.some(p=>p.identity.split(',')[0].toUpperCase()===identity(a.provider.identity).name.toUpperCase()),'AUTHORITY_ALIAS_CUSTOM_PROVIDER_FORBIDDEN');}
 }
 return providers;
}
export function createSchema5VersionPairModel(basePolicy,selectedEntries){
 need([2,3,4].includes(basePolicy.schemaVersion),'EXPECTED_PRIOR_EXACT_SCHEMA_BASE');validateAliasPolicyModel(basePolicy);need(Array.isArray(selectedEntries)&&selectedEntries.length>=1&&selectedEntries.length<=16,'AUTHORITY_FRAMEWORK_ALIAS_TABLE_SIZE');const p=structuredClone(basePolicy),previous=new Set((p.exactAliases??[]).map(a=>a.requestedIdentity));for(const c of p.contexts)c.requests=c.requests.filter(r=>!previous.has(r.requestedIdentity));p.schemaVersion=5;p.exactAliases=structuredClone(selectedEntries);
 for(const a of p.exactAliases)for(const c of p.contexts)c.requests.push({requestedIdentity:a.requestedIdentity,targetContext:a.targetContext,targetPath:a.provider.path});validateAliasPolicyModel(p);return p;
}
export function schema5ProviderCatalog(policy){need(policy.schemaVersion===5,'EXPECTED_SCHEMA5');validateAliasPolicyModel(policy);return [...versionPairDeclaration(policy,maps(policy)).values()].map(p=>structuredClone(p));}
export function assertSchema4To5OnlyVersionPairDelta(base,p){
 need(base.schemaVersion===4&&p.schemaVersion===5,'EXPECTED_SCHEMA4_TO5');validateAliasPolicyModel(base);validateAliasPolicyModel(p);const strip=v=>{const c=structuredClone(v),identities=new Set((c.exactAliases??[]).map(a=>a.requestedIdentity));for(const ctx of c.contexts)ctx.requests=ctx.requests.filter(r=>!identities.has(r.requestedIdentity));delete c.exactAliases;c.schemaVersion=2;return c;};assert.deepEqual(strip(base),strip(p));return {status:'PASS_EXACT_VERSION_PAIR_ONLY_NO_MANAGED_OR_OTHER_BASE_CHANGE',aliases:p.exactAliases.length,uniqueProviders:schema5ProviderCatalog(p).length,managed:p.contexts.map(c=>c.managed.length),runtimeTested:false};
}

// Schema6 has one explicit Default scope and never changes schema5's three scopes.
function joinedDefaultVersionPairDeclaration(policy,model){
 const aliases=policy.exactAliases;need(Array.isArray(aliases)&&aliases.length>=1&&aliases.length<=16,'AUTHORITY_FRAMEWORK_ALIAS_TABLE_SIZE');
 const c=policy.contexts[0],entry=model.paths.get('Default')?.get(full(policy.entry));
 need(policy.contexts.length===1&&c?.id==='Default'&&c.kind==='default'&&c.registrationType==null&&c.registrationAssembly==null&&entry?.identity==='JoinedFactoryProbe, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'&&entry.role==='tool','AUTHORITY_JOINED_DEFAULT_POLICY_SHAPE');
 const requested=new Set(),providers=new Map(),paths=new Map();
 for(const a of aliases){
  need(exactVersionPair(a),'AUTHORITY_FRAMEWORK_VERSION_PAIR_DECLARATION_MISMATCH');need(!requested.has(a.requestedIdentity),'AUTHORITY_FRAMEWORK_VERSION_PAIR_DUPLICATE_REQUEST');requested.add(a.requestedIdentity);
  need((!providers.has(a.provider.identity)||providerSame(providers.get(a.provider.identity),a.provider))&&(!paths.has(full(a.provider.path))||providerSame(paths.get(full(a.provider.path)),a.provider)),'AUTHORITY_FRAMEWORK_VERSION_PAIR_PROVIDER_CONFLICT');providers.set(a.provider.identity,a.provider);paths.set(full(a.provider.path),a.provider);
  need(Array.isArray(a.requestingContexts)&&a.requestingContexts.length===1&&a.requestingContexts[0]==='Default','AUTHORITY_JOINED_DEFAULT_CONTEXT_SET_MISMATCH');
  need(providerSame(model.paths.get('Default').get(full(a.provider.path)),a.provider)&&a.provider.role==='framework','AUTHORITY_FRAMEWORK_ALIAS_PROVIDER_NOT_DEFAULT_PIN');
  const r=model.requests.get('Default').get(a.requestedIdentity);need(r&&r.targetContext==='Default'&&abs(r.targetPath)&&same(r.targetPath,a.provider.path),'AUTHORITY_ALIAS_EXACT_ROUTE_REQUIRED');
 }
 return providers;
}
export function createSchema6JoinedDefaultModel(basePolicy,selectedEntries){
 need(basePolicy.schemaVersion===2&&basePolicy.exactAliases==null,'EXPECTED_EXACT_SCHEMA2_JOINED_DEFAULT_BASE');validateAliasPolicyModel(basePolicy);need(Array.isArray(selectedEntries)&&selectedEntries.length>=1&&selectedEntries.length<=16,'AUTHORITY_FRAMEWORK_ALIAS_TABLE_SIZE');
 const p=structuredClone(basePolicy);p.schemaVersion=6;p.exactAliases=structuredClone(selectedEntries);for(const a of p.exactAliases)p.contexts[0].requests.push({requestedIdentity:a.requestedIdentity,targetContext:a.targetContext,targetPath:a.provider.path});validateAliasPolicyModel(p);return p;
}
export function schema6ProviderCatalog(policy){need(policy.schemaVersion===6,'EXPECTED_SCHEMA6');validateAliasPolicyModel(policy);return [...joinedDefaultVersionPairDeclaration(policy,maps(policy)).values()].map(p=>structuredClone(p));}
