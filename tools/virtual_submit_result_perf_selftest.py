"""Source-bound Native SubmitPad result DOM benchmark. No product/device/pipe runs."""
from pathlib import Path
import argparse,datetime,hashlib,json,subprocess,sys,difflib,re,os
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function,match_brace
REPO=Path(__file__).resolve().parents[1]
sha=lambda s:hashlib.sha256(s.encode('utf-8') if isinstance(s,str) else s).hexdigest()
def result_block(s):
 f=source_function(s,'static json inputHostSubmitPad(')
 a=f.index('    json frameProof = json::object();');b=f.index('    return result;',a)+len('    return result;')
 return f[a:b]
def candidate(s):
 a=s.index('    result["leftStick"] = json::object(');b=s.index('    return result;',a)
 code='''    // Each diagnostic child owns its values once; avoid initializer-list arrays/copies.
    json leftStick = json::object();
    leftStick["x"] = lx; leftStick["y"] = ly;
    result["leftStick"] = std::move(leftStick);
    json rightStick = json::object();
    rightStick["x"] = normalizedRx; rightStick["y"] = normalizedRy;
    result["rightStick"] = std::move(rightStick);
    json gyroDps = json::object();
    gyroDps["x"] = cgx; gyroDps["y"] = cgy; gyroDps["z"] = cgz;
    result["gyroDps"] = std::move(gyroDps);
    json motionContributionResult = json::object();
    motionContributionResult["x"] = useMotion ? motionContribution.shortX / 32767.0 : 0.0;
    motionContributionResult["y"] = useMotion ? motionContribution.shortY / 32767.0 : 0.0;
    result["motionContribution"] = std::move(motionContributionResult);
    json layoutContribution = json::object();
    layoutContribution["x"] = outX / 32767.0; layoutContribution["y"] = outY / 32767.0;
    result["layoutContribution"] = std::move(layoutContribution);
    json motionManager = json::object();
    motionManager["mapped"] = motionContribution.mapped;
    motionManager["triggered"] = motionContribution.triggered;
    motionManager["velocityMode"] = motionContribution.velocityMode;
    json gyroShort = json::object();
    gyroShort["x"] = motionContribution.shortX; gyroShort["y"] = motionContribution.shortY;
    motionManager["gyroShort"] = std::move(gyroShort);
    // HC AxisActions.ApplyAxisModifiers output before gyro-weight blending.
    json modifiedShort = json::object();
    modifiedShort["x"] = contributionX; modifiedShort["y"] = contributionY;
    motionManager["modifiedShort"] = std::move(modifiedShort);
    result["motionManager"] = std::move(motionManager);
    json layoutManager = json::object();
    json physicalStick = json::object();
    physicalStick["x"] = baseX / 32767.0; physicalStick["y"] = baseY / 32767.0;
    layoutManager["physicalStick"] = std::move(physicalStick);
    layoutManager["stickNorm"] = stickNorm;
    layoutManager["gyroWeight"] = g_realStickTest.gyroWeight;
    layoutManager["weightFactor"] = weightFactor;
    layoutManager["mergeOrder"] = "physical-stick-plus-motion-contribution";
    result["layoutManager"] = std::move(layoutManager);
'''
 return s[:a]+code+s[b:]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--baseline',type=Path);ap.add_argument('--shape-only',action='store_true');ap.add_argument('--source',type=Path);args=ap.parse_args()
 if args.shape_only and args.baseline:ap.error('--shape-only uses only the current/source implementation, without a baseline')
 run=args.out/datetime.datetime.now(datetime.timezone.utc).isoformat().replace(':','-').replace('.','-');run.mkdir(parents=True,exist_ok=False)
 source_path=args.source or REPO/'native/main.cpp'
 current=source_path.read_bytes();baseline=args.baseline.read_bytes() if args.baseline else current
 bs=baseline.decode('utf-8');cs=current.decode('utf-8');old=result_block(bs);new=old if args.shape_only else candidate(old)
 bound=result_block(cs)
 assert bound in [old,new],'Current result builder differs from audited baseline/candidate'
 (run/'main.baseline.cpp').write_bytes(baseline);(run/'main.current.cpp').write_bytes(current)
 (run/'result.before.inc').write_text(old,encoding='utf-8');(run/'result.candidate.inc').write_text(new,encoding='utf-8')
 (run/'candidate.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='result.before',tofile='result.candidate')),encoding='utf-8')
 a=bs.index('struct MotionContribution {');b=match_brace(bs,bs.index('{',a))+2;motion=bs[a:b]
 bindings='''
 const auto g_inputHostSampleSequence=v.sampleSequence;
 const auto& frameSubmission=v.frameSubmission;
 const bool accepted=v.accepted,hostActive=v.hostActive,firstFrame=v.firstFrame,targetLeft=v.targetLeft;
 const bool semanticNeutral=v.semanticNeutral,useMotion=v.useMotion,analogAdmitted=v.analogAdmitted,effUseMotion=v.effUseMotion;
 const auto& lifecycle=v.lifecycle; const auto& motionContribution=v.motionContribution;
 const float lx=v.lx,ly=v.ly,normalizedRx=v.rx,normalizedRy=v.ry;
 const double cgx=v.cgx,cgy=v.cgy,cgz=v.cgz,contributionX=v.contributionX,contributionY=v.contributionY;
 const double outX=v.outX,outY=v.outY,baseX=v.baseX,baseY=v.baseY,stickNorm=v.stickNorm,weightFactor=v.weightFactor;
 const struct {double gyroWeight;} g_realStickTest{v.gyroWeight};
 json commonFrameReceipt=v.receipt;
'''
 header=motion+'\n'+'''struct Input {
 unsigned long long sampleSequence=17;
 struct {bool rejected=false,sourceNeutralized=false;ymcc::input::frameSubmission::Verdict reason=ymcc::input::frameSubmission::Verdict::Allowed;} frameSubmission;
 bool accepted=true,hostActive=true,firstFrame=false,targetLeft=false,semanticNeutral=false,useMotion=true,analogAdmitted=true,effUseMotion=true;
 std::string lifecycle="ready",persona="steamdeck";
 MotionContribution motionContribution{1234.0,-3210.0,true,true,false};
 float lx=.25f,ly=-.5f,rx=.5f,ry=-1.f;
 double cgx=12,cgy=3,cgz=-4,contributionX=111,contributionY=-112,outX=123,outY=-124,baseX=-1,baseY=2,stickNorm=.8,weightFactor=.55,gyroWeight=.5;
 json receipt=nullptr;
};
static const Input* context=nullptr;
static unsigned personaCalls=0,proofCalls=0,g_commonFrameEvidenceFailures=0;
static const char* inputHostPersona(){++personaCalls;return context->persona.c_str();}
static const char* inputCaptureMotionProofId(bool on){++proofCalls;return on?"hc-pair-current":"no-pair-safe-zero";}
'''
 for name,body in [('baseline',old),('candidate',new)]:
  header+='\nstatic NOINLINE json '+name+'(const Input& v){context=&v;personaCalls=proofCalls=g_commonFrameEvidenceFailures=0;\n'+bindings+body+'\n}\n'
 (run/'generated.hpp').write_text(header,encoding='utf-8')
 deps=['deps/json/json.hpp','native/input_frame_submission.h','native/virtual_output_status.h']
 for rel in deps:(run/Path(rel).name).write_bytes((REPO/rel).read_bytes())
 (run/'fixture.cpp').write_bytes((REPO/'tools/virtual_submit_result_perf_selftest.cpp').read_bytes())
 meta={'baselineSourceSHA256':sha(baseline),'currentSourceSHA256':sha(current),'mode':'shape-only' if args.shape_only else 'performance-comparison','actualCurrentEqualsCandidate':bound==new,'oldBlockSHA256':sha(old),'candidateBlockSHA256':sha(new),'sourceLine':bs[:bs.index(old)].count('\n')+1,'dependencies':{rel:sha((REPO/rel).read_bytes()) for rel in deps},'sourcePath':str(source_path),'scope':'Exact complete SubmitPad result construction after submission; actual MotionContribution and frame verdict naming. Protocol, decisions, source admission, wire, locks and lifecycle are not executed. Helpers/persona are fixed process-local leaves; receipt input is copied before both builders. No device/Host/product/Steam execution.'}
 (run/'SOURCE-BINDING.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
 result={}
 child_env=os.environ.copy();system_root=child_env.get('SystemRoot',r'C:\Windows');child_env['PATH']=';'.join(str(Path(system_root)/part) for part in ['System32','','System32/Wbem','System32/WindowsPowerShell/v1.0'])
 vc=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
 for profile,flags in [('Release','/O2 /DNDEBUG /MT'),('Checked','/Od /RTC1 /D_DEBUG /MTd')]:
  d=run/profile;d.mkdir();cmd=d/'build.cmd';q=lambda p:'"'+str(p)+'"'
  cmd.write_text('@echo off\r\ncall '+q(vc)+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc '+flags+(' /DYMCC_RESULT_SHAPE_ONLY' if args.shape_only else '')+' '+q(run/'fixture.cpp')+' /I'+q(run)+' /Fo'+q(d/'fixture.obj')+' /Fe'+q(d/'fixture.exe')+'\r\nexit /b %errorlevel%\r\n',encoding='utf-8',newline='')
  p=subprocess.run(['cmd.exe','/d','/c',str(cmd)],cwd=d,capture_output=True,timeout=90,env=child_env);(d/'build.log').write_bytes(p.stdout+p.stderr)
  if p.returncode:raise RuntimeError('Build failed '+str(d))
  p=subprocess.run([str(d/'fixture.exe')],cwd=d,capture_output=True,timeout=150,env=child_env);(d/'result.json').write_bytes(p.stdout);(d/'stderr.log').write_bytes(p.stderr)
  if p.returncode:raise RuntimeError('Fixture failed '+str(d))
  result[profile]=json.loads(p.stdout);print(profile,json.dumps({k:v for k,v in result[profile].items() if k!='batches'}),flush=True)
 after=source_path.read_bytes().decode('utf-8');stable=result_block(after)==bound
 j={'run':str(run),'passed':all(v['passed'] for v in result.values()) and stable,'profiles':result,'testedRegionStable':stable,'wholeSourceChanged':sha(source_path.read_bytes())!=sha(current),'hardwareAcceptance':False,'productionFilesWritten':[]}
 (run/'RESULT.json').write_text(json.dumps(j,indent=2)+'\n',encoding='utf-8');(args.out/'LATEST.json').write_text(json.dumps(j,indent=2)+'\n',encoding='utf-8')
 if not j['passed']:raise SystemExit(1)
if __name__=='__main__':main()
