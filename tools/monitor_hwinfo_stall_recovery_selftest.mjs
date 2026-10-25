import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_HWINFO_STALL_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/local-hwinfo-lifecycle-20261016').replaceAll('\\','/');fs.mkdirSync(out,{recursive:true});
const src=fs.readFileSync('native/main.cpp','utf8');const start=src.indexOf('enum class HwInfoRecoveryDecision'),end=src.indexOf('\nstatic void nativeMonitorDeleteOutputs()',start);assert(start>0&&end>start);const policy=src.slice(start,end);
const cpp=String.raw`
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "__ROOT__/deps/json/json.hpp"
#include "__ROOT__/native/monitor_hwinfo_recovery.h"
using json=nlohmann::json;
__POLICY__
int main(int argc,char**argv){
 using Guard=ymcc::monitorrecovery::StalledProducer;using D=HwInfoRecoveryDecision;
 json names=json::array();auto check=[&](const char*name,bool ok){if(!ok)throw std::runtime_error(name);names.push_back(name);};
 for(bool wait:{false,true})for(bool data:{false,true})for(bool armed:{false,true}){
  const auto old=!armed?D::None:wait?D::Defer:data?D::CancelDataPresent:D::Recover;
  check("existing short fault and wake truth table remains identical",monitorHWiNFORecoveryDecision(wait,data,armed)==old);
 }
 Guard g;
 check("stale grace begins at tick zero without sentinel ambiguity",!g.observe(true,true,false,false,0,6000));
 check("one missing 3s sample does not recover",!g.observe(true,true,false,false,3000,6000));
 check("extra grace cannot recover before 15 seconds",!g.observe(true,true,false,false,14999,6000));
 check("sustained readable stale producer crosses exact 15 second grace",g.observe(true,true,false,false,15000,6000));
 check("sustained stall may recover through existing arm",monitorHWiNFORecoveryDecision(false,true,true,true)==D::Recover);
 check("wake wait always wins over persistent evidence",monitorHWiNFORecoveryDecision(true,true,true,true)==D::Defer);
 check("one attempt remains one attempt until healthy rearm",monitorHWiNFORecoveryDecision(false,true,false,true)==D::None);
 check("genuine producer recovery resets stale episode",!g.observe(true,true,true,false,16000,6000)&&!g.tracking);
 check("missing mapping uses original immediate recovery path",!g.observe(false,false,false,false,17000,6000)&&monitorHWiNFORecoveryDecision(false,false,true)==D::Recover);
 g.observe(true,true,false,false,20000,6000);
 check("resume wait resets accumulated stale episode",!g.observe(true,true,false,true,50000,6000)&&!g.tracking);
 check("post resume readable data gets its complete grace",!g.observe(true,true,false,false,53000,6000)&&!g.observe(true,true,false,false,67999,6000));
 check("clock rollback starts a new conservative interval",!g.observe(true,true,false,false,1,6000));
 g.reset();g.observe(true,true,false,false,100,63000);
 check("large supported producer period retains larger freshness budget",!g.observe(true,true,false,false,63099,63000)&&g.observe(true,true,false,false,63100,63000));
 bool healthyStable=true;for(unsigned i=0;i<1000;i++)healthyStable&=!g.observe(true,true,true,false,100000+i*1000,6000);
 check("1000 healthy producer observations never restart",healthyStable&&!g.tracking);
 std::ifstream in(argv[1]);json archives;in>>archives;unsigned total=0;json replay=json::array();
 for(const auto&archive:archives){Guard history;unsigned changes=0,rows=0;for(const auto&r:archive["rows"]){const bool data=r.value("readable",false),healthy=r.value("healthy",false);const auto period=r.value("period",3000ULL);const auto budget=(std::max)(5000ULL,(std::min)(60000ULL,(std::max)(500ULL,period))+3000ULL);const bool stall=history.observe(data,data,healthy,false,r.value("at",0ULL),budget);if(monitorHWiNFORecoveryDecision(false,data,true,stall)!=monitorHWiNFORecoveryDecision(false,data,true))++changes;++rows;}total+=rows;check("archived machine and sleep observations introduce zero additional recovery",changes==0);replay.push_back({{"archive",archive["name"]},{"rows",rows},{"additionalRecoveries",changes}});}
 json result={{"ok",true},{"checks",names.size()},{"cases",names},{"archivedObservations",total},{"archiveReplay",replay},{"hardwareOrCpuPercentageMeasured",false}};std::ofstream(argv[2])<<result.dump(2);std::cout<<result.dump(2);
}
`.replaceAll('__ROOT__',root).replace('__POLICY__',policy);
fs.writeFileSync(out+'/stall-recovery.cpp',cpp);const bat=out+'/compile-stall-recovery.bat';fs.writeFileSync(bat,`@echo off\r\nset "PATH=C:\\Windows\\System32;C:\\Windows;%PATH%"\r\ncall "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" 10.0.26100.0 >nul\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\ncl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 "${out}/stall-recovery.cpp" /Fo"${out}/stall-recovery.obj" /Fe"${out}/stall-recovery.exe"\r\nexit /b %ERRORLEVEL%\r\n`);
const c=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/s','/c','""'+bat+'""'],{windowsVerbatimArguments:true,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4e6});fs.writeFileSync(out+'/STALL-COMPILE.log',(c.stdout||'')+(c.stderr||''));assert.equal(c.status,0,c.stdout+c.stderr);
if(!fs.existsSync(out+'/ARCHIVE-RECOVERY-INPUT.json'))fs.writeFileSync(out+'/ARCHIVE-RECOVERY-INPUT.json','[]');
const run=spawnSync(out+'/stall-recovery.exe',[out+'/ARCHIVE-RECOVERY-INPUT.json',out+'/STALL-LOGIC-RESULT.json'],{encoding:'utf8',windowsHide:true,timeout:15000});assert.equal(run.status,0,run.stdout+run.stderr);
const r=JSON.parse(fs.readFileSync(out+'/STALL-LOGIC-RESULT.json','utf8'));r.sourceSha256={main:crypto.createHash('sha256').update(fs.readFileSync('native/main.cpp')).digest('hex'),guard:crypto.createHash('sha256').update(fs.readFileSync('native/monitor_hwinfo_recovery.h')).digest('hex')};r.actualDecisionFunctionCompiled=true;fs.writeFileSync(out+'/STALL-LOGIC-RESULT.json',JSON.stringify(r,null,2));console.log(JSON.stringify({ok:r.ok,checks:r.checks,archivedObservations:r.archivedObservations,archiveReplay:r.archiveReplay},null,2));
