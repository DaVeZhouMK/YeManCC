"""Actual native request functions with all power/user32/token APIs mocked."""
from pathlib import Path
import argparse,json,hashlib,subprocess,os,sys
sys.stdout.reconfigure(encoding='utf-8')
p=argparse.ArgumentParser();p.add_argument('--output-dir',required=True);p.add_argument('--mutation',choices=['none','downgrade-on-any-error','hibernate-first'],default='none');args=p.parse_args()
r=Path(__file__).resolve().parents[1];out=Path(args.output_dir).resolve();out.mkdir(parents=True,exist_ok=True)
js=r'''import fs from 'node:fs';import {extract} from './tools/fixtures/legion_motion_consumer/extract_main.mjs';
const source=fs.readFileSync('native/main.cpp','utf8');const entries=[extract(source,'SgSystemSleepRequestResult','struct'),...['sgAutomaticClassicSleepPending','sgUserWakeCancelsSleepIntent','sgPrepareSleepPrivilege','sgRequestSystemSleep'].map(n=>extract(source,n))];process.stdout.write(JSON.stringify(entries));'''
extracted=subprocess.run(['node','--input-type=module','-e',js],cwd=r,capture_output=True,text=True,encoding='utf-8',check=True)
entries=json.loads(extracted.stdout);(out/'production_request.inc').write_text('\n\n'.join(x['code'].replace('\r\n','\n').replace('\r','\n') for x in entries)+'\n',encoding='utf-8');(out/'source-extractions.json').write_text(json.dumps(entries,indent=2)+'\n',encoding='utf-8')
original=(r/'native/sleep_request_fallback.h').read_text('utf-8-sig');header=original
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
c=subprocess.run([os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(bat)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'compile.log').write_bytes(c.stdout)
if c.returncode:print(c.stdout.decode('utf-8',errors='replace'));raise SystemExit(c.returncode)
fixture=r/'tools/fixtures/sleep_non_user_wake/g3e_manual_wake_dispatch_race.json'
(out/'archived-manual-wake-input.json').write_bytes(fixture.read_bytes())
t=subprocess.run([str(exe),str(out/'result.json'),str(fixture)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace');(out/'run.log').write_text(t.stdout,encoding='utf-8')
ok=(t.returncode==0) if expected is None else (t.returncode==1 and expected in t.stdout)
receipt={'status':'passed' if ok else 'failed','mutation':args.mutation,'compileExit':c.returncode,'runExit':t.returncode,'expectedFaultCaught':expected is not None and ok,'expectedFailure':expected,'output':t.stdout.strip(),'productionHeaderSha256':hashlib.sha256((r/'native/sleep_request_fallback.h').read_bytes()).hexdigest(),'functions':[{k:x[k] for k in ['symbol','sha256','startLine','endLine']} for x in entries],'archivedFixture':{'path':str(fixture.relative_to(r)).replace('\\','/'),'sha256':hashlib.sha256(fixture.read_bytes()).hexdigest()},'sourceInputs':[{'path':name,'sha256':hashlib.sha256((r/name).read_bytes()).hexdigest()} for name in ['native/main.cpp','native/sleep_request_fallback.h','tools/sleep_request_fallback_selftest.py','tools/sleep_request_fallback_selftest.cpp','tools/fixtures/sleep_non_user_wake/g3e_manual_wake_dispatch_race.json','tools/fixtures/hc_reviewed_calibration/toolchain.mjs']],'realSleepTriggered':False}
(out/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(receipt,ensure_ascii=False,indent=2));raise SystemExit(0 if ok else 1)
