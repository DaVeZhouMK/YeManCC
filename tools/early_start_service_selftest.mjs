import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import vm from 'node:vm';import {createRequire} from 'node:module';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';
const repo=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(repo,'package.json'));
const index=process.argv.indexOf('--out-dir');if(index<0||!process.argv[index+1])throw Error('--out-dir required');const out=path.resolve(process.argv[index+1]);if(out.startsWith(repo+path.sep))throw Error('Evidence must be outside source');fs.mkdirSync(out,{recursive:true});
const ts=require('typescript'),{transformSync}=require('esbuild'),cases=[];
const check=async(name,run)=>{await run();cases.push({name,pass:true});};
function functionSource(file,name){const source=fs.readFileSync(file,'utf8'),ast=ts.createSourceFile(file,source,ts.ScriptTarget.Latest,true);const fn=ast.statements.find(node=>ts.isFunctionDeclaration(node)&&node.name?.text===name);assert(fn,'Missing function '+name);return fn.getText(ast);}
function evaluate(source,bindings={}){const module={exports:{}};vm.runInNewContext(transformSync(source,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,console,...bindings});return module.exports;}
const bridgeSource=functionSource(path.join(repo,'src/bridge/yeman.ts'),'steamMasterSet');
const bridge=(reply)=>{const calls=[];const fn=evaluate(bridgeSource,{invoke:async(...args)=>{calls.push(args);return reply;}}).steamMasterSet;return {fn,calls};};
await check('Steam enable confirms automatic running service with bounded native timeout',async()=>{const f=bridge({ok:true,enabled:true,service:{ok:true,known:true,installed:true,automatic:true,running:true}});await f.fn(true);assert.equal(f.calls.length,1);assert.equal(f.calls[0][0],'steam.startup.set');assert.equal(f.calls[0][2].timeoutMs,6000);});
for(const [reason,error,text] of [['service-missing',1060,/未安装/],['service-enable-failed',5,/权限不足/],['service-start-pending',0,/正在启动/],['service-start-failed',1053,/service-start-failed/]])await check('Service failure remains visible: '+reason,async()=>{const f=bridge({ok:true,enabled:true,service:{ok:false,known:true,installed:reason!=='service-missing',automatic:false,running:false,reason,win32Error:error}});await assert.rejects(f.fn(true),text);});
await check('Old native response without service evidence cannot claim Steam startup success',async()=>{await assert.rejects(bridge({ok:true,enabled:true}).fn(true),/未提供服务检查/);});
await check('Disabling startup accepts missing service and never asks for repair',async()=>{const f=bridge({ok:true,enabled:false});await f.fn(false);assert.equal(f.calls.length,1);assert.equal(f.calls[0][1].enabled,false);});
const policy=evaluate(fs.readFileSync(path.join(repo,'src/bridge/gamingHomePolicy.ts'),'utf8'));
const make=({device=3,startup=0,home='Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App',fail=false}={})=>{
 const values={DeviceForm:device,StartupToGamingHome:startup,GamingHomeApp:home},events=[];
 return {values,events,port:{read:async(_root,_key,name)=>values[name]??null,write:async(root,key,name,value)=>{events.push({op:'write',root,key,name,value});if(fail&&name==='StartupToGamingHome')return false;values[name]=value;return true;},remove:async(root,key,name)=>{events.push({op:'remove',root,key,name});delete values[name];return true;},saveBackup:async(state)=>events.push({op:'backup',state})}};
};
await check('Xbox self-heal backs up then repairs handheld identity and Windows setting',async()=>{const f=make();const result=await policy.setGamingHomeStartup(f.port,true);assert.equal(result.deviceForm,46);assert.equal(result.startupValue,1);assert.equal(f.events[0].op,'backup');assert.deepEqual(f.events.filter(row=>row.op==='write').map(row=>row.name),['DeviceForm','StartupToGamingHome']);assert.equal(result.homeApp,'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App');});
await check('Already repaired Xbox settings are idempotent',async()=>{const f=make({device:46,startup:1});await policy.setGamingHomeStartup(f.port,true);assert.equal(f.events.length,0);});
await check('standalone startup-only disable preserves handheld identity and custom home app',async()=>{const f=make({device:46,startup:1,home:'ArtemShpynov.AnyFSE_by4wjhxmygwn4!App'});const result=await policy.setGamingHomeStartup(f.port,false);assert.equal(result.deviceForm,46);assert.equal(result.startupValue,0);assert.equal(result.homeApp,'ArtemShpynov.AnyFSE_by4wjhxmygwn4!App');assert.deepEqual(f.events.filter(row=>row.op==='write').map(row=>row.name),['StartupToGamingHome']);});
await check('legacy startup toggle is independent of model/support detection and preserves home selection',async()=>{
 const f=make({device:3,startup:0,home:'Custom.Home!App'}),reads=[];const rawRead=f.port.read;
 f.port.read=async(root,key,name)=>{reads.push({root,key,name});assert(['DeviceForm','StartupToGamingHome','GamingHomeApp'].includes(name),'Unexpected hardware eligibility read');return rawRead(root,key,name);};
 await policy.setGamingHomeStartup(f.port,true);assert.equal(f.values.DeviceForm,0x2e);assert.equal(f.values.StartupToGamingHome,1);assert.equal(f.values.GamingHomeApp,'Custom.Home!App');
 f.events.length=0;await policy.setGamingHomeStartup(f.port,false);assert.equal(f.values.DeviceForm,0x2e);assert.equal(f.values.StartupToGamingHome,0);assert.deepEqual(f.events.filter(e=>e.op==='write').map(e=>e.name),['StartupToGamingHome']);
 f.values.DeviceForm=3;await policy.setGamingHomeStartup(f.port,true);assert.equal(f.values.DeviceForm,46);assert.equal(f.values.StartupToGamingHome,1);assert.equal(f.values.GamingHomeApp,'Custom.Home!App');assert(reads.length>0);
});
await check('Failed user setting rolls handheld identity back to its original value',async()=>{const f=make({fail:true});await assert.rejects(policy.setGamingHomeStartup(f.port,true));assert.equal(f.values.DeviceForm,3);assert.equal(f.values.StartupToGamingHome,0);});
await check('Registry inspection is read-only and type-invalid values cannot be repaired blindly',async()=>{const f=make();await policy.readGamingHomeState(f.port);assert.equal(f.events.length,0);const bad=make({startup:'1'});await assert.rejects(policy.setGamingHomeStartup(bad.port,true),/DWORD/);assert.equal(bad.events.length,0);});
// Compatibility: standalone callers omit explicit home selection. Fixed modes are covered below.
await check('legacy repairHandheld option without selection preserves GamingHomeApp',async()=>{for(const device of [null,3,46]){const f=make({device,startup:1,home:'Custom.Home!App'});const result=await policy.setGamingHomeStartup(f.port,false,true);assert.equal(result.deviceForm,46);assert.equal(result.startupValue,0);assert.equal(result.homeApp,'Custom.Home!App');assert(!f.events.some(e=>e.name==='GamingHomeApp'));}});
await check('Steam repair is idempotent for 46/0 and a failed startup write rolls back handheld repair',async()=>{const same=make({device:46,startup:0});await policy.setGamingHomeStartup(same.port,false,true);assert.equal(same.events.length,0);const f=make({device:3,startup:1,fail:true});await assert.rejects(policy.setGamingHomeStartup(f.port,false,true));assert.equal(f.values.DeviceForm,3);assert.equal(f.values.StartupToGamingHome,1);});
const homeWriteSource=functionSource(path.join(repo,'src/bridge/yeman.ts'),'writeGamingHomeStartup');
const homeBridge=()=>{const f=make({device:3,startup:1,home:'Custom.Home!App'});const fn=evaluate(homeWriteSource,{gamingHomeWriteTail:Promise.resolve(),gamingHomeRegistry:f.port,setGamingHomeStartup:policy.setGamingHomeStartup,retireLegacyGamingTask:async()=>{}}).writeGamingHomeStartup;return {f,fn};};
await check('legacy two-argument Windows bridge callers preserve the existing home without app discovery',async()=>{for(const enabled of [true,false]){const {f,fn}=homeBridge();await fn(enabled,true);assert.equal(f.values.DeviceForm,46);assert.equal(f.values.StartupToGamingHome,enabled?1:0);assert.equal(f.values.GamingHomeApp,'Custom.Home!App');}});
await check('explicit advanced preparation sends silent command repair, while ordinary Steam toggle retains old IPC shape',async()=>{const f=bridge({ok:true,enabled:true,service:{ok:true,known:true,installed:true,automatic:true,running:true}});await f.fn(true,true);assert.equal(f.calls[0][1].enabled,true);assert.equal(f.calls[0][1].repairCommand,true);const ordinary=bridge({ok:true,enabled:false});await ordinary.fn(false);assert.deepEqual(Object.keys(ordinary.calls[0][1]),['enabled']);});
await check('explicit mode 2/4 selects Xbox and mode 3 selects None immediately, including the screenshot flag-on/home-missing state',async()=>{
 for(const home of [null,'',policy.XBOX_GAMING_HOME_AUMID,'ArtemShpynov.AnyFSE_by4wjhxmygwn4!App'])for(const enabled of [true,false]){
  const f=make({device:3,startup:1,home});const selected=enabled?'xbox':'none';const result=await policy.setGamingHomeStartup(f.port,enabled,true,selected);
  assert.equal(result.deviceForm,46);assert.equal(result.startupValue,enabled?1:0);assert.equal(result.homeApp,enabled?policy.XBOX_GAMING_HOME_AUMID:'');
  assert.equal(f.values.GamingHomeApp,enabled?policy.XBOX_GAMING_HOME_AUMID:'');assert.equal(f.events[0]?.op,'backup');
  const writes=f.events.filter(e=>e.op==='write');if(enabled&&writes.some(e=>e.name==='StartupToGamingHome'))assert(writes.findIndex(e=>e.name==='GamingHomeApp')<writes.findIndex(e=>e.name==='StartupToGamingHome'));
 }
});
await check('explicit Xbox selection precedes enabling startup, and disabling startup precedes selecting None',async()=>{
 const f=make({device:3,startup:0,home:null});await policy.setGamingHomeStartup(f.port,true,true,'xbox');assert.deepEqual(f.events.filter(e=>e.op==='write').map(e=>e.name),['DeviceForm','GamingHomeApp','StartupToGamingHome']);
 f.events.length=0;await policy.setGamingHomeStartup(f.port,false,true,'none');assert.deepEqual(f.events.filter(e=>e.op==='write').map(e=>e.name),['StartupToGamingHome','GamingHomeApp']);
});
await check('explicit home selection is idempotent while standalone startup toggle still preserves an existing home',async()=>{
 const xbox=make({device:46,startup:1,home:policy.XBOX_GAMING_HOME_AUMID});await policy.setGamingHomeStartup(xbox.port,true,true,'xbox');assert.equal(xbox.events.length,0);
 const none=make({device:46,startup:0,home:''});await policy.setGamingHomeStartup(none.port,false,true,'none');assert.equal(none.events.length,0);
 const ordinary=make({home:'Custom.Home!App'});await policy.setGamingHomeStartup(ordinary.port,true);assert.equal(ordinary.values.GamingHomeApp,'Custom.Home!App');
});
await check('all explicit home write failure points roll back only their own attempted values',async()=>{
 for(const failAt of ['DeviceForm','GamingHomeApp','StartupToGamingHome'])for(const partial of [false,true]){
  const f=make({device:3,startup:0,home:null});const write=f.port.write;let failed=false;f.port.write=async(root,key,name,value)=>{if(name===failAt&&!failed){failed=true;if(partial)await write(root,key,name,value);return false;}return write(root,key,name,value);};
  await assert.rejects(policy.setGamingHomeStartup(f.port,true,true,'xbox'),new RegExp(failAt));assert.equal(f.values.DeviceForm,3);assert.equal(f.values.StartupToGamingHome,0);assert.equal(f.values.GamingHomeApp??null,null);
 }
});
await check('None selection failure restores original Xbox home and startup flag',async()=>{
 const f=make({device:46,startup:1,home:policy.XBOX_GAMING_HOME_AUMID});const write=f.port.write;let failed=false;f.port.write=async(root,key,name,value)=>{if(name==='GamingHomeApp'&&value===''&&!failed){failed=true;await write(root,key,name,value);return false;}return write(root,key,name,value);};
 await assert.rejects(policy.setGamingHomeStartup(f.port,false,true,'none'),/GamingHomeApp/);assert.equal(f.values.GamingHomeApp,policy.XBOX_GAMING_HOME_AUMID);assert.equal(f.values.StartupToGamingHome,1);
});
await check('backup and home-type failures stop before mutation; dropped home writes fail readback',async()=>{
 const backup=make({home:''});backup.port.saveBackup=async()=>{throw Error('backup-failed');};await assert.rejects(policy.setGamingHomeStartup(backup.port,true,true,'xbox'),/backup-failed/);assert.equal(backup.events.length,0);
 const bad=make({home:17});await assert.rejects(policy.setGamingHomeStartup(bad.port,true,true,'xbox'),/GamingHomeApp/);assert.equal(bad.events.length,0);
 const dropped=make({device:46,startup:1,home:''});dropped.port.write=async()=>true;await assert.rejects(policy.setGamingHomeStartup(dropped.port,true,true,'xbox'),/GamingHomeApp/);
});
await check('failure rollback preserves a concurrent home owner instead of restoring stale Xbox selection',async()=>{
 const f=make({device:3,startup:0,home:'Old.Home!App'});const write=f.port.write;f.port.write=async(root,key,name,value)=>{if(name==='StartupToGamingHome'&&value===1){f.values.GamingHomeApp='Concurrent.Home!App';return false;}return write(root,key,name,value);};
 await assert.rejects(policy.setGamingHomeStartup(f.port,true,true,'xbox'));assert.equal(f.values.GamingHomeApp,'Concurrent.Home!App');assert.equal(f.values.DeviceForm,3);assert.equal(f.values.StartupToGamingHome,0);
});
await check('real Windows bridge forwards explicit Xbox/None selection through serialized immediate preparation',async()=>{
 const {f,fn}=homeBridge();await Promise.all([fn(true,true,'xbox'),fn(false,true,'none'),fn(true,true,'xbox')]);assert.equal(f.values.GamingHomeApp,policy.XBOX_GAMING_HOME_AUMID);assert.equal(f.values.StartupToGamingHome,1);
});
const previousPolicyIndex=process.argv.indexOf('--before-gaming-policy');let homeSelectionNegative=null;
if(previousPolicyIndex>=0){
 const old=evaluate(fs.readFileSync(path.resolve(process.argv[previousPolicyIndex+1]),'utf8'));
 const f=make({device:46,startup:1,home:''});await old.setGamingHomeStartup(f.port,true,true,'xbox');
 await assert.rejects(async()=>assert.equal(f.values.GamingHomeApp,policy.XBOX_GAMING_HOME_AUMID));
 homeSelectionNegative={pass:true,oldStartupOnlyImplementationRejected:true,oldHome:f.values.GamingHomeApp};
}
// Old bridge is a semantic negative: it must accept the same failed service reply.
const before=process.argv.indexOf('--before-yeman');let bridgeNegative=null;
if(before>=0){const old= functionSource(path.resolve(process.argv[before+1]),'steamMasterSet');let accepted=false;await evaluate(old,{invoke:async()=>({ok:true,enabled:true,service:{ok:false,known:true,installed:false,reason:'service-missing'}})}).steamMasterSet(true).then(()=>accepted=true);assert(accepted,'Expected previous bug to accept missing service');bridgeNegative={detected:true,previousIncorrectlyAcceptedMissingService:true};}
const main=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),setAt=main.indexOf('ipc_on("steam.startup.set"'),setEnd=main.indexOf('ipc_on("steam.uiState"',setAt),setter=main.slice(setAt,setEnd),getAt=main.indexOf('ipc_on("steam.startup.get"'),getter=main.slice(getAt,setAt);
assert(setter.includes('enabled?ymcc::earlystart::repair():ymcc::earlystart::read()'));assert(setter.indexOf('startup.set(enabled,args.value("repairCommand",false))')<setter.indexOf('ymcc::earlystart::repair()'));assert(!getter.includes('repair()'));assert(setter.includes('g_aiFanMockSession.isolated'));
const msvc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207',sdk='C:/Program Files (x86)/Windows Kits/10',version='10.0.26100.0';
const env={...process.env,INCLUDE:[msvc+'/include',sdk+'/Include/'+version+'/um',sdk+'/Include/'+version+'/shared',sdk+'/Include/'+version+'/ucrt',repo+'/deps/json'].join(';'),LIB:[msvc+'/lib/x64',sdk+'/Lib/'+version+'/um/x64',sdk+'/Lib/'+version+'/ucrt/x64'].join(';'),PATH:[msvc+'/bin/Hostx64/x64','C:/Windows/System32',process.env.PATH].join(';')};
const original=fs.readFileSync(path.join(repo,'native/early_start_service.h'),'utf8');let cpp=fs.readFileSync(path.join(repo,'tools/early_start_service_selftest.cpp'),'utf8');
const startupHeader=fs.readFileSync(path.join(repo,'native/steam_native_launch_windows.h'),'utf8');
const startupPrefix=startupHeader.slice(startupHeader.indexOf('namespace ymcc::steamlaunch {'),startupHeader.indexOf('inline std::wstring registryText'))+'\n}\n';
const startupFixture=String.raw`
namespace {
void advancedStartupCases(){
 namespace fs=std::filesystem;using ymcc::steamlaunch::Startup;
 const auto root=fs::current_path()/L"owned-startup-fixture";fs::create_directories(root);
 struct Cleanup{fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}}cleanup{root};
 const auto a=root/L"profile-a",b=root/L"profile-b";fs::create_directories(a);fs::create_directories(b);
 const std::wstring expected=L"\"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat\"\r\n";
 Startup first(L"C:\\SOFT\\YeMan\\PowerControl",L"C:\\fixture\\YeManCC.exe",a.wstring());
 std::wstring text;check(first.read(text)&&text.empty(),"absent advanced command is an empty owned profile observation");
 check(first.set(true,true)&&first.read(text)&&text==expected&&first.authorized(),"actual earlystart file contains exactly quoted original BAT with CRLF");
 Startup second(L"C:\\SOFT\\YeMan\\PowerControl",L"C:\\fixture\\YeManCC.exe",b.wstring());
 check(second.read(text)&&text.empty()&&first.file!=second.file,"profile belongs to current passed account, never hardcoded developer profile");
 check(ymcc::steamlaunch::writeText(first.file,L"C:\\Tools\\Previous.exe\r\n")&&!first.set(true)&&first.read(text)&&text==L"C:\\Tools\\Previous.exe\r\n","ordinary toggle retains custom-command protection");
 check(first.set(true,true)&&first.read(text)&&text==expected,"explicit mode repair replaces stale advanced command without confirmation");
 unsigned backups=0;bool content=false;for(const auto& file:fs::directory_iterator(a)){if(file.path().filename().wstring().rfind(L".earlystart.ymcc-before-",0)==0){++backups;std::wstring value;content=ymcc::steamlaunch::readText(file.path(),value)&&value==L"C:\\Tools\\Previous.exe\r\n";}}
 check(backups==1&&content,"exact previous advanced command is backed up before repair");
 check(first.set(true,true)&&first.read(text)&&text==expected,"repeated explicit preparation keeps exact canonical BAT");
 check(ymcc::steamlaunch::writeText(first.file,L"C:\\Tools\\OldBoot.bat\r\n")&&first.set(false,true)&&first.read(text)&&text.empty(),"explicit Xbox-only preparation backs up and clears an unknown old BAT command");
 unsigned repairedBackups=0;for(const auto& file:fs::directory_iterator(a))if(file.path().filename().wstring().rfind(L".earlystart.ymcc-before-",0)==0)++repairedBackups;
 check(repairedBackups==2,"explicit enable and disable repairs each retain the prior command");
 check(first.set(false)&&first.read(text)&&text.empty()&&!first.authorized(),"Xbox-only clears own earlystart command without running BAT");
 check(ymcc::steamlaunch::writeText(first.file,L"\"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.vbs\"\r\n")&&first.set(true,true)&&first.read(text)&&text==expected,"recognized older VBS entry migrates back to original BAT");
 check(fs::exists(first.file)&&!fs::exists(second.file),"only owned fixture profile is written; second profile remains untouched");
}
}
`;
cpp=cpp.replace('int main()', '#include <filesystem>\n#include <vector>\n#include <array>\n#include <cstring>\n#include <algorithm>\n#include <cwctype>\n#include <shlobj.h>\n'+startupPrefix+startupFixture+'\nint main()').replace('try{','try{advancedStartupCases();');
const native=[];
for(const [name,header,expected] of [['current',original,true],['negative-no-auto',original.replace('if(result.state.startType!=SERVICE_AUTO_START){','if(false){'),false],['negative-no-start',original.replace('if(result.state.status==SERVICE_STOPPED){','if(false){'),false]]){
 const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});const h=path.join(dir,'early_start_service.h');fs.writeFileSync(h,header);const source=cpp.replace('"../native/early_start_service.h"',JSON.stringify(h.replaceAll('\\','/'))).replace('"../native/boot_mode_platform.h"',JSON.stringify(path.join(repo,'native/boot_mode_platform.h').replaceAll('\\','/')));const file=path.join(dir,'consumer.cpp'),exe=path.join(dir,'consumer.exe');fs.writeFileSync(file,source);
 const compile=spawnSync(msvc+'/bin/Hostx64/x64/cl.exe',['/nologo','/EHsc','/std:c++20','/utf-8','/W4','/DNOMINMAX','/DUNICODE','/D_UNICODE',file,'/Fo'+path.join(dir,'consumer.obj'),'/Fe'+exe,'/link','Advapi32.lib','User32.lib','Shell32.lib','Ole32.lib'],{cwd:dir,env,windowsHide:true,encoding:'utf8',timeout:45000});fs.writeFileSync(path.join(dir,'build.log'),(compile.stdout||'')+(compile.stderr||''));assert.equal(compile.status,0,compile.stdout+compile.stderr);
 const run=spawnSync(exe,[],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:10000});fs.writeFileSync(path.join(dir,'result.log'),(run.stdout||'')+(run.stderr||''));assert.equal(run.status===0,expected,name+' unexpected result '+run.stdout+run.stderr);if(!expected)assert.match(run.stderr,/disabled stopped service becomes automatic and running/);native.push({name,pass:expected?run.status===0:run.status!==0,negative:!expected});
}
const inputs=['native/early_start_service.h','native/steam_native_launch_windows.h','native/boot_mode_platform.h','native/main.cpp','src/bridge/yeman.ts','src/bridge/bootModes.ts','src/bridge/gamingHomePolicy.ts','src/components/BootModesPanel.vue','tools/early_start_service_selftest.cpp','tools/early_start_service_selftest.mjs'];
const sourceHashes=Object.fromEntries(inputs.map(file=>[file,createHash('sha256').update(fs.readFileSync(path.join(repo,file))).digest('hex')]));
const result={pass:true,cases,native,bridgeNegative,homeSelectionNegative,sourceHashes,scope:'actual native service policy plus compiled Win32 adapter; memory service/registry/IPC terminals; no real services, registry, applications or keyboard input executed'};fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({pass:true,bridgeAndRegistryCases:cases.length,nativeCases:29,negativeCases:native.filter(row=>row.negative).length+(bridgeNegative?1:0)}));
