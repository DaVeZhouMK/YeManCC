"""Opt-in composition of the original complete worker/callback fixture.

Executes the current worker and OEM init/reconcile bodies verbatim. Capability
selection and external OS/capture/Host leaves remain declared memory ports.
The original frozen timer-retirement default and its results are untouched.
"""
from pathlib import Path
import datetime as dt,hashlib,json,re

def run(ret,output,source_path,case='all'):
    ret.need(case in ('all','optional-failures'),'Unknown native OEM worker case')
    output=Path(output).resolve();output.mkdir(exist_ok=False)
    source_path=Path(source_path).resolve();raw=source_path.read_bytes();text=raw.decode('utf-8-sig')
    parts,globals=ret.extract(text)
    helper_names=('inputCaptureInitializeConsumers','inputCaptureReconcileRequestedInitialization')
    actual={name:ret.function(text,name) for name in helper_names}
    template_path=ret.ROOT/'tools/fixtures/input_capture_timer_retirement/actual_worker_fixture.cpp.in'
    template=template_path.read_text(encoding='utf-8')
    marker='int main(int argc,char**argv){'
    ret.need(template.count(marker)==1,'Original worker fixture main boundary')
    template=template[:template.index(marker)]
    # Explicit ports replace the template's former no-op optional-init leaf.
    for name in helper_names:
        old=ret.function(template,name)['text']
        ret.need(template.count(old)==1,'Unique existing init/reconcile port')
        template=template.replace(old,'',1)
    anchor='@FAULT@'
    ret.need(template.count(anchor)==1,'Original worker fault placement')
    declarations=r'''
enum class PowerLifecycle {Ready,Suspending};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_inputCaptureReady{false},g_oemInputCaptureOnly{false},g_oemInputCaptureInitRequested{false},g_exitRequested{false},g_inputReady{true};
static std::atomic<unsigned> nativeFullInitCalls{0},nativeConfigLoads{0},nativeReturns{0},nativeLoopCalls{0};
static bool nativeRequestVirtual=false;
static bool nativeSensorStartFailure=false;
static bool nativeCapabilitiesAvailable=true;
static HANDLE g_inputCaptureThread=nullptr;
static std::unique_ptr<std::thread> nativeThread;
static std::atomic<bool> nativeClosedAtTimerRegistration{false};
'''
    template=template.replace('@GLOBALS@','@GLOBALS@\n'+declarations,1)
    ports=r'''
static thread_local bool g_inputCaptureOwnerDispatchesBus=false;
static json inputHostLifecycleImplementationEvidence() noexcept{return json::object();}
static bool inputCaptureOemConsumersAvailable(){return nativeCapabilitiesAvailable;}
static void inputCaptureLoadRealStickTestConfig(){++nativeConfigLoads;g_realStickTest.virtualTargetEnabled=nativeRequestVirtual;}
static void msiCrashRecoveryInitialize(){}
static void inputHostTargetReturnQueueColdStartup(){++nativeReturns;}
'''
    ports+='\n'+ret.function(text,'inputCaptureOwnerFallbackBusTick')['text']+'\n'
    for name in helper_names:
        p=actual[name];ports+='\n#line '+str(p['line'])+' "actual-main.cpp"\n'+p['text']+'\n#line 1 "fixture-generated.cpp"\n'
    template=template.replace(anchor,ports+'\n'+anchor,1)
    template=template.replace('bool virtualTargetEnabled=false,motionEnabled=false,enabled=false;','bool virtualTargetEnabled=false,motionEnabled=false,enabled=false,standaloneTestLane=false;')
    timer=ret.function(template,'timeSetEvent')['text']
    template=template.replace(timer,timer.replace('    current->flags=flags;','    nativeClosedAtTimerRegistration=g_oemInputCaptureOnly.load()&&!g_inputCaptureReady.load();\n    current->flags=flags;',1),1)
    init=ret.function(template,'inputCaptureInit')['text']
    # Three cases: normal native startup; optional sensor failure at startup;
    # explicit promotion from native to the pre-existing full initialization.
    template=template.replace(init,r'''static void inputCaptureInit(){
        ++nativeFullInitCalls;
        if(current->fault==1)throw std::runtime_error(nativeSensorStartFailure?
            "sensor STA start request did not complete":"sensor STA unavailable or quarantined");
        g_inputCaptureReady=true;
    }''',1)
    lifecycle=ret.function(template,'inputHostRunQueuedLifecycleWork')['text']
    template=template.replace(lifecycle,r'''static void inputHostRunQueuedLifecycleWork(){
        ++nativeLoopCalls;std::lock_guard<std::mutex> lock(current->mutex);current->cv.notify_all();
    }''',1)
    shutdown=ret.function(template,'inputCaptureShutdown')['text']
    template=template.replace(shutdown,shutdown[:-1]+'g_inputCaptureReady=false;g_oemInputCaptureOnly=false;g_oemInputCaptureInitRequested=false;}',1)
    template=template.replace('#include <mutex>','#include <mutex>\n#include <memory>',1)
    cpp=ret.render(template,parts,globals,parts['inputCaptureThreadProc']['text'])
    cpp+=r'''
static bool inputHostExitFenceRaised(){return false;}
static void gamebarTraceStartInitThread(){}
static bool applyInputStartupOnce(){return true;}
static bool inputCaptureAssetsPresent(){return false;}
static bool inputCaptureResetStartupStop(){g_inputCaptureStop=false;return true;}
static void inputCaptureRequestStop(){g_inputCaptureStop=true;}
static HANDLE CreateThread(LPVOID,std::size_t,DWORD(*entry)(LPVOID),LPVOID argument,DWORD,LPVOID){
    nativeThread=std::make_unique<std::thread>([=]{entry(argument);std::lock_guard<std::mutex> lock(current->mutex);current->workerDone=true;current->cv.notify_all();});
    return nativeThread.get();
}
'''
    cpp+='\n'+ret.function(text,'inputCaptureStart')['text']+'\n'

    cpp+=r'''
static json nativeScenario(unsigned kind,bool sensorStartFailure=false){
    Scenario scenario;scenario.tail=false;scenario.fault=kind==1?1:0;current=&scenario;
    nativeCapabilitiesAvailable=true;nativeClosedAtTimerRegistration=false;nativeRequestVirtual=kind==1;nativeSensorStartFailure=sensorStartFailure;nativeFullInitCalls=0;nativeConfigLoads=0;nativeReturns=0;nativeLoopCalls=0;
    g_inputCaptureReady=false;g_oemInputCaptureOnly=false;g_oemInputCaptureInitRequested=false;
    g_inputCaptureStop=false;g_exitRequested=false;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_realStickTest={};
    g_captureMmTimerRetired=true;g_captureResolutionOwed=false;g_captureRetainedEvents.fill(nullptr);g_captureEventOverflow=false;
    g_captureAdmission.state.store(ymcc::captureLifecycle::AdmissionState::Idle);
    inputCaptureStart();
    check(g_inputCaptureThread!=nullptr&&bool(nativeThread),"actual Start admits existing OEM source with both optional asset folders missing");
    check(scenario.wait([&]{return nativeLoopCalls.load()>0;}),"actual native worker reaches its existing lifecycle loop");
    check(g_oemInputCaptureOnly&&!g_inputCaptureReady,"native or failed optional init leaves OEM-only owner live");
    check(nativeClosedAtTimerRegistration,"actual worker closes new-Host admission before first timer callback can run");
    check(nativeFullInitCalls.load()==(kind==1?1u:0u),"native-only startup performs no optional sensor initialization");
    check(nativeConfigLoads==1,"same worker loads native input configuration once");
    check(nativeReturns==1,"prior native return ownership remains queued");
    if(kind==2){
        g_powerLifecycle=PowerLifecycle::Suspending;g_realStickTest.virtualTargetEnabled=true;g_oemInputCaptureInitRequested=true;
        const auto at=nativeLoopCalls.load();
        check(scenario.wait([&]{return nativeLoopCalls.load()>at+1;}),"existing worker continues while shared power is closed");
        check(nativeFullInitCalls==0&&g_oemInputCaptureInitRequested,"no promotion or request loss before shared Ready");
        g_powerLifecycle=PowerLifecycle::Ready;
        check(scenario.wait([&]{return g_inputCaptureReady.load()&&!g_oemInputCaptureOnly.load();}),"same live worker consumes explicit promotion after shared Ready");
        check(nativeFullInitCalls==1,"explicit promotion invokes original full initialization once");
    }
    g_inputCaptureStop=true;scenario.releaseAll();
    check(scenario.wait([&]{return scenario.workerDone;}),"native worker returns after its own stop");
    nativeThread->join();nativeThread.reset();g_inputCaptureThread=nullptr;if(scenario.callback.joinable())scenario.callback.join();
    check(scenario.active==0&&scenario.eventCreates==scenario.eventCloses,"native worker returns callback and event owners");
    check(scenario.periodBalance==0&&scenario.comBalance==0,"native worker returns timer resolution and COM leaves");
    check(scenario.shutdowns==1&&!g_oemInputCaptureOnly&&!g_inputCaptureReady,"native shutdown occurs once and retires mode state");
    check(!scenario.gateTimedOut&&scenario.closedEventSignals==0&&scenario.lateBus==0,"no late callback uses retired native state");
    return {{"kind",kind},{"sensorFailure",kind==1?(sensorStartFailure?"start":"ensure"):"none"},
        {"coldReturnQueueCalls",nativeReturns.load()},{"fullInitCalls",nativeFullInitCalls.load()},{"loopObserved",nativeLoopCalls.load()>0},
        {"threadsJoined",true},{"activeCallbacks",scenario.active.load()},{"remainingEvents",scenario.eventCreates.load()-scenario.eventCloses.load()},
        {"comBalance",scenario.comBalance.load()},{"timerBalance",scenario.periodBalance.load()}};
}
int main(int argc,char**argv){
    const std::string selection=argc==2?argv[1]:"all";
    if(selection!="all"&&selection!="optional-failures")return 2;
    json cases=json::array();
    if(selection=="optional-failures"){
        cases.push_back(nativeScenario(1,false));cases.push_back(nativeScenario(1,true));
    }else{
    for(unsigned kind:{0u,1u,2u})cases.push_back(nativeScenario(kind));
    Scenario unavailable;current=&unavailable;nativeCapabilitiesAvailable=false;
    g_captureAdmission.state.store(ymcc::captureLifecycle::AdmissionState::Idle);g_inputCaptureThread=nullptr;g_exitRequested=false;inputCaptureStart();
    check(!g_inputCaptureThread&&!nativeThread,"actual Start with missing assets and no implemented OEM source retains original rejection");
    nativeCapabilitiesAvailable=true;g_captureAdmission.state.store(ymcc::captureLifecycle::AdmissionState::Idle);g_exitRequested=true;inputCaptureStart();
    check(!g_inputCaptureThread&&!nativeThread,"actual Start cannot revive OEM source after exit");
    }
    std::cout<<json({{"pass",failures==0},{"checks",checks},{"failures",failures},{"selection",selection},{"cases",cases},
        {"wholeStartWorkerAndCallbackVerbatim",true},{"initAndReconcileVerbatim",true},{"capabilitySelection","memory port; real binding tested separately"},
        {"deviceIo",false},{"fullShutdownBodyExecuted",false}}).dump()<<'\n';return failures?1:0;}
'''
    cpp_path=output/'native-oem-worker.cpp';cpp_path.write_text(cpp,encoding='utf-8')
    inputs=['native/input_timer_evidence.h','native/input_capture_completion.h','deps/json/json.hpp',
        'tools/input_capture_worker_retirement_selftest.py','tools/input_timer_evidence_adapter_selftest.py',
        'tools/fixtures/input_capture_timer_retirement/native_oem_worker.py','tools/fixtures/input_capture_timer_retirement/actual_worker_fixture.cpp.in']
    captured={n:(ret.ROOT/n).read_bytes() for n in inputs}
    for name,data in captured.items():
        target=output/'source'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    compiler,env=ret.helper.compiler_environment(output)
    exe=output/'native-oem-worker.exe';deps=output/'dependencies.json'
    command=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/sourceDependencies',deps,cpp_path,
        '/I'+str(output/'source/native'),'/I'+str(output/'source/deps/json'),'/Fo'+str(output/'native-oem-worker.obj'),'/Fe'+str(exe)]
    built=ret.helper.execute(command,output,env,output/'BUILD.log',120)
    ret.need(built.returncode==0,'Native OEM full-worker compile failed: '+str(output/'BUILD.log'))
    result=ret.helper.execute([exe,case],output,env,output/'RUN.log',30)
    value=json.loads(result.stdout.strip().splitlines()[-1])
    ret.need(result.returncode==0 and value.get('pass') is True,'Native OEM full-worker assertions failed')
    ret.need(source_path.read_bytes()==raw and all((ret.ROOT/n).read_bytes()==b for n,b in captured.items()),'Worker sources changed')
    dependency=json.loads(deps.read_text(encoding='utf-8-sig'))['Data']
    actualInputs=[{'path':p,'sha256':ret.sha(Path(p).read_bytes())} for p in [dependency['Source'],*dependency['Includes']]]
    record={'atUtc':dt.datetime.now(dt.timezone.utc).isoformat(),'status':'PASS','result':value,'source':str(source_path),
        'sourceSha256':ret.sha(raw),'actualWorkerSha256':parts['inputCaptureThreadProc']['sha256'],
        'actualHelpers':{n:{k:v for k,v in actual[n].items() if k!='text'} for n in actual},'actualCompileInputs':actualInputs,
        'scope':'opt-in original Start/worker/callback fixture plus actual OEM initialization; capability/OS/full shutdown are explicit memory ports',
        'oldFreezeOrDefaultGroupsExecuted':False}
    (output/'RESULT.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':'PASS','checks':value['checks'],'cases':len(value['cases']),'output':str(output)},ensure_ascii=False))
    return 0
