import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import assert from 'node:assert/strict';
import {REPO,generateEndpoint,sourceFiles,hash} from './contract.mjs';
import {currentProject} from './compile-managed.mjs';
const source=fs.readFileSync(path.join(REPO,'native/BatteryHost/Program.cs'),'utf8');
test('receive fragment is current validation then real router; no copied dispatch implementation',()=>{
 const x=generateEndpoint(source);
 assert.ok(x.text.includes(x.chunk));
 assert.ok(x.chunk.indexOf('RuntimeContract.Validate(root)')<x.chunk.indexOf('object result=domains.Dispatch(root)'));
 assert.ok(x.dispatchMethod.includes('Snapshot result=op switch'));
 assert.ok(!x.text.includes(x.dispatchMethod)&&!x.text.includes(x.restore));
 assert.ok(!x.text.includes('Snapshot result=op switch'));
 assert.ok(x.text.includes('Exchange(HostDomainRouter domains'));
 for(const fragment of ['RuntimeContract.Identity()','catch(OperationCanceledException)','catch(Exception e)','requestTimeout.CancelAfter(TimeSpan.FromSeconds(18))'])assert.ok(x.text.includes(fragment));
});
test('missing, legacy or reordered validation/router sources require review',()=>{
 for(const mutated of [source.replace('RuntimeContract.Validate(root);',''),source.replace('object result=domains.Dispatch(root);','Snapshot result=op switch {};'),source.replace('RuntimeContract.Validate(root);','').replace('object result=domains.Dispatch(root);','object result=domains.Dispatch(root); RuntimeContract.Validate(root);'),source.replace('internal static Snapshot DispatchBattery','internal static Snapshot LegacyDispatch')])assert.throws(()=>generateEndpoint(mutated));
});
test('current project graph and both leaf closures retain router and compile dependencies',async()=>{
 const host=currentProject(REPO,'native/BatteryHost/YeManBatteryHost.csproj');
 const required=['native/BatteryHost/HostDomainRouter.cs','native/BatteryHost/SecondaryDisplayController.cs','capabilities/HcSecondaryDisplayCapabilityCatalog.cs'];
 const ui=await import('../battery_ui_native_recovery/contract.mjs');
 for(const relative of required){assert.ok(host.compile.includes(path.join(REPO,relative)));assert.ok(sourceFiles().includes(relative));assert.ok(ui.sourceFiles().includes(relative));}
 // The approved scenario plans and original exact faults are not rewritten by router maintenance.
 assert.equal(JSON.parse(fs.readFileSync(path.join(REPO,'tools/fixtures/battery_native_host_roundtrip/cases.json'))).positiveCases,14);
 assert.equal(JSON.parse(fs.readFileSync(path.join(REPO,'tools/fixtures/battery_ui_native_recovery/cases.json'))).positiveCases,3);
 assert.match(hash(path.join(REPO,'native/BatteryHost/HostDomainRouter.cs')),/^[a-f0-9]{64}$/);
});
