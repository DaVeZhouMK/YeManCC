"""Source-bound steady-state controller snapshot broadcast test. No actual WebView/Host/device."""
from pathlib import Path
import argparse,datetime,hashlib,importlib.util,json,os,subprocess,sys
sys.dont_write_bytecode=True
sys.stdout.reconfigure(encoding='utf-8')
REPO=Path(__file__).resolve().parents[1]
_spec=importlib.util.spec_from_file_location('gyro_source_extract',REPO/'tools/virtual_gyro_envelope_perf_selftest.py')
_extract=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_extract)
sha=lambda b:hashlib.sha256(b).hexdigest()
PATTERNS={
 'resolver':rb'^static ComPtr<ICoreWebView2> ipc_view_for_child\(int childId\) \{',
 'post':rb'^static void ipc_post_json\(int childId, const json& message\) \{',
 'generic':rb'^static void ipc_emit\(const std::string& ev, const json& data = \{\}\) \{',
 'gyro':rb'^static void ipc_emit_owned_gyro_telemetry\(std::string ev, json data\) \{',
 'utf16':rb'^static std::wstring U2W\(const std::string& s\) \{',
 'helper':rb'^static void ipc_broadcast_controller_snapshot\(const json& message\) \{',
}
HELPER=r'''
// The wire belongs to this immutable controller snapshot only. Resolve each
// renderer at the original delivery point; cache only successful conversion.
static void ipc_broadcast_controller_snapshot(const json& message) {
    std::optional<std::wstring> wire;
    bool wireReady = false;
    const auto post = [&](int childId) {
        auto target = ipc_view_for_child(childId);
        if (!target) return;
        if (!wireReady) {
            const std::string utf8 = message.dump();
            if (utf8.empty()) {
                wire.emplace();
                wireReady = true;
            } else {
                const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
                auto& text = wire.emplace(n, L'\0');
                const int written = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), text.data(), n);
                wireReady = n > 0 && written == n;
            }
        }
        target->PostWebMessageAsJson(wire->c_str());
    };
    post(0);
    for (const auto& [id, cw] : g_children) {
        if (cw && cw->view) post(id);
    }
}
'''
PLAN={'candidateVariant':'V2-inplace-optional-success-only','setBeforeMeasurement':True,'pairs':11,'iterationsPerArm':128,'warmupIterations':16,
 'profiles':['Release','Checked'],'events':['gamepad.state','gyro.telemetry','other.snapshot'],
 'shapes':['normal','wide-unicode'],'targets':[1,2,4],'logging':[False,True],
 'multitargetControllerRequirements':{'elapsedPairedMedianReductionAtLeast':0.05,'elapsedFasterPairsAtLeast':9,'threadCyclePairedMedianReductionAtLeast':0.05,'allocationsStrictlyDecrease':True},
 'singleTargetAndOtherEventControls':{'maximumMedianRegressionFraction':0.05,'orMaximumAbsoluteRegressionNs':100,'allocationRequestsAndBytesMustNotGrow':True},
 'noRetimingAfterFailure':True,'scope':'Complete source-extracted event emitters and actual JSON/UTF16 conversion. WebView delivery, diagnostic clock/sinks and current power source are inert boundaries. Input capture, Host and devices are not executed.'}
def extract(raw,name):return _extract.extract(raw,PATTERNS[name])
def normalize(b):return b.replace(b'\r\n',b'\n').replace(b'\r',b'')
def change_loop(body,new):
 text=normalize(body).decode('utf-8')
 old='    ipc_post_json(0, m);\n    for (const auto& [id, cw] : g_children) {\n        if (cw && cw->view) ipc_post_json(id, m);\n    }'
 assert text.count(old)==1,'broadcast-loop binding changed'
 return text.replace(old,new,1).encode('utf-8')
def candidate_fragments(raw):
 result={k:extract(raw,k)['bytes'] for k in PATTERNS if k!='helper'}
 result['generic']=change_loop(result['generic'],'    if (ev == "gamepad.state") {\n        ipc_broadcast_controller_snapshot(m);\n    } else {\n        ipc_post_json(0, m);\n        for (const auto& [id, cw] : g_children) {\n            if (cw && cw->view) ipc_post_json(id, m);\n        }\n    }')
 result['gyro']=change_loop(result['gyro'],'    ipc_broadcast_controller_snapshot(m);')
 result['helper']=HELPER.encode('utf-8')
 return result
def header(parts,name,stale=False,observed=True,brokenRetry=False):
 chunks=[]
 for k in ['utf16','resolver','post','helper','generic','gyro']:
  if k not in parts:continue
  body=normalize(parts[k]).decode('utf-8')
  if observed and k=='resolver':body=body.replace('{','{\n    if (observing) resolutions.push_back(childId);',1)
  if observed:
   body=body.replace('message.dump()','delivery_dump(message)').replace('MultiByteToWideChar(', 'observedConvert(')
   if k=='helper':body=body.replace('if (!wireReady) {','if (!wireReady) {\n            if (observing) ++conversions;',1)
  if observed and k=='utf16':body=body.replace('{','{\n    if (observing) ++conversions;',1)
  if k=='helper' and stale:body=body.replace('    std::optional<std::wstring> wire;','    static std::optional<std::wstring> wire;').replace('    bool wireReady = false;','    static bool wireReady = false;')
  if k=='helper' and brokenRetry:body=body.replace('wireReady = n > 0 && written == n;','wireReady = true; (void)written;')
  chunks.append(body)
 return ('namespace '+name+' {\n#include "fixture_state.inc"\n'+'\n'.join(chunks)+'\n}\n').encode('utf-8')
def run(args,cwd,out):
 p=subprocess.run(args,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=240)
 out.write_bytes(p.stdout)
 if p.returncode:raise RuntimeError(str(out)+' exit '+str(p.returncode))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--baseline',type=Path);ap.add_argument('--correctness-only',action='store_true');ap.add_argument('--negative-stale-cache',action='store_true');ap.add_argument('--negative-conversion-cache',action='store_true');args=ap.parse_args()
 path=REPO/'native/main.cpp';current=path.read_bytes();baseline=args.baseline.read_bytes() if args.baseline else current
 original={k:extract(baseline,k)['bytes'] for k in PATTERNS if k!='helper'}
 desired=candidate_fragments(baseline)
 if args.baseline:
  actual={k:extract(current,k)['bytes'] for k in PATTERNS}
  for k in desired:
   expected=extract(desired[k],'helper')['bytes'] if k=='helper' else desired[k]
   assert normalize(actual[k]).strip()==normalize(expected).strip(),('current candidate mismatch',k)
  desired=actual
 else:assert b'static void ipc_broadcast_controller_snapshot(' not in current,'use --baseline for final current-source verification'
 stamp=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).strftime('%Y%m%d-%H%M%S-%f');out=args.out/stamp;out.mkdir(parents=True,exist_ok=False)
 (out/'acceptance-plan.json').write_text(json.dumps(PLAN,indent=2)+'\n',encoding='utf-8')
 (out/'baseline-main.cpp').write_bytes(baseline)
 (out/'baseline.inc').write_bytes(header(original,'baseline'))
 (out/'candidate.inc').write_bytes(header(desired,'candidate',args.negative_stale_cache,brokenRetry=args.negative_conversion_cache))
 (out/'measured.inc').write_bytes(header(original,'baseline_measured',observed=False)+header(desired,'candidate_measured',args.negative_stale_cache,observed=False,brokenRetry=args.negative_conversion_cache))
 fixture=REPO/'tools/controller_snapshot_wire_selftest.cpp';state=REPO/'tools/controller_snapshot_wire_fixture.inc'
 (out/'fixture.cpp').write_bytes(fixture.read_bytes());(out/'fixture_state.inc').write_bytes(state.read_bytes())
 manifest={'baselineMainSha256':sha(baseline),'currentMainSha256':sha(current),'phase':'final-correctness' if args.baseline else 'unapplied-candidate','fragments':{k:{'baseline':sha(original[k]) if k in original else None,'candidate':sha(v)} for k,v in desired.items()},'sourcePath':str(path),'jsonHeaderSha256':sha((REPO/'deps/json/json.hpp').read_bytes()),'runnerSha256':sha(Path(__file__).read_bytes()),'fixtureSha256':sha(fixture.read_bytes()),'boundarySha256':sha(state.read_bytes()),'negativeStaleCache':args.negative_stale_cache,'negativeConversionCache':args.negative_conversion_cache,'productionWritten':False,'performanceRun':not args.correctness_only}
 (out/'source-binding.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
 vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
 env=os.environ.copy();env['PATH']=r'C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0';env['VSCMD_SKIP_SENDTELEMETRY']='1'
 results={}
 for profile in ['Release','Checked']:
  exe=out/(profile+'.exe');flags='/O2 /DNDEBUG /MT' if profile=='Release' else '/Od /D_DEBUG /MTd /Zi'
  cmd='@echo off\ncall "'+str(vc)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\ncl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN '+flags+' /I"'+str(REPO/'deps/json')+'" "'+str(out/'fixture.cpp')+'" /Fe:"'+str(exe)+'" /Fo:"'+str(out/(profile+'.obj'))+'"\n'
  bat=out/('build-'+profile+'.cmd');bat.write_text(cmd,encoding='utf-8')
  p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=240);(out/('compile-'+profile+'.log')).write_bytes(p.stdout)
  if p.returncode:raise RuntimeError('compile failed '+str(out/('compile-'+profile+'.log')))
  call=[str(exe),str(out/(profile+'-result.json'))]
  if args.correctness_only:call+=['--correctness-only']
  if args.negative_stale_cache:call+=['--expect-negative']
  if args.negative_conversion_cache:call+=['--expect-conversion-negative']
  run(call,out,out/('run-'+profile+'.log'))
  results[profile]=json.loads((out/(profile+'-result.json')).read_text(encoding='utf-8'))
 after=path.read_bytes();now={k:extract(after,k)['bytes'] for k in (PATTERNS if args.baseline else original)}
 manifest['relevantLiveFragmentsStableAtEnd']=all(normalize(v)==normalize((desired if args.baseline else original)[k]) for k,v in now.items())
 manifest['wholeMainStableAtEnd']=sha(after)==sha(current)
 summary={'run':str(out),'manifest':manifest,'profiles':results,'correctnessPassed':all(x['correctnessPassed'] for x in results.values()),'performanceAccepted':None if args.correctness_only else all(x['performanceAccepted'] for x in results.values()),'productionEdited':False}
 (out/'RESULT.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8');args.out.mkdir(parents=True,exist_ok=True);(args.out/'LATEST.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'run':str(out),'correctnessPassed':summary['correctnessPassed'],'performanceAccepted':summary['performanceAccepted'],'fragmentsStable':manifest['relevantLiveFragmentsStableAtEnd']}))
if __name__=='__main__':main()