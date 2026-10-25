// Offline: run the actual mode-report constructor with a fake write boundary.
// No Windows/HID imports, product main, Host, SDK or controller calls execute.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.argv[2]??'G:/YeManCC-Work/Build/Validation/MSI-Controller-R12-20261013/mode-report-payload-20261014');
fs.mkdirSync(out,{recursive:true});
const native=path.join(repo,'native/main.cpp');
const hc=path.join(repo,'deps/handheldcompanion-runtime/source/Devices/MSI/ClawA1M.cs');
const ctw='G:/YeManCC-Work/Isolated/Tasks/Gamepad/_scratch/ctw-helper-decomp/XboxGamingBarHelper.decompiled.cs';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const inputs=[native,hc,ctw];const before=inputs.map(file=>({file,sha256:hash(fs.readFileSync(file))}));
const nativeText=fs.readFileSync(native,'utf8');
function extract(src,anchor){const begin=src.indexOf(anchor);if(begin<0)throw Error('Missing '+anchor);let depth=0,end=-1;const first=src.indexOf('{',begin);
 const mask=src.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"/g,m=>m.replace(/[^\n]/g,' '));
 for(let i=first;i<mask.length;i++){if(mask[i]==='{')depth++;if(mask[i]==='}'&&!--depth){end=i+1;break;}}
 if(end<0)throw Error('Unclosed '+anchor);return {code:src.slice(begin,end),line:src.slice(0,begin).split('\n').length};}
const diag=extract(nativeText,'struct MsiClawHidDiag {');
const state=extract(nativeText,'enum class MsiClawIoState {');
const result=extract(nativeText,'struct MsiClawWriteResult {');
const outcome=extract(nativeText,'struct MsiClawCommandOutcome {');
const method=extract(nativeText,'static MsiClawCommandOutcome msiClawSwitchModeOnHandle(');
const ct=extract(fs.readFileSync(ctw,'utf8'),'private bool SendSwitchMode(byte mode)');
const ht=extract(fs.readFileSync(hc,'utf8'),'protected bool SwitchMode(GamepadMode gamepadMode)');
if(!ct.code.includes('new byte[64]')||!ct.code.includes('array[5] = mode')||!ct.code.includes('array[6] = 0'))throw Error('CTW baseline changed');
if(!ht.code.includes('15, 0, 0, 60')||!ht.code.includes('device.Write(msg, 0, 64)'))throw Error('HC baseline changed');
const header=Array(64).fill(0);
for(const m of ct.code.matchAll(/array\[(\d+)\]\s*=\s*(\d+);/g))header[Number(m[1])]=Number(m[2]);
if(header[4]!==36||header[6]!==0)throw Error('CTW switch/m-key baseline changed');
fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({source:before,actualMethod:{line:method.line,sha256:hash(method.code)},ctw:{line:ct.line,sha256:hash(ct.code)},hc:{line:ht.line,sha256:hash(ht.code)},faultInjectionDefined:false,liveHardware:false},null,2));
const source=`#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <json.hpp>
using BYTE=uint8_t;using USHORT=uint16_t;using DWORD=uint32_t;using ULONG=uint32_t;using HANDLE=intptr_t;
constexpr HANDLE INVALID_HANDLE_VALUE=-1;
${diag.code};
${state.code};
${result.code};
${outcome.code};
static std::vector<BYTE> sent;static int writes=0,closes=0;static HANDLE selected=0;static MsiClawWriteResult injected;
static MsiClawWriteResult msiClawCommandWriteBounded(HANDLE h,const BYTE* frame,size_t len){++writes;selected=h;sent.assign(frame,frame+len);return injected;}
static void msiClawCloseModeCommand(HANDLE){++closes;}
${method.code}
int main(int argc,char** argv){if(argc!=2)return 2;using json=nlohmann::json;json cases=json::array();unsigned failures=0;
 auto run=[&](const char* name,BYTE mode,HANDLE handle,bool ok,bool retained,DWORD written){writes=closes=0;sent.clear();injected={};injected.ok=ok;injected.handleRetained=retained;injected.written=written;injected.attempted=handle!=INVALID_HANDLE_VALUE;
 MsiClawHidDiag d;d.productId=0x1902;d.usagePage=0xFFF0;d.usage=0x40;d.outputReportByteLength=64;d.interfacePath="fake-known-msi-command";
 const auto actual=msiClawSwitchModeOnHandle(mode,handle,d);std::vector<BYTE> expected={${header.join(',')}};expected[5]=mode;
 const bool passed=sent==expected&&writes==1&&selected==handle&&closes==(handle!=INVALID_HANDLE_VALUE&&!retained?1:0)&&actual.write.ok==ok&&actual.write.written==written&&actual.diag.interfacePath==d.interfacePath;
 if(!passed)++failures;cases.push_back({{"name",name},{"passed",passed},{"mode",mode},{"payload",sent},{"writeCalls",writes},{"closeCalls",closes},{"sdkOrHidCalls",0}});};
 run("XInput exact CTW payload",1,77,true,false,64);run("DInput exact CTW payload",2,77,true,false,64);run("Desktop constructor parity only",4,77,true,false,64);
 run("retained IO owns handle",2,77,false,true,0);run("short write result preserved",2,77,false,false,63);run("missing handle result preserved",1,INVALID_HANDLE_VALUE,false,false,0);
 std::ofstream output(argv[1]);output<<json({{"passed",failures==0},{"cases",cases},{"failureCount",failures},{"actualConstructorExecuted",true},{"actualIoOwnerExecuted",false},{"productExecuted",false},{"hardwareCalls",0}}).dump(2);std::cout<<"MODE_REPORT_PAYLOAD cases=6 failures="<<failures<<" fake-only\\n";return failures?1:0;}
`;
fs.writeFileSync(path.join(out,'actual-mode-report-selftest.cpp'),source);
const batch=path.join(out,'compile.cmd');fs.writeFileSync(batch,'@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /WX "'+path.join(out,'actual-mode-report-selftest.cpp')+'" /I"'+path.join(repo,'deps/json')+'" /Fo"'+path.join(out,'selftest.obj')+'" /Fe"'+path.join(out,'selftest.exe')+'"\r\nexit /b %errorlevel%\r\n');
function run(label,exe,args){const r=spawnSync(exe,args,{encoding:'utf8',timeout:180000});fs.writeFileSync(path.join(out,label+'.log'),(r.stdout??'')+(r.stderr??''));if(r.error||r.status!==0)throw Error(label+' failed: '+(r.error??r.status));}
run('build','cmd.exe',['/d','/c',batch]);run('run',path.join(out,'selftest.exe'),[path.join(out,'report.json')]);
for(const row of before)if(hash(fs.readFileSync(row.file))!==row.sha256)throw Error('Source changed '+row.file);
console.log('PASS actual mode report, six fake cases; byte-for-byte fixed CTW parity, HC prefix/count parity; no real HID/SDK/product execution.');
