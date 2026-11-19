import test from 'node:test';import assert from 'node:assert/strict';import path from 'node:path';import {createHash} from 'node:crypto';
import {privateSettingsMutexDefinition,privateSettingsMutexDefinitionForRuntimeRoot,isolateCurrentFixtureSettingsMutex} from './settings_mutex_isolation.mjs';
const root=path.resolve(import.meta.dirname,'../../../../../../Testing/YMCC/Evidence/RuntimeCurrent105-20261023/runtime');
test('legacy definition and generated include wrapper retain the existing default',()=>{assert.equal(privateSettingsMutexDefinitionForRuntimeRoot(),privateSettingsMutexDefinition);const source='// fixture\n#include "main.inert.cpp"\n';const result=isolateCurrentFixtureSettingsMutex(source);assert(result.text.includes(privateSettingsMutexDefinition));assert.equal(result.text.replace(result.edits[0].after,result.edits[0].before),source);});
test('one explicit core runtime root replaces the old substring admission only',()=>{const generated=privateSettingsMutexDefinitionForRuntimeRoot(root);assert(generated.includes('root!=normalizeWindowsPathForCompare(L'+JSON.stringify(root)+')'));assert(!generated.includes('root.find('));assert(generated.includes('FILE_FLAG_OPEN_REPARSE_POINT'));assert(generated.includes('identity.nFileIndexHigh'));assert(generated.includes('return ::CreateMutexW(attributes,initialOwner,name.c_str());'));});
for(const candidate of ['',null,'relative/path','D:/HC61-current-public-20261021/run',path.resolve(root,'../../../../Workspace/run'),path.resolve(root,'../../../Evidence-other/run'),path.resolve(root,'../..'),'G:/YeManCC-Work/Testing/YMCC/Evidence/x\nnext'])test('refuses unowned runtime root '+JSON.stringify(candidate),()=>assert.throws(()=>privateSettingsMutexDefinitionForRuntimeRoot(candidate)));
test('configured transformation remains reversible and refuses duplicate application',()=>{const source='#include "main.inert.cpp"\n',result=isolateCurrentFixtureSettingsMutex(source,{runtimeRoot:root});assert.equal(result.text.replace(result.edits[0].after,result.edits[0].before),source);assert.throws(()=>isolateCurrentFixtureSettingsMutex(result.text,{runtimeRoot:root}));assert.throws(()=>isolateCurrentFixtureSettingsMutex('unknown include',{runtimeRoot:root}));});

test('new module maintenance request uses its own directory-derived local name',()=>{const source='#include "main.inert.cpp"\n',r=isolateCurrentFixtureSettingsMutex(source,{runtimeRoot:root,moduleMaintenance:true});assert.equal(r.text.replace(r.edits[0].after,r.edits[0].before),source);assert(r.text.includes('const bool moduleRequest=requestedName'));assert(r.text.includes('baseName+L"-ModuleMaintenance"'));assert(r.text.includes('return ::CreateMutexW(attributes,initialOwner,name.c_str());'));assert.equal(r.moduleMaintenanceProductionNameReached,false);assert.equal(r.productionMutexReached,false);assert(r.text.includes('root!=normalizeWindowsPathForCompare(L'+JSON.stringify(root)+')'));});
test('the new module boundary never activates by default and requires one explicit runtime',()=>{const source='#include "main.inert.cpp"\n';assert.equal(isolateCurrentFixtureSettingsMutex(source,{runtimeRoot:root,moduleMaintenance:false}).text,isolateCurrentFixtureSettingsMutex(source,{runtimeRoot:root}).text);assert.throws(()=>isolateCurrentFixtureSettingsMutex(source,{moduleMaintenance:true}),/EXPLICIT_MODULE_MAINTENANCE_RUNTIME_REQUIRED/);for(const moduleMaintenance of [1,'yes',null])assert.throws(()=>isolateCurrentFixtureSettingsMutex(source,{runtimeRoot:root,moduleMaintenance}),/BOOLEAN_MODULE/);});

test('core Workspace validation runtime admits exactly its supplied directory without installed mutex fallback',()=>{
 const core=path.resolve(import.meta.dirname,'../../../../../../Testing/YMCC/Workspace');
 const runtime=path.join(core,'fixed-source-owner','Build','Validation','native26','qualification-runtime');
 const definition=privateSettingsMutexDefinitionForRuntimeRoot(runtime);
 assert(definition.includes('root!=normalizeWindowsPathForCompare(L'+JSON.stringify(runtime)+')'));
 assert(!definition.includes('root.find('));
 assert(definition.includes('FILE_FLAG_OPEN_REPARSE_POINT'));
 assert.equal(privateSettingsMutexDefinitionForRuntimeRoot(),privateSettingsMutexDefinition);
});
test('Workspace scope excludes source tree owner root validation root and sibling directories',()=>{
 const core=path.resolve(import.meta.dirname,'../../../../../../Testing/YMCC/Workspace');
 for(const candidate of [core,path.join(core,'owner'),path.join(core,'owner','Mainline','run'),path.join(core,'owner','Build'),path.join(core,'owner','Build','Validation'),path.join(core,'owner','Build-other','Validation','run'),path.resolve(core,'../Workspace-other/owner/Build/Validation/run')])
  assert.throws(()=>privateSettingsMutexDefinitionForRuntimeRoot(candidate),/RUNTIME_ROOT_OUTSIDE_CORE_EVIDENCE/);
});

test('explicit26 mutex runtime admits only the pipe and original16 exact mode paths',()=>{
 const runtime=path.join(root,'qualification-runtime'),id='current105-terminal26-sdk-lifetime-compact-v1';
 const boundary=privateSettingsMutexDefinitionForRuntimeRoot(runtime,{managedInputContract:id});
 const literals=[...boundary.matchAll(/root!=normalizeWindowsPathForCompare\(L("(?:\\.|[^"\\])*")\)/g)].map(row=>JSON.parse(row[1]));
 assert.equal(literals.length,17);assert.equal(new Set(literals).size,17);assert.equal(literals[0],path.join(runtime,'pipe'));
 assert(literals.every(dir=>path.dirname(dir)===runtime));assert(!literals.includes(runtime));
 for(const label of ['positive-record-read','positive-entry','negative-rollback-switch-fault-owner'])assert(literals.includes(path.join(runtime,label)));
 assert(!boundary.includes('root.find('));assert(boundary.includes('FILE_FLAG_OPEN_REPARSE_POINT'));
});
test('explicit26 runtime layout cannot silently use a missing root or unknown contract',()=>{
 const id='current105-terminal26-sdk-lifetime-compact-v1';
 assert.throws(()=>privateSettingsMutexDefinitionForRuntimeRoot(undefined,{managedInputContract:id}),/EXPLICIT26_RUNTIME_ROOT_REQUIRED/);
 assert.throws(()=>privateSettingsMutexDefinitionForRuntimeRoot(root,{managedInputContract:id}),/QUALIFICATION_RUNTIME_ROOT_REQUIRED/);
 assert.throws(()=>privateSettingsMutexDefinitionForRuntimeRoot(root,{managedInputContract:'unknown'}),/UNKNOWN_MUTEX_RUNTIME_CONTRACT/);
});
test('explicit26 finite directory adaptation preserves the original real mutex and inverse',()=>{
 const runtime=path.join(root,'qualification-runtime'),source='#include "main.inert.cpp"\n';
 const generated=isolateCurrentFixtureSettingsMutex(source,{runtimeRoot:runtime,moduleMaintenance:true,managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1'});
 assert.equal(generated.text.replace(generated.edits[0].after,generated.edits[0].before),source);
 assert(generated.text.includes('return ::CreateMutexW(attributes,initialOwner,name.c_str());'));
 assert(generated.text.includes('identity.nFileIndexHigh'));assert.equal(generated.productionMutexReached,false);
 for(const managedInputContract of ['current105-exact21-v1','current105-terminal22-v1','current105-terminal23-sd-decoder-v1'])assert.equal(privateSettingsMutexDefinitionForRuntimeRoot(runtime,{managedInputContract}),privateSettingsMutexDefinitionForRuntimeRoot(runtime));
});
