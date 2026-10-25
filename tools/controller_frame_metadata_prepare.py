"""C02 bounded source-extracted metadata fixture. prepare never compiles or times.
Execution modes require --allow-execution after human slot authorization.
Never modifies native/main.cpp. Formal performance is one-shot per prepared run.
"""
import argparse,datetime,difflib,hashlib,json,os,re,subprocess,sys
from pathlib import Path
MAIN=Path(__file__).resolve().parents[1]
TASK=MAIN.parents[2]/'Build/Validation/VirtualInput-Cpu-Exhaustion-20261018/Metadata'
SHA=lambda b:hashlib.sha256(b).hexdigest()
def block(text,marker):
 start=text.index(marker)
 while True:
  brace=text.index('{',start);semi=text.find(';',start)
  if semi<0 or semi>brace:break
  start=text.index(marker,start+len(marker))
 depth=0
 for t in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',text[brace:]):
  if t.group()=='{':depth+=1
  elif t.group()=='}':
   depth-=1
   if not depth:return text[start:brace+t.end()]+(';' if text[brace+t.end():].startswith(';') else '')
 raise ValueError(marker)
def write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 if isinstance(data,bytes):path.write_bytes(data)
 else:path.write_text(data,encoding='utf-8')
def prepare(run):
 run.mkdir(parents=True,exist_ok=False)
 source=MAIN/'native/main.cpp';raw=source.read_bytes();text=raw.decode('utf-8-sig').replace('\r\n','\n');bindings=[]
 def save(p,rel):
  data=p.read_bytes();dst=run/'SourceSnapshot'/rel;write(dst,data);bindings.append(dict(file=str(p),snapshot=str(dst),sha256=SHA(data)));return data
 save(source,'native/main.cpp')
 for name in ['windows_source_view.h','windows_motion_submission.h','windows_motion_processing.h','reviewed_gyro_calibration.h','msi_motion_capabilities.h','hc_model_motion_catalog.h','hc_default_motion_catalog.h','hc_legacy_motion_catalog.h']:
  save(MAIN/'native'/name,'native/'+name)
 save(MAIN/'deps/json/json.hpp','deps/json/json.hpp')
 for name in ['controller_frame_metadata_prepare.py','controller_frame_metadata_selftest.cpp']:save(MAIN/'tools'/name,'tools/'+name)
 frame=block(text,'static void inputCaptureOnGamepad(')
 scope=re.search(r'    const ymcc::sensors::view::ProcessingScope<InputCaptureSensorMeta> windowsProcessingScope\([\s\S]*?\);',frame).group()
 start=frame.index('    InputCaptureSensorMeta sensorMeta;');end=frame.index('    const bool privateMotionPairProven',start)
 call=frame[start:end]
 assert call.count('sensorMeta = inputCaptureSensorMetaSnapshot();')==1
 candidate=call.replace('InputCaptureSensorMeta sensorMeta;','const InputCaptureSensorMeta& sensorMeta = windowsSensorView.metadata;').replace('        sensorMeta = inputCaptureSensorMetaSnapshot();\n','')
 assert frame.index(scope)<start
 rest=frame[start:]
 for bad in ['windowsSensorView.metadata=', 'windowsSensorView.metadata =','windowsSensorView=','windowsSensorView =','const_cast<InputCaptureSensorMeta','const_cast<WindowsSensorView']:
  assert bad not in rest,bad
 view_calls=[]
 for m in re.finditer(r'(\w+)\(windowsSensorView(?:[,\)])',rest):
  name=m.group(1)
  if name in ['current','currentForOutput']:continue
  assert re.search(r'\b'+name+r'\(const WindowsSensorView&',text),name
  view_calls.append(name)
 assert rest.count('&sensorMeta')==1
 expected_uses=['sensorMeta = inputCaptureSensorMetaSnapshot();','sensorMeta.gyroDeviceId','sensorMeta.accelDeviceId','sensorMeta.gyroFamily','sensorMeta.accelFamily','sensorMeta);','&sensorMeta','inputCaptureCandidateMatrixReceipt(sensorMeta)','inputCaptureMotionCapabilityReceipt(sensorMeta,','sensorMeta.gyroHcLegacyFields','sensorMeta.accelHcLegacyFields']
 uses=[line.strip() for line in rest.splitlines() if 'sensorMeta' in line]
 assert all(any(v in line for v in expected_uses) for line in uses if line!='InputCaptureSensorMeta sensorMeta;'),uses
 changed=text.replace(call,candidate,1);patch=''.join(difflib.unified_diff(text.splitlines(True),changed.splitlines(True),fromfile='native/main.cpp',tofile='native/main.cpp',n=5))
 assert patch.count('@@')==2 and changed.count('const InputCaptureSensorMeta& sensorMeta = windowsSensorView.metadata;')==1
 write(run/'candidate.patch',patch)
 functions=[('snapshot','static InputCaptureSensorMeta inputCaptureSensorMetaSnapshot()'),('provider','static std::string inputCaptureSensorProviderKey('),('pair','static bool inputCaptureMotionPairAdmitted('),('recipe','static json inputCaptureCandidateMatrixReceipt('),('capability','static json inputCaptureMotionCapabilityReceipt('),('calibration-current','static bool inputCaptureWindowsCalibrationFrameCurrent('),('calibration-admit','static bool inputCaptureWindowsSourceCalibrationAdmitted(')]
 decl=block(text,'struct InputCaptureSensorMeta {')
 chunks=[decl,'''static std::mutex g_sensorMetaMx;
static InputCaptureSensorMeta g_sensorMeta;
using WindowsSensorView=ymcc::sensors::view::Snapshot<InputCaptureSensorMeta>;
static thread_local const InputCaptureSensorMeta* t_windowsProcessingMeta=nullptr;
static thread_local const ymcc::windows_motion::Stamp* t_windowsProcessingStamp=nullptr;
static thread_local const WindowsSensorView* t_windowsProcessingView=nullptr;
struct FixtureSink {std::atomic<std::uint64_t> activeBindingGeneration{7};};
static std::shared_ptr<FixtureSink> g_inputCaptureSink;
struct FixtureMachine {std::string deviceClass,product,gyroMatrixId,accelMatrixId;};
struct FixtureSelector {std::string gyroMatrixId,accelMatrixId,matrixSourceSha256;};
static FixtureMachine g_machineIdentity;
static FixtureSelector g_hcMotionSelector;
struct FixturePolicy {bool enabled=true,bound=true;};
static FixturePolicy g_realStickTest;
static bool inputCaptureNewHcMotionSelectorBound(){return g_realStickTest.bound;}
static bool inputCaptureDefaultHcMotionSelectorBound(){return g_realStickTest.bound;}
static bool inputCaptureLegacyHcMotionSelectorBound(){return g_realStickTest.bound;}
static bool inputCaptureMotionSelectorBound(){return g_realStickTest.bound;}
static bool inputCaptureDefaultHcMotionIdentity(const FixtureMachine& m){return ymcc::hc_default_motion::find(m.deviceClass)!=nullptr;}
static const ymcc::hc_model_motion::Recipe* inputCaptureSourceQualifiedCalibrationRecipe(){return &ymcc::hc_model_motion::recipes[0];}
static bool inputCaptureSourceQualifiedCalibrationBound(){return g_realStickTest.bound;}
struct TickPreHostSubScope {template<class T>TickPreHostSubScope(int,T*) noexcept{}};
static constexpr int kTickCostPreHostMeta=0;
static struct {long long metaUs=0;} g_tickPreHostSub;
struct Projection {std::string gyroProvider,accelProvider;json recipe,capability;bool pair=false,current=false,calibration=false;};
struct Inputs {bool live=true,detail=false,wanted=true,finite=true;};
''']
 excerpts=[]
 for label,marker in functions:
  b=block(text,marker);chunks.append(b);at=text.index(b);excerpts.append(dict(symbol=label,startLine=text.count('\n',0,at)+1,source=b,sha256=SHA(b.encode())))
 for name,body in [('baseline',call),('candidate',candidate)]:
  chunks.append('static Projection '+name+'(WindowsSensorView& windowsSensorView,const Inputs& f){\n'+scope+'''
    const bool live=f.live,gyro=windowsSensorView.parts[0].usable,accel=windowsSensorView.parts[1].usable;
    const auto gyroBindingGeneration=windowsSensorView.parts[0].generation;
    const auto accelBindingGeneration=windowsSensorView.parts[1].generation;
    const bool windowsSensorViewCurrent=ymcc::sensors::view::current(windowsSensorView);
'''+body+'''
    Projection out;out.gyroProvider=std::move(gyroProvider);out.accelProvider=std::move(accelProvider);
    out.pair=pairProven;out.current=windowsSensorViewCurrent;
    out.calibration=inputCaptureWindowsSourceCalibrationAdmitted(f.wanted,gyro,accel,sameProvider,f.finite,
        windowsSensorView.submission,accelBindingGeneration,sensorMeta);
    if(f.detail){out.recipe=inputCaptureCandidateMatrixReceipt(sensorMeta);out.capability=inputCaptureMotionCapabilityReceipt(sensorMeta,false);}
    return out;
}\n''')
 write(run/'metadata-extracted.h','\n\n'.join(chunks))
 write(run/'fixture.cpp',(MAIN/'tools/controller_frame_metadata_selftest.cpp').read_bytes())
 proof_names=['inputCaptureWindowsProcessingAdopt','inputCaptureWindowsProcessingProcess','inputCaptureWindowsProcessingNoteFrame','inputCaptureWindowsProcessingFrameCurrent','inputCaptureReviewedCalibrationSource','inputCaptureReviewedCalibrationObserve','inputCaptureReviewedCalibrationTick','inputCaptureWriteReviewedCalibration','inputCaptureGyroCalibrationTick']
 proof=[]
 for name in proof_names:
  m=re.search(r'(?m)^static[^\n]*\b'+name+r'\(',text);assert m,name
  b=block(text,text[m.start():text.index('(',m.start())+1]);proof.append(dict(symbol=name,startLine=text.count('\n',0,m.start())+1,source=b,sha256=SHA(b.encode())))
 plan=dict(schemaVersion=1,frozenBeforeCompileAndTiming=True,profiles=['Release','Checked'],campaigns=2,pairs=11,iterationsPerArm=128,warmupPerArm=16,sceneCount=16,rowsPerProfile=32,privateReachability='synthetic boundary/control only; no production publisher; not a hotspot',order='AB/BA alternates by pair+campaign',timedBoundary='actual ProcessingScope + actual local metadata declaration/getter/provider/admission + optional actual recipe/capability owned result construction/destruction; excludes source acquisition, whole frame, Host/pipe/UI',hotLongMetadata=dict(minimumPairedMedianElapsedReduction=0.05,minimumPairedMedianThreadCycleReduction=0.05,minimumFasterPairs=9,allocationRequestsStrictlyDecrease=True,allocationBytesStrictlyDecrease=True),controls=dict(maximumMedianRegression=0.05,orAbsoluteRegressionNs=100,allocationRequestsMayGrow=False,allocationBytesMayGrow=False,maximumThreadCycleRegression=0.05),allRowsMustPass=True,noRetimingAfterFailure=True,noThresholdChange=True)
 write(run/'PERFORMANCE-PLAN.json',json.dumps(plan,ensure_ascii=False,indent=2)+'\n')
 report=dict(sourceSha256=SHA(raw),source=str(source),snapshot=str(run/'SourceSnapshot/native/main.cpp'),sourceReadOnly=True,candidateApplied=False,patch=str(run/'candidate.patch'),patchSha256=SHA(patch.encode()),oneHunk=True,actualType=decl,actualScopeCall=scope,baselineCallSite=call,candidateCallSite=candidate,functions=excerpts,scopeAndAliasProof=proof,metadataUses=uses,postBorrowViewCalls=view_calls,sourceBindings=bindings,compilePerformed=False,timingPerformed=False,semanticStatus='static-invariants-checked; runtime-correctness-not-yet-executed',fixtureSeams=['source sampling excluded; fixture supplies already-owned view and real atomic leases/counters','device/model policy predicates supplied as fixed data-only leaves; actual catalog lookups and receipt builders retained','TickPreHostSubScope inert; no diagnostic clock/log/recording in this local fixture','calibration storage callback chain source-reviewed only, never executes files/devices or product calibration'])
 write(run/'SOURCE-BINDING.json',json.dumps(report,ensure_ascii=False,indent=2)+'\n')
 write(run/'STATIC-OWNERSHIP.md','''# C02 local borrow proof

Only replace the local sensorMeta declaration and remove its later snapshot assignment in one hunk. No global getter, scope, lock, freshness/generation check or cross-frame cache changes.

ProcessingScope is declared in the outer function before this point and binds its metadata slot to the same local windowsSensorView.metadata. The getter therefore takes the non-null slot branch; its global lock branch is unreachable at this call. Nested scopes restore the prior pointer.

All later view-taking processing calls are const WindowsSensorView&. The one mutable Adopt call precedes the target declaration; it only advances submission/owner bookkeeping. No later metadata assignment or whole-view reassignment occurs. SOURCE-BINDING records every sensorMeta use and the actual function bodies.

Recipe/capability helpers take const metadata and synchronously return owned JSON values. The local selector lambda is called before its enclosing sample block exits. The only address use passes const metadata into calibration. ReviewedCalibrationSource copies the strings into owned Source fields; Observe and transaction state retain Source values, not metadata pointers. The calibration current lambda is used locally and passed to WriteReviewedCalibration by const std::function reference; that function only invokes it synchronously and does not store or enqueue it. Sensor requests carry enum values. See the actual callback-chain source excerpts, not a fixture claim of running calibration/storage.

The fixture includes an intentionally wrong no-scope/global-source negative control and a local-metadata-mutation counterexample sentinel. Such mutation is absent in the actual production closure; if source binding later shows it, reject this patch. Runtime tests are still required before acceptance. Allocation-failure sequence identity is not claimed when removing an owned copy.
''')
 write(TASK/'LATEST-PREPARED.json',json.dumps(dict(run=str(run),sourceSha256=SHA(raw),preparedOnly=True),indent=2)+'\n')
 return report

def execute(run,mode):
 binding=json.loads((run/'SOURCE-BINDING.json').read_text(encoding='utf-8'));plan=json.loads((run/'PERFORMANCE-PLAN.json').read_text(encoding='utf-8'))
 for r in binding['sourceBindings']:assert SHA(Path(r['snapshot']).read_bytes())==r['sha256'],r
 if mode=='performance':
  prior=json.loads((run/'CORRECTNESS-RESULTS.json').read_text(encoding='utf-8'));assert prior['passed']
  assert not (run/'PERFORMANCE-RESULTS.json').exists(),'Formal performance already exists; no retiming'
 vc=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
 env=os.environ.copy();env['PATH']=r'C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem'
 results={}
 for profile,flags in [('Release','/O2 /DNDEBUG /MT'),('Checked','/Od /RTC1 /D_DEBUG /MTd')]:
  d=run/profile;d.mkdir(exist_ok=True);exe=d/'fixture.exe'
  if mode=='correctness':
   q=lambda p:'"'+str(p)+'"'
   command='@echo off\r\ncall '+q(vc)+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc '+flags+' '+q(run/'fixture.cpp')+' /I'+q(run)+' /I'+q(run/'SourceSnapshot/native')+' /I'+q(run/'SourceSnapshot/deps/json')+' /Fo'+q(d/'fixture.obj')+' /Fe'+q(exe)+'\r\nexit /b %errorlevel%\r\n'
   write(d/'build.cmd',command);p=subprocess.run(['cmd.exe','/d','/c',str(d/'build.cmd')],cwd=d,env=env,capture_output=True,timeout=120);write(d/'build.log',p.stdout+p.stderr)
   if p.returncode:raise RuntimeError('compile failed: '+str(d/'build.log'))
  p=subprocess.run([str(exe),'--'+mode],cwd=d,env=env,capture_output=True,timeout=300);write(d/(mode+'.json'),p.stdout);write(d/(mode+'.stderr'),p.stderr)
  if p.returncode:raise RuntimeError('fixture failed: '+str(d/(mode+'.json')))
  results[profile]=json.loads(p.stdout.decode('utf-8-sig'))
  if mode=='correctness':results[profile]['executableSha256']=SHA(exe.read_bytes())
 out=dict(mode=mode,passed=all(r['passed'] for r in results.values()),profiles=results,plan=plan,sourceSha256=binding['sourceSha256'])
 write(run/('CORRECTNESS-RESULTS.json' if mode=='correctness' else 'PERFORMANCE-RESULTS.json'),json.dumps(out,ensure_ascii=False,indent=2)+'\n');return out

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mode',choices=['prepare','correctness','performance'],default='prepare');p.add_argument('--run',type=Path);p.add_argument('--allow-execution',action='store_true');a=p.parse_args()
 if a.mode=='prepare':
  run=a.run or TASK/('Prepared-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));b=prepare(run);print(json.dumps(dict(run=str(run),sourceSha256=b['sourceSha256'],compiled=False,timed=False)))
 else:
  if not a.allow_execution:p.error('Execution requires --allow-execution and a human-assigned time slot')
  if not a.run:p.error('--run required; do not prepare a new source after formal failure')
  r=execute(a.run,a.mode);print(json.dumps(dict(mode=a.mode,passed=r['passed'],run=str(a.run))))
if __name__=='__main__':main()
