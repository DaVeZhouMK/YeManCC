// Candidate/current actual publisher: locale, oversized retention, UTF-8 and
// allocation-failure recovery. Instrumentation observes scratch only, no IO.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import crypto from 'node:crypto';
const root=process.cwd().replaceAll('\\','/'),task=path.resolve(process.env.YMCC_TOP_SERIALIZER_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/top-text-serializer-20261017').replaceAll('\\','/'),probe=process.argv.includes('--probe'),folder=task+(probe?'/probe':'/accepted'),out=folder+'/robustness';fs.mkdirSync(out,{recursive:true});const parent=JSON.parse(fs.readFileSync(folder+'/RESULT.json','utf8'));const main=fs.readFileSync('native/main.cpp','utf8').replaceAll('\r\n','\n');const pubStart=main.indexOf('static std::string nativeMonitorTopPayloadText('),pubEnd=main.indexOf('\nstatic ymcc::monitorgpu::StickySelection g_monitorGpuSelection',pubStart);assert(pubStart>=0&&pubEnd>pubStart);const selected=probe?fs.readFileSync(task+'/proposed-top.inc','utf8').replaceAll('\r\n','\n'):main.slice(pubStart,pubEnd);assert.equal(crypto.createHash('sha256').update(selected).digest('hex'),parent.sourcePins.after,'Publisher changed: regenerate the actual serializer selftest first');
const generated=fs.readFileSync(folder+'/top-strings.cpp','utf8'),begin=generated.indexOf('int main(int argc,char**argv)');assert(begin>0);let prefix=generated.slice(0,begin);
const fail='static thread_local std::size_t failAfter=SIZE_MAX;\n';prefix=prefix.replace('void* operator new(std::size_t n){',fail+'void* operator new(std::size_t n){if(failAfter!=SIZE_MAX){if(failAfter==0){failAfter=SIZE_MAX;throw std::bad_alloc();}--failAfter;}');
const ns='namespace after {\n';assert.equal(prefix.split(ns).length,2);prefix=prefix.replace(ns,ns+'static thread_local std::size_t scratchCapacity=0,serializerBuilds=0;\n');
const release='if (text.capacity() > retainedLimit()) std::string{}.swap(text);';assert.equal(prefix.split(release).length,2);prefix=prefix.replace(release,release+' scratchCapacity=text.capacity();');
const commit='thousands = nextThousands; decimal = nextDecimal;';assert.equal(prefix.split(commit).length,2);prefix=prefix.replace(commit,commit+' ++serializerBuilds;');
const large='std::string result; result.swap(text); return result;';assert.equal(prefix.split(large).length,2);prefix=prefix.replace(large,'std::string result; result.swap(text); scratchCapacity=text.capacity(); return result;');
const returned='            return text;';assert.equal(prefix.split(returned).length,2);prefix=prefix.replace(returned,'            scratchCapacity=text.capacity(); return text;');
const tests=String.raw`
#include <clocale>
#include "__CONTRACT_HEADER__"
static_assert(ymcc::monitorsnapshot::topMaxBytes==16u*1024u,"review serialized scratch limit when snapshot budget changes");
#include <dbghelp.h>
#pragma comment(lib,"dbghelp.lib")
static void same(const Input& input,const json& c){const auto expected=oldCall(input,c),actual=legacyCall(input,c);check(actual==expected,"complete bytes match dump() under exceptional input");check(after::scratchCapacity<=16384,"serialized scratch retention remains bounded");++comparisons;}
int main(int argc,char**argv){std::set_terminate([]{try{if(auto e=std::current_exception())std::rethrow_exception(e);}catch(const std::exception&e){std::cerr<<"TERMINATE "<<e.what()<<"\n";}catch(...){std::cerr<<"TERMINATE unknown\n";}void* frames[32]{};const auto n=CaptureStackBackTrace(0,32,frames,nullptr);const auto proc=GetCurrentProcess();SymInitialize(proc,nullptr,TRUE);for(unsigned k=0;k<n;++k){alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO)+512]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=511;DWORD64 offset=0;if(SymFromAddr(proc,reinterpret_cast<DWORD64>(frames[k]),&offset,symbol))std::cerr<<symbol->Name<<"+"<<offset<<"\n";}std::_Exit(88);});check(argc==2,"result path");json rows=json::array();Input input=seed();same(input,core(16));
// Every byte plus incomplete/overlong/surrogate sequences from device labels.
for(unsigned value=0;value<256;++value){input=seed();input.hw.gpuName.fill(0);input.hw.gpuName[0]='G';input.hw.gpuName[1]=char(value);input.hw.gpuName[2]='X';same(input,core(value%129));}
for(const std::string bad:std::vector<std::string>{"\xc0\xaf","\xed\xa0\x80","\xf0\x80\x80\x80","\xf4\x90\x80\x80","\xf0\x9f\x92","\xe2\x82","\xe4\xb8\xad\xe6\x96\x87","\xf0\x9f\x8e\xae"}){input=seed();input.hw.gpuName.fill(0);std::memcpy(input.hw.gpuName.data(),bad.data(),bad.size());same(input,core(16));}
// Oversized documents retain old complete output, never a truncated JSON.
for(std::size_t size:{0U,16U,128U,256U,1024U,4096U,12000U}){
 input=seed();json c=json::array();for(std::size_t n=0;n<size;++n)c.push_back(double(n)+.123456789);
 const auto expected=oldCall(input,c),actual=legacyCall(input,c);check(actual==expected,"oversized payload bytes preserved");check(after::scratchCapacity<=16384,"oversized capacity released at return");if(actual.size()>16384)check(after::scratchCapacity==std::string{}.capacity(),"large output buffer transferred to owned result");rows.push_back({{"coreSlots",size},{"outputBytes",actual.size()},{"retainedCapacity",after::scratchCapacity}});same(input,core(16));
}
std::cerr<<"PHASE byte/oversize passed\n";
// Legacy structured inputs keep the original recursive array behavior.
for(const json structured:std::vector<json>{json::array({json{{"slot",1}},json::array({1,2}),"legacy",nullptr}),json::array({json::array({json{{"nested",true}}})})}){
 same(seed(),structured);same(seed(),core(16));same(seed(),structured);same(seed(),json(nullptr));
}
// Cached characters match dump() across supported Windows numeric locales.
const auto* current=std::setlocale(LC_NUMERIC,nullptr);const std::string saved=current?current:"C";json locales=json::array();std::size_t supported=0;
for(const auto* name:{"C","German_Germany.1252","French_France.1252","Chinese_China.936","C"}){
 std::cerr<<"LOCALE "<<name<<"\n";const auto* selected=std::setlocale(LC_NUMERIC,name);if(!selected){locales.push_back({{"requested",name},{"supported",false}});continue;}++supported;input=seed();input.hw.gpuPowerW=123.456789;input.file.readMBps=12.345;input.file.writeMBps=.125;same(input,core(128));const auto* info=std::localeconv();locales.push_back({{"requested",name},{"supported",true},{"decimal",int(static_cast<unsigned char>(*info->decimal_point))},{"serializerBuilds",after::serializerBuilds}});
}
check(std::setlocale(LC_NUMERIC,saved.c_str())!=nullptr,"restore original numeric locale");check(supported>=3,"numeric locale branches actually executed");check(after::serializerBuilds>=3,"serializer rebuild occurs across locale characters");same(seed(),core(16));
std::cerr<<"PHASE locale passed\n";
// Fail the nth actual new() while building/serializing, then repair next input.
std::size_t faults=0,successes=0;for(bool owned:{false,true})for(std::size_t index=0;index<22;++index){
 same(seed(),core(16));input=seed();input.ts+=index;input.coreTs=input.ts;json large=json::array();for(unsigned n=0;n<12000;++n)large.push_back(double(n)+.123456789);
 const auto expected=oldCall(input,large);std::cerr<<"FAIL-SCHEDULE "<<index<<"\n";failAfter=index;try{const auto result=owned?newCall(input,large,&large):legacyCall(input,large);failAfter=SIZE_MAX;check(result==expected,"successful fault-injection call still has complete output");++successes;std::cerr<<"CALL succeeded\n";}catch(const std::bad_alloc&){failAfter=SIZE_MAX;++faults;std::cerr<<"CALL caught bad_alloc\n";}
 check(after::scratchCapacity<=16384,"failure does not pin oversize serialized capacity");std::cerr<<"RECOVER begin\n";auto recovered=seed();recovered.healthy=index%2;recovered.hw.gpuSelectedPresent=index%3;same(recovered,core(index%129));std::cerr<<"RECOVER end\n";
}
check(faults>2&&successes>0,"fault and successful paths both execute");
json result={{"ok",true},{"checks",checks},{"completeExceptionalComparisons",comparisons},{"faults",faults},{"successfulFaultSchedules",successes},{"supportedLocaleCases",supported},{"serializerBuilds",after::serializerBuilds},{"locales",locales},{"oversizeCases",rows},{"hardwareReads",false},{"wholeProcessCpuMeasured",false},{"scope","Actual before/candidate publisher and existing Input builder. Only failure/capacity counters injected; no installed app or hardware. Size cap covers serialized scratch, not pre-existing topJson storage."}};std::ofstream(argv[1])<<result.dump(2);std::cout<<"ROBUSTNESS PASS "<<checks<<" checks / "<<comparisons<<" exceptional comparisons / "<<faults<<" allocation faults\n";
}
`;
const code=prefix+tests.replaceAll('__CONTRACT_HEADER__',root+'/native/monitor_snapshot_contract.h');fs.writeFileSync(out+'/robustness.cpp',code);const batch=out+'/run.cmd';fs.writeFileSync(batch,['@echo off','call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul','if errorlevel 1 exit /b %errorlevel%',`cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /Zi "${out}/robustness.cpp" /Fo"${out}/robustness.obj" /Fe"${out}/robustness.exe" /Fd"${out}/debug.pdb" /link /DEBUG`,'if errorlevel 1 exit /b %errorlevel%',`"${out}/robustness.exe" "${out}/RESULT.json"`,'exit /b %errorlevel%',''].join('\r\n'));const run=spawnSync('cmd.exe',['/d','/s','/c','""'+batch+'""'],{encoding:'utf8',windowsHide:true,windowsVerbatimArguments:true,timeout:60000,maxBuffer:3e6});fs.writeFileSync(out+'/COMPILE-RUN.log',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,run.stdout+run.stderr);const result=JSON.parse(fs.readFileSync(out+'/RESULT.json','utf8'));assert(result.ok);result.proposedOnly=probe;result.instrumentedSourceSha256=crypto.createHash('sha256').update(code).digest('hex');fs.writeFileSync(out+'/RESULT.json',JSON.stringify(result,null,2)+'\n');console.log(run.stdout.trim());
