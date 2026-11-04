"""Actual native request functions with all power/user32/token APIs mocked."""
from pathlib import Path
import argparse,json,hashlib,subprocess,os,sys,time,ast,re
from datetime import datetime,timezone
started_utc=datetime.now(timezone.utc).isoformat();started=time.perf_counter()
sys.stdout.reconfigure(encoding='utf-8')
p=argparse.ArgumentParser();p.add_argument('--output-dir',required=True);p.add_argument('--mutation',choices=['none','downgrade-on-any-error','hibernate-first'],default='none');p.add_argument('--case-filter',choices=['all','timeout-intent','intent-and-late-506'],default='all');args=p.parse_args()
if args.case_filter!='all' and args.mutation!='none':p.error('--case-filter and --mutation cannot both select directed cases')
r=Path(__file__).resolve().parents[1];out=Path(args.output_dir).resolve();out.mkdir(parents=True,exist_ok=True)
js=r'''import fs from 'node:fs';import crypto from 'node:crypto';import {extract} from './tools/fixtures/legion_motion_consumer/extract_main.mjs';
const source=fs.readFileSync('native/main.cpp','utf8');const entries=[extract(source,'SgSystemSleepRequestResult','struct'),...['sgAutomaticClassicSleepPending','sgUserWakeCancelsSleepIntent','sgPrepareSleepPrivilege','sgRequestSystemSleep','sgInternalSleepRequestMatches506','sgSleepRetryActive','sgRetryKindName'].map(n=>extract(source,n))];process.stdout.write(JSON.stringify({entries,sourceSha256:crypto.createHash('sha256').update(source).digest('hex')}));'''
extracted=subprocess.run(['node','--input-type=module','-e',js],cwd=r,capture_output=True,text=True,encoding='utf-8',check=True)
snapshot=json.loads(extracted.stdout);entries=snapshot['entries']
# Reuse the original wake replay's brace-aware reader for the actual PBT branch.
source_bytes=(r/'native/main.cpp').read_bytes();assert hashlib.sha256(source_bytes).hexdigest()==snapshot['sourceSha256'],'source changed during extraction'
reader=r/'tools/sleep_non_user_wake_selftest.py';tree=ast.parse(reader.read_text('utf-8-sig'))
reader_functions=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ['mask_cpp','braced','unique_anchor','body_after']]
assert len(reader_functions)==4
context={'source':source_bytes.decode('utf-8'),'re':re};exec(compile(ast.Module(body=reader_functions,type_ignores=[]),str(reader),'exec'),context)
context['masked']=context['mask_cpp'](context['source']);suspend=context['body_after']('else if (w == PBT_APMSUSPEND)')
start=suspend.index('if (sgSleepRetryActive())');opening=suspend.index('{',start);end=context['braced'](opening,context['mask_cpp'](suspend));confirmation=suspend[start:end]
assert context['source'].count(confirmation)==1
start_offset=context['source'].index(confirmation)
confirmation_entry={'symbol':'PBT_APMSUSPEND/sleep-retry-confirmation','sha256':hashlib.sha256(confirmation.encode()).hexdigest(),'startLine':context['source'][:start_offset].count('\n')+1,'endLine':context['source'][:start_offset+len(confirmation)].count('\n')+1,'code':confirmation}
(out/'production_suspend_confirmation.inc').write_text(confirmation.replace('\r\n','\n').replace('\r','\n')+'\n',encoding='utf-8')
(out/'production_request.inc').write_text('\n\n'.join(x['code'].replace('\r\n','\n').replace('\r','\n') for x in entries)+'\n',encoding='utf-8');(out/'source-extractions.json').write_text(json.dumps(entries+[confirmation_entry],indent=2)+'\n',encoding='utf-8')
header_bytes=(r/'native/sleep_request_fallback.h').read_bytes();original=header_bytes.decode('utf-8-sig').replace('\r\n','\n').replace('\r','\n');header=original
expected=None
if args.mutation=='downgrade-on-any-error':
    old='return code==1 || code==50 || code==120;';assert header.count(old)==1;header=header.replace(old,'return code!=0;');expected='generic modern failure stops downgrade'
elif args.mutation=='hibernate-first':
    old='Mode::ModernStandby,Mode::Sleep,Mode::Hibernate';assert header.count(old)==1;header=header.replace(old,'Mode::Hibernate,Mode::Sleep,Mode::ModernStandby');expected='capability priority matrix'
(out/'sleep_request_fallback.h').write_text(header,encoding='utf-8')
vc=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
js='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
pre=subprocess.run(['node','--input-type=module','-e',js,vc],cwd=r,capture_output=True,text=True,encoding='utf-8',check=True).stdout
exe=out/'selftest.exe';bat=out/'compile.cmd';bat.write_text(pre+f'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /MT /I"{out}" /I"{r / "deps/json"}" /I"{r / "native"}" "{r / "tools/sleep_request_fallback_selftest.cpp"}" /Fo"{out / "selftest.obj"}" /Fe"{exe}"\r\n',encoding='ascii')
compile_started=time.perf_counter()
c=subprocess.run([os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(bat)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'compile.log').write_bytes(c.stdout)
compile_seconds=time.perf_counter()-compile_started
if c.returncode:print(c.stdout.decode('utf-8',errors='replace'));raise SystemExit(c.returncode)
fixture=r/'tools/fixtures/sleep_non_user_wake/g3e_manual_wake_dispatch_race.json'
(out/'archived-manual-wake-input.json').write_bytes(fixture.read_bytes())
run_started=time.perf_counter()
t=subprocess.run([str(exe),str(out/'result.json'),str(fixture),args.mutation,args.case_filter],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace');(out/'run.log').write_text(t.stdout,encoding='utf-8')
run_seconds=time.perf_counter()-run_started
result=json.loads((out/'result.json').read_text('utf-8'))
selected=result['selectedCases']
selection_ok=(selected==[expected] and result['scenarios']==1) if expected is not None else (result['scenarios']==len(selected)==len(set(selected)) and bool(selected))
if args.case_filter=='timeout-intent':selection_ok=selection_ok and len(selected)==16 and all('timeout' in name for name in selected)
if args.case_filter=='intent-and-late-506':selection_ok=selection_ok and len(selected)==97 and all('timeout' in name or name.startswith('internal506 / ') or name.startswith('suspend-confirmation retains internal506 marker / ') for name in selected)
ok=selection_ok and (t.returncode==0) if expected is None else (selection_ok and t.returncode==1 and result.get('failure')==expected and expected in t.stdout)
receipt={'status':'passed' if ok else 'failed','mutation':args.mutation,'caseFilter':args.case_filter,'compileExit':c.returncode,'runExit':t.returncode,'expectedFaultCaught':expected is not None and ok,'expectedFailure':expected,'output':t.stdout.strip(),'productionHeaderSha256':hashlib.sha256(header_bytes).hexdigest(),'functions':[{k:x[k] for k in ['symbol','sha256','startLine','endLine']} for x in entries+[confirmation_entry]],'archivedFixture':{'path':str(fixture.relative_to(r)).replace('\\','/'),'sha256':hashlib.sha256(fixture.read_bytes()).hexdigest()},'sourceInputs':[{'path':name,'sha256':snapshot['sourceSha256'] if name=='native/main.cpp' else hashlib.sha256(header_bytes if name=='native/sleep_request_fallback.h' else (r/name).read_bytes()).hexdigest()} for name in ['native/main.cpp','native/sleep_request_fallback.h','tools/sleep_request_fallback_selftest.py','tools/sleep_request_fallback_selftest.cpp','tools/sleep_non_user_wake_selftest.py','tools/fixtures/sleep_non_user_wake/g3e_manual_wake_dispatch_race.json','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','deps/json/json.hpp']],'realSleepTriggered':False,'physicalSleepConfirmed':False,'osPowerApisMocked':True,'timeoutPolicyPort':'mocked; actual 240-second/shared-budget policy not executed','selectedCases':selected,'scenarios':result['scenarios'],'checks':result['checks'],'mockSuspendCalls':result['mockSuspendCalls'],'monitorCalls':result['monitorCalls'],'windowQueries':result['windowQueries'],'selectionVerified':selection_ok,'executedThisRun':True,'startedUtc':started_utc,'finishedUtc':datetime.now(timezone.utc).isoformat(),'compileSeconds':compile_seconds,'runSeconds':run_seconds,'wallSeconds':time.perf_counter()-started}
(out/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(receipt,ensure_ascii=False,indent=2));raise SystemExit(0 if ok else 1)
