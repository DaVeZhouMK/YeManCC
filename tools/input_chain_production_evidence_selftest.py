"""Offline exact production InputChain counter/capture regression. No devices or host."""
from pathlib import Path
import argparse, hashlib, json, subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--validation-root', required=True)
a=parser.parse_args()
root=Path(__file__).resolve().parents[1]
out=Path(a.validation_root).resolve()
out.mkdir(parents=True,exist_ok=True)
data=(root/'native/main.cpp').read_bytes()
text=data.decode('utf-8-sig')
start=text.index('struct InputChainCounters {')
end=text.index('// GP-926R17 §4：IntentHost',start)
fragment=text[start:end]
init_start=text.index('            ymcc::inputChainAck::View chainAck;',text.index('static json inputHostSubmitPad('))
init_end=text.index('            inputHostNotePublicationStage(',init_start)
init=text[init_start:init_end]
fill_start=text.index('            chainAck.nativeAccepted = accepted;',init_end)
fill_end=text.index('            diagSubmitAttempted = true;',fill_start)
fill=text[fill_start:fill_end]
# Capture is read-only scalar/reference metadata. The existing single counter
# call stays in the already-owned submit mutex and after the native result.
submit_start=text.index('static json inputHostSubmitPad(')
call=text.index('            inputChainCountFrame(true, accepted, firstFrame, semanticNeutral, lastRejectDetail,',fill_end)
assert text.count('inputChainCountFrame(true, accepted, firstFrame, semanticNeutral, lastRejectDetail,')==1
assert 'frameSequence, chainAck);' in text[call:call+220]
assert 'g_inputHostMx' in text[submit_start:init_start]
assert all(x not in init for x in ('json(', 'project(', 'appendNative', 'std::lock_guard', '.store('))
assert fragment.count('ymcc::inputChainAck::project(ackIdentity)')==3
prelude=r'''
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include "input_chain_ack_evidence.h"
using json=nlohmann::json;using ull=unsigned long long;
static ull gen=1;static std::atomic<ull> hostEpoch{1};
static ull currentPowerGeneration(){return gen;}
static ull inputHostEpochToken(){return hostEpoch.load(std::memory_order_acquire);}
static bool g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostActive=true;
static void* g_inputHostProcess=reinterpret_cast<void*>(1);
static ull g_xinputConnectedMask=0,g_xinputPhysicalSourceMask=0,g_inputCaptureBusTickGlobal=0;
static bool g_xinputPhysicalSourceLocked=false;
static std::atomic<ull> g_physicalReleaseWindowStage{0};
static bool physicalReleaseWindowActive(){return false;}
static std::vector<json> emitted;
static void appendNativeLifecycleLog(const char* event,json row){row["event"]=event;emitted.push_back(row);}
static ull g_inputHostBoundPowerGeneration=1,g_inputHostEpoch=5,g_inputHostSequence=6;
static std::string g_inputHostRunId="run-before-request",g_inputHostHostInstanceId="host-before-request";
static ull inputHostProtocolPowerGenerationLocked(){return g_inputHostBoundPowerGeneration?g_inputHostBoundPowerGeneration:gen;}
struct InputHostFastReceipt {bool used=false;std::string hostInstanceId;};
static ymcc::inputChainAck::View actualCapture(bool frameTextReady,const std::string& frameRequestId,bool accepted,const InputHostFastReceipt& fastReceipt,const json& receipt){
'''
bridge=prelude+init+fill+'    return chainAck;\n}\n'+fragment
main=r'''
static unsigned count(const char* event){unsigned n=0;for(auto&e:emitted)n+=e["event"]==event;return n;}
static json latest(const char* event){for(auto i=emitted.rbegin();i!=emitted.rend();++i)if((*i)["event"]==event)return *i;return json::object();}
int main(){json tests=json::array();auto check=[&](const char* name,bool pass){tests.push_back({{"name",name},{"pass",pass}});};
 ull wire=0;for(int i=0;i<50;i++)inputChainCountFrame(true,true,i==0,true,"",++wire);
 check("prior-generation-50-accepted",count("wake-input-chain-sustained")==1);emitted.clear();gen=2;
 inputChainCountFrame(true,true,false,true,"",++wire);check("new-generation-one-not-sustained",count("wake-input-chain-sustained")==0);
 for(int i=0;i<48;i++)inputChainCountFrame(true,true,false,true,"",++wire);check("new-generation-49-not-sustained",count("wake-input-chain-sustained")==0);
 inputChainCountFrame(true,true,false,false,"",++wire);auto s=latest("wake-input-chain-sustained");
 check("new-generation-50-exact-threshold",count("wake-input-chain-sustained")==1&&s.value("acceptedInCurrentGeneration",0ull)==50);
 check("neutral-nonneutral-and-boot-separated",s.value("nonNeutralAcceptedInCurrentGeneration",999ull)==1&&s.value("acceptedSinceBoot",0ull)==100&&s.value("nonNeutralAcceptedSinceBoot",999ull)==1);
 check("host-only-unverified-game",s.value("evidenceScope",std::string())=="host-only"&&s.value("gameInputUnverified",false)&&s.value("hostEpochScope",std::string())=="sampled-at-count-not-frame-tuple-proof");
 inputChainCountFrame(true,true,false,true,"",++wire);check("sustained-once-per-count-scope",count("wake-input-chain-sustained")==1);
 emitted.clear();hostEpoch.store(2);inputChainCountFrame(true,true,false,false,"",++wire);
 check("host-epoch-first-restarts-evidence",count("wake-input-chain-first-frame")==1&&latest("wake-input-chain-first-frame").value("sampledHostEpoch",0ull)==2);
 check("host-epoch-one-no-count-borrow",count("wake-input-chain-sustained")==0);
 for(int i=0;i<48;i++)inputChainCountFrame(true,true,false,true,"",++wire);check("host-epoch-49-not-sustained",count("wake-input-chain-sustained")==0);
 inputChainCountFrame(true,false,false,false,"reject",++wire);inputChainCountFrame(true,false,false,false,"reject",++wire);
 check("reject-does-not-count-and-transition-bounded",count("wake-input-chain-sustained")==0&&count("input-host-frame-reject")==1&&g_inputChain.frameRejected.load()==2);
 inputChainCountFrame(true,true,false,true,"",++wire);s=latest("wake-input-chain-sustained");check("host-epoch-50-without-reject-borrow",count("wake-input-chain-sustained")==1&&s.value("acceptedInCurrentGeneration",0ull)==50);
 auto calls=g_inputChain.frameCalls.load(),acceptedCount=g_inputChain.frameAccepted.load();auto rowCount=emitted.size();inputChainCountFrame(false,true,true,false,"",++wire);
 check("not-attempted-no-counters-or-log",g_inputChain.frameCalls.load()==calls&&g_inputChain.frameAccepted.load()==acceptedCount&&emitted.size()==rowCount);
 emitted.clear();gen=3;hostEpoch.store(3);inputChainCountFrame(true,false,false,false,"different-reject",++wire);inputChainCountFrame(true,true,false,true,"",++wire);
 check("rejected-first-neutral-accepted-generation",latest("wake-input-chain-first-frame").value("acceptedInCurrentGeneration",0ull)==1&&count("wake-input-chain-sustained")==0);
 std::string request="request-before-send";InputHostFastReceipt fast{true,g_inputHostHostInstanceId};json receipt=json::object();
 auto ack=actualCapture(true,request,true,fast,receipt);
 check("production-capture-protocol-binding-distinct-from-current",ack.protocolPowerGeneration==1&&ack.hostEpoch==5&&ack.requestedSequence==7&&gen==3);
 emitted.clear();gen=4;hostEpoch.store(9);inputChainCountFrame(true,true,false,false,"",7,ack);auto first=latest("wake-input-chain-first-frame");
 check("schema3-emits-exact-fast-ACK-in-current-count-scope",first["inputChainEvidenceSchema"]==3&&first["countPowerGeneration"]==4&&first["sampledHostEpoch"]==9&&first["ackIdentity"]["protocolPowerGeneration"]==1&&first["ackIdentity"]["hostEpoch"]==5&&first["ackIdentity"]["wireSequence"]==7&&first["ackIdentity"]["tupleVerified"]==true);
 check("ACK-never-promotes-game-or-Steam",first["ackIdentity"]["gameInputUnverified"]==true&&first["ackIdentity"]["steamConsumptionProven"]==false&&first["gameInputUnverified"]==true);
 for(int i=0;i<48;i++)inputChainCountFrame(true,true,false,true,"",8+i,ack);check("no-per-frame-projection-events",emitted.size()==1);
 inputChainCountFrame(true,true,false,false,"",56,ack);s=latest("wake-input-chain-sustained");check("50th-only-sparse-ACK-evidence",emitted.size()==2&&s["acceptedInCurrentGeneration"]==50&&s["ackIdentity"]["tupleVerified"]==true);
 // Native result still governs counters even if optional identity is absent.
 emitted.clear();gen=5;auto missing=ack;missing.requestBindingCaptured=false;inputChainCountFrame(true,true,false,true,"",8,missing);first=latest("wake-input-chain-first-frame");
 check("optional-missing-ACK-does-not-change-acceptance",first["acceptedInCurrentGeneration"]==1&&first["ackIdentity"]["nativeAccepted"]==true&&first["ackIdentity"]["tupleVerified"]==false);
 json dom={{"protocolVersion",1},{"command","SUBMIT_FRAME"},{"requestId",request},{"sequence",7},{"runId",g_inputHostRunId},{"epoch",5},{"powerGeneration",1},{"hostInstanceId",g_inputHostHostInstanceId},{"ok",true},{"status","frame-accepted"}};
 fast.used=false;auto domAck=actualCapture(true,request,true,fast,dom);emitted.clear();gen=6;inputChainCountFrame(true,true,false,true,"",7,domAck);first=latest("wake-input-chain-first-frame");
 check("production-DOM-ACK-uses-parsed-reply",first["ackIdentity"]["tupleVerified"]==true&&first["ackIdentity"]["identityOrigin"]=="existing-validator-and-parsed-dom-reply");
 auto rejected=actualCapture(true,request,false,fast,dom);inputChainCountFrame(true,false,false,false,"foreign-result",7,rejected);auto reject=latest("input-host-frame-reject");
 check("native-rejection-cannot-borrow-good-DOM",reject["ackIdentity"]["nativeAccepted"]==false&&reject["ackIdentity"]["tupleVerified"]==false);
 auto bad=dom;bad["sequence"]=8;auto mismatch=actualCapture(true,request,true,fast,bad);emitted.clear();gen=7;inputChainCountFrame(true,true,false,true,"",7,mismatch);
 check("DOM-tuple-mismatch-remains-unknown",latest("wake-input-chain-first-frame")["ackIdentity"]["tupleVerified"]==false);
 g_inputHostBoundPowerGeneration=0;auto unbound=actualCapture(true,request,true,fast,dom);check("no-bound-protocol-does-not-borrow-shared-power",!unbound.requestBindingCaptured&&!ymcc::inputChainAck::project(unbound).value("tupleVerified",false));
 for(int i=0;i<12;i++)inputChainCountFrame(true,false,false,false,std::to_string(i),++wire,rejected);check("reject-eight-row-cap-preserved",g_inputChain.rejectRows>8&&count("input-host-frame-reject")<=8);
 bool pass=true;for(auto&t:tests)pass&=t["pass"].get<bool>();std::cout<<json{{"schema","ymcc-r37-production-input-chain-regression-v1"},{"passed",pass},{"caseCount",tests.size()},{"tests",tests},{"offline",true},{"deviceOperations",0},{"actualGameConsumption","unknown"}}.dump(2)<<"\n";return pass?0:1;}
'''
cpp=out/'production-input-chain.cpp'
cpp.write_text(bridge+main,encoding='utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
cmd=out/'build.cmd'
cmd.write_text(f'@echo off\ncall "{vs}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "{cpp}" /I"{root / "deps/json"}" /I"{root / "native"}" /Fo"{out / "fixture.obj"}" /Fe"{out / "fixture.exe"}"\nif errorlevel 1 exit /b 1\n"{out / "fixture.exe"}" > "{out / "result.json"}"\nexit /b %errorlevel%\n',encoding='utf-8')
run=subprocess.run(['cmd.exe','/c',str(cmd)],capture_output=True)
(out/'build.log').write_bytes(run.stdout+run.stderr)
receipt={'productionSource':str(root/'native/main.cpp'),'wholeSourceSha256':hashlib.sha256(data).hexdigest(),'counterFragmentSha256':hashlib.sha256(fragment.encode()).hexdigest(),'captureFragmentSha256':hashlib.sha256((init+fill).encode()).hexdigest(),'compileExitCode':run.returncode,'singleExistingCounterCall':True,'captureInExistingSubmitMutex':True,'existingSparseProjectionSites':3,'deviceOperations':0}
if (out/'result.json').is_file(): receipt['result']=json.loads((out/'result.json').read_text())
(out/'source-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'passed':run.returncode==0 and receipt.get('result',{}).get('passed',False),'caseCount':receipt.get('result',{}).get('caseCount'),'validationRoot':str(out)}))
raise SystemExit(run.returncode)