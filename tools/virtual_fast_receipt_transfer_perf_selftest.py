#!/usr/bin/env python3
"""R31 source-bound final FastReceipt copy-to-move fixture; never edits production.

Default prepares immutable source/ZIP snapshots and MSVC Release/Checked
correctness only. Benchmark mode requires a separate explicit parent receipt,
checks immutable bindings, and permanently claims the only R31 formal attempt.
"""
from __future__ import annotations
import argparse
import datetime as dt
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid
import zipfile
sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
OUTPUT = REPO.parents[2] / 'Build/Validation/VirtualInputPerf-R31-20261015/transfer-worker'
EXPECTED_MAIN = '007f7702cdf2949503b8739a382bed5e96e733250da36917119ce22241f05dc6'
R28B_REFERENCE = Path('G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R29-20261015/lookup-worker/prepare-20261015T023215279493Z-03d17c/native-main.snapshot.cpp')
EXPECTED_HOST = '5c4de79ae9125ad12ef24409902451047de16bbc23dd5e7748d83e3b35ede3a6'
ARCHIVE = Path('C:/Users/DaVe/Desktop/日志-侧边栏全开后/YeManCC-logs-20261014-160313--G3E虚拟手柄OK.zip')
ARCHIVE_SHA = 'e886bcacb06e9250976235f4d1c0dae28c8f8e46902e7a1210f148c4cc01a479'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PLAN = {
 'schemaVersion':1, 'candidate':'R31-final-FastReceipt-owning-transfer-only',
 'profiles':['Release','Checked'], 'campaigns':2, 'pairedRounds':11,
 'minimumFasterRounds':10, 'minimumMedianPairedReduction':.05,
 'minimumRatioOfMediansReduction':.05,
 'allocationRule':'No increase in ordinary/aligned request counts or requested bytes on any affected or control row.',
 'productionScenarios':['archive-first-32hex','archive-last-32hex','archive-six-hosts-32hex'],
 'controlScenarios':['sso-8byte-control','stress-4096byte-control'],
 'controlRule':'Both median paired and ratio-of-medians reduction must be >= -5%; no minimum faster-round requirement on explicit controls.',
 'scopes':['actual-full-scanner-and-fast-branch-transfer','actual-transfer-and-local-disposal'],
 'batchSize':64,'measurementBatches':128,'warmupBatches':6,'allocationBatches':4,
 'profileOrder':['Release','Checked','Checked','Release'],
 'pairOrder':'Alternate baseline first by round plus campaign.',
 'requiredRows':24,'controlRows':16,'totalRows':40,
 'frames':'Reconstructed actual HostResponseFields serializer order and types, 32 fields; observed host IDs/run IDs/epoch/command sequence/persona/power; unobserved requestId/nonce/configRevision/firstFrame/submit time/PnP state explicitly supplied by fixture. Archive rows are observations, not original wire ACKs.',
 'timingBoundary':'Actual extracted production scanner, full successful fast branch, shared after-accept helper and final transfer after transport, or actual final transfer plus local source disposal. Context/input construction, prepared source allocation, result verification, reporting and allocation instrumentation excluded from timed windows. Production destinations freshly default constructed as in actual caller; reuse/prefill covered by correctness.',
 'diagnostics':'Diagnostic/observation enabled paths and failure order verified only; timing uses production gates off and null observation.',
 'exceptionBoundary':'Move is noexcept for actual struct/standard string allocator. Earlier errors preserve output and side-effect order; old final-copy bad_alloc is intentionally eliminated, not claimed exception-equivalent.',
 'decision':'Every affected row must pass every gate in both profiles and campaigns, and every explicit control must meet its nonregression/allocation gates. No relaxation, retuning or retiming.',
 'permission':'Correctness first; no benchmark until separate parent permission binds exact source and prepared run.',
 'productionEdits':False,'wholeHardwareClaim':False,
}


def sha(value):
 return hashlib.sha256(value.encode('utf-8') if isinstance(value,str) else value).hexdigest()


def write(path,value,exclusive=False):
 path=Path(path).resolve()
 if not path.is_relative_to(OUTPUT.resolve()): raise RuntimeError(f'write outside R31 scope: {path}')
 path.parent.mkdir(parents=True,exist_ok=True)
 with path.open('xb' if exclusive else 'wb') as stream: stream.write(value.encode('utf-8') if isinstance(value,str) else value)


def save(path,value,exclusive=False): write(path,json.dumps(value,ensure_ascii=False,indent=2)+'\n',exclusive)


def masked(text):
 pattern=r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/'
 return re.sub(pattern,lambda m:''.join('\n' if c=='\n' else ' ' for c in m.group()),text)


def closing(text,opening):
 clean=masked(text);depth=0
 for pos in range(opening,len(text)):
  if clean[pos]=='{':depth+=1
  elif clean[pos]=='}':
   depth-=1
   if not depth:return pos
 raise RuntimeError('unterminated source block')


def function(text,marker):
 results=[]
 for match in re.finditer(re.escape(marker),text):
  start=match.start();opening=text.find('{',start);semi=text.find(';',start)
  if opening>=0 and (semi<0 or opening<semi):results.append(text[start:closing(text,opening)+1])
 if len(results)!=1:raise RuntimeError(f'expected unique source definition: {marker}, got {len(results)}')
 return results[0]


def declaration(text,name):
 hits=re.findall(r'^static [^\r\n;]*\b'+re.escape(name)+r'\b[^;]*;',text,re.M)
 if len(hits)!=1:raise RuntimeError('ambiguous source declaration: '+name)
 return hits[0]


def extract(source,host,fields_source):
 fn=function(source,'static bool inputHostRequestPreparedLocked(')
 caller=function(source,'static json inputHostSubmitPad(')
 original='if (fastOut) *fastOut = fastReceipt;'
 candidate='if (fastOut) *fastOut = std::move(fastReceipt);'
 if fn.count(original)!=1 or source.count(original)!=1:raise RuntimeError('final copy is not unique')
 a=fn.index('    if (fastOut && std::strcmp(command, "SUBMIT_FRAME") == 0) {')
 branch=fn[a:closing(fn,fn.index('{',a))+1]
 if not re.search(re.escape(original)+r'\s*return true;\s*}\s*}$',branch):raise RuntimeError('final transfer is not immediately followed by return and local-scope disposal')
 if branch.count('InputHostFastReceipt fastReceipt;')!=1:raise RuntimeError('local source receipt ownership changed')
 if not (branch.index('verifyAgainstDom')<branch.index('inputHostAfterSubmitFrameAccepted')<branch.index('observation->hostInstanceId = fastReceipt.hostInstanceId;')<branch.index('if (advanceSequence)')<branch.index(original)):
  raise RuntimeError('observed side-effect/transfer order changed')
 if caller.count('InputHostFastReceipt fastReceipt;')!=1 or '&frameSequence, &fastReceipt)' not in caller:
  raise RuntimeError('actual caller no longer passes its distinct automatic destination')
 if 'chainAck.parsedFastHostInstanceId = &fastReceipt.hostInstanceId;' not in caller:raise RuntimeError('caller downstream ownership consumer changed')
 prefix_start=fn.index('    struct ObservationPublisher {')
 prefix_end=fn.index('    // 字段提取与判定',prefix_start)
 publisher=fn[prefix_start:prefix_end]
 start=source.index('namespace c7aReceiptFast {');namespace=source[start:closing(source,source.index('{',start))+1]
 rs=source.index('struct InputHostFastReceipt {');receipt=source[rs:closing(source,source.index('{',rs))+2]
 names=['g_powerGeneration','g_inputHostPersonaMx','g_inputHostBuildPersona','g_inputHostBoundPersona',
  'g_inputHostHostInstanceId','g_inputHostRunId','g_inputHostStartupNonce','g_inputHostEpoch',
  'g_inputHostBoundPowerGeneration','g_inputHostConfigRevision','g_inputHostSequence','g_inputHostTupleHealPending',
  'g_inputHostLastSubmitElapsedUs','kInputHostProtocolConfigHash','kInputHostTargetId',
  'kInputHostOwnerIdentity','kInputHostLockedCoreSha256','kInputHostLockedCoreMvid']
 decls={name:declaration(source,name) for name in names}
 helpers={m.split('(')[0].split()[-1]:function(source,m) for m in [
  'static std::string inputHostConfiguredPersona(','static const char* inputHostPersona(',
  'static const char* inputHostProfileIdentityOf(','static const char* inputHostProfileIdentity(',
  'static const char* inputHostBoundOrCurrentPersona(','static const char* inputHostBoundOrCurrentProfileIdentity(',
  'static unsigned long long inputHostProtocolPowerGenerationLocked(']}
 config=re.findall(r'^\s*std::string virtualTargetPersona[^;]*;',source,re.M)
 if len(config)!=1:raise RuntimeError('settings member type/default ambiguous')
 tick_start=source.index('enum TickCostStage {');tick=source[tick_start:closing(source,source.index('{',tick_start))+2]
 after_helper=function(source,'static void inputHostAfterSubmitFrameAccepted(')
 guid=re.findall(r'^\s*private readonly string _hostInstanceId = Guid.NewGuid\(\).ToString\("N"\);',host,re.M)
 if len(guid)!=1:raise RuntimeError('Host no longer generates verified 32hex Guid N identity')
 order=list(dict.fromkeys(re.findall(r'writer\.Write(?:Number|Boolean|String|Null)\(n_(\w+)',fields_source)))
 if len(order)!=32:raise RuntimeError('actual HostResponseFields serializer field count changed')
 oracle=function(namespace,'void verifyAgainstDom(')
 return {'function':fn,'caller':caller,'branch':branch,'publisher':publisher,'namespace':namespace,
  'receipt':receipt,'declarations':decls,'helpers':helpers,'configMember':config[0].strip(),
  'tickEnum':tick,'afterHelper':after_helper,'originalTransfer':original,'candidateTransfer':candidate,
  'hostGuidDeclaration':guid[0].strip(),'fieldOrder':order,'oracle':oracle,
  'nativeIdGenerator':function(source,'static std::string inputHostNewId(')}


def header(parts):
 p=parts
 context='\n'.join(p['declarations'].values())+'\n'
 context+='struct FixtureSettings { '+p['configMember']+' };\nstatic FixtureSettings g_realStickTest;\n'
 context+='\n\n'.join(p['helpers'].values())+'\n'+p['receipt']+'\n'+p['tickEnum']+'\n'
 context+='''static bool tickCostEnabled();
static void tickCostAccumulate(TickCostStage,unsigned long long);
static unsigned long long tickCostElapsedUs(std::chrono::steady_clock::time_point);
static void appendNativeLifecycleLog(const char*,const json&);
static void r31TracePoint(const char*,const InputHostFastReceipt&);
'''
 context+=p['afterHelper']+'\n'
 context+='static const char* const r31WireFieldOrder[]={'+','.join(json.dumps(n) for n in p['fieldOrder'])+'};\n'
 for name in ['baselineScanner','candidateScanner']:
  renamed=p['namespace'].replace('namespace c7aReceiptFast {','namespace '+name+' {',1)
  assert renamed.replace('namespace '+name+' {','namespace c7aReceiptFast {',1)==p['namespace']
  context+=renamed+'\n'
 oracle=p['oracle'];body=oracle[oracle.index('    json response ='):oracle.index('    const bool agree =')]
 context+='''struct DomResult { bool accepted,firstFrame; unsigned long long submitUs; std::string host; bool discarded; };
static DomResult actualDomOracle(const char* begin,const char* end,const char* expectedStatus,
 const std::string& requestIdValue,unsigned long long sequenceValue) {
'''+body+'    return {domAccept,domFirstFrame,domSubmitUs,domHostInstanceId,response.is_discarded()};\n}\n'
 for after in [False,True]:
  variant='candidate' if after else 'baseline';ns='candidateScanner' if after else 'baselineScanner'
  branch=p['branch'].replace('c7aReceiptFast::',ns+'::')
  if after:branch=branch.replace(p['originalTransfer'],p['candidateTransfer'],1)
  restored=branch.replace(ns+'::','c7aReceiptFast::')
  if after:restored=restored.replace(p['candidateTransfer'],p['originalTransfer'],1)
  assert restored==p['branch']
  for traced in [False,True]:
   content=branch
   if traced:
    content=content.replace('InputHostFastReceipt fastReceipt;', 'InputHostFastReceipt fastReceipt;\n        r31TracePoint("scanner-enter",fastReceipt);',1)
    content=content.replace('fastReceipt.used = true;','fastReceipt.used = true;\n            r31TracePoint("accepted",fastReceipt);',1)
    content=content.replace(ns+'::verifyAgainstDom(', 'r31TracePoint("parity",fastReceipt);\n                    '+ns+'::verifyAgainstDom(',1)
    content=content.replace('if (auto* observation = receiptObservation.get()) {','if (auto* observation = receiptObservation.get()) {\n                r31TracePoint("observation",fastReceipt);',1)
    expression=p['candidateTransfer'] if after else p['originalTransfer']
    content=content.replace(expression,'r31TracePoint("transfer",fastReceipt);\n            '+expression,1)
   context+='''__declspec(noinline) static bool '''+variant+('Traced' if traced else 'Fast')+'''(
 const std::string& responseBuffer,const char* command,const char* expectedStatus,
 const std::string& requestIdValue,unsigned long long sequenceValue,InputHostFastReceipt* fastOut,
 bool receiptCostOn,bool advanceSequence,inputHostSelfTestRules::ReceiptObservation* receiptObservationOut) {
 const std::size_t responseBytes=responseBuffer.size();
 const auto receiptValidateStarted=std::chrono::steady_clock::time_point{};
 const unsigned long long writeElapsedUs=11,readElapsedUs=13;
'''+p['publisher']+content+'\n    return false; // Exact fast region fell through to the unchanged production DOM path.\n}\n'
  context+='__declspec(noinline) static void '+variant+'Transfer(InputHostFastReceipt& fastReceipt,InputHostFastReceipt* fastOut) {\n    '+(p['candidateTransfer'] if after else p['originalTransfer'])+'\n}\n'
 return context


def corpus(run):
 raw=ARCHIVE.read_bytes()
 if sha(raw)!=ARCHIVE_SHA:raise RuntimeError('authorized archive SHA256 mismatch')
 rows=[]
 with zipfile.ZipFile(ARCHIVE) as archive:
  for name in ['virtual-gamepad.log.1','virtual-gamepad.log','native-lifecycle.log.1','native-lifecycle.log']:
   if name not in archive.namelist():continue
   for line_number,line in enumerate(archive.read(name).splitlines(),1):
    try:row=json.loads(line)
    except (ValueError,UnicodeDecodeError):continue
    proof=row.get('frameProof');receipt=proof.get('hostReceipt') if isinstance(proof,dict) else None
    if isinstance(receipt,dict):
     if not re.fullmatch('[0-9a-f]{32}',receipt.get('hostInstanceId','')):raise RuntimeError('observed host identity is not source Guid-N shape')
     rows.append({'file':name,'line':line_number,'rawLineSha256':sha(line),'receipt':receipt,'powerGeneration':row.get('powerGeneration')})
 if len(rows)!=918:raise RuntimeError('expected authorized 918 observed receipts')
 identities=sorted({r['receipt']['hostInstanceId'] for r in rows})
 if len(identities)!=6:raise RuntimeError('expected six observed Host identities')
 save(run/'receipts.json',rows)
 save(run/'provenance.json',{'archive':str(ARCHIVE),'archiveSha256':ARCHIVE_SHA,'observations':918,
  'uniqueHostIds':identities,'hostIdLength':32,'scope':PLAN['frames'],
  'notOriginalWireAck':True,'notFreshHardwareEvidence':True})
 return rows


def run_process(args,run,stem,timeout=300):
 result=subprocess.run(args,cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=timeout)
 write(run/(stem+'.stdout.log'),result.stdout);write(run/(stem+'.stderr.log'),result.stderr)
 save(run/(stem+'-process.json'),{'returnCode':result.returncode,'args':args,'fixtureOnly':True})
 return result


def build(run,profile):
 flags=['/O2','/DNDEBUG','/MT'] if profile=='Release' else ['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']
 exe=run/(profile+'.exe')
 args=['cl','/nologo','/Bv','/std:c++20','/utf-8','/EHsc','/W4','/fp:precise',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(exe)]
 batch=run/(profile+'-build.bat')
 write(batch,'@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\n')
 result=run_process(['cmd.exe','/d','/c',str(batch)],run,profile+'-build')
 if result.returncode:raise RuntimeError('MSVC '+profile+' build failed; logs retained in '+str(run))
 return exe


def binding_map(run):
 return {str(p.relative_to(run)).replace('\\','/'):sha(p.read_bytes()) for p in sorted(run.rglob('*')) if p.is_file() and p.name!='manifest.json'}


def verify_bindings(run,manifest):
 for name,digest in manifest['artifacts'].items():
  path=(run/name).resolve()
  if not path.is_relative_to(run.resolve()) or sha(path.read_bytes())!=digest:raise RuntimeError('prepared artifact changed: '+name)


def prepare():
 OUTPUT.mkdir(parents=True,exist_ok=True);plan_path=OUTPUT/'PREDECLARED-PLAN.json'
 if plan_path.exists():
  if json.loads(plan_path.read_text(encoding='utf-8'))!=PLAN:raise RuntimeError('frozen R31 plan changed')
 else:save(plan_path,PLAN,True)
 run=OUTPUT/('prepare-'+dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')+'-'+uuid.uuid4().hex[:6]);run.mkdir()
 paths={'native-main.snapshot.cpp':REPO/'native/main.cpp','host-program.snapshot.cs':REPO/'InputHost/Program.cs',
        'host-response-fields.snapshot.cs':REPO/'InputHost/HostResponseFields.cs','json.hpp':REPO/'deps/json/json.hpp',
        'input_host_selftest_rules.h':REPO/'native/input_host_selftest_rules.h'}
 originals={name:path.read_bytes() for name,path in paths.items()}
 if sha(originals['native-main.snapshot.cpp'])!=EXPECTED_MAIN or sha(originals['host-program.snapshot.cs'])!=EXPECTED_HOST:
  raise RuntimeError('actual source does not match parent R28B Native/Host baseline')
 source=originals['native-main.snapshot.cpp'].decode('utf-8-sig');host=originals['host-program.snapshot.cs'].decode('utf-8-sig')
 p=extract(source,host,originals['host-response-fields.snapshot.cs'].decode('utf-8-sig'))
 reference=R28B_REFERENCE.read_bytes()
 if sha(reference)!='3dbffd032612bba6130b904297f317f0de6be9d94906f4b06158d6483d6b184c':raise RuntimeError('R28B reference identity changed')
 previous=b'4bc9e31f575334c5e29ffb71127aead90862015ec6bfe609f34bc6f2c046d6f9'
 updated=b'fdb9e78e1b75c72686f67707d9c6cab7e33d93f16269c4b554b0c574f7135997'
 if reference.count(previous)!=1 or reference.replace(previous,updated,1)!=originals['native-main.snapshot.cpp']:
  raise RuntimeError('source drift exceeds parent-authorized unrelated updater manifest string')
 reference_parts=extract(reference.decode('utf-8-sig'),host,originals['host-response-fields.snapshot.cs'].decode('utf-8-sig'))
 if p!=reference_parts:raise RuntimeError('input/ACK/scanner/caller/helper source differs from R28B')
 save(run/'source-drift-rebind.json',{'parentAuthorizedCurrentMainSha256':EXPECTED_MAIN,'r28bReferenceSha256':sha(reference),
  'onlyChangedBytes':'updater expectedFanHostV2ManifestSha256 string',
  'completeScopedSourcePartsEqual':True,'inputBodiesUnchanged':True,'fanChangePreserved':True,
  'testsRepeatedForUnrelatedDrift':False,'initialR31AttemptStoppedBeforeCompilation':True})
 candidate=source.replace(p['originalTransfer'],p['candidateTransfer'],1)
 assert candidate.replace(p['candidateTransfer'],p['originalTransfer'],1)==source
 for name,data in originals.items():write(run/name,data)
 write(run/'runner.snapshot.py',Path(__file__).read_bytes());write(run/'fixture.cpp',Path(__file__).with_suffix('.cpp').read_bytes())
 write(run/'source-bound.h',header(p));write(run/'request-complete.snapshot.cpp',p['function']);write(run/'submit-caller-complete.snapshot.cpp',p['caller'])
 write(run/'fast-branch.snapshot.cpp',p['branch']);write(run/'candidate-transfer.cpp',p['candidateTransfer']+'\n')
 write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(source.splitlines(True),candidate.splitlines(True),fromfile='a/native/main.cpp',tofile='b/native/main.cpp')))
 save(run/'plan.json',PLAN);rows=corpus(run)
 save(run/'source-binding.json',{'nativeMainSha256':EXPECTED_MAIN,'hostProgramSha256':EXPECTED_HOST,
  'hostResponseFieldsSha256':sha(originals['host-response-fields.snapshot.cs']),
  'sourceFunctionSha256':sha(p['function']),'completeCallerSha256':sha(p['caller']),
  'scannerNamespaceSha256':sha(p['namespace']),'sourceFastBranchSha256':sha(p['branch']),
  'actualStruct':p['receipt'],'exactDeclarationsAndDefaults':p['declarations'],
  'helperHashes':{n:sha(v) for n,v in p['helpers'].items()},'actualSharedAfterAcceptHelper':p['afterHelper'],
  'hostGuidDeclaration':p['hostGuidDeclaration'],'hostSerializerFieldOrder':p['fieldOrder'],
  'nativeIdGenerator':p['nativeIdGenerator'],'originalTransfer':p['originalTransfer'],'candidateTransfer':p['candidateTransfer'],
  'outsideFinalExpressionBytesUnchanged':True,'callerDestinationIsDistinctFromCalleeLocal':True,
  'observationAndSideEffectsBeforeTransfer':True,'noLaterSourceConsumer':True,
  'fixtureSeams':['reduced settings object retains exact source member type/default',
   'no real transport: wrappers enter after ACK bytes exist and report fast success vs unchanged DOM fallthrough',
   'tick elapsed values and diagnostic sink are deterministic offline seams; actual after-accept body and atomics retained',
   'separate raw timed wrappers and hook-instrumented correctness wrappers',
   'wire fixtures reconstructed from actual serializer/source plus 918 observed identities; not original ACK bytes'],
  'exceptionBoundary':PLAN['exceptionBoundary'],'productionEdited':False})
 reports={}
 for profile in PLAN['profiles']:
  print('building/verifying '+profile+': '+run.name,flush=True)
  exe=build(run,profile);result=run_process([str(exe),'verify',str(run/'receipts.json')],run,profile+'-verify')
  try:report=json.loads(result.stdout)
  except json.JSONDecodeError as error:raise RuntimeError('fixture failed without JSON; see '+str(run)) from error
  save(run/(profile+'-correctness.json'),report)
  reports[profile]={'exitCode':result.returncode,'pass':report['pass'],'assertions':report['assertions'],
   'failures':report['failureCount'],'candidateSourceDifferences':report['candidateSourceDifferences'],'domSafetyViolations':report['domSafetyViolations']}
 stable=all(path.read_bytes()==originals[name] for name,path in paths.items())
 passed=all(r['pass'] and r['exitCode']==0 for r in reports.values())
 manifest={'recordType':'immutable-R31-correctness-preparation','runName':run.name,
  'nativeMainSha256':EXPECTED_MAIN,'hostProgramSha256':EXPECTED_HOST,'sourceStable':stable,
  'sourceInputs':{name:{'path':str(paths[name]),'sha256':sha(data)} for name,data in originals.items()},
  'archiveSha256':ARCHIVE_SHA,'archiveObservations':len(rows),'profiles':reports,
  'correctnessPassed':passed,'benchmarkReady':passed and stable,'benchmarkExecuted':False,
  'planSha256':sha((run/'plan.json').read_bytes()),'productionEdited':False,'artifacts':binding_map(run)}
 save(run/'manifest.json',manifest)
 save(OUTPUT/'READINESS.json',{'state':'correctness-ready-await-parent-timing-release' if passed and stable else 'correctness-failed',
  'run':str(run),'nativeMainSha256':EXPECTED_MAIN,'hostProgramSha256':EXPECTED_HOST,'profiles':reports,
  'benchmarkExecuted':False,'measurementAuthorized':False,'productionEdited':False,'plan':PLAN})
 print(json.dumps({'run':str(run),'benchmarkReady':manifest['benchmarkReady'],'profiles':reports,'benchmarkExecuted':False},indent=2))
 return 0 if passed and stable else 2


def benchmark(args):
 run=Path(args.benchmark_ready_run).resolve()
 if not run.is_relative_to(OUTPUT.resolve()):raise RuntimeError('benchmark run outside R31 scope')
 manifest=json.loads((run/'manifest.json').read_text(encoding='utf-8'))
 if not manifest['benchmarkReady']:raise RuntimeError('fresh correctness is not ready')
 verify_bindings(run,manifest)
 if PLAN!=json.loads((run/'plan.json').read_text(encoding='utf-8')):raise RuntimeError('plan differs from frozen preparation')
 for entry in manifest['sourceInputs'].values():
  if sha(Path(entry['path']).read_bytes())!=entry['sha256']:raise RuntimeError('actual source dependency drift; do not time')
 permission=json.loads(Path(args.permission_file).read_text(encoding='utf-8'))
 for key,value in {'benchmarkApproved':True,'approver':'parent','nativeMainSha256':manifest['nativeMainSha256'],
  'hostProgramSha256':manifest['hostProgramSha256'],'preparedRunName':run.name}.items():
  if permission.get(key)!=value:raise RuntimeError('parent authorization does not bind '+key)
 claim={'preparedRun':str(run),'startedUtc':dt.datetime.now(dt.timezone.utc).isoformat(),'permission':permission,'singleAttempt':True,'noRetiming':True}
 save(OUTPUT/'FORMAL-BENCHMARK-ATTEMPT.json',claim,True);save(run/'BENCHMARK-ATTEMPT.json',claim,True)
 results=[]
 for campaign in (1,2):
  for profile in (PLAN['profiles'] if campaign==1 else list(reversed(PLAN['profiles']))):
   print(f'campaign {campaign}/2 {profile}: {run.name}',flush=True)
   result=run_process([str(run/(profile+'.exe')),'benchmark',str(run/'receipts.json'),str(campaign)],run,f'{profile}-campaign-{campaign}')
   report=json.loads(result.stdout);save(run/f'{profile}-campaign-{campaign}.json',report)
   results.append({'profile':profile,'campaign':campaign,'exitCode':result.returncode,**report})
 stable=all(sha(Path(e['path']).read_bytes())==e['sha256'] for e in manifest['sourceInputs'].values())
 summary={'pass':stable and all(r['pass'] and r['exitCode']==0 for r in results),'sourceStable':stable,
  'results':results,'productionEdited':False,'wholeHardwareClaim':False,'retiming':False}
 save(run/'benchmark-summary.json',summary)
 print(json.dumps({'pass':summary['pass'],'run':str(run),'passedRows':sum(x['pass'] for r in results for x in r['rows']),
  'totalRows':sum(len(r['rows']) for r in results)},indent=2))
 return 0 if summary['pass'] else 2


def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--prepare',action='store_true')
 parser.add_argument('--benchmark-ready-run')
 parser.add_argument('--permission-file')
 args=parser.parse_args()
 if args.benchmark_ready_run:
  if not args.permission_file or args.prepare:parser.error('benchmark requires separate explicit --permission-file; cannot prepare and time together')
  return benchmark(args)
 if args.permission_file:parser.error('permission applies only to benchmark mode')
 return prepare()

if __name__=='__main__':
 try:raise SystemExit(main())
 except Exception as error:
  print('R31 ERROR: '+str(error),file=sys.stderr);raise SystemExit(1)
