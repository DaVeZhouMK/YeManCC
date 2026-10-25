// Real Windows Script Host and inert console children; only inventory/registry
// and launch targets are substituted. Never starts Steam or a real linked app.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
const exec = promisify(execFile), repo = process.cwd();
const out = path.resolve(process.argv[2] || 'G:/YeManCC-Work/Build/Validation/SteamVbsLaunch-20261016/vbs');
fs.mkdirSync(out, { recursive: true });
const source = fs.readFileSync(path.join(repo, 'PowerControl/YeManSteam.vbs'), 'utf8');
const checks=[];function check(text, work){work();checks.push(text);console.log('PASS '+text)}
const cpp=String.raw`#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <json.hpp>
int wmain(){
 wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
 auto utf8=[](const std::wstring& s){const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;};
 nlohmann::json row={{"pid",GetCurrentProcessId()},{"consolePresent",GetConsoleWindow()!=nullptr},{"consoleVisible",GetConsoleWindow()&&IsWindowVisible(GetConsoleWindow())},{"command",utf8(GetCommandLineW())}};
 std::ofstream f(std::filesystem::path(std::wstring(exe)+L".result.json"));f<<row.dump(2);return f.good()?0:1;
}`;
fs.writeFileSync(path.join(out,'inert.cpp'),cpp);
const compile=path.join(out,'compile.cmd');
fs.writeFileSync(compile,`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\nif not defined WindowsSdkDir set "WindowsSdkDir=C:\\Program Files (x86)\\Windows Kits\\10\\"\r\nif "%WindowsSDKVersion%"=="\\" set "WindowsSDKVersion=10.0.26100.0\\"\r\nif not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\\"\r\nset "INCLUDE=%WindowsSdkDir%Include\\%WindowsSDKVersion%um;%WindowsSdkDir%Include\\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\\%WindowsSDKVersion%winrt;%INCLUDE%"\r\nset "LIB=%WindowsSdkDir%Lib\\%WindowsSDKVersion%um\\x64;%WindowsSdkDir%Lib\\%WindowsSDKVersion%ucrt\\x64;%LIB%"\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"${repo}\\deps\\json" "${out}\\inert.cpp" /Fo"${out}\\inert.obj" /Fe"${out}\\inert.exe" /link user32.lib\r\n`);
const build=await exec('cmd.exe',['/d','/c',compile],{windowsHide:true,timeout:30000});fs.writeFileSync(path.join(out,'build.log'),build.stdout+build.stderr);
const vb=s=>'"'+s.replaceAll('"','""')+'"';
const cases=[];
async function run(name,{existing=[],queryError=false,registry=true,fallback=false,host='cscript.exe'}={}){
 const dir=path.join(out,name+'-'+Date.now());fs.mkdirSync(path.join(dir,'YeManSteam'),{recursive:true});fs.mkdirSync(path.join(dir,'profile'));
 const steam=path.join(dir,'steam.exe'),addon=path.join(dir,'Console Addon & Space.exe'),unicode=path.join(dir,'插件.exe');
 const managedCss=path.join(dir,'CssLoader-Standalone-Headless.exe');
 for(const file of [steam,addon,unicode,managedCss])fs.copyFileSync(path.join(out,'inert.exe'),file);
 for(const [name,value] of [['legacy-css-preset.txt',managedCss],['custom-css-alias.txt',managedCss],['css.txt',addon],['duplicate.txt',addon],['custom-utf8.txt',unicode],['custom-disabled.txt.disabled',steam],['invalid.txt','cmd.exe /c echo INVALID'],['missing.txt',path.join(dir,'missing.exe')]])fs.writeFileSync(path.join(dir,'YeManSteam',name),value+'\r\n');
 const log=path.join(dir,'dispatch.txt');
 const snapshot=existing.map((entry,index)=>`Set entry${index}=New FixtureProcess\nentry${index}.Name=${vb(entry.name||'steam.exe')}\nentry${index}.ExecutablePath=${vb(entry.path==='addon'?addon:steam)}\nentry${index}.Foreign=${entry.foreign?'True':'False'}\nentry${index}.Unreadable=${entry.unreadable?'True':'False'}`).join('\n');
 const shims=`
Class FixtureShell
 Public CurrentDirectory
 Public Function RegRead(key)
  ${registry?`If ${fallback?'False':'True'} And key="HKCU\\Software\\Valve\\Steam\\SteamExe" Then RegRead=${vb(steam)} : Exit Function\nIf ${fallback?'True':'False'} And key="HKLM\\SOFTWARE\\WOW6432Node\\Valve\\Steam\\InstallPath" Then RegRead=${vb(dir)} : Exit Function`:''}
  Err.Raise 2
 End Function
 Public Function ExpandEnvironmentStrings(value)
  ExpandEnvironmentStrings=${vb(path.join(dir,'profile'))}
 End Function
 Public Sub Run(command, style, wait)
  Dim shell, stream
  Set stream=CreateObject("Scripting.FileSystemObject").OpenTextFile(${vb(log)},8,True,-1)
  stream.WriteLine CStr(style) & vbTab & CStr(wait) & vbTab & command
  stream.Close
  If command="steam://open/gamepadui" Then Exit Sub
  If Left(command,Len(${vb('"'+steam+'"')})) <> ${vb('"'+steam+'"')} And command <> ${vb('"'+addon+'"')} And command <> ${vb('"'+unicode+'"')} Then Err.Raise 5
  Set shell=CreateObject("WScript.Shell")
  shell.CurrentDirectory=CurrentDirectory
  shell.Run command, style, wait
 End Sub
End Class
Class FixtureProcess
 Public Name, ExecutablePath, Foreign, Unreadable
 Public Function GetOwner(ByRef user,ByRef domain)
  Dim network
  Set network=CreateObject("WScript.Network")
  user=network.UserName : domain=network.UserDomain
  If Foreign Then user="FixtureOtherAccount"
  GetOwner=0
  If Unreadable Then GetOwner=2
 End Function
End Class
Function FixtureProcesses()
 Dim stream${existing.map((_,i)=>', entry'+i).join('')}
 Set stream=CreateObject("Scripting.FileSystemObject").OpenTextFile(${vb(path.join(dir,'snapshot.txt'))},8,True,-1)
 stream.WriteLine "one-shot-snapshot" : stream.Close
 ${queryError?'Err.Raise 5':''}
 ${snapshot}
 FixtureProcesses=Array(${existing.map((_,i)=>'entry'+i).join(',')})
End Function
`;
 let entry=source.replace('Set ws = CreateObject("WScript.Shell")','Set ws = New FixtureShell')
 .replace(/Set service = GetObject\([^\r\n]+\)/,'Set service = Nothing')
 .replace('Set processes = service.ExecQuery("SELECT * FROM Win32_Process")','processes = FixtureProcesses()');
 assert(!entry.includes('Set service = GetObject('));assert(entry.includes('processes = FixtureProcesses()'));
 const script=path.join(dir,'YeManSteam.vbs');fs.writeFileSync(script,Buffer.concat([Buffer.from([0xff,0xfe]),Buffer.from(entry+'\r\n'+shims,'utf16le')]));
 const begin=Date.now();let code=0,result;
 try{result=await exec(host,['//B','//NoLogo',script],{windowsHide:true,timeout:10000})}catch(e){if(typeof e.code!=='number')throw e;code=e.code;result=e;}
 const elapsed=Date.now()-begin;
 const due=Date.now()+5000;const expected=fs.existsSync(log)?fs.readFileSync(log,'utf16le').replace(/^\uFEFF/,'').trim().split(/\r?\n/).filter(Boolean):[];
 const startedFiles=[steam,addon,unicode].filter(file=>expected.some(line=>line.includes('"'+file+'"')));
 while(startedFiles.some(file=>!fs.existsSync(file+'.result.json'))&&Date.now()<due)await new Promise(r=>setTimeout(r,30));
 const children=startedFiles.map(file=>{assert(fs.existsSync(file+'.result.json'),'missing inert child evidence: '+file);return{file,...JSON.parse(fs.readFileSync(file+'.result.json','utf8'))}});
 const snapshotCount=fs.readFileSync(path.join(dir,'snapshot.txt'),'utf16le').replace(/^\uFEFF/,'').trim().split(/\r?\n/).length;
 const record={name,code,elapsed,snapshotCount,dispatch:expected,children,stderr:result.stderr||''};cases.push(record);return record;
}
const cold=await run('cold');
check('actual VBS runs under Windows Script Host and dispatches once',()=>{assert.equal(cold.code,0);assert.equal(cold.snapshotCount,1);assert.equal(cold.dispatch.length,3);assert(cold.elapsed<5000)});
check('console addons with spaces, ampersand and Unicode paths have hidden windows',()=>{const addons=cold.children.filter(c=>!c.file.endsWith('steam.exe'));assert.equal(addons.length,2);for(const child of addons)assert.equal(child.consoleVisible,false);for(const line of cold.dispatch.filter(l=>!l.includes('-bigpicture')))assert(line.startsWith('0\tFalse\t'))});
check('Steam gets one asynchronous Big Picture launch; disabled/malformed/duplicate links add no launches',()=>{assert.equal(cold.dispatch.filter(l=>l.includes('-bigpicture')).length,1);assert(cold.dispatch.every(l=>l.includes('\tFalse\t')));assert(!cold.dispatch.some(l=>/cmd \/c|powershell|nircmd/i.test(l)))});
const running=await run('running',{existing:[{}, {name:'Console Addon & Space.exe',path:'addon'}]});
check('verified existing Steam is preserved with no mode URI, process launch or repeated Big Picture opening',()=>{assert.equal(running.code,0);assert.equal(running.dispatch.length,1);assert(!running.dispatch.some(l=>/steam:\/\/|-bigpicture|Console Addon & Space.exe/.test(l)))});
const foreign=await run('foreign',{existing:[{foreign:true},{name:'Console Addon & Space.exe',path:'addon',foreign:true}]});
check('another Windows account cannot masquerade as the current users Steam/addon',()=>{assert.equal(foreign.code,0);assert.equal(foreign.dispatch.length,3);assert(foreign.dispatch.some(l=>l.includes('-bigpicture')))});
const unknown=await run('unknown',{existing:[{unreadable:true}]});
check('unverifiable Steam ownership refuses launches instead of duplicating or touching addons',()=>{assert.equal(unknown.code,4);assert.equal(unknown.dispatch.length,0)});
const unknownAddon=await run('unknown-addon',{existing:[{name:'Console Addon & Space.exe',path:'addon',unreadable:true}]});
check('unverifiable addon is skipped while a verified Steam launch remains possible',()=>{assert.equal(unknownAddon.code,0);assert.equal(unknownAddon.dispatch.length,2);assert(!unknownAddon.dispatch.some(l=>l.includes('Console Addon & Space.exe')))});
const queryFail=await run('query-fails',{queryError:true});
check('process snapshot failure issues no fallback or retry launch',()=>{assert.equal(queryFail.code,2);assert.equal(queryFail.dispatch.length,0);assert.equal(queryFail.snapshotCount,1)});
const noRegistry=await run('no-registry',{registry:false});
check('missing Steam install issues no addon or URI launch',()=>{assert.equal(noRegistry.code,3);assert.equal(noRegistry.dispatch.length,0)});
const fallback=await run('registry-fallback',{fallback:true});
check('Steam registry fallback still reaches the original Big Picture arguments',()=>{assert.equal(fallback.code,0);assert.equal(fallback.dispatch.filter(l=>l.includes('-bigpicture -offline')).length,1)});
const windowless=await run('wscript-cold',{host:'wscript.exe'});
check('the production wscript host runs the same direct hidden launcher successfully',()=>{assert.equal(windowless.code,0);assert.equal(windowless.dispatch.length,3);for(const child of windowless.children.filter(c=>!c.file.endsWith('steam.exe')))assert.equal(child.consoleVisible,false)});
check('VBS has no resident loop, sleeps, console shell chain, Xbox termination or startup writes',()=>{assert(!/WScript\.Sleep|Do\s+While|While\s+True|RegWrite|taskkill|cmd\s+\/c|nircmd\.exe|powershell|\.earlystart|YeManSteam2?\.bat/i.test(source.replace(/^'.*$/gm,'')));assert(source.includes('Launch(Quote(executable), 0)'))});
const report={ok:true,passed:checks.length,checks,cases,realSteamOperations:0,realAddonLaunches:0,actualWshExecuted:true,actualInertChildProcesses:true,consoleVisibilityObserved:true,inventoryAndRegistryMocked:true};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log('STEAM_VBS_OK passed='+checks.length+' realSteamOperations=0');
