import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {execFile} from 'node:child_process';
import {transformSync} from 'esbuild';
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
& $Component -Action $Action -Payload $Payload -BackendPath $BackendPath
`);
const run=(action,payload='',backendPath='')=>new Promise((resolve,reject)=>execFile('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',wrapper,'-Component',component,'-Profile',profile,'-Action',action,'-Payload',payload,'-BackendPath',backendPath],{timeout:45000,windowsHide:true,maxBuffer:1024*1024},(error,stdout,stderr)=>{if(error&&typeof error.code!=='number')reject(error);else resolve({exitCode:error?.code||0,stdout,stderr})}));
const mod={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync('src/bridge/steamAppearance.ts','utf8'),{loader:'ts',format:'cjs'}).code,{module:mod,exports:mod.exports,require:()=>({fs:{},shell:{},settingsStore:{}}),Error,TextEncoder,btoa,performance});
const cases=[],calls=[];let nativeCalls=0;
const deps={location:async()=>({directory:path.dirname(component)}),exists:async file=>fs.existsSync(file),run:async(_program,args)=>{const get=name=>{const i=args.indexOf(name);return i<0?'':args[i+1]};calls.push(get('-Action'));return run(get('-Action'),get('-Payload'),get('-BackendPath'))},hidden:async()=>{throw Error('unexpected helper launch')},delay:async()=>{},idlePolicy:async()=>{nativeCalls++;throw Error('unexpected native call offline')}};
const service=mod.exports.createSteamAppearance(deps);
const first=await service.read();assert.equal(first.controlProtocol,2);assert.equal(first.steamRunning,false);cases.push('real PowerShell status advertises the protocol required by the current frontend');
assert.equal(first.themes.filter(row=>row.configured).length,8);assert.equal(first.themes.some(row=>row.installed),false);assert.equal(fs.existsSync(path.join(profile,'homebrew/themes')),false);cases.push('fresh-profile Status exposes 8/8 default selections without writing files or starting Steam');
const firstEdit=await service.apply(first,{'Round':false});assert.equal(firstEdit.ok,true);assert.equal(firstEdit.snapshot.themes.filter(row=>row.configured).length,7);assert.equal(firstEdit.snapshot.themes.find(row=>row.name==='Round').configured,false);cases.push('first single-theme edit preserves the other seven enabled defaults across preset takeover');
const direct=await run('Status','','C:\\fixture\\CssLoader-Standalone-Headless.exe');assert.equal(direct.exitCode,0,direct.stderr);assert.equal(JSON.parse(direct.stdout).ok,true);cases.push('real parameter binding accepts BackendPath and emits parseable JSON');
const restored=await service.reset('preset');assert.equal(restored.ok,true);assert.equal(restored.saved,true);assert.equal(restored.snapshot.themes.filter(row=>row.installed&&row.configured).length,8);cases.push('actual frontend reset invokes real script and restores all eight defaults');
for(const theme of mod.exports.STEAM_APPEARANCE_THEMES){const actual=fs.readFileSync(path.join(profile,'homebrew/themes',theme.name,'config_USER.json'));const expected=fs.readFileSync(path.join(path.dirname(component),'steam-appearance/themes',theme.name,'config_USER.json'));assert.deepEqual(actual,expected);}cases.push('reset copies exact bundled parameter bytes for every theme');
const edited=await service.apply(await service.read(),{'Darken Unfocused Games':false,'Game Cover Reflections':false,'NEX-85 Keyboard':false});assert.equal(edited.ok,true);assert.equal(edited.snapshot.themes.filter(row=>row.configured).length,5);cases.push('normal user selections remain separately saved at 5/8');
const resetAgain=await service.reset('preset');assert.equal(resetAgain.snapshot.themes.filter(row=>row.configured).length,8);assert.equal(JSON.parse(fs.readFileSync(path.join(profile,'homebrew/themes/Mini Carousel/config_USER.json'),'utf8')).Size,'0.7');cases.push('reset after personal edits returns to 8/8 and reference carousel size 0.7');
const detailOff=await service.apply(await service.read(),{'YMCC Game Details':false});assert.equal(detailOff.ok,true);assert.equal(detailOff.snapshot.themes.find(row=>row.name==='YMCC Game Details').configured,false);assert.equal(detailOff.snapshot.themes.filter(row=>row.configured).length,7);const detailConfig=path.join(profile,'homebrew/themes/YMCC Game Details/config_USER.json');assert.equal(JSON.parse(fs.readFileSync(detailConfig,'utf8')).active,false);assert.deepEqual(fs.readFileSync(path.join(profile,'homebrew/themes/YMCC Game Details/shared.css')),fs.readFileSync('PowerControl/steam-appearance/themes/YMCC Game Details/shared.css'));cases.push('YMCC details toggle installs the local scoped CSS and turns it off without changing the original seven themes');
const detailOn=await service.apply(await service.read(),{'YMCC Game Details':true});assert.equal(detailOn.ok,true);assert.equal(detailOn.snapshot.themes.filter(row=>row.configured).length,8);cases.push('the details layout can be enabled again through the real frontend-to-PowerShell chain without Steam startup');
const allOff=await service.apply(await service.read(),Object.fromEntries(mod.exports.STEAM_APPEARANCE_THEMES.map(row=>[row.name,false])));assert.equal(allOff.ok,true);assert.equal(allOff.snapshot.themes.filter(row=>row.configured).length,0);
const allOn=await service.apply(await service.read(),Object.fromEntries(mod.exports.STEAM_APPEARANCE_THEMES.map(row=>[row.name,true])));assert.equal(allOn.ok,true);assert.equal(allOn.snapshot.themes.filter(row=>row.configured).length,8);cases.push('the real PowerShell validator accepts all eight master-switch states in one batch and restores 8/8');
assert.equal(nativeCalls,0);assert(!calls.includes('Ensure'));cases.push('offline restore needs no CSS/native startup and never launches Steam');
const result={ok:true,cases,passed:cases.length,component,profile,calls,realSteamOperations:0,realCssOperations:0};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
