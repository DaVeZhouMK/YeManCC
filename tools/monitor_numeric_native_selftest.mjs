// Pure production memory/network normalizer. No Windows/sensor calls.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const root=process.cwd(),out=path.resolve(process.env.YMCC_MONITOR_NUMERIC_NATIVE_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/numeric-data-audit-20261015/native');fs.mkdirSync(out,{recursive:true});
const header=path.resolve(process.env.YMCC_MONITOR_NUMERIC_NATIVE_HEADER||'native/monitor_memory_network.h').replaceAll('\\','/');
const source=`#include <iostream>
#include <limits>
#include "${header}"
int main(){int failed=0,total=0;auto check=[&](const char*name,bool ok){++total;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\\n";};using namespace ymcc::monitormemory;
 auto m=normalize({8192,24576,32768,99});check("contradictory load derives from coherent physical capacities",m.load==25);
 m=normalize({8192,24576,32768,24.9});check("reported direct-load rounding preserved",m.load==24.9);
 m=normalize({8192,24576,16384,50});check("impossible available size invalidates the conflicting tuple",m.used<0&&m.available<0&&m.load<0&&m.total==16384);
 m=normalize({8192,24576,40000,50});check("sum conflict is not derived",m.used<0&&m.available<0&&m.load<0);
 m=normalize({0,32768,32768,0});check("genuine empty-used memory remains zero",m.used==0&&m.load==0);
 m=normalize({8192,24576,-1,-1});check("missing total/load derives from used+available",m.total==32768&&m.load==25);
 m=normalize({-1,24576,32768,-1});check("used derives only from total and available",m.used==8192&&m.load==25);
 m=normalize({8192,-1,32768,-1});check("available derives only from total and used",m.available==24576&&m.load==25);
 m=normalize({-1,-1,-1,0});check("load-only genuine zero preserved",m.load==0&&m.total<0);
 m=normalize({std::numeric_limits<double>::infinity(),-1,-1,-1});check("nonfinite amount stays unavailable",m.used<0&&m.load<0);
 auto amount=[&](const char*u,double v){return toMb(MonitorFoldedText(std::string_view(u)),v);};
 check("GB memory is reported in MiB convention",amount("GB",8)==8192);check("byte memory converts to MiB",amount("B",8388608)==8);check("invalid amount unit unavailable",amount("rpm",8)<0);
 auto rate=[&](const char*u,double v){return toMBps(MonitorFoldedText(std::string_view(u)),v,u);};
 check("KB/s uses decimal byte rate",rate("KB/s",4200)==4.2);check("MiB/s uses binary multiplier to decimal MB/s",rate("MiB/s",1)==1.048576);check("unsupported bit unit is not mistaken for bytes",rate("Mb/s",100)<0);
 std::cout<<"TOTAL "<<total<<" FAILED "<<failed<<"\\n";return failed?2:0;}
`;
fs.writeFileSync(out+'/numeric-native.cpp',source);
const command=`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /I"${root}/native" "${out}/numeric-native.cpp" /Fo"${out}/numeric-native.obj" /Fe"${out}/numeric-native.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/numeric-native.exe"\r\nexit /b %errorlevel%\r\n`;
fs.writeFileSync(out+'/build-numeric-native.cmd',command);
const run=spawnSync('cmd.exe',['/d','/c',out+'/build-numeric-native.cmd'],{cwd:out,encoding:'utf8',maxBuffer:2e6});fs.writeFileSync(out+'/NATIVE-NUMERIC.log',(run.stdout||'')+(run.stderr||''));
const lines=(run.stdout||'').split(/\r?\n/),cases=lines.filter(l=>/^(PASS|FAIL) /.test(l)).map(l=>({name:l.slice(5),ok:l.startsWith('PASS')}));const result={ok:run.status===0&&cases.length===16,cases,header,exitCode:run.status};fs.writeFileSync(out+'/NATIVE-NUMERIC-RESULT.json',JSON.stringify(result,null,2));console.log('Native numeric:',cases.length,'PASS',cases.filter(c=>c.ok).length,'FAIL',cases.filter(c=>!c.ok).length);if(!result.ok){console.log((run.stdout||'')+(run.stderr||''));process.exitCode=2;}
