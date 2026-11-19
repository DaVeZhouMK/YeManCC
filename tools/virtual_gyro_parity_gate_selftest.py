"""Owned-gyro diagnostic regression, with legacy verification and typed preparation.

Legacy invocation retains the frozen R15 runner. --typed --prepare-only binds
--source-main and --record-header without historical Evidence reads or subprocesses.
The typed C++ branch retains both gate matrices, formatter/lazy checks and ownership.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,importlib.util,subprocess,shutil
P=Path(__file__).resolve().parents[1];E=Path(r"G:/YeManCC-Work/Build/Validation/VirtualInputParityGate-20261015")
F=Path(r"G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R15-20261015/gyro-envelope-local/run-20261015-061152")
H=lambda b:hashlib.sha256(b).hexdigest()
def save(path, data):path.write_text(json.dumps(data,indent=2)+"\n",encoding="utf-8")
def replacement(old):
    a=b'    if (tickCostEnabled()) {\r\n        g_c17aParityChecks.fetch_add(1, std::memory_order_relaxed);\r\n        if (m.dump() != referenceWire) {\r\n            g_c17aParityFailures.fetch_add(1, std::memory_order_relaxed);\r\n            appendNativeLifecycleLog("c17a-gyro-wire-parity-mismatch", {});\r\n        }\r\n    }'
    b=b'    if (tickCostEnabled()) {\r\n        if (referenceWire.empty()) {\r\n            // The gate reopened without a reference. Preserve dump validation\r\n            // and its exception point, but there is no parity comparison to count.\r\n            (void)m.dump();\r\n        } else {\r\n            g_c17aParityChecks.fetch_add(1, std::memory_order_relaxed);\r\n            if (m.dump() != referenceWire) {\r\n                g_c17aParityFailures.fetch_add(1, std::memory_order_relaxed);\r\n                appendNativeLifecycleLog("c17a-gyro-wire-parity-mismatch", {});\r\n            }\r\n        }\r\n    }'
    assert old.count(a)==1,"baseline gate block drift"
    return old.replace(a,b),a,b

# Typed preparation is independent of legacy E/F and never compiles or runs C++.
import os,re

TYPED_VARIANTS = ('Parity', 'MissingRef', 'Collapsed', 'CachedFailure', 'Eager', 'SuppressMismatch')
TYPED_FAILURES = {
    'Parity': [],
    'MissingRef': ['missing-reference comparisons must not count or fail'],
    'Collapsed': ['typed gate/counter/cost/log/target trace changed'],
    'CachedFailure': ['lazy wire generated early, duplicated after success, or failed to retry'],
    'Eager': ['typed exception identity or diagnostic changed', 'typed gate/counter/cost/log/target trace changed'],
    'SuppressMismatch': ['typed gate/counter/cost/log/target trace changed', 'typed available-reference real mismatch suppressed'],
}
TYPED_PATTERNS = {
    'diagnostic-gate': (rb'^static inline bool tickCostEnabled\(\) \{', False),
    'cost-name-hash': (rb'^static inline unsigned long long uiCostNameHash\(const char\* s\) \{', False),
    'ipc-cost-scope': (rb'^struct UiCostIpcScope \{', True),
    'utf8-to-utf16': (rb'^static std::wstring U2W\(const std::string& s\) \{', False),
    'target-resolver': (rb'^static ComPtr<ICoreWebView2> ipc_view_for_child\(int childId\) \{', False),
    'json-emitter': (rb'^static void ipc_emit_owned_gyro_telemetry\(std::string ev, json data\) \{', False),
    'typed-emitter': (rb'^static void ipc_emit_owned_gyro_record\(ymcc::gyro_telemetry::Record data\) \{', False),
    'snapshot-generated': (rb'^template<class MakeUtf8>\r?\nstatic void ipc_broadcast_controller_snapshot_generated\(MakeUtf8&& makeUtf8, const std::string\* validatedUtf8 = nullptr\) \{', False),
    'snapshot-json': (rb'^static void ipc_broadcast_controller_snapshot\(const json& message, const std::string\* validatedUtf8 = nullptr\) \{', False),
}

def replace_once(raw, old, new):
    assert raw.count(old) == 1, ('expected exactly one adaptation anchor', old, raw.count(old))
    return raw.replace(old, new, 1)

def select_fixture(raw, typed=True):
    """Select only our marked opt-in branches; leave all other C++ bytes alone."""
    result=[]; inside=False; enabled=True
    for line in raw.splitlines(keepends=True):
        marker=line.rstrip(b'\r\n')
        if marker == b'#ifdef YMCC_GYRO_PARITY_TYPED':
            assert not inside, 'nested typed branch'
            inside=True; enabled=typed
        elif marker == b'#else // YMCC_GYRO_PARITY_TYPED':
            assert inside, 'orphan typed else'
            enabled=not typed
        elif marker == b'#endif // YMCC_GYRO_PARITY_TYPED':
            assert inside, 'orphan typed endif'
            inside=False; enabled=True
        elif enabled:
            result.append(line)
    assert not inside, 'unclosed typed branch'
    return b''.join(result)

def typed_header(source):
    """Bind actual source bodies; reconstruct only the two frozen JSON oracles.

    The pre-fix emitter/post are regression oracles owned by this consumer.
    The supplied-source JSON baseline uses the extracted JSON emitter and the
    same lazy broadcast body, substituting message.dump() for makeUtf8().
    """
    parts={name:extract(source,*spec) for name,spec in TYPED_PATTERNS.items()}
    body=parts['json-emitter']['bytes']; typed=parts['typed-emitter']['bytes']
    generated=parts['snapshot-generated']['bytes']
    eol=b'\r\n' if b'\r\n' in generated else b'\n'
    baseline=replace_once(generated,b'template<class MakeUtf8>'+eol,b'')
    baseline=replace_once(baseline,
        b'ipc_broadcast_controller_snapshot_generated(MakeUtf8&& makeUtf8,',
        b'ipc_broadcast_controller_snapshot(const json& message,')
    baseline=replace_once(baseline,b'generatedUtf8.emplace(makeUtf8())',b'generatedUtf8.emplace(message.dump())')
    def renamed(raw, symbol, name):
        assert raw.count(symbol)>=1, symbol
        return raw.replace(symbol,name,1)
    old=HISTORICAL_EMITTER.replace(b'ipc_post_json',b'historical_ipc_post_json')
    header=b'// Frozen production functions. No application target is linked.\n'
    header+=b'#define MultiByteToWideChar parityMultiByteToWideChar\n'
    for name in ('diagnostic-gate','cost-name-hash','ipc-cost-scope','utf8-to-utf16','target-resolver'):
        header+=parts[name]['bytes']
    header+=renamed(HISTORICAL_POST,b'ipc_post_json',b'historical_ipc_post_json')
    header+=generated+parts['snapshot-json']['bytes']
    header+=renamed(baseline,b'ipc_broadcast_controller_snapshot',b'baseline_snapshot')
    base_body=body.replace(b'ipc_broadcast_controller_snapshot(',b'baseline_snapshot(')
    for name,raw in ((b'baseline_json_emit',base_body),(b'baseline_emit',old),(b'candidate_emit',body)):
        header+=renamed(raw,b'ipc_emit_owned_gyro_telemetry',name)
    marker=b'    json m = {{"event", std::move(ev)}, {"data", std::move(data)}};'
    for name,raw,hook in (
        (b'baseline_observed',old,b'observeLocals(ev, data, m);'),
        (b'candidate_observed',body,b'observeLocals(ev, data, m);'),
        (b'baseline_json_observed',base_body,b'observeLocals(ev, data, m);'),
        (b'typed_oracle_observed',old,b'observeRecordOracle(ev, data, m);')):
        eol=b'\r\n' if b'\r\n' in raw else b'\n'
        observed=replace_once(raw,marker,marker+eol+b'    '+hook)
        header+=renamed(observed,b'ipc_emit_owned_gyro_telemetry',name)
    header+=typed
    observed=replace_once(typed,b'    std::optional<std::string> validatedWire;',
                          b'    observeRecord(data);\n    std::optional<std::string> validatedWire;')
    header+=renamed(observed,b'ipc_emit_owned_gyro_record',b'typed_record_observed')
    header+=b'#undef MultiByteToWideChar\n'
    return header,parts,baseline

def typed_variant(header, name):
    """The five original semantic mutations, including their exact rejections."""
    if name not in TYPED_VARIANTS:
        raise ValueError('unknown typed variant: '+name)
    if name == 'Parity':
        return header
    # Original mutant preparation used read_text, hence LF for the entire header.
    header=header.replace(b'\r\n',b'\n')
    def change(symbol, fn):
        nonlocal header
        body=extract(header,rb'^static void '+re.escape(symbol)+rb'\(')['bytes']
        replacement=fn(body)
        assert replacement!=body, 'mutation made no change'
        header=replace_once(header,body,replacement)
    if name == 'MissingRef':
        change(b'ipc_emit_owned_gyro_record',lambda b:replace_once(b,
            b'if (referenceWire.empty()) {\n            validatedWire = data.envelope();',
            b'if (referenceWire.empty()) {\n            g_c17aParityChecks.fetch_add(1, std::memory_order_relaxed);\n            validatedWire = data.envelope();'))
    elif name == 'Collapsed':
        def collapse(body):
            assert body.count(b'tickCostEnabled()')==3, 'typed gate count drift'
            body=body.replace(b'tickCostEnabled()',b'collapsedGate')
            line=b'    if (!g_view && g_children.empty()) return;'
            return replace_once(body,line,line+b'\n    const bool collapsedGate=tickCostEnabled();')
        change(b'ipc_emit_owned_gyro_record',collapse)
    elif name == 'CachedFailure':
        change(b'ipc_broadcast_controller_snapshot_generated',lambda b:replace_once(b,
            b'wireReady = n > 0 && written == n;',b'wireReady = true;'))
    elif name == 'Eager':
        line=b'    UiCostIpcScope ipcCost("gyro.telemetry");'
        change(b'ipc_emit_owned_gyro_record',lambda b:replace_once(b,line,
            line+b'\n    const auto eagerUnusedWire=data.envelope();'))
    else:
        for symbol in (b'ipc_emit_owned_gyro_record',b'typed_record_observed'):
            change(symbol,lambda b:replace_once(b,b'if (*validatedWire != referenceWire) {',b'if (false) {'))
    return header

def prepare_typed(source_main, record_header, out_dir, *, json_header=None, variants=TYPED_VARIANTS):
    """Preparation-only public API; exclusive output, no subprocess or Evidence reads.

    Returns PREPARATION.json data. The owner supplies any compile/run environment,
    source/tool/dependency checks and its existing cumulative execution budget.
    """
    variants=tuple(variants)
    if not variants or len(set(variants))!=len(variants) or any(v not in TYPED_VARIANTS for v in variants):
        raise ValueError('typed variants must be a nonempty unique subset of '+repr(TYPED_VARIANTS))
    paths={'source-main':Path(source_main).resolve(),'record-header':Path(record_header).resolve(),
           'json-header':Path(json_header or P/'deps/json/json.hpp').resolve(),
           'consumer-cpp':P/'tools/virtual_gyro_parity_gate_selftest.cpp',
           'consumer-python':Path(__file__).resolve()}
    blobs={name:path.read_bytes() for name,path in paths.items()}
    header,parts,baseline=typed_header(blobs['source-main'])
    fixture=select_fixture(blobs['consumer-cpp'])
    outputs={'fixture.cpp':fixture,'production-extracted.h':header,
             'gyro_telemetry_record.h':blobs['record-header'],'json.hpp':blobs['json-header']}
    rows=[]
    for name in variants:
        prefix='' if name=='Parity' else name+'/'
        if prefix:
            outputs[prefix+'fixture.cpp']=fixture
            outputs[prefix+'production-extracted.h']=typed_variant(header,name)
        rows.append({'name':name,'fixture':prefix+'fixture.cpp','expectedFailures':TYPED_FAILURES[name],
                     'includeDirectories':['.'],'runArgv':['0'],'expectedExitCode':0 if name=='Parity' else 2})
    for name,path in paths.items():
        assert path.read_bytes()==blobs[name], 'input drift: '+str(path)
    manifest={'status':'PREPARED_ONLY_NOT_COMPILED_NOT_EXECUTED','sourceReady':False,
        'mode':'typed','candidateMainSha256':H(blobs['source-main']),
        'recordHeaderSha256':H(blobs['record-header']),
        'inputs':{name:{'path':str(path),'bytes':len(blobs[name]),'sha256':H(blobs[name])} for name,path in paths.items()},
        'bindings':{name:{k:v for k,v in part.items() if k!='bytes'}|{'bytes':len(part['bytes']),'sha256':H(part['bytes'])} for name,part in parts.items()},
        'oracles':{'historicalEmitterSha256':H(HISTORICAL_EMITTER),'historicalPostSha256':H(HISTORICAL_POST),
            'baselineSnapshotSha256':H(baseline),'baselineEmitter':'same extracted source JSON emitter',
            'baselineSnapshot':'generated broadcast body with template/callable replaced by JSON message/dump'},
        'expected':{'originalCases':2829,'typedCases':2829,'formatterCases':289,'lazyCases':62},
        'variants':rows,'compileExecuted':False,'businessExecuted':False,'timingExecuted':False,
        'newCanonicalIds':[],'newParentGate':False,
        'outputs':{name:{'bytes':len(raw),'sha256':H(raw)} for name,raw in outputs.items()}}
    out=Path(out_dir).resolve()
    out.mkdir(parents=True,exist_ok=False)
    for name,raw in outputs.items():
        path=out/name;path.parent.mkdir(parents=True,exist_ok=True)
        with path.open('xb') as stream:
            stream.write(raw);stream.flush();os.fsync(stream.fileno())
    for name,path in paths.items():
        assert path.read_bytes()==blobs[name], 'closing input drift: '+str(path)
    with (out/'PREPARATION.json').open('xb') as stream:
        stream.write((json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
        stream.flush();os.fsync(stream.fileno())
    return manifest


def close_brace(raw: bytes, start: int) -> int:
    """Lexical brace matching, including comments, escapes and C++ raw strings."""
    assert raw[start:start+1] == b'{'
    i, depth, state = start, 0, 'code'
    while i < len(raw):
        c, pair = raw[i:i+1], raw[i:i+2]
        if state == 'line':
            if c == b'\n': state = 'code'
        elif state == 'block':
            if pair == b'*/': state = 'code'; i += 1
        elif state in ('string', 'char'):
            if c == b'\\': i += 1
            elif c == (b'"' if state == 'string' else b"'"): state = 'code'
        elif pair == b'//': state = 'line'; i += 1
        elif pair == b'/*': state = 'block'; i += 1
        elif pair == b'R"':
            delimiter_end = raw.find(b'(', i+2)
            assert 0 <= delimiter_end-i <= 18, 'Unexpected raw literal'
            end_token = b')' + raw[i+2:delimiter_end] + b'"'
            end = raw.find(end_token, delimiter_end+1)
            assert end >= 0, 'Unclosed raw literal'
            i = end + len(end_token) - 1
        elif c == b'"': state = 'string'
        elif c == b"'": state = 'char'
        elif c == b'{': depth += 1
        elif c == b'}':
            depth -= 1
            if depth == 0: return i
        i += 1
    raise AssertionError('Unclosed block')

def extract(raw: bytes, pattern: bytes, is_struct=False):
    hits = list(re.finditer(pattern, raw, re.M))
    assert len(hits) == 1, f'Binding needs exactly one definition: {pattern!r}, got {len(hits)}'
    start = hits[0].start()
    opening = raw.find(b'{', hits[0].end()-1)
    assert opening >= 0
    end = close_brace(raw, opening)+1
    if is_struct:
        assert raw[end:end+1] == b';'
        end += 1
    if raw[end:end+2] == b'\r\n': end += 2
    elif raw[end:end+1] == b'\n': end += 1
    return {'bytes': raw[start:end], 'start': start, 'end': end,
            'line': raw[:start].count(b'\n')+1}

HISTORICAL_EMITTER = 'static void ipc_emit_owned_gyro_telemetry(std::string ev, json data) {\n    UiCostIpcScope ipcCost(ev.c_str());\n    if (!g_view && g_children.empty()) return;\n    if (tickCostEnabled()) g_c17aOwnedEvents.fetch_add(1, std::memory_order_relaxed);\n    if (!data.is_object()) data = json::object();\n    // The old path is reconstructed only while diagnostics are enabled.  This\n    // keeps the production path move-only while giving the short-screen run a\n    // direct byte-for-byte guard over the complete WebView envelope.\n    std::string referenceWire;\n    if (tickCostEnabled()) {\n        json reference = data;\n        json oldEnvelope = {{"event", ev}, {"data", reference}};\n        referenceWire = oldEnvelope.dump();\n    }\n    json m = {{"event", std::move(ev)}, {"data", std::move(data)}};\n    if (tickCostEnabled()) {\n        g_c17aParityChecks.fetch_add(1, std::memory_order_relaxed);\n        if (m.dump() != referenceWire) {\n            g_c17aParityFailures.fetch_add(1, std::memory_order_relaxed);\n            appendNativeLifecycleLog("c17a-gyro-wire-parity-mismatch", {});\n        }\n    }\n    ipc_post_json(0, m);\n    for (const auto& [id, cw] : g_children) {\n        if (cw && cw->view) ipc_post_json(id, m);\n    }\n}\n'.replace("\n", "\r\n").encode("utf-8")
HISTORICAL_POST = 'static void ipc_post_json(int childId, const json& message) {\n    auto target = ipc_view_for_child(childId);\n    if (!target) return;\n    target->PostWebMessageAsJson(U2W(message.dump()).c_str());\n}\n'.replace("\n", "\r\n").encode("utf-8")

def main(argv=None):
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--final',action='store_true',help='legacy final verification (compiles/runs)')
    ap.add_argument('--typed',action='store_true',help='opt into actual typed telemetry preparation')
    ap.add_argument('--prepare-only',action='store_true',help='generate inputs without compiling/running')
    ap.add_argument('--source-main',type=Path)
    ap.add_argument('--record-header',type=Path)
    ap.add_argument('--json-header',type=Path)
    ap.add_argument('--out',type=Path)
    ap.add_argument('--variant',action='append',choices=TYPED_VARIANTS)
    args=ap.parse_args(argv)
    if args.typed:
        if args.final or not args.prepare_only or not all((args.source_main,args.record_header,args.out)):
            ap.error('--typed requires --prepare-only --source-main --record-header --out and excludes --final')
        result=prepare_typed(args.source_main,args.record_header,args.out,
            json_header=args.json_header,variants=args.variant if args.variant is not None else TYPED_VARIANTS)
        print(json.dumps({'status':result['status'],'out':str(args.out.resolve()),
            'sourceReady':False,'candidateMainSha256':result['candidateMainSha256'],
            'variants':[row['name'] for row in result['variants']]},indent=2))
        return
    if args.prepare_only or any((args.source_main,args.record_header,args.json_header,args.out,args.variant)):
        ap.error('typed preparation options require --typed')
    E.mkdir(parents=True,exist_ok=True);run=E/(('final-' if args.final else 'candidate-')+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir()
    spec=importlib.util.spec_from_file_location('frozen_r15_extractor',F/'runner.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
    before=(F/'baseline-main.cpp').read_bytes();baselineBound=r.bind(before);old=baselineBound['owned-emitter']['bytes'];candidate,a,b=replacement(old)
    current=(P/'native/main.cpp').read_bytes();bound=r.bind(current)
    for name,piece in bound.items():
        expected=candidate if name=='owned-emitter' and args.final else baselineBound[name]['bytes']
        assert piece['bytes']==expected,name+' drift'
    compiled=bound['owned-emitter']['bytes'] if args.final else candidate
    shared=['diagnostic-gate','cost-name-hash','ipc-cost-scope','utf8-to-utf16','target-resolver','target-post']
    header=b'// Actual production bodies with inert bindings; observation hooks are test-only.\n'+b''.join(bound[k]['bytes'] for k in shared)
    for label,body in [('baseline_emit',old),('candidate_emit',compiled)]:
        header+=body.replace(b'ipc_emit_owned_gyro_telemetry',label.encode(),1)
    needle=b'    json m = {{"event", std::move(ev)}, {"data", std::move(data)}};'
    for label,body in [('baseline_observed',old),('candidate_observed',compiled)]:
        assert body.count(needle)==1
        instrumented=body.replace(needle,needle+b'\r\n    observeLocals(ev, data, m);',1)
        header+=instrumented.replace(b'ipc_emit_owned_gyro_telemetry',label.encode(),1)
    (run/'production-extracted.h').write_bytes(header)
    (run/'baseline-main.cpp').write_bytes(before);(run/'source-main.cpp').write_bytes(current)
    (run/'actual-or-candidate-function.cpp').write_bytes(compiled)
    snapshots={}
    for rel,target in [('deps/json/json.hpp','json.hpp'),('tools/virtual_gyro_parity_gate_selftest.cpp','fixture.cpp'),('tools/virtual_gyro_parity_gate_selftest.py','runner.py')]:
        data=(P/rel).read_bytes();snapshots[rel]={'sha256':H(data),'bytes':len(data)};(run/target).write_bytes(data)
    manifest={'mode':'final' if args.final else 'candidate','currentMainSha256':H(current),'baselineMainSha256':H(before),'baselineFunctionSha256':H(old),'candidateFunctionSha256':H(candidate),'compiledFunctionSha256':H(compiled),'inputs':snapshots,'productionBindings':{k:H(v['bytes']) for k,v in bound.items()},'generatedHeaderSha256':H(header),'scope':'Only availability guard in owned gyro parity diagnostic; always dump at the old parity gate; no broadcast/control change','oldGateBlock':a.decode(),'newGateBlock':b.decode(),'productionEditedByRunner':False}
    save(run/'manifest.json',manifest)
    profiles=[];vcvars=Path(r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    for name,options in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        exe=run/(name+'.exe');command=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*options,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(exe)]
        batch=run/(name+'-build.bat');batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\n',encoding='utf-8',newline='\r\n')
        built=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (run/(name+'-build.log')).write_text(built.stdout+built.stderr,encoding='utf-8');assert built.returncode==0,name+' compile failed'
        output=subprocess.run([str(exe),'0'],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (run/(name+'-result.json')).write_text(output.stdout,encoding='utf-8');(run/(name+'-stderr.log')).write_text(output.stderr,encoding='utf-8');assert output.returncode==0,output.stderr
        result=json.loads(output.stdout);assert result['correctness']['pass'] and result['correctness']['cases']==2829
        profiles.append({'name':name,'assertions':result['assertions'],'cases':result['correctness']['cases'],'resultSha256':H(output.stdout.encode()),'executableSha256':H(exe.read_bytes()),'correctness':result['correctness']})
    after=(P/'native/main.cpp').read_bytes();assert all(r.bind(after)[n]['bytes']==x['bytes'] for n,x in bound.items())
    for rel,row in snapshots.items():assert H((P/rel).read_bytes())==row['sha256'],rel
    result={'pass':True,'mode':manifest['mode'],'runDirectory':str(run),'manifest':manifest,'profiles':profiles,'wholeMainStable':current==after,'bindingsStable':True,'noPerformanceClaim':True,'ctrlCauseProven':False,'productionEditedByRunner':False}
    save(run/'result.json',result);save(E/('final-result.json' if args.final else 'candidate-result.json'),result)
    save(run/'artifact-hashes.json',{str(f.relative_to(run)):{'sha256':H(f.read_bytes()),'bytes':f.stat().st_size} for f in sorted(run.rglob('*')) if f.is_file()})
    print(json.dumps({'pass':True,'mode':manifest['mode'],'run':str(run),'profiles':profiles},indent=2))
if __name__=='__main__':main()
