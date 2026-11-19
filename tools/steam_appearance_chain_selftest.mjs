import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {execFile} from 'node:child_process';
import {buildSync,transformSync} from 'esbuild';
const root=process.cwd(),out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Build/Validation/SteamPresetChain-20261013-172935/chain');fs.mkdirSync(out,{recursive:true});
const component=path.resolve(process.argv[3]||'PowerControl/steam-appearance.ps1');
const profile=path.join(out,'profile-'+Date.now());fs.mkdirSync(profile,{recursive:true});
const wrapper=path.join(out,'invoke-component.ps1');
fs.writeFileSync(wrapper,'\ufeff'+String.raw`param([string]$Component,[string]$Profile,[string]$Action,[string]$Payload='',[string]$BackendPath='')
$ErrorActionPreference='Stop'
$env:USERPROFILE=$Profile
$env:LOCALAPPDATA=Join-Path $Profile 'AppData\Local'
function Get-Process { param($Name,$Id) @() }
function Get-NetTCPConnection { param($State,$LocalPort) @() }
$fixtureAction=$Action
. $Component -Action Library -Payload $Payload -BackendPath $BackendPath
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($Component,[ref]$tokens,[ref]$errors)
if($errors.Count){throw $errors[0]}
$controller=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Invoke-AppearanceControl'},$true)
if(!$controller){throw 'Actual appearance controller missing'}
Invoke-Expression $controller.Extent.Text
function Invoke-AppearanceSharedSteam($Request){
 if($Request.operation -cne 'identity'){throw 'Unexpected public Steam read in offline fixture'}
 [pscustomobject]@{ok=$true;protocol=1;presence=0;pid=0;birth='0';root='';processEpoch='1';accountEpoch='1';powerGeneration='1'}
}
function Get-AppearanceProcessCandidates {param($Kind,[switch]$Refresh) @()}
function Get-AppearanceTcpListeners {param($LocalPort) @()}
try {$result=Invoke-AppearanceControl $fixtureAction;Write-JsonResult $result;if(!$result.ok){exit 1}}
catch {Write-JsonResult ([pscustomobject]@{ok=$false;message=$_.Exception.Message});exit 1}
`);
const run=(action,payload='',backendPath='')=>new Promise((resolve,reject)=>execFile(path.join(process.env.SystemRoot||'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe'),['-NoProfile','-ExecutionPolicy','Bypass','-File',wrapper,'-Component',component,'-Profile',profile,'-Action',action,'-Payload',payload,'-BackendPath',backendPath],{timeout:45000,windowsHide:true,maxBuffer:1024*1024},(error,stdout,stderr)=>{if(error&&typeof error.code!=='number')reject(error);else resolve({exitCode:error?.code||0,stdout,stderr})}));
const preferenceModule={exports:{}};vm.runInNewContext(buildSync({entryPoints:[path.resolve(import.meta.dirname,'../src/bridge/bootModeGlobalPreferences.ts')],bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text,{module:preferenceModule,exports:preferenceModule.exports,structuredClone,TextEncoder});
const mod={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync('src/bridge/steamAppearance.ts','utf8'),{loader:'ts',format:'cjs'}).code,{module:mod,exports:mod.exports,require:name=>name==='./bootModeGlobalPreferences'?preferenceModule.exports:({fs:{},shell:{},settingsStore:{}}),Error,TextEncoder,btoa,performance});
const cases=[],calls=[];let nativeCalls=0;
const deps={location:async()=>({directory:path.dirname(component)}),exists:async file=>fs.existsSync(file),run:async(_program,args)=>{const get=name=>{const i=args.indexOf(name);return i<0?'':args[i+1]};calls.push(get('-Action'));return run(get('-Action'),get('-Payload'),get('-BackendPath'))},hidden:async()=>{throw Error('unexpected helper launch')},delay:async()=>{},idlePolicy:async()=>{nativeCalls++;return {supported:true,version:3,sharedSteamReadProtocol:1,processQuery:'Local\\YMCC.CssInventory.'+'a'.repeat(32),pid:0,birth:'0'}}};
const service=mod.exports.createSteamAppearance(deps);
const first=await service.read();assert.equal(first.controlProtocol,2);assert.equal(first.steamRunning,false);cases.push('real PowerShell status advertises the protocol required by the current frontend');
assert.equal(first.themes.filter(row=>row.configured).length,7);assert.equal(first.themes.some(row=>row.installed),false);assert.equal(fs.existsSync(path.join(profile,'homebrew/themes')),false);cases.push('fresh-profile Status exposes 7/7 default selections without writing files or starting Steam');
const firstEdit=await service.apply(first,{'Round':false});assert.equal(firstEdit.ok,true);assert.equal(firstEdit.snapshot.themes.filter(row=>row.configured).length,6);assert.equal(firstEdit.snapshot.themes.find(row=>row.name==='Round').configured,false);cases.push('first single-theme edit preserves the other six enabled defaults across preset takeover');
const direct=await run('Status','','C:\\fixture\\CssLoader-Standalone-Headless.exe');assert.equal(direct.exitCode,0,direct.stderr);assert.equal(JSON.parse(direct.stdout).ok,true);cases.push('real parameter binding accepts BackendPath and emits parseable JSON');
const restored=await service.reset('preset');assert.equal(restored.ok,true);assert.equal(restored.saved,true);assert.equal(restored.snapshot.themes.filter(row=>row.installed&&row.configured).length,7);cases.push('actual frontend reset invokes real script and restores all seven defaults');
for(const theme of mod.exports.STEAM_APPEARANCE_THEMES){const actual=fs.readFileSync(path.join(profile,'homebrew/themes',theme.name,'config_USER.json'));const expected=fs.readFileSync(path.join(path.dirname(component),'steam-appearance/themes',theme.name,'config_USER.json'));assert.deepEqual(actual,expected);}cases.push('reset copies exact bundled parameter bytes for every theme');
const edited=await service.apply(await service.read(),{'Darken Unfocused Games':false,'NEX-85 Keyboard':false});assert.equal(edited.ok,true);assert.equal(edited.snapshot.themes.filter(row=>row.configured).length,5);cases.push('normal user selections remain separately saved at 5/7');
const resetAgain=await service.reset('preset');assert.equal(resetAgain.snapshot.themes.filter(row=>row.configured).length,7);assert.equal(JSON.parse(fs.readFileSync(path.join(profile,'homebrew/themes/Mini Carousel/config_USER.json'),'utf8')).Size,'0.7');cases.push('reset after personal edits returns to 7/7 and reference carousel size 0.7');
const detailOff=await service.apply(await service.read(),{'YMCC Game Details':false});assert.equal(detailOff.ok,true);assert.equal(detailOff.snapshot.themes.find(row=>row.name==='YMCC Game Details').configured,false);assert.equal(detailOff.snapshot.themes.filter(row=>row.configured).length,6);const detailConfig=path.join(profile,'homebrew/themes/YMCC Game Details/config_USER.json');assert.equal(JSON.parse(fs.readFileSync(detailConfig,'utf8')).active,false);assert.deepEqual(fs.readFileSync(path.join(profile,'homebrew/themes/YMCC Game Details/shared.css')),fs.readFileSync('PowerControl/steam-appearance/themes/YMCC Game Details/shared.css'));cases.push('YMCC details toggle installs the local scoped CSS and turns it off without changing the other six themes');
const detailOn=await service.apply(await service.read(),{'YMCC Game Details':true});assert.equal(detailOn.ok,true);assert.equal(detailOn.snapshot.themes.filter(row=>row.configured).length,7);cases.push('the details layout can be enabled again through the real frontend-to-PowerShell chain without Steam startup');
const allOff=await service.apply(await service.read(),Object.fromEntries(mod.exports.STEAM_APPEARANCE_THEMES.map(row=>[row.name,false])));assert.equal(allOff.ok,true);assert.equal(allOff.snapshot.themes.filter(row=>row.configured).length,0);
const allOn=await service.apply(await service.read(),Object.fromEntries(mod.exports.STEAM_APPEARANCE_THEMES.map(row=>[row.name,true])));assert.equal(allOn.ok,true);assert.equal(allOn.snapshot.themes.filter(row=>row.configured).length,7);cases.push('the real PowerShell validator accepts all seven master-switch states in one batch and restores 7/7');
assert(nativeCalls>0);assert(!calls.includes('Ensure'));cases.push('offline restore consumes public absent facts without CSS/native startup and never launches Steam');
const result={ok:true,cases,passed:cases.length,component,profile,calls,realSteamOperations:0,realCssOperations:0};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
