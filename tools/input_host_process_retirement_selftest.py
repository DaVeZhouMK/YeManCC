#!/usr/bin/env python3
"""Execute the actual checked helper, whole Stop/Clear/quarantine and Start fragments.

Protocol/visibility/endpoint leaves are explicit memory ports. Only private test
children/threads/events use Windows. No Host, SDK, devices, deployment or sleep.
Unmodified unsafe consumers are retained as failing parent gaps, never PASS.
"""
from __future__ import annotations
import sys
sys.dont_write_bytecode = True
import argparse
import datetime as dt
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
FIX=ROOT/'tools/fixtures/input_host_process_retirement'
HELPER=ROOT/'tools/input_timer_evidence_adapter_selftest.py'
spec=importlib.util.spec_from_file_location('existing_native_test_tools',HELPER)
helper=importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
def sha(data):return hashlib.sha256(data).hexdigest()
def need(value,message):
    if not value:raise RuntimeError(message)
def save(file,value):file.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def block(text,pattern):
    masked=helper.mask_cpp(text)
    matches=list(re.finditer(pattern,masked,re.M))
    need(len(matches)==1,'Expected unique source block: '+pattern)
    start=matches[0].start();opening=masked.index('{',start);end=opening+1;depth=1
    while depth:
        need(end<len(masked),'Unclosed actual source')
        depth+=(masked[end]=='{')-(masked[end]=='}');end+=1
    return dict(text=text[start:end],line=text.count('\n',0,start)+1,sha256=sha(text[start:end].encode()))
def function(text,name):return block(text,r'^static[^\n;{}]*\b'+name+r'\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{')
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=Path('G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostRetire23')/dt.datetime.now().strftime('%H%M%S-%f'))
    parser.add_argument('--expected-main-sha256')
    parser.add_argument('--before-quarantine',type=Path)
    parser.add_argument('--expected-before-sha256')
    parser.add_argument('--before-start',type=Path)
    parser.add_argument('--expected-before-start-sha256')
    args=parser.parse_args();out=args.out.resolve();need(not out.exists(),'Use a fresh evidence directory');out.mkdir(parents=True)
    files=[Path(__file__).resolve(),HELPER,*sorted(FIX.glob('*')),*[ROOT/'native'/n for n in ('main.cpp','input_host_process_retirement.h','virtual_controller_stop_runtime.h','virtual_controller_start_runtime.h','virtual_controller_runtime_contract.h','virtual_start_protection.h')],ROOT/'deps/json/json.hpp']
    files=[p for p in files if p.is_file()]
    need(bool(args.before_quarantine)==bool(args.expected_before_sha256),'Baseline path and approved SHA are required together')
    if args.before_quarantine:files.append(args.before_quarantine.resolve())
    need(bool(args.before_start)==bool(args.expected_before_start_sha256),'Start baseline and approved SHA are required together')
    if args.before_start:files.append(args.before_start.resolve())
    inputs={p:p.read_bytes() for p in files}
    receipt={'createdAt':dt.datetime.now(dt.timezone.utc).isoformat(),'parentStatus':'INCOMPLETE','localFixVerified':False,'realHostOrSdkExecuted':False,'physicalOrExternalReturnProven':False,'ownedPredicateScope':'owned session cleanup incl physical duty; not an app-exit wait condition','inputs':[{'path':str(p),'sha256':sha(v)} for p,v in inputs.items()]}
    try:
        actualMainSha=sha(inputs[ROOT/'native/main.cpp'])
        receipt['mainSha256']=actualMainSha
        if args.expected_main_sha256:need(actualMainSha==args.expected_main_sha256.lower(),'Main differs from explicitly requested SHA')
        if args.before_quarantine:need(sha(inputs[args.before_quarantine.resolve()])==args.expected_before_sha256.lower(),'Historical baseline identity mismatch')
        if args.before_start:need(sha(inputs[args.before_start.resolve()])==args.expected_before_start_sha256.lower(),'Historical Start identity mismatch')
        source=out/'source';source.mkdir()
        for path,data in inputs.items():
            relative=path.relative_to(ROOT) if path.is_relative_to(ROOT) else Path('external-inputs')/path.name
            dest=source/relative;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
        mainText=inputs[ROOT/'native/main.cpp'].decode('utf-8-sig')
        stopText=inputs[ROOT/'native/virtual_controller_stop_runtime.h'].decode('utf-8-sig')
        startText=inputs[ROOT/'native/virtual_controller_start_runtime.h'].decode('utf-8-sig')
        parts={}
        for name in ('inputHostStopRestoresVisibilityWithoutProof','inputHostStopHoldsPhysicalVisibility','inputHostOwnedCleanupProvenLocked','inputHostDisabledCleanupProvenLocked','inputHostStopLocked'):
            parts[name]=dict(file='native/virtual_controller_stop_runtime.h',**function(stopText,name))
        for name in ('inputHostPrimaryThreadRetirementProvenLocked','inputHostClearLocked','inputHostPipeOnlyReturnAdmittedLocked','inputHostReconcileQuarantine','inputHostReconcileQuarantineOnLifecycleThread'):
            parts[name]=dict(file='native/main.cpp',**function(mainText,name))
        globals=[]
        for name in ('g_inputHostProcess','g_inputHostPrimaryThread','g_inputHostServerPipe','g_inputHostRunId','g_inputHostEpoch'):
            matches=list(re.finditer(r'^static[^\n;{}]*\b'+name+r'\b[^\n;]*;',mainText,re.M))
            need(len(matches)==1,'Expected unique actual ownership global '+name)
            declaration=matches[0].group()
            globals.append(declaration)
            parts['global.'+name]={'file':'native/main.cpp','text':declaration,'line':mainText.count('\n',0,matches[0].start())+1,'sha256':sha(declaration.encode())}
        (source/'tools/fixtures/input_host_process_retirement/actual_host_handle_globals.hpp').write_bytes(('\n'.join(globals)+'\n').encode())
        receipt['primaryThreadScope']={'actualGlobalAndHelperCompiled':True,'defaultOwner':'nullptr in every original case; no primary close attempt is injected','newPrimaryAcceptance':False,'unchangedOriginalCases':34}
        start=function(startText,'inputHostStart')['text']
        first=start.index('    if (g_inputHostProcess) {')
        final=start.index('    if (g_inputHostStartBlocked)',first)
        parts['startOldSessionAndPostGates']={'file':'native/virtual_controller_start_runtime.h','text':start[first:final],'line':startText.count('\n',0,startText.index(start)+first)+1,'sha256':sha(start[first:final].encode())}
        parts['startQuarantineGate']=dict(file='native/virtual_controller_start_runtime.h',**block(start,r'^    if \(g_inputHostReleaseQuarantined\)\s*\{'))
        if args.before_quarantine:
            oldText=inputs[args.before_quarantine.resolve()].decode('utf-8-sig')
            parts['baselineQuarantine']=dict(file=str(args.before_quarantine.resolve()),**function(oldText,'inputHostReconcileQuarantine'))
        if args.before_start:
            beforeStartText=inputs[args.before_start.resolve()].decode('utf-8-sig')
            beforeStart=function(beforeStartText,'inputHostStart')['text']
            begin=beforeStart.index('    if (g_inputHostProcess) {');end=beforeStart.index('    if (g_inputHostStartBlocked)',begin)
            parts['baselineStartOldSessionAndPostGates']={'file':str(args.before_start.resolve()),'text':beforeStart[begin:end],'sha256':sha(beforeStart[begin:end].encode())}
            parts['baselineStartQuarantineGate']=dict(file=str(args.before_start.resolve()),**block(beforeStart,r'^    if \(g_inputHostReleaseQuarantined\)\s*\{'))
        save(out/'source-bindings.json',parts)
        production='\n'.join(parts[n]['text'] for n in ('inputHostPrimaryThreadRetirementProvenLocked','inputHostClearLocked','inputHostPipeOnlyReturnAdmittedLocked','inputHostStopRestoresVisibilityWithoutProof','inputHostStopHoldsPhysicalVisibility','inputHostOwnedCleanupProvenLocked','inputHostDisabledCleanupProvenLocked','inputHostStopLocked','inputHostReconcileQuarantine','inputHostReconcileQuarantineOnLifecycleThread'))
        production+='\nstatic bool startOldSessionConsumer(){\n'+parts['startQuarantineGate']['text']+'\n'+parts['startOldSessionAndPostGates']['text']+'\n++ports.creates;return false;\n}\n'
        if args.before_start:production+='\nstatic bool baselineStartOldSessionConsumer(){\n'+parts['baselineStartQuarantineGate']['text']+'\n'+parts['baselineStartOldSessionAndPostGates']['text']+'\n++ports.creates;return false;\n}\n'
        checkedQuarantine='processRetirement::retire' in parts['inputHostReconcileQuarantine']['text']
        production='#define HISTORICAL_START '+str(int(bool(args.before_start)))+'\n'+production
        production='#define CHECKED_QUARANTINE '+str(int(checkedQuarantine))+'\n#define HISTORICAL_QUARANTINE '+str(int(bool(args.before_quarantine)))+'\n'+production
        if args.before_quarantine:production+='\n'+parts['baselineQuarantine']['text'].replace('inputHostReconcileQuarantine()', 'baselineReconcileQuarantine()',1)+'\n'
        template=inputs[FIX/'suite.cpp.in'].decode('utf-8-sig')
        need(template.count('@@PRODUCTION@@')==1,'production marker')
        fixture=out/'fixture.cpp';fixture.write_bytes(template.replace('@@PRODUCTION@@',production).encode('utf-8'))
        startBegin=production.index('static bool startOldSessionConsumer(){')
        startEnd=production.index('\nstatic bool baselineStartOldSessionConsumer(){',startBegin) if args.before_start else len(production)
        startCurrent=production[startBegin:startEnd]
        need(startCurrent.count('g_inputHostReleaseQuarantined = true;')==2,'Bind both Start failure quarantine enrollments')
        need(startCurrent.count('g_inputHostStartBlocked = true;')==2,'Bind both Start failure rearm gates')
        unquarantinedStart=startCurrent.replace('g_inputHostReleaseQuarantined = true;','/* negative: missing quarantine enrollment */').replace('g_inputHostStartBlocked = true;','/* negative: missing start gate */')
        unquarantinedProduction=production[:startBegin]+unquarantinedStart+production[startEnd:]
        unquarantinedFixture=out/'missing-quarantine.cpp'
        unquarantinedFixture.write_bytes(template.replace('@@PRODUCTION@@',unquarantinedProduction).encode('utf-8'))
        compiler,env=helper.compiler_environment(out)
        receipt['compiler']={'path':str(compiler),'sha256':sha(compiler.read_bytes()),'vcvarsPath':str(helper.VCVARS),'vcvarsSha256':sha(helper.VCVARS.read_bytes()),'version':env.get('VCToolsVersion'),'windowsSDKVersion':env.get('WindowsSDKVersion')}
        receipt['profiles']=[]
        mutation=out/'unchecked-close-mutation';mutation.mkdir()
        originalHeader=inputs[ROOT/'native/input_host_process_retirement.h'].decode('utf-8')
        needle='if (receipt.handleClosed) owner = nullptr;'
        need(originalHeader.count(needle)==1,'Mutation binds the actual checked-close line')
        (mutation/'input_host_process_retirement.h').write_bytes(originalHeader.replace(needle,'owner = nullptr; // explicit historical unchecked-close negative control').encode())
        for profile,flags in [('Release',['/MT','/O2','/DNDEBUG']),('Checked',['/MTd','/Od','/D_DEBUG'])]:
            target=out/profile;target.mkdir();exe=target/'fixture.exe'
            argv=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/permissive-','/W4','/WX','/wd4505',*flags,'/I'+str(source/'native'),'/I'+str(source/'deps/json'),'/I'+str(source/'tools/fixtures/input_host_process_retirement'),fixture,'/Fo'+str(target/'fixture.obj'),'/Fe'+str(exe)]
            save(target/'build-command.json',[str(v) for v in argv])
            built=helper.execute(argv,target,env,target/'build.log',60);need(built.returncode==0,profile+' build failed: '+str(target/'build.log'))
            run=helper.execute([exe],target,env,target/'run.log',45)
            need(run.returncode==0,profile+' execution failed: '+str(target/'run.log'))
            result=json.loads(run.stdout);save(target/'result.json',result)
            imported=helper.execute([compiler.parent/'dumpbin.exe','/imports',exe],target,env,target/'imports.txt',20)
            need(imported.returncode==0,'dumpbin imports')
            need(not any(bad in imported.stdout.lower() for bad in helper.FORBIDDEN),'Unexpected device/UI import')
            negativeExe=target/'unchecked.exe'
            negativeArgs=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/permissive-','/W4','/WX','/wd4505',*flags,'/I'+str(mutation),'/I'+str(source/'native'),'/I'+str(source/'deps/json'),'/I'+str(source/'tools/fixtures/input_host_process_retirement'),fixture,'/Fo'+str(target/'unchecked.obj'),'/Fe'+str(negativeExe)]
            save(target/'unchecked-build-command.json',[str(v) for v in negativeArgs])
            negativeBuild=helper.execute(negativeArgs,target,env,target/'unchecked-build.log',60)
            need(negativeBuild.returncode==0,'unchecked mutation must compile')
            negativeRun=helper.execute([negativeExe],target,env,target/'unchecked-run.log',45)
            negative=json.loads(negativeRun.stdout);save(target/'unchecked-result.json',negative)
            rejected=[row['name'] for row in negative['rows'] if not row['pass']]
            requiredNegativeAssertions={'helper-real-protected-close-failure-then-same-handle-retry','helper-explicit-memory-close-failure','whole-stop-protected-close-retains-tuple-and-allows-independent-restore','parent-checked-helper-interface-resumes-actual-quarantine-clear','whole-stop-explicit-memory-close-failure','actual-main-quarantine-retains-close-failure-and-deduplicates-fault-log'}
            need(negativeRun.returncode!=0 and requiredNegativeAssertions.issubset(rejected),'All six existing unchecked-close negative assertions must remain rejected')
            quarantineExe=target/'missing-quarantine.exe'
            quarantineArgs=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/permissive-','/W4','/WX','/wd4505',*flags,'/I'+str(source/'native'),'/I'+str(source/'deps/json'),'/I'+str(source/'tools/fixtures/input_host_process_retirement'),unquarantinedFixture,'/Fo'+str(target/'missing-quarantine.obj'),'/Fe'+str(quarantineExe)]
            save(target/'missing-quarantine-build-command.json',[str(v) for v in quarantineArgs])
            quarantineBuild=helper.execute(quarantineArgs,target,env,target/'missing-quarantine-build.log',60)
            need(quarantineBuild.returncode==0,'Missing-quarantine mutation must compile')
            quarantineRun=helper.execute([quarantineExe],target,env,target/'missing-quarantine-run.log',45)
            quarantineResult=json.loads(quarantineRun.stdout);save(target/'missing-quarantine-result.json',quarantineResult)
            quarantineRejected=[row['name'] for row in quarantineResult['rows'] if not row['pass']]
            requiredQuarantineFailures={'actual-start-wake-protected-close-failure-retains-owner-then-retries-same-handle','actual-start-wake-live-wait-failed-retains-owner'}
            need(quarantineRun.returncode!=0 and requiredQuarantineFailures.issubset(quarantineRejected),'Actual Start must reject missing quarantine on both Close/Wait failures')
            receipt['profiles'].append({'name':profile,'exeSha256':sha(exe.read_bytes()),'result':result,'uncheckedCloseNegativeControl':{'rejected':True,'exitCode':negativeRun.returncode,'failedAssertions':rejected},'missingQuarantineNegativeControl':{'rejected':True,'exitCode':quarantineRun.returncode,'failedAssertions':quarantineRejected}})
        receipt['historicalStartExecuted']=bool(args.before_start)
        receipt['historicalBaselineExecuted']=bool(args.before_quarantine)
        receipt['checkedMainQuarantineExecuted']=checkedQuarantine
        receipt['sourceStable']=all(p.read_bytes()==data for p,data in inputs.items())
        receipt['localFixVerified']=receipt['sourceStable'] and all(p['result']['localFailures']==0 for p in receipt['profiles'])
        receipt['scope']='Primary actual global/helper compiled with null owner in original cases; no new primary acceptance. Actual pipe-only helper consumes original memory fact ports. Actual checked helper; verbatim full StopLocked, ClearLocked, DisabledCleanup, quarantine probe/clear; exact old-session Start/gates. Protocol, MSI/PS/visibility/return ledger and budgets use explicit memory ports. Private real kernel process/thread/event; native protected-close and no-SYNCHRONIZE wait errors; explicit OS-memory failures. No full Start/Host/SDK/physical lifecycle acceptance.'
        receipt['parentGapsRemain']=any(p['result']['unmodifiedConsumerGaps'] for p in receipt['profiles'])
        need(receipt['localFixVerified'],'Local contract assertions failed')
    except Exception as exc:
        receipt['error']=str(exc);save(out/'result.json',receipt);raise
    save(out/'result.json',receipt)
    print(json.dumps({'out':str(out),'localFixVerified':receipt['localFixVerified'],'parentStatus':receipt['parentStatus'],'parentGapsRemain':receipt['parentGapsRemain'],'profiles':[{'name':p['name'],'rows':len(p['result']['rows']),'gaps':p['result']['unmodifiedConsumerGaps']} for p in receipt['profiles']]},ensure_ascii=False))
if __name__=='__main__':main()
