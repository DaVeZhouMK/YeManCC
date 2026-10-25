// Paired isolated publisher timing with default allocator. This measures no live app CPU.
import crypto from 'node:crypto';import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),task=path.resolve(process.env.YMCC_TOP_STRING_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/top-string-reuse-20261017').replaceAll('\\','/'),probe=process.argv.includes('--probe'),folder=task+(probe?'/probe':'/accepted'),out=folder+'/timing';fs.mkdirSync(out,{recursive:true});
const sha=x=>crypto.createHash('sha256').update(x).digest('hex'),parent=JSON.parse(fs.readFileSync(folder+'/RESULT.json','utf8'));
const main=fs.readFileSync('native/main.cpp','utf8').replaceAll('\r\n','\n'),cut=(a,b)=>{const i=main.indexOf(a),j=main.indexOf(b,i);assert(i>=0&&j>i,a);return main.slice(i,j)};
const current=probe?fs.readFileSync(task+'/proposed-top.inc','utf8').replaceAll('\r\n','\n'):cut('static std::string nativeMonitorTopPayloadText(','\nstatic ymcc::monitorgpu::StickySelection g_monitorGpuSelection');
assert.equal(sha(current),parent.sourcePins.after,'Publisher changed: regenerate the matching production selftest first');
assert.equal(sha(cut('struct NativeMonitorHw {','\n};')+'\n};'),parent.sourcePins.structure,'Native sample shape changed: regenerate the selftest first');
if(process.argv.includes('--verify-only')){console.log(JSON.stringify({ok:true,publisher:sha(current),structure:parent.sourcePins.structure}));process.exit(0)}
let prefix=fs.readFileSync(folder+'/top-strings.cpp','utf8');prefix=prefix.slice(0,prefix.indexOf('int main(int argc,char**argv)'));
const start=prefix.indexOf('void* operator new('),end=prefix.indexOf('static void check(',start);assert(start>0&&end>start);prefix=prefix.slice(0,start)+prefix.slice(end);
const code=prefix+String.raw`
static std::size_t sink=0;
static double measure(bool changed,bool moved,std::size_t slots){
 auto input=seed();auto source=core(slots);
 auto run=[&](){if(moved){auto owned=source;const auto value=changed?newCall(input,owned,&owned):beforeMoveCall(input,owned);sink+=value.size();}else{const auto value=changed?legacyCall(input,source):oldCall(input,source);sink+=value.size();}};
 for(unsigned i=0;i<300;++i)run();
 ULONG64 before=0,after=0;check(QueryThreadCycleTime(GetCurrentThread(),&before)!=0,"initial thread cycles");
 for(unsigned i=0;i<2500;++i)run();
 check(QueryThreadCycleTime(GetCurrentThread(),&after)!=0&&after>before,"final thread cycles");
 return double(after-before)/2500.0;
}
int main(int argc,char**argv){check(argc==2,"timing output supplied");json rows=json::array();
 for(unsigned round=0;round<9;++round)for(bool moved:{false,true})for(std::size_t slots:{0U,16U,128U}){
  double before=0,after=0;if(round%2){after=measure(true,moved,slots);before=measure(false,moved,slots);}else{before=measure(false,moved,slots);after=measure(true,moved,slots);}
  rows.push_back({{"round",round},{"moved",moved},{"coreSlots",slots},{"beforeCyclesPerCall",before},{"afterCyclesPerCall",after},{"ratio",after/before}});
 }
 json report={{"ok",true},{"rows",std::move(rows)},{"sink",sink},{"scope","Default-allocator isolated actual full publisher, thread cycle counts, 9 paired alternating rounds. Owned-call cycle includes equal preparation copy of core input on both sides. No acquisition, running YMCC/HWiNFO CPU percentage, game or hardware test."}};std::ofstream(argv[1])<<report.dump(2);
}
`;
fs.writeFileSync(out+'/timing.cpp',code);fs.writeFileSync(out+'/run.cmd',['@echo off','call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul','if errorlevel 1 exit /b %errorlevel%',`cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG "${out}/timing.cpp" /Fo"${out}/timing.obj" /Fe"${out}/timing.exe"`,'if errorlevel 1 exit /b %errorlevel%',`"${out}/timing.exe" "${out}/RESULT.json"`,'exit /b %errorlevel%',''].join('\r\n'));
const run=spawnSync('cmd.exe',['/d','/c',out+'/run.cmd'],{windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:3e6});fs.writeFileSync(out+'/COMPILE-RUN.log',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,run.stdout+run.stderr);
const result=JSON.parse(fs.readFileSync(out+'/RESULT.json','utf8')),median=xs=>xs.sort((a,b)=>a-b)[Math.floor(xs.length/2)];const summary=[];
for(const moved of [false,true])for(const slots of [0,16,128]){const rows=result.rows.filter(x=>x.moved===moved&&x.coreSlots===slots);summary.push({moved,coreSlots:slots,pairedMedianRatio:median(rows.map(x=>x.ratio)),improvedPairs:rows.filter(x=>x.ratio<1).length,pairs:rows.length});}
result.summary=summary;result.proposedOnly=probe;result.sourcePins={publisher:sha(current),structure:parent.sourcePins.structure,generatedFixtureSha256:sha(fs.readFileSync(folder+'/top-strings.cpp')),timingCppSha256:sha(code),releaseDefines:['NDEBUG']};fs.writeFileSync(out+'/RESULT.json',JSON.stringify(result,null,2));console.log(JSON.stringify({ok:true,proposedOnly:probe,summary},null,2));
