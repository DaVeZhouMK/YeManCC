# Actual native leaf observation wrapper + fake call core. Never runs WMI/HC.
import argparse,datetime,hashlib,json,pathlib,subprocess
from log_frequency_selftest import ROOT,extract
from tdp_parent_static_selftest import PRELUDE

def main():
 p=argparse.ArgumentParser();p.add_argument('--output-dir',type=pathlib.Path,required=True);a=p.parse_args();run=a.output_dir/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));run.mkdir(parents=True)
 source=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig');wrapper=extract(source,'    Microsoft::WRL::ComPtr<IWbemClassObject> call(const wchar_t* name, unsigned char block, unsigned char value) {')
 baseline=a.output_dir.parent/'native-observation-baseline';core=extract(source,'    Microsoft::WRL::ComPtr<IWbemClassObject> callObservedCore(const wchar_t* name, unsigned char block, unsigned char value) {');old=(baseline/'call-before.cpp.txt').read_bytes().decode('utf-8').replace('\r\r\n','\n').replace('\r\n','\n');assert core.replace(' callObservedCore(',' call(',1)==old
 setter=extract(source,'static json msiG3eTdpSet(const json& a) {');oldSetter=(baseline/'setter-before.cpp.txt').read_bytes().decode('utf-8').replace('\r\r\n','\n').replace('\r\n','\n');assert setter.replace('g_tdpNativeObservation ? *g_tdpNativeObservation : tdpParentContext(a, "msi-g3e-wmi")','tdpParentContext(a, "msi-g3e-wmi")')==oldSetter
 signatures=['static std::string W2U(const wchar_t* w, int len = -1) {','static std::string W2U(const std::wstring& w) {','static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {','static std::string tdpObservationRunId() {','static void appendTdpObservation(const char* event, json detail, bool raw = false) noexcept {']
 scope='static thread_local const json* g_tdpNativeObservation = nullptr;\n'+extract(source,'struct TdpNativeObservationScope {')+';'
 fake=r'''
struct IWbemClassObject{};namespace Microsoft::WRL{template<class T>struct ComPtr{T* pointer=nullptr;};}
static std::vector<std::array<int,3>> calls;static int failAt=-1;
struct MsiG3eTdpWmi {
 Microsoft::WRL::ComPtr<IWbemClassObject> callObservedCore(const wchar_t* name,unsigned char block,unsigned char value){
  calls.push_back({wcscmp(name,L"Set_Data")==0?1:0,block,value});
  if(static_cast<int>(calls.size())==failAt)throw std::runtime_error("fixture-existing-WMI-error");
  static IWbemClassObject result;return {&result};
 }
'''+wrapper+'\n};\n'
 loopStart=setter.index('        for (const auto step : {std::pair<int, int>{80, 8}, {81, pl2}, {80, pl1}, {210, 198}}) {');loop=setter[loopStart:setter.index('\n        }',loopStart)+len('\n        }')]
 test=r'''
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;g_testRoot=argv[1];std::vector<json> checks;
 auto check=[&](const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});};
 auto readRows=[](){for(auto& stream:g_tdpObservationStreams)stream.flush();std::ifstream in(fspath::path(yemancc_data_dir())/L"tdp.log");std::vector<json> rows;for(std::string line;std::getline(in,line);)if(!line.empty())rows.push_back(json::parse(line));return rows;};
 json context={{"operationId","fixture-native-leaf"},{"source","ui"}};TdpNativeObservationScope scope(context);
 const int pl1=30,pl2=32;bool triggered=false;json result={{"steps",json::array()}};auto fresh=[](){};MsiG3eTdpWmi wmi;
'''+loop+r'''
 wmi.call(L"Get_Data",80,0);wmi.call(L"Get_Data",81,0);wmi.call(L"Get_Data",210,0);
 check("actual-setter-loop-unchanged-WMI-sequence-fake-core",calls==std::vector<std::array<int,3>>{{1,80,8},{1,81,32},{1,80,30},{1,210,198},{0,80,0},{0,81,0},{0,210,0}});
 auto rows=readRows();bool linked=rows.size()==14;for(const auto& r:rows)linked=linked&&r.value("operationId","")=="fixture-native-leaf"&&!r.value("physicalPowerVerified",true);check("every-leaf-start-result-linked-never-physical-power-proof",linked);
 calls.clear();failAt=2;bool thrown=false;try{
'''+loop+r'''
 }catch(const std::exception& e){thrown=std::string(e.what())=="fixture-existing-WMI-error";}
 rows=readRows();const auto& last=rows.back();check("failed-step-retains-block-target-original-error-no-replay",thrown&&calls.size()==2&&last["blockId"]==81&&last["targetNumericValue"]==32&&last["error"]=="fixture-existing-WMI-error"&&last["hardwareCommandReturned"]=="not-observed"&&last["outcomeUnknown"]==true);
 const auto goodRoot=g_testRoot;const auto denied=fspath::path(g_testRoot)/L"denied";fspath::create_directories(denied);std::ofstream(denied/L"data")<<"blocks-log-directory";g_testRoot=denied.wstring();const auto priorFailures=g_tdpObservationFailures.load();failAt=-1;auto returned=wmi.call(L"Set_Data",80,30);check("leaf-logging-failure-counted-without-changing-call-result",returned.pointer&&g_tdpObservationFailures.load()>priorFailures);g_testRoot=goodRoot;
 check("original-COM-call-body-text-identical-except-name-and-line-endings",true);check("original-controller-body-text-identical-except-diagnostic-context-and-line-endings",true);check("zero-real-hardware-leases",hardwareLeaseCalls==0);
 int failures=0;for(const auto& c:checks)if(!c["pass"].get<bool>())++failures;
 std::cout<<json({{"checks",checks},{"failures",failures},{"realHardwareExecuted",false},{"scope","ACTUAL_LEAF_WRAPPER_AND_SETTER_LOOP_FAKE_CORE_NO_WMI"}}).dump(2);return failures?1:0;
}
'''
 code=PRELUDE+'\n'+'\n'.join(extract(source,s) for s in signatures)+'\n'+scope+'\n'+fake+'\n'+test
 cpp=run/'fixture.cpp';cpp.write_text(code,encoding='utf-8');exe=run/'fixture.exe';vc=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat';batch=run/'build.cmd';batch.write_text(f'@echo off\ncall "{vc}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "{cpp}" /I"{ROOT/"deps/json"}" /I"{ROOT/"native"}" /Fo"{run/"fixture.obj"}" /Fe"{exe}" /link advapi32.lib user32.lib\n')
 compile=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace');(run/'compile.log').write_text(compile.stdout+compile.stderr,encoding='utf-8')
 if compile.returncode:print(compile.stdout+compile.stderr);return compile.returncode
 result=subprocess.run([str(exe),str(run/'files')],cwd=run,capture_output=True,encoding='utf-8',errors='replace');(run/'stderr.txt').write_text(result.stderr,encoding='utf-8');print(result.stdout)
 if not result.stdout:return result.returncode or 1
 report=json.loads(result.stdout);report.update(sourceSha256=hashlib.sha256(source.encode()).hexdigest(),runDirectory=str(run));(a.output_dir/'report.json').write_text(json.dumps(report,indent=2)+'\n');return result.returncode
if __name__=='__main__':raise SystemExit(main())
