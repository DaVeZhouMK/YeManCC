#!/usr/bin/env python3
"""R17 inert source-bound hcCandidateObj fixture. Default: correctness only.
Formal timing requires --benchmark-ready-run RUN --parent-benchmark-go benchmark-go.
Only after explicit parent authorization; two independent Release processes.
Only authorized MSI ZIP is opened; production and historical evidence stay read-only.
"""
from __future__ import annotations
import argparse, datetime, difflib, importlib.util, json, math, os, re, subprocess, sys, zipfile
from pathlib import Path
sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
WORKSPACE = REPO.parents[2]
OUTPUT = WORKSPACE / 'Build/Validation/VirtualInputPerf-R17-20261015/hc-tree-local'
EXPECTED_MAIN = '2c745ba4badccf691b1468193600e66c01f4b239b8f137dd37427f0396ee3d75'
ARCHIVE = Path('C:/Users/DaVe/Desktop/日志-侧边栏全开后/YeManCC-logs-20261014-160313--G3E虚拟手柄OK.zip')
ARCHIVE_SHA = 'e886bcacb06e9250976235f4d1c0dae28c8f8e46902e7a1210f148c4cc01a479'
CORPUS = WORKSPACE / 'Build/Validation/VirtualInputPerf-R16-20261015/sample-tree-local/run-20261015-062238-40784/all-authorized-samples.json'
CORPUS_SHA = '885a0b5ec68b801cd38cfe4a74bfdaece57240128c1dde14bd51810877e938f2'
ENTRIES = [('input-capture.jsonl.prev', 5244444, 'eed512efcb50bdc8fd7203102ac7546c6089024f552cb959a32e0cac448b48a6'),
           ('input-capture.jsonl', 847725, 'bba5f0335014d9ff9dbabe99c0b05bcd6af4c5a2d96d2ee6920fae291ebd92f0')]
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PLAN = {'campaigns': 2, 'independentCampaignProcesses': True, 'roundsPerCampaign': 11,
        'minimumFasterRounds': 10, 'minimumMedianReductionPercent': 5.0,
        'requireFewerAllocationRequests': True, 'requireFewerRequestedBytes': True,
        'batchSize': 32, 'batchesPerMeasurement': 24, 'warmupBatches': 6,
        'scopes': ['construction', 'owned-record-dump', 'writer-copy-dump'],
        'medianRule': 'Both median paired reduction and ratio of medians must be at least 5 percent',
        'decision': 'Every scenario and scope must pass in BOTH independent campaigns; otherwise reject. No threshold relaxation.'}
spec = importlib.util.spec_from_file_location('r17_source_helpers', REPO/'tools/virtual_sample_tree_perf_selftest.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
clean, match_brace, source_function, sha = helper.clean, helper.match_brace, helper.source_function, helper.sha


def require(ok, message):
    if not ok: raise RuntimeError(message)


def write(path, data):
    path = path.resolve()
    require(path.is_relative_to(OUTPUT.resolve()), 'Write outside authorized evidence subtree: '+str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)


def save_json(path, data):
    write(path, json.dumps(data, ensure_ascii=False, indent=2)+'\n')


def scalar_types(declarations, names):
    result=helper.scalar_types(declarations,list(dict.fromkeys(list(names)+['currentBindingGeneration'])))
    return {n:result[n] for n in names}


def binding(source):
    bound, declarations = helper.source_binding(source)
    for name, sig in [('receipt', 'static json inputCaptureCandidateMatrixReceipt('),
                      ('sensor-meta-type', 'struct InputCaptureSensorMeta'),
                      ('selector-type', 'struct HcMotionSelectorReceipt'),
                      ('machine-type', 'struct MachineIdentity'), ('family-type', 'enum class YmccFamily')]:
        bound[name] = source_function(source, sig)
    start, stop, expr = helper.block(source, 'hcCandidateObj')
    bound['hc-block'] = source[start:stop]
    for needle in ['double hcCandidateGx =', 'double hcCandidateAx =']:
        i = bound['callback'].index(needle)
        declarations.append(bound['callback'][i:bound['callback'].index(';', i)+1])
    bound['type-declarations'] = '\n'.join(declarations)
    bound['capability-header'] = (REPO/'native/msi_motion_capabilities.h').read_bytes().decode('utf-8-sig')
    return bound, declarations, (start, stop, expr)


def candidate_block(expr):
    require(helper.pairs(expr) == [
        ('"gyro"', '{{"gx", hcCandidateGx}, {"gy", hcCandidateGy}, {"gz", hcCandidateGz}}'),
        ('"accel"', '{{"x", hcCandidateAx}, {"y", hcCandidateAy}, {"z", hcCandidateAz}}'),
        ('"resolved"', 'g_hcMotionSelector.resolved'), ('"providerBindingProven"', 'false'),
        ('"recipeReceipt"', 'inputCaptureCandidateMatrixReceipt(sensorMeta)')], 'Unexpected current HC initializer expressions')
    rows = ['        json hcCandidateObj = json::object();'];children = []
    for key, val in helper.pairs(expr):
        if val.startswith('{'):
            child = 'hcCandidateObj_'+json.loads(key)
            rows += helper.explicit_object(child, val, [])
            val = 'std::move('+child+')'; children.append(child)
        elif key == '"recipeReceipt"':
            child = 'hcCandidateObj_recipeReceipt'
            rows.append('        json '+child+' = '+val+';')
            val = 'std::move('+child+')'; children.append(child)
        rows.append('        hcCandidateObj['+key+'] = '+val+';')
    result = '\n'.join(rows)
    sequence = ['hcCandidateGx','hcCandidateGy','hcCandidateGz','hcCandidateAx','hcCandidateAy','hcCandidateAz',
                'g_hcMotionSelector.resolved','false','inputCaptureCandidateMatrixReceipt(sensorMeta)']
    actual = re.findall(r'= (hcCandidate\w+|g_hcMotionSelector\.resolved|false|inputCaptureCandidateMatrixReceipt\(sensorMeta\));', result)
    require(actual == sequence, 'Scalar/receipt evaluation sequence changed')
    require(all(result.count('std::move('+c+')') == 1 for c in children), 'Completed children must move once')
    return result, sequence, children


def generated_header(bound, declarations, candidate):
    names = ['hcCandidateGx','hcCandidateGy','hcCandidateGz','hcCandidateAx','hcCandidateAy','hcCandidateAz']
    types = scalar_types(declarations, names)
    require(all(types[n] == 'double' for n in names), 'HC scalar type drift')
    text = '\n'.join(bound[n]+';' for n in ['family-type','machine-type','selector-type','sensor-meta-type'])+'\n'
    text += '''struct HcInput {
    double hcCandidateGx=0,hcCandidateGy=0,hcCandidateGz=0,hcCandidateAx=0,hcCandidateAy=0,hcCandidateAz=0;
    MachineIdentity machine;
    HcMotionSelectorReceipt selector;
    InputCaptureSensorMeta sensorMeta;
};
static thread_local MachineIdentity g_machineIdentity;
static thread_local HcMotionSelectorReceipt g_hcMotionSelector;
static void bindReceipt(const HcInput& v) {g_machineIdentity=v.machine;g_hcMotionSelector=v.selector;}
#include "msi_motion_capabilities.h"
'''+bound['receipt']+'\n'
    text += '''namespace ymcc::msi_sensor {
static const MotionCapability* tracedMotionCapability(std::string_view dc,std::string_view board) {
    traceEvent(TraceEvent::Capability); if(trace.failStage==2)throw ReceiptSentinel{2};
    return motionCapability(dc,board);
}
static std::string_view tracedLegacyRecipeSha256(const MotionCapability& c) {
    traceEvent(TraceEvent::LegacyDigest); ++trace.digestCalls;
    if(trace.failStage==3&&trace.digestCalls==1)throw ReceiptSentinel{3};
    if(trace.failStage==4&&trace.digestCalls==2)throw ReceiptSentinel{4};
    return legacyRecipeSha256(c);
}
}
// Correctness-only instrumented copy; timed builders use exact function above.
#define inputCaptureCandidateMatrixReceipt instrumentedActualReceipt
#define motionCapability tracedMotionCapability
#define legacyRecipeSha256 tracedLegacyRecipeSha256
'''+bound['receipt']+'''
#undef legacyRecipeSha256
#undef motionCapability
#undef inputCaptureCandidateMatrixReceipt
static json tracedReceipt(const InputCaptureSensorMeta& meta) {
    ++trace.receiptCalls;traceEvent(TraceEvent::ReceiptEnter);
    faultRequests=0;faultAt=trace.receiptFaultAt;
    try {
        if(trace.failStage==1)throw ReceiptSentinel{1};
        json result=instrumentedActualReceipt(meta);
        faultAt=-1;trace.receiptRequests=faultRequests;
        trace.receiptOwner=&result.get_ref<const json::object_t&>();
        traceEvent(TraceEvent::ReceiptReturn);return result;
    } catch(...) {faultAt=-1;trace.receiptRequests=faultRequests;traceEvent(TraceEvent::ReceiptThrow);throw;}
}
'''
    for fname, block in [('baselineHc',bound['hc-block']),('candidateHc',candidate),
                         ('tracedBaselineHc',bound['hc-block']),('tracedCandidateHc',candidate)]:
        body = block
        if fname.startswith('traced'):
            for i,n in enumerate(names): body = re.sub(r'(?<![\w])'+n+r'(?![\w])', 'tracedLeaf(TraceEvent('+str(i)+'),'+n+')',body)
            body = body.replace('g_hcMotionSelector.resolved','tracedLeaf(TraceEvent::Resolved,g_hcMotionSelector.resolved)')
            body = body.replace('"providerBindingProven", false','"providerBindingProven", tracedLeaf(TraceEvent::ProviderFalse,false)')
            body = body.replace('["providerBindingProven"] = false;','["providerBindingProven"] = tracedLeaf(TraceEvent::ProviderFalse,false);')
            body = body.replace('inputCaptureCandidateMatrixReceipt(sensorMeta)','tracedReceipt(sensorMeta)')
            if fname == 'tracedCandidateHc':
                for child in ['gyro','accel']:
                    n='hcCandidateObj_'+child
                    line='        hcCandidateObj["'+child+'"] = std::move('+n+');'
                    body=body.replace(line, '        auto owner_'+child+' = &'+n+'.get_ref<const json::object_t&>();\n'+line+'\n        check('+n+'.is_null() && owner_'+child+' == &hcCandidateObj.at("'+child+'").get_ref<const json::object_t&>(), "completed child moved without deep copy");')
        text += '\nstatic NOINLINE json '+fname+'(const HcInput& values) {\n'+helper.aliases(types)+'\n'
        text += '    const auto& g_hcMotionSelector=values.selector;\n    const auto& sensorMeta=values.sensorMeta;\n'+body+'\n    return hcCandidateObj;\n}\n'
    observer_types = {'now':'const ULONGLONG','motionDeltaSeconds':'const float','canonicalPad':'const XINPUT_GAMEPAD',
        'ox':'float','oy':'float','oz':'float','conf':'float','steady':'bool','psx':'float','psy':'float','wsx':'float','wsy':'float',
        'gyroSequence':'UINT64','accelSequence':'UINT64'}
    require(scalar_types(declarations,list(observer_types)) == observer_types, 'Current record scalar types changed')
    for fname, checked in [('observedRecord',False),('observedRecordChecked',True)]:
        text+='\nstatic NOINLINE json '+fname+'(const HcInput& values,const RecordInput& context,json&& hcCandidateObj,json&& imuObj,json&& eventObj,json&& testLaneLiveObj,json&& extras) {\n'
        text+=helper.aliases(observer_types,'context')+'\n'
        text+='''    const unsigned long long g_inputCaptureBusTickGlobal=context.g_inputCaptureBusTickGlobal;
    const bool live=context.live;const DWORD primarySlot=context.primarySlot,connectedMask=context.connectedMask;
    const auto& inputSource=context.inputSource;const auto& matrixIdentity=context.matrixIdentity;
    const bool hcMatrixAdmitted=context.hcMatrixAdmitted;
    const auto& g_realStickTest=context.g_realStickTest;const auto& g_hcMotionSelector=values.selector;
    void* g_gm=context.g_gm;json rec;
'''
        if checked:
            text+='    auto hcOwner=&hcCandidateObj.get_ref<const json::object_t&>();\n    auto imuOwner=&imuObj.get_ref<const json::object_t&>();\n    auto eventOwner=&eventObj.get_ref<const json::object_t&>();\n    auto testOwner=&testLaneLiveObj.get_ref<const json::object_t&>();\n'
        text+=bound['record']+'\n'+bound['record-transfers']+'\n'
        if checked:
            for child,var,key in [('hcMatrixCandidate','hcCandidateObj','hc'),('imu','imuObj','imu'),('event','eventObj','event'),('testLane','testLaneLiveObj','test')]:
                text+='    check('+var+'.is_null() && '+key+'Owner == &rec.at("'+child+'").get_ref<const json::object_t&>(), "original record transfer owns completed tree");\n'
        text+='    for(auto it=extras.begin();it!=extras.end();++it)rec[it.key()]=std::move(it.value());\n'+bound['inactive-override']+'\n    return rec;\n}\n'
    text+='\nstatic NOINLINE std::string writerDump(const json& rec,DWORD fixturePid) {\n'+bound['writer-copy-dump'].replace('GetCurrentProcessId()','fixturePid')+'\n    return line;\n}\n'
    return text,types


def archive_inputs(run):
    raw = ARCHIVE.read_bytes();require(sha(raw)==ARCHIVE_SHA,'Authorized ZIP hash drift')
    rows,refs=[],[]
    with zipfile.ZipFile(ARCHIVE) as z:
        for name,size,digest in ENTRIES:
            content=z.read(name);require(len(content)==size and sha(content)==digest,'ZIP input entry hash drift')
            for number,line in enumerate(content.splitlines(),1):
                try:r=json.loads(line)
                except (ValueError,UnicodeDecodeError):continue
                if r.get('kind')=='sample':rows.append(r);refs.append({'entry':name,'line':number,'rawSha256':sha(line)})
        motion=z.read('gyro-motion.log');statuses=[]
        for number,line in enumerate(motion.splitlines(),1):
            try:r=json.loads(line)
            except (ValueError,UnicodeDecodeError):continue
            if r.get('event')=='sensorapi-status' and r.get('modelClass')=='HandheldCompanion.Devices.ClawCG3EM':
                statuses.append({'line':number,'rawSha256':sha(line),'record':r})
        require(statuses and all(s['record']['clawHidLayoutPermitted'] for s in statuses),'Missing actual G3E capability binding')
    cached=CORPUS.read_bytes();require(sha(cached)==CORPUS_SHA,'Reusable 2301 corpus hash drift')
    require(json.loads(cached)==rows,'Reusable corpus differs from exact authorized ZIP sample rows')
    require(len(rows)==2301 and sum('gx' in r['imu'] for r in rows)==2284 and sum(r['imu'].get('imuActive') is False for r in rows)==17,'Authorized sample counts changed')
    active=[i for i,r in enumerate(rows) if 'gx' in r['imu']]
    selected=[('active-real-early',active[0]),('active-real-late',active[-1]),
              ('active-real-delta',max(active,key=lambda i:len(json.dumps(rows[i])))),
              ('inactive-real',next(i for i,r in enumerate(rows) if r['imu'].get('imuActive') is False))]
    save_json(run/'samples.json',[{'name':n,'sample':rows[i],'source':refs[i]} for n,i in selected])
    write(run/'all-authorized-samples.json',cached);save_json(run/'authorized-sample-lines.json',refs)
    save_json(run/'archive-receipt-binding.json',{'gyroMotionEntry':'gyro-motion.log','entrySha256':sha(motion),'g3eCapabilityStatuses':statuses,
        'bindingLimit':'MS-1T91 board token is supplied from current capability source row, not reconstructed DMI. Archive receipts independently match current function with this permitted capability. Hidden base matrix IDs/digest use explicit fixture defaults, unused by legacy paths.'})
    return {'archive':str(ARCHIVE),'archiveSha256':sha(raw),'entries':[{'entry':n,'bytes':s,'sha256':d} for n,s,d in ENTRIES],
        'reusedCorpus':str(CORPUS),'reusedCorpusSha256':sha(cached),'all2301RowsComparedToAuthorizedArchive':True,
        'counts':{'samples':2301,'active':2284,'inactive':17},'selected':[{'name':n,**refs[i]} for n,i in selected],'otherArchiveOpened':False}


def invoke(run,profile,mode,timeout=240):
    r=subprocess.run([str(run/(profile+'.exe')),mode,str(run/'samples.json'),str(run/'all-authorized-samples.json')],cwd=run,
        capture_output=True,encoding='utf-8',errors='replace',timeout=timeout)
    write(run/(profile+'-'+mode+'.json'),r.stdout);write(run/(profile+'-'+mode+'.stderr.log'),r.stderr)
    require(r.returncode==0,profile+' '+mode+' failed: '+r.stderr)
    result=json.loads(r.stdout);require(result['correctnessPass'],'Fixture correctness failed')
    return result


def rebind(run):
    index=json.loads((run/'source-hash-index.json').read_text(encoding='utf-8'))
    raw=(REPO/'native/main.cpp').read_bytes();bound,_,_=binding(raw.decode('utf-8-sig'))
    checks={k:sha(bound[k])==v for k,v in index['boundSourceHashes'].items()}
    for p,d in index['inputHashes'].items():checks['input:'+p]=sha((REPO/p).read_bytes())==d
    for p,d in index['artifactHashes'].items():checks['artifact:'+p]=sha((run/p).read_bytes())==d
    checks['fixed-plan']=json.loads((run/'acceptance-plan.json').read_text(encoding='utf-8'))==PLAN
    checks['authorized-archive']=sha(ARCHIVE.read_bytes())==ARCHIVE_SHA
    require(all(checks.values()),'Source/evidence drift; reprepare required: '+repr([k for k,v in checks.items() if not v]))
    save_json(run/'last-rebind.json',{'allBoundStable':True,'checks':checks,'wholeMainUnchanged':sha(raw)==index['baselineMainSha256'],'currentMainSha256':sha(raw),'unrelatedMainEditsAllowed':True})
    return sha(raw)


def prepare():
    raw=(REPO/'native/main.cpp').read_bytes();require(sha(raw)==EXPECTED_MAIN,'Initial main SHA must match explicit R17 baseline')
    source=raw.decode('utf-8-sig');bound,declarations,(a,b,expr)=binding(source)
    candidate,sequence,children=candidate_block(expr)
    require(all(source.count(c)==0 for c in children),'Candidate child name collision')
    header,types=generated_header(bound,declarations,candidate)
    nl='\r\n' if '\r\n' in bound['hc-block'] else '\n'
    new=candidate.replace('\n',nl);candidate_source=source[:a]+new+source[b:]
    require(candidate_source[:a]+bound['hc-block']+candidate_source[a+len(new):]==source,'Outside candidate block bytes changed')
    candidate_raw=(b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'')+candidate_source.encode('utf-8')
    run=OUTPUT/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
    write(run/'baseline-main.cpp',raw);write(run/'candidate-main.cpp',candidate_raw)
    patch=''.join(difflib.unified_diff(source.splitlines(True),candidate_source.splitlines(True),fromfile='a/native/main.cpp',tofile='b/native/main.cpp',n=3))
    write(run/'candidate-UNAPPLIED.patch',patch);write(run/'hcCandidateObj.baseline.cpp',bound['hc-block']);write(run/'hcCandidateObj.candidate.cpp',new)
    for n,v in bound.items():write(run/('source-'+n+'.cpp.txt'),v)
    write(run/'extracted.hpp',header);write(run/'json.hpp',(REPO/'deps/json/json.hpp').read_bytes())
    write(run/'msi_motion_capabilities.h',(REPO/'native/msi_motion_capabilities.h').read_bytes())
    write(run/'fixture.cpp',(REPO/'tools/virtual_hc_candidate_tree_perf_selftest.cpp').read_bytes());write(run/'virtual_hc_candidate_tree_perf_selftest.py',Path(__file__).read_bytes())
    save_json(run/'acceptance-plan.json',PLAN)
    save_json(run/'expression-sequence.json',{'scalarCount':6,'types':types,'evaluationSequence':sequence,'childrenMovedOnce':children,
        'receiptFunctionUnmodified':True,'receiptActualCallRetained':True,'rawOutsideAnchorPreserved':True,
        'receiptOwnershipNote':'Original initializer_list already moves owned rvalue json_ref payloads. No claimed baseline deep copy of completed receipt; candidate removes outer initializer scaffolding only.'})
    provenance=archive_inputs(run);save_json(run/'provenance.json',provenance);profiles=[]
    for profile,opts in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*opts,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(run/(profile+'.exe'))]
        bat=run/(profile+'-build.bat');write(bat,'@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\n')
        result=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180)
        write(run/(profile+'-build.log'),result.stdout+result.stderr);require(result.returncode==0,'Compile failed: '+str(run/(profile+'-build.log')))
        print(profile+': inert fixture compiled',flush=True);verified=invoke(run,profile,'verify');profiles.append({'profile':profile,**verified})
        print(profile+': correctness passed, '+str(verified['assertions'])+' assertions, no formal timing',flush=True)
    inputs=['deps/json/json.hpp','native/msi_motion_capabilities.h','tools/virtual_sample_tree_perf_selftest.py','tools/virtual_hc_candidate_tree_perf_selftest.py','tools/virtual_hc_candidate_tree_perf_selftest.cpp']
    artifacts={str(p.relative_to(run)):sha(p.read_bytes()) for p in run.rglob('*') if p.is_file()}
    index={'baselineMainSha256':sha(raw),'candidateMainSha256':sha(candidate_raw),'boundSourceHashes':{n:sha(v) for n,v in bound.items()},
        'inputHashes':{p:sha((REPO/p).read_bytes()) for p in inputs},'artifactHashes':artifacts,'candidateApplied':False,'rawOutsideAnchorPreserved':True,'recipeReceiptFunctionWritten':False}
    save_json(run/'source-hash-index.json',index);current=rebind(run)
    report={'stage':'correctness-ready-awaiting-parent-benchmark-go','correctnessPass':True,'formalTimingExecuted':False,'candidateAccepted':None,
        'runDirectory':str(run),'profiles':profiles,'provenance':provenance,'acceptancePlan':PLAN,'manifest':index,'currentMainSha256':current,
        'productionWritten':False,'installedProductOrPhysicalApisExecuted':False,
        'scopeLimit':'HC builder/receipt/destruction; actual owned record + dump; actual writer copy + pid assignment + dump. Other trees and binding/scalar setup excluded. No whole-process CPU claim.'}
    save_json(run/'correctness-ready.json',report);save_json(OUTPUT/'correctness-ready.json',report)
    write(run/'READY.txt','Correctness ready. Formal timing has not run. Await explicit parent benchmark-go.\n')
    text='R17 hcCandidateObj preparation\n\nRun: '+str(run)+'\nBaseline main SHA256: '+sha(raw)+'\nProduction unchanged; patch UNAPPLIED; formal timing NOT EXECUTED.\nRelease and Checked correctness pass. Fixed gates and exact source snapshots are recorded in correctness-ready.json.\nReceipt function and its capability/string/nested construction costs stay inside every measured scope.\n'
    write(run/'report.md',text);write(OUTPUT/'report.md',text)
    print(json.dumps({'stage':report['stage'],'runDirectory':str(run),'candidateSha256':sha(candidate_raw),'formalTimingExecuted':False},indent=2),flush=True)


def crosscheck(result):
    require(result['formalTimingExecuted'] and result['correctnessPass'],'Campaign must include correctness and timing')
    accepted=True;require(len(result['campaigns'])==1,'Each campaign must be a separate process')
    require(len(result['campaigns'][0]['results'])==30,'All ten fixed scenarios and three scopes required')
    for row in result['campaigns'][0]['results']:
        rounds=row['rounds'];require([r['round'] for r in rounds]==list(range(1,12)),'Paired round count changed')
        b=[r['baselineNsPerOperation'] for r in rounds];a=[r['candidateNsPerOperation'] for r in rounds]
        require(all(math.isfinite(x) and x>0 for x in a+b),'Invalid timing');median=lambda xs:sorted(xs)[len(xs)//2]
        faster=sum(x<y for x,y in zip(a,b));paired=median([(y-x)/y*100 for x,y in zip(a,b)]);ratio=(median(b)-median(a))/median(b)*100
        alloc=row['allocation'];requests=alloc['candidate']['requestsPerOperation']<alloc['baseline']['requestsPerOperation'];bytes_less=alloc['candidate']['requestedBytesPerOperation']<alloc['baseline']['requestedBytesPerOperation']
        passed=faster>=10 and paired>=5 and ratio>=5 and requests and bytes_less
        require(faster==row['fasterRounds'] and math.isclose(paired,row['medianPairedReductionPercent'],rel_tol=1e-10,abs_tol=1e-10)
                and math.isclose(ratio,row['ratioOfMediansReductionPercent'],rel_tol=1e-10,abs_tol=1e-10) and passed==row['meetsAllPredeterminedCriteria'],'Campaign statistics mismatch')
        accepted=accepted and passed
    require(accepted==result['candidateAccepted'],'Campaign decision mismatch');return accepted


def benchmark(run,authorization):
    require(authorization=='benchmark-go','Explicit parent benchmark-go argument required')
    run=run.resolve();require(run.is_relative_to(OUTPUT.resolve()),'Run outside R17 evidence')
    ready=json.loads((run/'correctness-ready.json').read_text(encoding='utf-8'))
    require(ready['correctnessPass'] and not ready['formalTimingExecuted'],'Invalid prepared run');require(not (run/'result.json').exists(),'Frozen run cannot be remeasured')
    rebind(run);save_json(run/'parent-benchmark-go.json',{'authorization':authorization,'time':datetime.datetime.now().astimezone().isoformat()});results=[]
    for number in [1,2]:
        rebind(run);result=invoke(run,'Release','benchmark-'+str(number),timeout=360);crosscheck(result);results.append(result)
        print('Independent campaign '+str(number)+' complete; passes='+str(result['candidateAccepted']),flush=True);rebind(run)
    accepted=all(crosscheck(r) for r in results)
    report={**ready,'stage':'accepted' if accepted else 'rejected','formalTimingExecuted':True,'candidateAccepted':accepted,'independentCampaigns':results,'currentMainSha256':rebind(run)}
    save_json(run/'result.json',report);save_json(OUTPUT/'result.json',report)
    print(json.dumps({'stage':report['stage'],'candidateAccepted':accepted,'runDirectory':str(run)},indent=2),flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--benchmark-ready-run',type=Path);p.add_argument('--parent-benchmark-go',choices=['benchmark-go']);args=p.parse_args()
    require(bool(args.benchmark_ready_run)==bool(args.parent_benchmark_go),'Benchmark path and parent benchmark-go must appear together')
    if args.benchmark_ready_run:benchmark(args.benchmark_ready_run,args.parent_benchmark_go)
    else:prepare()


if __name__=='__main__':main()