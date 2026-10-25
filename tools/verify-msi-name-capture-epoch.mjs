// Compile/run only the extracted class with fake API boundaries; no observer launch.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';import{fileURLToPath}from'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.argv[2]??'G:/YeManCC-Work/Build/Validation/MSI-Controller-R12-20261013/five-stage-name-gap-20261014/actual-epoch-final');
fs.mkdirSync(out,{recursive:true});
const source=path.join(repo,'native','msi_usb_name_origin_windows.h'),context=path.join(repo,'native','msi_usb_name_origin_context.h'),fixture=path.join(repo,'tools','msi_usb_name_capture_epoch_actual_selftest.cpp');
const inputs=[source,context,fixture,fileURLToPath(import.meta.url)];const sha=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');const before=inputs.map(f=>({path:f,sha256:sha(f)}));
const text=fs.readFileSync(source,'utf8'),start=text.indexOf('class NameProvenance {'),end=text.lastIndexOf('\n};');if(start<0||end<start)throw Error('No bounded actual class');
const original=text.slice(start,end+3);
const actual=original.replaceAll('ContextReader','FakeContextReader').replaceAll('ReadOnlyUsbIo','FakeReadOnlyUsbIo').replaceAll('WaitForSingleObject','FakeParentWait').replaceAll('GetTickCount64','FakeTick');
const fake=String.raw`#define NOMINMAX
#include <windows.h>
#include <cfgmgr32.h>
#include <usbioctl.h>
#include <functional>
#include <set>
#include <json.hpp>
#include "msi_usb_name_origin_context.h"
#include "msi_usb_name_origin_decode.h"
#include "msi_usb_name_origin_join.h"
namespace ymcc::msi::usbName {
using Json=nlohmann::json;
struct Fixture {Context current{};uint64_t now=100;bool parentLive=true,changeEpochOnResolve=false;unsigned rootResolves=0,queries=0;};inline Fixture fixture;
class FakeContextReader {public:bool open(const std::wstring&){return true;}bool snapshot(Context&out){out=fixture.current;return true;}};
inline DWORD FakeParentWait(HANDLE,DWORD){return fixture.parentLive?WAIT_TIMEOUT:WAIT_OBJECT_0;}
inline uint64_t FakeTick(){return fixture.now;}
struct IoReply{bool ok=false;std::vector<uint8_t>bytes;Json receipt=Json::object();};
class FakeReadOnlyUsbIo{public:void reap(){}unsigned pending()const{return 0;}IoReply query(const std::wstring&,DWORD,std::vector<uint8_t>,uint64_t){++fixture.queries;return{};}};
struct RootBinding{bool ok=false;DEVINST node=0,parent=0;std::wstring driverKey,hubPath;Json facts={{"state","fake-root-unavailable"}};};
inline RootBinding resolveRoot(const std::string&){++fixture.rootResolves;if(fixture.changeEpochOnResolve&&fixture.rootResolves==1)++fixture.current.hostEpoch;return{};}
inline Json contextFacts(const Context&c){return{{"sourceGeneration",c.sourceGeneration},{"powerGeneration",c.powerGeneration},{"hostEpoch",c.hostEpoch},{"sourceValid",c.sourceValid==1}};}
inline std::string utf8(const std::wstring&){return"fake-unreachable";}
inline bool driverKey(const IoReply&,std::wstring&){return false;}
inline std::vector<uint8_t>descriptorRequest(unsigned,unsigned,unsigned,unsigned,unsigned){return{};}
`;
const generated=path.join(out,'NameProvenance.actual.generated.h');fs.writeFileSync(generated,fake+actual+'\n}\n');
const batch=path.join(out,'compile.cmd'),exe=path.join(out,'actual-epoch-selftest.exe');
fs.writeFileSync(batch,'@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /WX /DUNICODE /D_UNICODE "'+fixture+'" /I"'+out+'" /I"'+path.join(repo,'native')+'" /I"'+path.join(repo,'deps','json')+'" /Fo"'+path.join(out,'actual-epoch.obj')+'" /Fe"'+exe+'"\r\nexit /b %errorlevel%\r\n');
function run(name,command,args){const result=spawnSync(command,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout:60000});fs.writeFileSync(path.join(out,name+'.log'),(result.stdout??'')+(result.stderr??''));fs.writeFileSync(path.join(out,name+'-command.json'),JSON.stringify({command,args,cwd:out,status:result.status,error:result.error?.message??null},null,2));return result;}
const build=run('build','cmd.exe',['/d','/c',batch]);if(build.error||build.status!==0)throw Error('Fake build failed; see build.log');
const resultPath=path.join(out,'report.json'),test=run('run',exe,[resultPath]);
const stable=before.every(s=>sha(s.path)===s.sha256);fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({sources:before,actualClassSha256:crypto.createHash('sha256').update(original).digest('hex'),actualClassStartLine:text.slice(0,start).split('\n').length,substitutions:['ContextReader -> FakeContextReader','ReadOnlyUsbIo -> FakeReadOnlyUsbIo','WaitForSingleObject -> FakeParentWait','GetTickCount64 -> FakeTick','root resolution/descriptor builders -> inert local functions; real Windows structures only'],sourceStable:stable,productExecuted:false,liveDeviceApis:0},null,2));
if(!stable)throw Error('Source changed during fixture');
console.log(fs.readFileSync(path.join(out,'run.log'),'utf8'));if(test.error||test.status!==0)process.exitCode=1;
