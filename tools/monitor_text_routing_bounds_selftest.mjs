// Actual folded view versus the frozen view, including unterminated bounded
// bytes, embedded zeros, non-ASCII bytes, large offsets and lvalue mutation.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import crypto from 'node:crypto';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_GPU_ROUTING_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/gpu-family-routing-20261016').replaceAll('\\','/');fs.mkdirSync(out+'/bounds',{recursive:true});
const code=String.raw`
#define MonitorFoldedText BeforeFoldedText
#define monitorTextFieldView beforeTextFieldView
#include "__BEFORE__"
#undef MonitorFoldedText
#undef monitorTextFieldView
#include "__CURRENT__"
#include <array>
#include <iostream>
#include <fstream>
#include <limits>
#include <stdexcept>
#include "__JSON__"
using json=nlohmann::json;
int main(int argc,char**argv){if(argc!=2)return 2;json checks=json::array();auto check=[&](const std::string&n,bool ok){if(!ok)throw std::runtime_error(n);checks.push_back(n);};
 const std::vector<std::string> rows={"","G","GPU","GPU ","gPu ClOcK","GPU Core Voltage",std::string({'G','P','U',char(0),'P','o','w','e','r'}),std::string(128,'A'),std::string("GPU ")+char(0xe9)+" Power"};
 for(size_t n=0;n<rows.size();++n){const auto&raw=rows[n];const std::string_view bytes(raw.data(),raw.size());MonitorFoldedText current(bytes);BeforeFoldedText before(bytes);
  for(size_t i=0;i<raw.size()+2;++i){char expected=0;if(i<raw.size()){expected=raw[i];if(expected>='A'&&expected<='Z')expected+='a'-'A';}check("bounded classification byte "+std::to_string(n)+"/"+std::to_string(i),current.asciiAt(i)==expected);}
  check("maximum byte offset is rejected "+std::to_string(n),current.asciiAt((std::numeric_limits<size_t>::max)())==0);
  for(size_t offset:std::vector<size_t>{0,1,3,4,5,raw.size(),raw.size()+1,(std::numeric_limits<size_t>::max)()}){
   const auto tail=current.after(offset);const auto expected=offset<=bytes.size()?bytes.substr(offset):std::string_view{};const BeforeFoldedText oracle(expected);
   const std::vector<std::string>queries={"","clock","ore voltage",std::string(expected),"not present",std::string(128,'a')};
   for(const auto&q:queries)if((tail==q.c_str())!=(oracle==q.c_str()))throw std::runtime_error("suffix changed exact-match semantics");
   check("suffix exact matches agree with frozen oracle "+std::to_string(n)+"/"+std::to_string(offset),true);
  }
  for(const auto*q:{"","gpu","power","clock","not present"})check("full original equality/find/prefix behavior "+std::to_string(n)+"/"+q,(current==q)==(before==q)&&current.find(q)==before.find(q)&&current.startsWith(q)==before.startsWith(q));
 }
 std::string mutableName="GPU Clock";MonitorFoldedText live(mutableName);BeforeFoldedText old(mutableName);mutableName="GPU Core Voltage Longer";check("original lvalue view still follows current string bytes",(live=="gpu core voltage longer")&&(old=="gpu core voltage longer")&&live.asciiAt(9)=='v');
 std::array<char,128> noZero;noZero.fill('A');check("unterminated bounded field retains its full safe size",monitorTextFieldView(noZero.data(),noZero.size()).size()==128);check("null field remains empty",monitorTextFieldView(nullptr,128).empty());
 std::ofstream(argv[1])<<json({{"ok",true},{"checks",checks.size()},{"cases",checks},{"hardwareReads",false}}).dump(2);std::cout<<"TEXT_ROUTING_BOUNDS "<<checks.size()<<" checks\n";
}
`;
const file=out+'/bounds/test.cpp';fs.writeFileSync(file,code.replace('__BEFORE__',out+'/before/native/monitor_text_fold.h').replace('__CURRENT__',root+'/native/monitor_text_fold.h').replace('__JSON__',root+'/deps/json/json.hpp'));
const batch=out+'/bounds/compile.cmd';fs.writeFileSync(batch,`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\ncl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 "${file}" /Fo"${out}/bounds/test.obj" /Fe"${out}/bounds/test.exe"\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\n"${out}/bounds/test.exe" "${out}/TEXT-BOUNDS-RESULT.json"\r\nexit /b %ERRORLEVEL%\r\n`);
const run=spawnSync('cmd.exe',['/d','/s','/c','""'+batch+'""'],{windowsVerbatimArguments:true,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:2e6});fs.writeFileSync(out+'/TEXT-BOUNDS.log',(run.stdout||'')+(run.stderr||''));console.log(((run.stdout||'')+(run.stderr||'')).slice(-2400));assert.equal(run.status,0);
const r=JSON.parse(fs.readFileSync(out+'/TEXT-BOUNDS-RESULT.json','utf8'));r.headerSha256=crypto.createHash('sha256').update(fs.readFileSync('native/monitor_text_fold.h')).digest('hex');fs.writeFileSync(out+'/TEXT-BOUNDS-RESULT.json',JSON.stringify(r,null,2)+'\n');
