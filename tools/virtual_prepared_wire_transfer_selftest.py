#!/usr/bin/env python3
"""R39 actual current-source prepared wire ownership proof. Default prepare/build/verify.
All generated files stay in R39/wire-worker. Performance is separately held.
"""
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,os,re,subprocess,sys,uuid
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
OUTPUT=ROOT.parents[2]/'Build/Validation/VirtualInputPerf-R39-20261015/wire-worker'
VCVARS=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
EXPECTED_MAIN='a22e9c05f412ec1836ade13988e31ea9fbfbfdb045a1fc15a9cfe386ea7a06f0'
EXPECTED_HOST='5c4de79ae9125ad12ef24409902451047de16bbc23dd5e7748d83e3b35ede3a6'
AFFECTED=['prepared-neutral','prepared-moving','prepared-imu-touch','prepared-unicode-long','prepared-diagnostic-active']
CONTROLS=['json-legacy','json-prepare-diagnostic','prepared-diagnostic-closed','prepared-empty-sso','guard-rejected']
PLAN=dict(schemaVersion=1,candidate='exact-two-change-consumable-prepared-wire',profiles=['Release','Checked'],campaigns=2,pairedRounds=11,
 minimumFasterRounds=10,pairedStatistic='median(1-candidateNs/baselineNs) over 11 pairs',minimumPairedReduction=.05,minimumRatioReduction=.05,allocationRequestsAndBytesMustNotIncrease=True,
 ratioStatistic='1-median(candidateNs)/median(baselineNs) over 11 pairs',maximumControlRegression=.05,controlPairedAndRatioRequired=True,affected=AFFECTED,controls=CONTROLS,scopes=['actual-wire-build-preparation-extension-modeled-io-disposal','actual-frame-assembly-wire-build-preparation-extension-modeled-io-disposal'],
 totalRows=80,batchInputs=16,measurementBatches=64,allocationBatches=8,warmupBatches=12,
 inputKind='SYNTHETIC; no archive, hardware, environment, OS/pipe/product/SDK execution',
 diagnosticContextBoundary='Request/OfferPrepare/ACK and variant identity reset once before each measurement batch, outside timer/allocation counter. Metadata helper and actual ExtendRequest execute per operation. JSON PREPARE control invokes real repeated OfferPrepare without resetting per operation.',
 timingBoundary='Both variants build/dispose prepared owner and request/response storage fairly. Scope1 uses an immutable complete preassembled frame created outside timing; scope2 executes actual frame assembler inside each op. Both execute full current builder and contiguous preparation/extension and current modeled connect/write/read. Model trace serialization/verification excluded in timed mode equally. Actual excerpt production chrono clocks remain; no additional per-operation fixture clock. Standin timed write samples first/last byte; detailed verification hashes every byte.',
 decision='All affected rows require every CPU gate; all rows require ordinary/aligned request and byte nonincrease. Controls <=5% paired-median and ratio-of-medians regression. One fixed formal attempt; no retiming/tuning/control removal.',performance='HELD until parent reviews fresh correctness and bindings and explicitly releases.')
def sha(b):return hashlib.sha256(b.encode('utf-8') if isinstance(b,str) else b).hexdigest()
def write(p,b):
 p=Path(p).resolve()
 if not p.is_relative_to(OUTPUT.resolve()):raise ValueError('Write outside wire-worker')
 p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b.encode('utf-8') if isinstance(b,str) else b)
def save(p,x):write(p,json.dumps(x,indent=2,ensure_ascii=True)+'\n')
def close(s,start):
 depth=0;mode='code';i=start
 while i<len(s):
  c=s[i];two=s[i:i+2]
  if mode=='line':
   if c=='\n':mode='code'
  elif mode=='block':
   if two=='*/':mode='code';i+=1
  elif mode in ('string','char'):
   if c=='\\':i+=1
   elif c==('"' if mode=='string' else "'"):mode='code'
  elif two=='//':mode='line';i+=1
  elif two=='/*':mode='block';i+=1
  elif c=='"':mode='string'
  elif c=="'":mode='char'
  elif c=='{':depth+=1
  elif c=='}':
   depth-=1
   if depth==0:return i+1
  i+=1
 raise ValueError('Unclosed current source block')
def extract(s,marker,cls=False):
 hits=[]
 for m in re.finditer(re.escape(marker),s):
  a=m.start();b=s.find('{',a);semi=s.find(';',a)
  if b>=0 and (semi<0 or b<semi):
   end=close(s,b);end+=int(cls and s[end:end+1]==';');hits.append(s[a:end])
 if len(hits)!=1:raise ValueError('Missing/nonunique definition: '+marker)
 return hits[0]
def parts(s,pad,runtime):
 fn=extract(s,'static bool inputHostRequestPreparedLocked(')
 before='const std::string* preparedText';assignment='requestText = *preparedText;'
 assert fn.count(before)==fn.count(assignment)==1
 after=fn.replace(before,'std::string* preparedText',1).replace(assignment,'requestText = std::move(*preparedText);',1)
 assert after.replace('std::string* preparedText','const std::string* preparedText',1).replace('requestText = std::move(*preparedText);',assignment,1)==fn
 caller=extract(s,'static json inputHostSubmitPad(')
 null_wrappers=[extract(s,'static bool inputHostRequestLocked('),extract(s,'static bool inputHostRequestAdopted(')]
 assert len(re.findall(r'inputHostRequestPreparedLocked\s*\(',s))==4
 assert all(re.search(r'inputHostRequestPreparedLocked\(command, payload, expectedStatus, timeoutMs, responseOut,\s*nullptr, nullptr, nullptr, nullptr,',x) for x in null_wrappers)
 assert caller.count('std::string frameRequestId;')==caller.count('std::string frameRequestText;')==1
 assert len(re.findall(r'\bframeRequestText\b',caller))==4
 call=re.search(r'inputHostRequestPreparedLocked\("SUBMIT_FRAME", frame, "frame-accepted", 1000, &receipt,\s*&frameRequestText, &frameRequestId, &frameSequence, &fastReceipt\)',caller)
 assert call and 'frameRequestText' not in caller[call.end():]
 assert 'frameRequestText =' in caller[:call.start()] and 'frameRequestId =' in caller[:call.start()]
 prefix=fn[fn.index('    const bool reqBuildCostOn'):fn.index('    HANDLE pipe = g_inputHostServerPipe;')]
 io=fn[fn.index('    HANDLE pipe = g_inputHostServerPipe;'):fn.index('    if (!read) {')]
 seam=re.search(r'if \(ymStartPipeSeamServe\([^\n]+return true;',fn).group(0).rstrip('\r')
 guard=re.search(r'if \(!g_inputHostProcess[^\n]+return false;',fn).group(0).rstrip('\r')
 helpers=[]
 for m,cls in [('class C6aNumberTextCache',True),('static C6aNumberTextCache& c6aNumberTextCache()',False),('static void c6aAppendEscaped(std::string& out, const std::string& value)',False),('static void c6aAppendEscaped(std::string& out, const char* value)',False),('static void c6aAppendUInt(',False),('static void c6aAppendInt(',False),('static void c6aAppendJsonValue(',False)]:helpers.append(extract(s,m,cls))
 identities=[extract(s,m) for m in ['static std::string inputHostConfiguredPersona()','static const char* inputHostPersona()','static const char* inputHostProfileIdentityOf(','static const char* inputHostProfileIdentity()','static const char* inputHostBoundOrCurrentPersona()','static const char* inputHostBoundOrCurrentProfileIdentity()','static unsigned long long inputHostProtocolPowerGenerationLocked()']]
 constants=[]
 for n in ['kInputHostProtocolConfigHash','kInputHostTargetId','kInputHostOwnerIdentity']:
  values=re.findall(r'^static constexpr const char\* '+n+r'\s*=[^;]*;',s,re.M);assert len(values)==1;constants.append(values[0])
 diag=[]
 for marker,cls in [('struct MsiVirtualReportFrameMetadataScope {',True),('static ymcc::msi::virtualReportDiag::Binding inputHostMsiReportDiagnosticBinding()',False),('static ymcc::msi::virtualReportDiag::Gates inputHostMsiReportDiagnosticGates()',False),('static bool inputHostMsiReportDiagnosticAppendOptional(',False),('static void inputHostMsiReportDiagnosticExtendRequest(',False),('static std::string inputHostMsiReportDiagnosticFrameMetadata(',False)]:diag.append(extract(runtime,marker,cls))
 return dict(fullRequest=fn,candidateRequest=after,caller=caller,prefix=prefix,io=io,earlySeam=seam,earlyGuard=guard,
  helpers=helpers,identityHelpers=identities,constants=constants,nullPreparedWrappers=null_wrappers,
  builder=extract(s,'static std::string inputHostBuildSubmitFrameText('),envelope=extract(s,'static json inputHostEnvelopeLocked('),
  pads=[extract(pad,m,c) for m,c in [('struct Contact {',True),('struct Snapshot {',True),('inline Json snapshotJson(',False)]],
  frame=extract(s,'struct FocusFrameContent',True),normalize=extract(s,'static constexpr float inputHostNormalizeAxis('),assembler=extract(s,'static json focusIsolationAssembleFrame('),
  ioHelper=extract(s,'static bool inputHostPipeIo('),connectHelper=extract(s,'static bool inputHostWaitClientLocked('),diagnostics=diag,
  untouchedEntry=fn[:fn.index('    const bool reqBuildCostOn')],protectedAfterSend=fn[fn.index('    if (!read) {'):],
  callerBuildBlock=caller[caller.index('            const bool frameFastPath'):caller.index('            json receipt;')],
  aliasContract='preparedText MUST NOT alias preparedRequestId, command/expectedStatus storage or any later-used input/output; actual distinct wire/ID locals and SUBMIT_FRAME/frame-accepted literals mechanically checked; hypothetical alias counterexamples outside audited caller contract')
def generated(p):
 shared='namespace ymcc::screenpads { using Json=json;\n'+'\n'.join(p['pads'])+'\n}\n'+p['frame']+'\n'+p['normalize']+'\n'+p['assembler']+'\n'
 namespaces=[]
 for diagnostic in [0,1,2]:
  for candidate in [False,True]:
   name=({0:'plain_',1:'diag_',2:'novis_'}[diagnostic])+('candidate' if candidate else 'baseline')
   chunk='namespace '+name+' {\n'+ '\n'.join(p['constants'])+'\n'+CONTEXT+'\n'+'\n'.join(p['identityHelpers'])+'\n'+p['envelope']+'\n'+'\n'.join(p['helpers'])+'\n'+p['builder']+'\n'
   if diagnostic:
    chunk+=('#define YMCC_MSI_VISIBILITY_DIAGNOSTIC\n' if diagnostic==1 else '')+DIAG_CONTEXT+'\n'+'\n'.join(p['diagnostics'])+'\n'+DIAG_IMPLEMENT+'\n'+('#undef YMCC_MSI_VISIBILITY_DIAGNOSTIC\n' if diagnostic==1 else '')
   else:chunk+=PLAIN_DIAG_CONTEXT
   chunk+=p['ioHelper']+'\n'+p['connectHelper']+'\n'
   prefix=p['prefix']
   if candidate:prefix=prefix.replace('requestText = *preparedText;','requestText = std::move(*preparedText);',1)
   signature='std::string*' if candidate else 'const std::string*'
   if diagnostic:chunk+='#define YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC\n'
   chunk+='static Result Execute(const char* command,const json& payload,const char* expectedStatus,DWORD timeoutMs,json* responseOut,'+signature+' preparedText,const std::string* preparedRequestId,const unsigned long long* preparedSequence) {\n'+ENTRY_TRACK+'\n'+p['earlySeam']+'\n'+p['earlyGuard']+'\n'+prefix+'\n'+OWNER_ENTER+'\n'+p['io']+'\n'+RESULT_CAPTURE+'\n}\n'
   if diagnostic:chunk+='#undef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC\n'
   chunk+=VARIANT_API.replace('VARIANT_DIAGNOSTIC','true' if diagnostic else 'false')+'\n}\n'
   namespaces.append(chunk)
 return shared+'\n'+'\n'.join(namespaces)
# Inert identity/timing/log wrappers are explicit seams; identity getters above are real source.
CONTEXT=r'''
static std::mutex g_inputHostPersonaMx;
static struct {std::string virtualTargetPersona="steamdeck";} g_realStickTest;
static std::string g_inputHostBuildPersona,g_inputHostBoundPersona;
static std::string g_inputHostRunId="run-synthetic",g_inputHostStartupNonce="nonce-synthetic",g_inputHostPipeName="pipe-synthetic",g_inputHostHostInstanceId="host-synthetic";
static unsigned long long g_inputHostConfigRevision=17,g_inputHostEpoch=23,g_inputHostSequence=40,g_inputHostBoundPowerGeneration=29;
static std::atomic<unsigned long long> g_powerGeneration{31};
static HANDLE g_inputHostProcess=reinterpret_cast<HANDLE>(uintptr_t(0x2001)),g_inputHostServerPipe=reinterpret_cast<HANDLE>(uintptr_t(0x3001));
static bool g_inputHostPipeConnected=true;
static std::atomic<bool> g_exitRequested{false},g_inputHostExitFence{false},g_inputReady{true},g_inputHostPowerAdmissionRevoked{false};
enum class PowerLifecycle{Ready,Other};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static bool tickCostEnabled(){return false;}
static unsigned long long tickCostElapsedUs(std::chrono::steady_clock::time_point){return 0;}
static constexpr int kTickCostFrameBuild=0;static void tickCostAccumulate(int,unsigned long long){}
static std::string inputHostNewId(){return model.requestId;}
static bool ymStartPipeSeamServe(const char*,const char*,json*){return model.entrySeam;}
static HANDLE inputHostCreateServerPipeLocked(){model.action("server-create");return model.serverAvailable?reinterpret_cast<HANDLE>(uintptr_t(0x3001)):nullptr;}
static void recoverySnapSetPersona(const std::string& s){model.note("recovery-persona",s);}
static void recoverySnapNoteCommand(const char* s){model.note("recovery-command",s);}
static void inputHostSwitchAuditDispatch(const std::string& a,const std::string& b){model.note("switch-audit",a+"|"+b);}
'''
DIAG_CONTEXT=r'''
static ymcc::msi::virtualReportDiag::Session g_msiVirtualReportDiagnostic;
static thread_local const std::string* g_msiVirtualReportFrameMetadata=nullptr;
static bool msiClawKnownBoardAuthority(){return model.knownBoard;}
static bool domainLogEnabled(const char*){return model.detailEnabled;}
// Explicit request acquisition seam: Session Request is real; no environment/OS access.
static void inputHostMsiReportDiagnosticReadExplicitRequest() noexcept {model.action("explicit-request-seam");if(model.explicitRequest)g_msiVirtualReportDiagnostic.Request(model.sessionId,10000,128);}
'''
DIAG_IMPLEMENT=r'''
static void configureDiagnostic(const Config& c){
 std::destroy_at(&g_msiVirtualReportDiagnostic);std::construct_at(&g_msiVirtualReportDiagnostic);
 g_msiVirtualReportDiagnostic.ObserveHello(1);
 if(c.phase==0){g_msiVirtualReportDiagnostic.Close();return;}
 g_msiVirtualReportDiagnostic.Request(c.sessionId,10000,128);
 if(c.phase==1)return;
 ymcc::msi::virtualReportDiag::Gates initial;initial.knownMsiBoard=true;initial.visibilityDiagnosticBuild=true;initial.virtualDetailLogEnabled=true;initial.powerReady=true;
 auto offer=g_msiVirtualReportDiagnostic.OfferPrepare(inputHostMsiReportDiagnosticBinding(),initial,800);
 if(c.phase==2)return;
 if(offer)g_msiVirtualReportDiagnostic.AcceptPrepare(inputHostMsiReportDiagnosticBinding(),true,c.sessionId,900);
}
static json diagnosticState(){++diagnosticStateCalls;const auto& s=g_msiVirtualReportDiagnostic;return {{"phase",static_cast<int>(s.phase_)},{"version",s.version_},{"id",s.sessionId_},{"attempts",s.attempts_},{"records",s.records_},{"deadline",s.deadline_},{"epoch",s.binding_.epoch},{"power",s.binding_.powerGeneration},{"runId",s.binding_.runId}};}
static std::string metadataFor(const Config& c){
 if(!c.metadataOverride.empty())return c.metadataOverride;
 ymcc::msi::virtualReportDiag::SourceStamp source{};source.sequence=71;source.generation=7;source.capturedTick=950;source.valid=c.requireFreshSource;
 return inputHostMsiReportDiagnosticFrameMetadata(source,false,0);
}
'''
PLAIN_DIAG_CONTEXT=r'''
static void configureDiagnostic(const Config&){}
static json diagnosticState(){++diagnosticStateCalls;return nullptr;}
static std::string metadataFor(const Config&){return {};}
struct MsiVirtualReportFrameMetadataScope { explicit MsiVirtualReportFrameMetadataScope(const std::string&){} };
'''
ENTRY_TRACK=r'''
Result result; result.early=true;if(model.trace)result.trace=std::make_unique<ResultTrace>();
struct EntryExit { Result& r;~EntryExit(){if(r.early)model.action("entry-return");}} entryExit{result};
'''
OWNER_ENTER=r'''
result.early=false;result.sequence=sequenceValue;
if(model.trace){result.trace->wireAfterExtension=requestText;result.wirePointerMatchesSource=preparedText&&requestText.data()==model.sourcePointer;}
model.ownerEnter(requestText.data(),requestText.size());nextKind=g_inputHostPipeConnected?0:2;
struct RequestOwnerEnd { ~RequestOwnerEnd(){model.ownerExit();} } requestOwnerEnd;
'''
RESULT_CAPTURE=r'''
(void)writeElapsedUs;(void)readElapsedUs;result.connected=connected;result.written=written;result.read=read;result.error=lastError;result.responseBytes=responseBytes;
result.sequence=sequenceValue;if(model.trace)result.trace->requestId=requestIdValue;
if(model.trace){result.trace->response.assign(responseBuffer.data(),responseBuffer.data()+responseBytes);result.trace->sourceAfter=preparedText?*preparedText:std::string{};}
if(model.trace){result.trace->session=diagnosticState();result.trace->lifecycle={{"captured",lifecycleCommandEvidence.captured},{"request",lifecycleCommandEvidence.request},{"boundHost",lifecycleCommandEvidence.boundHost}};}return result;
'''
VARIANT_API=r'''
static void Reset(const Config& c) {
 g_inputHostExitFence.store(false);g_inputHostPowerAdmissionRevoked.store(false);g_powerLifecycle.store(PowerLifecycle::Ready);
 g_inputHostBuildPersona=c.buildPersona;g_inputHostBoundPersona=c.boundPersona;g_realStickTest.virtualTargetPersona=c.persona;
 g_inputHostRunId=c.runId;g_inputHostStartupNonce=c.nonce;g_inputHostSequence=c.sequence;
 g_inputHostProcess=c.validProcess?reinterpret_cast<HANDLE>(uintptr_t(0x2001)):nullptr;
 g_inputHostPipeName=c.validPipeName?"pipe-synthetic":"";
 g_inputHostServerPipe=c.serverPresent?reinterpret_cast<HANDLE>(uintptr_t(0x3001)):nullptr;g_inputHostPipeConnected=c.connected;
 g_inputHostBoundPowerGeneration=c.boundPower;g_powerGeneration.store(c.power);
 g_exitRequested.store(c.exitFence);g_inputReady.store(c.powerReady);
 configureDiagnostic(c);
}
static std::string Build(const json& frame,const std::string& id,unsigned long long seq){return inputHostBuildSubmitFrameText(frame,id,seq,71,1000,std::string(64,'a'),"motion-synthetic");}
static constexpr bool HasDiagnostic=VARIANT_DIAGNOSTIC;
'''
def invoke(run,args,stem,timeout=180):
 p=subprocess.run(list(map(str,args)),cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=timeout)
 write(run/(stem+'.stdout.log'),p.stdout);write(run/(stem+'.stderr.log'),p.stderr);return p

def prepare(stage_only=False):
 run=OUTPUT/('prepare-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')+'-'+uuid.uuid4().hex[:6]);run.mkdir(parents=True)
 paths=['native/main.cpp','InputHost/Program.cs','native/screen_touchpads.h','native/msi_virtual_report_diagnostic.h','native/msi_virtual_report_diagnostic_runtime.h','native/input_chain_command_evidence.h','native/input_chain_ack_evidence.h','deps/json/json.hpp']
 raw={p:(ROOT/p).read_bytes() for p in paths}
 assert sha(raw['native/main.cpp'])==EXPECTED_MAIN and sha(raw['InputHost/Program.cs'])==EXPECTED_HOST,'Live identity changed; parent must review rebound source'
 p=parts(raw['native/main.cpp'].decode('utf-8-sig'),raw['native/screen_touchpads.h'].decode('utf-8-sig'),raw['native/msi_virtual_report_diagnostic_runtime.h'].decode('utf-8-sig'))
 for path,b in raw.items():write(run/'snapshot'/path,b)
 # Capture all current native headers, no old baseline; only small explicit subset compiled.
 headers=[]
 for header in sorted((ROOT/'native').glob('*.h')):
  b=header.read_bytes();write(run/'snapshot/native'/header.name,b);headers.append(dict(path=str(header.relative_to(ROOT)),sha256=sha(b),bytes=len(b)))
 save(run/'plan.json',PLAN);save(run/'timing-status.json',dict(status='HELD',performanceExecuted=False))
 write(run/'runner.snapshot.py',Path(__file__).read_bytes());write(run/'fixture.cpp',Path(__file__).with_suffix('.cpp').read_bytes())
 write(run/'json.hpp',raw['deps/json/json.hpp']);write(run/'actual-session.h',raw['native/msi_virtual_report_diagnostic.h']);write(run/'actual-command.h',raw['native/input_chain_command_evidence.h']);write(run/'input_chain_ack_evidence.h',raw['native/input_chain_ack_evidence.h'])
 for i,text in enumerate(p['nullPreparedWrappers']):write(run/('null-prepared-wrapper-'+str(i)+'.cpp.txt'),text)
 for key in ['fullRequest','candidateRequest','caller','prefix','io','untouchedEntry','protectedAfterSend','callerBuildBlock']:write(run/(key+'.cpp.txt'),p[key])
 baseline=raw['native/main.cpp'].decode('utf-8-sig');after=baseline.replace(p['fullRequest'],p['candidateRequest'],1);assert after.replace(p['candidateRequest'],p['fullRequest'],1)==baseline
 write(run/'candidate-main.cpp',(b'\xef\xbb\xbf' if raw['native/main.cpp'].startswith(b'\xef\xbb\xbf') else b'')+after.encode('utf-8'))
 write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(baseline.splitlines(True),after.splitlines(True),fromfile='actual/native/main.cpp',tofile='candidate/native/main.cpp')))
 # Diagnostic query/config are fixture-only and appended to each variant's real Session glue.
 gen=generated(p);write(run/'source-bound.h',gen)
 save(run/'source-binding.json',dict(sources=[dict(path=k,sha256=sha(v),bytes=len(v)) for k,v in raw.items()],nativeHeaders=headers,
 currentBaselineAuthority=True,exactTwoChangeReversal=True,parts={k:sha(v) for k,v in p.items() if isinstance(v,str)},listParts={k:[dict(index=i,sha256=sha(v)) for i,v in enumerate(values)] for k,values in p.items() if isinstance(values,list)},candidateMainSha256=sha((run/'candidate-main.cpp').read_bytes()),
 callerAliasContract=p['aliasContract'],actualDistinctCallerLocalsMechanicallyProven=True,
 ownerTraceMeaning='scope-exit marker precedes requestText destruction; neither kernel completion nor exact allocator free; active AppendOptional may replace capacity before issue',
 aliasLiteralProof=dict(command='SUBMIT_FRAME',expectedStatus='frame-accepted',distinctLocals=['frameRequestText','frameRequestId','frameSequence'],onlyPreparedCall=True),
 sourceSeams=['Identity state is synthetic; configured/bound persona/profile/power functions are extracted actual source','GUID creation returns fixed synthetic model.requestId; OS clock/PID and Win32 I/O all fake','Explicit request acquisition invokes real Session::Request without env access','Receipt/sequence-after-send suffix not executed: protected exact source and explicitly modeled only','A1 lock-discipline/observation RAII entry is protected; exact early seam and unavailable-session guard execute','No baseline cache reset per operation; variants have independent actual caches'],productionEdited=False,performanceExecuted=False))
 if stage_only:print(json.dumps(dict(run=str(run),staged=True)));return
 verify(run)

def verify(run):
 for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  command=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
  bat='@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\nexit /b %errorlevel%\n';write(run/(name+'-build.bat'),bat)
  proc=invoke(run,['cmd.exe','/d','/c',run/(name+'-build.bat')],name+'-build',180)
  if proc.returncode:raise RuntimeError(name+' build failed: '+str(run))
  proc=invoke(run,[run/(name+'.exe'),'verify',run,name],name+'-verify',180)
  if proc.returncode:raise RuntimeError(name+' correctness failed: '+str(run))
  print(name+': '+proc.stdout.strip(),flush=True)
 binding=json.loads((run/'source-binding.json').read_text())
 for row in binding['sources']+binding['nativeHeaders']:assert sha((ROOT/row['path']).read_bytes())==row['sha256'],'Live input drift'
 binding['artifacts']={str(p.relative_to(run)):sha(p.read_bytes()) for p in run.rglob('*') if p.is_file() and p.name!='source-binding.json'};binding['liveInputsRechecked']=True;binding['measurementInstrumentation']=dict(leanResultAndOutcome='scalar fields + nullable unique_ptr; proof strings/JSON/vectors constructed only trace=true',proofStorageSelfCheck='PASS in both profiles; no semantic state getter on lean path',literalActions='const char* overload; no trace strings constructed when disabled',writeObservation='full checksum and copies only trace=true; size/first-last scalar observation otherwise; modeled issue only',planRows=80,pairedMedianAndRatioOfMedians=True,controlPairedAndRatio=True)
 save(run/'source-binding.json',binding);save(OUTPUT/'latest-prepare.json',dict(run=str(run),readyForParentReview=True,performance='HELD'))
 print(json.dumps(dict(run=str(run),readyForParentReview=True,performance='HELD',productionEdited=False)))

def benchmark(run,approved):
 if not approved:raise ValueError('Performance HELD pending explicit parent release')
 if (OUTPUT/'formal-attempt.json').exists():raise ValueError('Single formal attempt consumed')
 assert json.loads((run/'plan.json').read_text())==PLAN
 binding=json.loads((run/'source-binding.json').read_text())
 assert binding.get('liveInputsRechecked')
 for row in binding['sources']:assert sha((ROOT/row['path']).read_bytes())==row['sha256']
 for name,h in binding['artifacts'].items():assert sha((run/name).read_bytes())==h,name
 save(OUTPUT/'formal-attempt.json',dict(run=str(run),started=datetime.datetime.now(datetime.timezone.utc).isoformat()))
 save(run/'performance-permission.json',dict(authorization='explicit --performance-approved after parent release',at=datetime.datetime.now(datetime.timezone.utc).isoformat(),planSha256=sha((run/'plan.json').read_bytes())))
 for profile in PLAN['profiles']:
  for campaign in [1,2]:
   proc=invoke(run,[run/(profile+'.exe'),'benchmark',run,profile,campaign],f'{profile}-campaign-{campaign}',600)
   if proc.returncode not in [0,2]:raise RuntimeError('Formal run failure')
   print(proc.stdout.strip(),flush=True)
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--stage-only',action='store_true');ap.add_argument('--verify-run',type=Path);ap.add_argument('--benchmark-run',type=Path);ap.add_argument('--performance-approved',action='store_true');a=ap.parse_args()
 if a.benchmark_run:benchmark(a.benchmark_run,a.performance_approved)
 elif a.verify_run:verify(a.verify_run)
 else:prepare(a.stage_only)
