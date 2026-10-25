// Extract production code so publication tests cannot accidentally prove a mock.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const root=process.cwd(),out=path.resolve(process.env.YMCC_FPS_AUDIT_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/fps-limit-log-audit-20261012');fs.mkdirSync(out,{recursive:true});
const s=fs.readFileSync('native/main.cpp','utf8');const structure=s.slice(s.indexOf('struct NativeMonitorHw {'),s.indexOf('\n};',s.indexOf('struct NativeMonitorHw {'))+3);const start=s.indexOf('static std::string nativeMonitorTopPayloadText('),end=s.indexOf('\nstatic void nativeMonitorLoop()',start);if(start<0||end<start||!structure.startsWith('struct'))throw Error('Missing publisher anchors');const constructor=s.slice(start,end),r=root.replaceAll('\\','/');
const tests=`
int main(int argc,char** argv) {
    assert(argc==2); NativeMonitorHw hw; json cases=json::array();
    auto check=[&](const char* name,ymcc::monitorrtss::Limit cap,const char* expected){
        auto top=json::parse(nativeMonitorTopPayloadText(hw,true,false,-1,-1,0,0,false,json::array(),0,{},0,0,{},cap));
        const bool known=std::string(expected)!="—";
        assert(top["fpsLimitValid"]==known);assert(top["fpsLimitTs"]==(known?1700000000000LL:0));
        assert(top["fpsLimitSource"]==(known?"rtss-global":"unavailable"));
        if(!known)assert(top["fpsLimit"].is_null());else assert(top["fpsLimit"].is_number());
        cases.push_back({{"name",name},{"top",top},{"expected",expected}});
    };
    check("integral",{true,120,1700000000000LL},"120");check("explicit-unlimited",{true,0,1700000000000LL},"不限");
    check("fractional",{true,59.94,1700000000000LL},"59.94");check("missing",{},"—");
    check("invalid-zero",{false,0,1700000000000LL},"—");check("missing-time",{true,120,0},"—");
    check("negative",{true,-1,1700000000000LL},"—");check("overflow",{true,1000001,1700000000000LL},"—");
    check("NaN",{true,std::numeric_limits<double>::quiet_NaN(),1700000000000LL},"—");
    check("tiny-nonzero-not-unlimited",{true,0.000001,1700000000000LL},"0.000001");
    std::ofstream(argv[1])<<cases.dump();std::cout<<"Actual native FPS-cap publication PASS: "<<cases.size()<<" fixtures\\n";
}`;
const header=`#include <cassert>\n#include <fstream>\n#include <iostream>\n#include <limits>\n#include "${r}/deps/json/json.hpp"\n#include "${r}/native/monitor_battery_energy.h"\n#include "${r}/native/monitor_rtss_global.h"\n#include "${r}/native/monitor_voltage.h"\n#include "${root.replaceAll('\\','/')}/native/monitor_fan_receipt.h"\n#include "${root.replaceAll('\\','/')}/native/monitor_memory_network.h"\n#include "${root.replaceAll('\\','/')}/native/monitor_filesystem_rates.h"\nusing json=nlohmann::json;\n`;
fs.writeFileSync(path.join(out,'native-fps-publisher.cpp'),header+structure+'\n'+constructor+tests);
fs.writeFileSync(path.join(out,'FPS-PUBLISHER-PIN.json'),JSON.stringify({file:path.resolve('native/main.cpp'),constructorSha256:crypto.createHash('sha256').update(constructor).digest('hex'),scope:'Extracted unchanged real production constructor; synthetic arguments'},null,2));
fs.writeFileSync(path.join(out,'build-fps-tests.cmd'),`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /DUNICODE /D_UNICODE "${r}/tools/monitor_rtss_global_selftest.cpp" /Fo"${out}/reader-test.obj" /Fe"${out}/reader-selftest.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/reader-selftest.exe" "${out}/rtss-global-fixture"\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /DUNICODE /D_UNICODE "${out}/native-fps-publisher.cpp" /Fo"${out}/publisher-test.obj" /Fe"${out}/fps-publisher-selftest.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/fps-publisher-selftest.exe" "${out}/native-fps-payloads.json"\r\nexit /b %errorlevel%\r\n`);
console.log('Actual native FPS publisher fixture generated.');
