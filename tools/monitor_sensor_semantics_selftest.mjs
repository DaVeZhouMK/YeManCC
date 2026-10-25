// Regression fixtures exercise extracted production parsing/publication, not a substitute parser.
import fs from 'node:fs';import path from 'node:path';import cp from 'node:child_process';
const root=process.cwd(),out=path.resolve(process.env.YMCC_SENSOR_AUDIT_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/data-audit-20261013/native');fs.mkdirSync(out,{recursive:true});
cp.execFileSync(process.execPath,['tools/monitor_memory_publication_selftest.mjs'],{cwd:root,env:{...process.env,YMCC_MONITOR_MEMORY_OUT:out},windowsHide:true});
const generated=fs.readFileSync(path.join(out,'memory-publication.cpp'),'utf8');
const tests=String.raw`
int main(int argc,char** argv){assert(argc==2);json cases=json::array();
 auto check=[&](const char* name,bool ok){cases.push_back({{"name",name},{"ok",ok}});};
 auto read=[&](const std::vector<Row>& rows){auto b=buffer(rows);NativeMonitorHw hw;bool unstable=false;assert(monitorParseHWiNFOView(b.data(),b.size(),hw,unstable));assert(!unstable);return hw;};
 auto h=read({{"Framerate Presented (avg)","FPS",0},{"Framerate Presented (1% low)","FPS",0}});
 check("zero FPS never revives historical average",h.fps==0&&h.fps1==0);
 h=read({{"Framerate Presented (avg)","FPS",std::numeric_limits<double>::quiet_NaN()}});
 check("invalid FPS cannot promote historical average",h.fps<=0&&!h.fpsSensor);
 h=read({{"Framerate Presented (avg)","FPS",117}});
 check("missing 1 percent low is unknown",h.fps==117&&h.fps1<0);
 h=read({{"Framerate Presented (avg)","W",120}});
 check("FPS wrong units rejected",h.fps<=0&&!h.fpsSensor);
 auto p=parse({{"CPU Package Power","W",25},{"CPU Package Power Limit","W",65},{"CPU Package Power Maximum","W",120}});
 check("CPU real power cannot be replaced by limits",p["tdpW"]==25);
 p=parse({{"CPU Package Power Limit","W",65}});
 check("limit-only packet has no measured CPU power",p.value("cpuPowerValid",true)==false);
 p=parse({{"Physical Memory Used","MB",8192}});
 check("missing CPU sensors publish invalid proof",!p.value("cpuPowerValid",true)&&!p.value("cpuClockValid",true)&&!p.value("cpuTempValid",true));
 p=parse({{"CPU Package Power","W",0},{"Core 0 Clock","MHz",0},{"CPU Package","C",0}});
 check("real CPU zeros remain distinguishable from absence",p.value("cpuPowerValid",false)&&p.value("cpuClockValid",false)&&p.value("cpuTempValid",false));
 p=parse({{"CPU Package","W",40},{"Core 0 Clock","MHz",999999},{"CPU Package Power","W",1e30}});
 check("invalid CPU units and extreme values rejected",!p.value("cpuPowerValid",true)&&!p.value("cpuClockValid",true)&&!p.value("cpuTempValid",true));
 p=parse({{"Physical Memory Total","MB",32768},{"Physical Memory Available","MB",24576}});
 check("RAM total plus available derives used and load",p["physicalMemoryUsedMb"]==8192&&p["physicalMemoryLoadPct"]==25);
 p=parse({{"GPU Memory Total","MB",16384,2},{"GPU Memory Available","MB",4096,2}});
 check("VRAM total plus available derives used and load",p["gpuMemoryUsedMb"]==12288&&p["gpuMemoryLoadPct"]==75);
 p=parse({{"Physical Memory Total","MB",32768},{"Physical Memory Available","MB",40000}});
 check("invalid available never creates negative used",p["physicalMemoryUsedMb"].is_null()&&p["physicalMemoryLoadPct"].is_null());
 const bool ok=std::all_of(cases.begin(),cases.end(),[](const auto& c){return c.at("ok")==true;});
 std::ofstream(argv[1])<<json{{"ok",ok},{"cases",cases}}.dump(2);std::cout<<cases.dump(2)<<"\n";return ok?0:1;
}
`;
fs.writeFileSync(path.join(out,'sensor-semantics.cpp'),generated.slice(0,generated.indexOf('int main('))+tests);
const file=path.join(out,'build-sensor-semantics.cmd');fs.writeFileSync(file,`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 "${out}/sensor-semantics.cpp" /Fo"${out}/sensor-semantics.obj" /Fe"${out}/sensor-semantics.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/sensor-semantics.exe" "${out}/SENSOR-SEMANTICS.json"\r\nexit /b %errorlevel%\r\n`.replaceAll('\\r\\n','\r\n'));
const result=cp.spawnSync('cmd.exe',['/d','/c',file],{cwd:root,windowsHide:true,encoding:'utf8'});console.log(result.stdout);if(result.stderr)console.error(result.stderr);process.exitCode=result.status??1;

