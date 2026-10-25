// Actual shared native exclusions and admission expression; synthetic candidates.
// No process launch/control, sensor reads, saved settings, or Steam requests.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import cp from 'node:child_process';import assert from 'node:assert/strict';
const root=process.cwd(),out=path.resolve(process.env.YMCC_MONITOR_REPAIR_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-repair-20261015');fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync('native/main.cpp','utf8');
function extract(start){const i=source.indexOf(start);assert(i>=0,start);let depth=0,opened=false;for(let j=source.indexOf('{',i);j<source.length;++j){if(source[j]==='{'){depth++;opened=true}else if(source[j]==='}'&&--depth===0&&opened)return source.slice(i,j+1)}throw Error('Unclosed function '+start)}
const matcher=extract('static bool sgNamePatternMatches('),excludes=extract('static bool sgNameExcludedBy('),builtin=extract('static bool nativeMonitorExcluded(');
const predicate='(!isWhitelisted && (nativeMonitorExcluded(name) || sgNameExcludedBy(name, userExcludes)))';assert(source.includes(predicate));
const code=`#include <algorithm>\n#include <string>\n#include <vector>\n#include <iostream>\n#include <cassert>\n#include <fstream>\n#include "${root.replaceAll('\\','/')}/deps/json/json.hpp"\nusing json=nlohmann::json;\n#include "${root.replaceAll('\\','/')}/native/monitor_frame_output.h"\n${matcher}\n${excludes}\n${builtin}\nint main(int argc,char**argv){assert(argc==2);json checks=json::array();
 const auto denied=[&](const std::wstring&name,bool isWhitelisted,const std::vector<std::wstring>&userExcludes){return ${predicate};};
 for(const auto*name:{L"cl",L"clang",L"clang-cl",L"clang++",L"gcc",L"g++",L"cc1",L"cc1plus",L"link",L"lld",L"lld-link",L"ld",L"rustc",L"msbuild",L"cmake",L"ninja",L"python3",L"python3.13",L"python3.13t",L"python3.14",L"pythonw3.13",L"python",L"pythonw",L"node",L"dotnet"}){assert(denied(name,false,{}));assert(!denied(name,true,{}));}
 checks.push_back("Known build/interpreter processes are rejected by the actual shared predicate; explicit whitelist keeps precedence");
 for(const auto*name:{L"cyberpunk2077",L"ace7game",L"eldenring",L"ff7rebirth",L"my-custom-game",L"clangame",L"pythonquest",L"link-adventure"})assert(!denied(name,false,{}));
 checks.push_back("Actual game stems and similarly prefixed custom names remain candidates");
 assert(denied(L"cyberpunk2077",false,{L"cyberpunk*"}));assert(!denied(L"cyberpunk2077",true,{L"cyberpunk*"}));
 checks.push_back("Existing user exclusions and explicit whitelist behavior stay intact");
 // Reproduce the observed large-build-worker competition using the actual
 // predicate. Working-set ordering is a synthetic model, not a live scanner.
 struct Candidate{const wchar_t*name;unsigned memory;};std::vector<Candidate> candidates{{L"cl",8192},{L"python3.13",4096},{L"cyberpunk2077",2048}};const wchar_t*best=nullptr;unsigned memory=0;for(const auto&v:candidates)if(!denied(v.name,false,{})&&v.memory>memory){best=v.name;memory=v.memory;}assert(best&&std::wstring(best)==L"cyberpunk2077");
 checks.push_back("Observed compiler/interpreter names cannot displace a real game solely through a large working set");
 ymcc::monitorframe::Output frames;const unsigned long long at=1700000000000ULL;
 frames.bind(77,99);frames.hardware(at/1000,120,80,10,30,"cyberpunk2077",at,8.333,1000);assert(!frames.packet(at).is_null());
 for(const auto*name:{L"cl",L"python3.13"}){const bool reject=denied(name,false,{});frames.bind(reject?0:88,reject?0:100);frames.hardware(at/1000,120,80,10,30,"compiler",at,8.333,1000);assert(frames.packet(at).is_null());}
 frames.bind(77,99);frames.hardware(at/1000+1,120,80,10,30,"cyberpunk2077",at+1000,8.333,1000);assert(frames.packet(at+1000)["fps"]==120);
 checks.push_back("Shared exclusion plus actual FPS Output clears compiler-associated data and admits a genuine game afterward");
 json result={{"ok",true},{"checks",checks},{"scope","Extracted production matcher, blacklist and admission predicate; synthetic names/ordering, no process or Steam changes."}};std::ofstream(argv[1])<<result.dump(2);std::cout<<"Shared game exclusions PASS: "<<checks.size()<<" cases\\n";
}`;
fs.writeFileSync(path.join(out,'process-exclusions.cpp'),code);
fs.writeFileSync(path.join(out,'build-process-exclusions.cmd'),`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /DNOMINMAX /DUNICODE /D_UNICODE "${out}/process-exclusions.cpp" /Fo"${out}/process-exclusions.obj" /Fe"${out}/process-exclusions.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/process-exclusions.exe" "${out}/PROCESS-EXCLUSIONS-RESULT.json"\r\nexit /b %errorlevel%\r\n`);
const run=cp.spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',path.join(out,'build-process-exclusions.cmd')],{encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'PROCESS-EXCLUSIONS-BUILD.log'),run.stdout+run.stderr);if(run.status!==0)throw Error(run.stdout+run.stderr);
const report=JSON.parse(fs.readFileSync(path.join(out,'PROCESS-EXCLUSIONS-RESULT.json'),'utf8'));assert(report.ok);
fs.writeFileSync(path.join(out,'PROCESS-EXCLUSIONS-PIN.json'),JSON.stringify({generatedAt:new Date().toISOString(),source:'native/main.cpp',matcherSha256:crypto.createHash('sha256').update(matcher).digest('hex'),exclusionsSha256:crypto.createHash('sha256').update(builtin).digest('hex')},null,2));
console.log('Shared game exclusions PASS:',report.checks.length,'cases');
