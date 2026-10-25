import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {extract,sha} from './fixtures/msi_owned_return_retired_io/source-binding.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');const args=process.argv.slice(2);let out,baseline;
for(let i=0;i<args.length;i+=2){assert(i+1<args.length);if(args[i]==='--out')out=path.resolve(args[i+1]);else if(args[i]==='--baseline-main')baseline=path.resolve(args[i+1]);else throw Error('Use --out FRESH --baseline-main PRESERVED_CPP');}
baseline??=path.join(repo,'tools/fixtures/msi_route_lifecycle/OriginalRouteWindow.cpp.txt');
assert(out&&baseline&&path.isAbsolute(out)&&!fs.existsSync(out));assert(!out.toLowerCase().startsWith(repo.toLowerCase()+path.sep));fs.mkdirSync(out,{recursive:true});
const inputs=['native/main.cpp','native/virtual_start_protection.h','native/msi_virtual_wake.h','deps/json/json.hpp','tools/fixtures/msi_route_lifecycle/Fixture.cpp','tools/fixtures/msi_route_lifecycle/OriginalRouteWindow.cpp.txt','tools/fixtures/msi_route_lifecycle/OriginalRouteWindow.receipt.json','tools/msi_dinput_route_lifecycle_selftest.mjs','tools/fixtures/msi_owned_return_retired_io/source-binding.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].map(relative=>({relative,path:path.join(repo,relative),sha256:sha(fs.readFileSync(path.join(repo,relative)))}));inputs.push({relative:'preserved-baseline-main',path:baseline,sha256:sha(fs.readFileSync(baseline))});
const source=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),old=fs.readFileSync(baseline,'utf8'),bindings=[];
const select=(s,name,kind='function')=>extract(s,kind,name,'native/main.cpp',bindings);
const constants=['kMsiDinputRouteFirstWindowMs','kMsiDinputRouteTotalWindowMs'].map(name=>{const match=source.match(new RegExp('^static constexpr ULONGLONG '+name+'[^;]+;','m'));assert(match);return match[0];}).join('\n');
const common=select(source,'MsiDinputRouteWindowAction','enum')+'\n'+constants;
const base=select(source,'msiDinputRouteWindowDecide'),current=select(source,'msiDinputRouteWindowDecideForWake'),historical=select(old,'msiDinputRouteWindowDecideForWake'),registration=select(source,'nativeLifecycleEventIsBase');
assert.notEqual(sha(current),sha(historical));const template=fs.readFileSync(path.join(repo,'tools/fixtures/msi_route_lifecycle/Fixture.cpp'),'utf8');
const vc=path.win32.normalize('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'),q=s=>{assert(!/["%\r\n]/.test(s),'Unsupported compiler path');return '"'+s+'"';};const results=[];
fs.writeFileSync(path.join(out,'SOURCE-BINDINGS.json'),JSON.stringify({inputs,bindings,scope:'Actual selected wrappers, base event registry and R3 authority. Global/OS boundaries are memory facts; no SDK or product main.'},null,2));
for(const variant of [
 {name:'positive',wrapper:current},
 {name:'original-10s-policy',wrapper:historical,expected:'late-DInput: route remains readable'},
 {name:'negative-wake-budget-leak',from:'if(!wake.armTick || admittedAt>=wake.armTick)return action;',to:'if(false)return action;',expected:'wake60s cannot lend budget'},
 {name:'negative-family-scope-leak',from:'playerPending && msiClawKnownBoardAuthority()',to:'playerPending && true',expected:'non-MSI has no player source-wait'}
]){
 const dir=path.join(out,variant.name);fs.mkdirSync(dir);let wrapper=variant.wrapper??current;if(variant.from){assert.equal(wrapper.split(variant.from).length,2);wrapper=wrapper.replace(variant.from,variant.to);}
 let code=template;for(const [token,value]of [['ENUM_AND_CONSTANTS',common],['PRODUCTION_DECIDE',base],['PRODUCTION_WRAPPER',wrapper],['PRODUCTION_BASE_EVENT',registration]]){assert.equal(code.split('@@'+token+'@@').length,2);code=code.replace('@@'+token+'@@',()=>value);}fs.writeFileSync(path.join(dir,'Fixture.cpp'),code);
 const command=calibrationToolchainPreamble(vc)+'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNOMINMAX /I'+q(path.join(repo,'native'))+' /I'+q(path.join(repo,'deps/json'))+' '+q(path.join(dir,'Fixture.cpp'))+' /Fe'+q(path.join(dir,'Fixture.exe'))+' /Fo'+q(path.join(dir,'Fixture.obj'))+'\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(path.join(dir,'build.cmd'),command);
 for(const [stage,exe,argv]of [['build','C:/Windows/System32/cmd.exe',['/d','/c',path.join(dir,'build.cmd')]],['run',path.join(dir,'Fixture.exe'),[]]]){
  const r=spawnSync(path.win32.normalize(exe),argv,{cwd:dir,encoding:'utf8',timeout:60000,windowsHide:true});fs.writeFileSync(path.join(dir,stage+'.log'),(r.stdout??'')+(r.stderr??'')+(r.error?String(r.error):''));const expected=stage==='run'&&variant.expected?1:0;if(r.error||r.status!==expected)throw Error(variant.name+' '+stage+' failed; inspect '+path.join(dir,stage+'.log'));
 }
 const receipt=JSON.parse(fs.readFileSync(path.join(dir,'run.log'),'utf8').trim());if(variant.expected)assert(receipt.failures.some(s=>s.includes(variant.expected)));else assert(receipt.passed);fs.writeFileSync(path.join(dir,'RESULT.json'),JSON.stringify(receipt,null,2));results.push({name:variant.name,expectedRejection:!!variant.expected,receipt});
}
assert(inputs.every(row=>sha(fs.readFileSync(row.path))===row.sha256),'Source drift');const result={status:'PASS',passed:true,results,sourceStillCurrent:true,originalPlayerBudgetMs:30000,changedSharedLifecycleAuthority:false,newModeWrites:false,actualPumpAcceptanceProven:false,hardwareAccepted:false};fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({status:result.status,positiveChecks:results[0].receipt.checks,positiveCases:results[0].receipt.cases.length,negativeFailureCounts:results.slice(1).map(r=>r.receipt.failures.length),output:out}));
