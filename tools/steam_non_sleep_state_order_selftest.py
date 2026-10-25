"""Offline event-order experiment using production record/UI decoders and existing memory fixtures.
Does not load Valve code, attach to a process, call private APIs, or change a live application.
The event writer is an explicit model of a frozen store, not a real sender/caller execution.
"""
from pathlib import Path
import argparse, hashlib, json, subprocess
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--fixtures',type=Path,required=True);args=p.parse_args()
root=Path(__file__).resolve().parents[1];out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((args.fixtures/'FROZEN-FIXTURES.json').read_text(encoding='utf-8-sig'))
fixture_hashes={}
for row in manifest['old_lookup_fixtures']:
    source=args.fixtures/row['path'];actual=hashlib.sha256(source.read_bytes()).hexdigest()
    assert actual==row['sha256'],f'Frozen fixture changed: {source}'
    fixture_hashes[row['path']]=actual
ui=(root/'tools/steam_native_app_state_diagnostic_selftest.cpp').read_text(encoding='utf-8-sig')
record=(root/'tools/steam_native_record_diagnostic_selftest.cpp').read_text(encoding='utf-8-sig')
ui=ui[ui.index('namespace state ='):ui.index('int main(')]
record=record[record.index('using U64 ='):record.index('void unavailable(')]
cpp=r"""
#include "steam_native_app_state_diagnostic.h"
#include "steam_native_record_diagnostic.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
namespace UIHarness {
__UI__
}
namespace RecordHarness {
using Json=ymcc::steamrecord::Json;using Query=ymcc::steamrecord::Query;namespace sr=ymcc::steamrecord;
__RECORD__
}
using Json=nlohmann::json;
int main(int argc,char** argv){
 try{
 if(argc!=2)throw std::runtime_error("frozen fixtures path required");
 const auto fixtures=std::filesystem::path(argv[1]);
 UIHarness::lookupBytes=UIHarness::load(fixtures/"17a0f0-17a1a6.bin",182);
 UIHarness::bitmapBytes=UIHarness::load(fixtures/"bde90-bdf00.bin",112);
 Json results=Json::array();
 for(int order=0;order<3;++order){
  UIHarness::Fixture ui;RecordHarness::Fixture record;
  record.q.gameId64=(static_cast<std::uint64_t>(UIHarness::kKey)<<32)|0x02000000ULL;
  record.records({{record.q.gameId64}});
  const auto originalRecordMemory=record.m.memory;
  ui.put32(UIHarness::kApp+0x20,0x2004);
  auto sample=[&](const char* stage){
   ui.reads.clear();ui.occurrences.clear();ui.attemptedBytes=0;ui.stableCalls=0;
   record.m.reads.clear();record.m.occurrences.clear();record.stableCalls=0;
   const auto ur=ui.run();const auto rr=record.run();record.check(rr);
   UIHarness::expect(rr.at("recordState")=="present-original-primary"&&rr.at("originalPrimaryMatched")==true,"original record changed");
   UIHarness::expect(record.m.memory==originalRecordMemory,"event modified record fixture");
   return Json{{"stage",stage},{"ui",ur},{"record",rr},{"recordBytesUnchanged",true}};
  };
  Json trace=Json::array();trace.push_back(sample("before"));
  if(order<2){
   const std::uint32_t first=order==0?0x2004U:4U,second=order==0?4U:0x2004U;
   ui.put32(UIHarness::kApp+0x20,first);trace.push_back(sample("first-modeled-update"));
   ui.put32(UIHarness::kApp+0x20,second);const auto last=sample("second-modeled-update");
   UIHarness::observed(last.at("ui"),second);trace.push_back(last);
   results.push_back({{"case",order==0?"running-then-clear":"clear-then-running"},{"trace",trace}});
  }else{
   ui.beforeRead=[&](UIHarness::Address address,unsigned occurrence){if(address==UIHarness::kApp+0x20&&occurrence==2)ui.put32(address,4);};
   const auto race=sample("clear-between-flags-reads");UIHarness::rejected(race.at("ui"),"app-flags-changed");trace.push_back(race);
   results.push_back({{"case","torn-ui-observation-rejected-record-still-present"},{"trace",trace}});
  }
 }
 std::cout<<Json{{"schema","steam-non-sleep-state-order.v1"},{"syntheticEvidenceOnly",true},{"eventWriter","modeled-memory-store-not-Valve-code"},
 {"causalClaim",false},{"realSenderExecuted",false},{"realUiProjectionVerified",false},{"originalInstanceRecoveryVerified",false},
 {"cases",results},{"passed",results.size()},{"hardwareOperations",0},{"realProcessReads",0},{"privateInterfaceCalls",0},{"writesToSteam",0}}.dump(2)<<"\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
""".replace('__UI__',ui).replace('__RECORD__',record)
source=out/'state-order.cpp';source.write_text(cpp,encoding='utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
sdk=Path(r'C:\Program Files (x86)\Windows Kits\10');exe=out/'state-order.exe'
cmd=out/'run-state-order.cmd'
cmd.write_text(f'@echo off\nsetlocal\ncall "{vs}" 10.0.26100.0 >nul\nif errorlevel 1 exit /b 1\nset "INCLUDE={sdk}\\Include\\10.0.26100.0\\um;{sdk}\\Include\\10.0.26100.0\\shared;{sdk}\\Include\\10.0.26100.0\\ucrt;%INCLUDE%"\nset "LIB={sdk}\\Lib\\10.0.26100.0\\um\\x64;{sdk}\\Lib\\10.0.26100.0\\ucrt\\x64;%LIB%"\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W3 "{source}" /I"{root / "native"}" /I"{root / "deps/json"}" /Fo"{out / "state-order.obj"}" /Fe"{exe}"\nexit /b %errorlevel%\n',encoding='ascii')
build=subprocess.run(['cmd.exe','/d','/c',str(cmd)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'build.log').write_bytes(build.stdout)
if build.returncode:print(build.stdout.decode('utf-8',errors='replace'));raise SystemExit(build.returncode)
run=subprocess.run([str(exe),str(args.fixtures.resolve())],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
(out/'stderr.log').write_bytes(run.stderr)
if run.returncode:print(run.stderr.decode('utf-8',errors='replace'));raise SystemExit(run.returncode)
result=json.loads(run.stdout);result['frozenFixtureHashes']=fixture_hashes
result['sourceHashes']={str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in [root/'native/steam_native_app_state_diagnostic.h',root/'native/steam_native_record_diagnostic.h',root/'tools/steam_native_app_state_diagnostic_selftest.cpp',root/'tools/steam_native_record_diagnostic_selftest.cpp',Path(__file__).resolve()]}
(out/'RESULT.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'passed':result['passed'],'cases':[v['case'] for v in result['cases']],'realProcessReads':0,'writesToSteam':0,'causalClaim':False},indent=2))
