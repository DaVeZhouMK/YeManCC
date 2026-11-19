"""Explicit original sender leaf. Windows power and privilege APIs are inert."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import time

def run(context):
    started = time.perf_counter()
    args, source, function = context['args'], context['source'], context['function']
    mask, braced = context['mask_cpp'], context['braced']
    out = args.output.resolve()
    if (out / 'RESULT.json').exists() or (out / 'sender.cpp').exists():
        raise RuntimeError('fresh sender evidence output required')
    selected = args.sleep_request_case
    template = Path(__file__).with_suffix('.cpp.in').read_text(encoding='utf-8')
    cases = re.findall(r'\btest\("([^"\r\n]+)"', template)
    exact = selected[5:] if selected and selected.startswith('from:') else selected
    if selected is not None and (not exact or exact not in cases):
        raise RuntimeError('unknown or empty sender case')
    def block(anchor):
        if source.count(anchor) != 1:
            raise RuntimeError('sender source anchor not unique: ' + anchor)
        start = source.index(anchor)
        return source[start:braced(mask(source).index('{', start))]
    enums = []
    for name in ['SgRetryKind', 'PowerLifecycle']:
        rows = re.findall(r'^enum class '+name+r'\s*:\s*uint8_t\s*\{[^\n]+\};\r?$', source, re.M)
        if len(rows) != 1:
            raise RuntimeError('sender enum not unique')
        enums.append(rows[0])
    original_reader = block('static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* output,uint32_t bytes) noexcept {')
    reader = original_reader.replace('readSharedSystemPower(', 'productionReadSharedSystemPower(', 1)
    sender = function('sgRequestSystemSleep')
    parts = {'ENUMS': '\n'.join(enums), 'RESULT': block('struct SgSystemSleepRequestResult {')+';',
             'USER_VETO': function('sgUserWakeCancelsSleepIntent'), 'READER': reader, 'SENDER': sender}
    cpp = template
    for name, value in parts.items():
        token = '@@'+name+'@@'
        if cpp.count(token) != 1:
            raise RuntimeError('sender fixture slot not unique')
        cpp = cpp.replace(token, value)
    if re.search(r'@@[A-Z_]+@@', cpp):
        raise RuntimeError('unknown sender fixture slot')
    (out / 'sender.cpp').write_text(cpp, encoding='utf-8')
    copied = []
    for name in ['sleep_power_dispatch.h', 'sleep_power_state.h', 'sleep_power_observation.h', 'sleep_request_fallback.h']:
        original = args.source.parent / name
        content = original.read_bytes()
        (out / name).write_bytes(content)
        copied.append({'path':str(original),'sha256':hashlib.sha256(content).hexdigest(),'bytes':len(content)})
    repo = args.source.parent.parent
    json_bytes = (repo / 'deps/json/json.hpp').read_bytes()
    (out / 'json.hpp').write_bytes(json_bytes)
    binding = {'canonicalId':'sleep.non-user-wake-consumer','selection':'sleep-request-notification-only',
               'selectedCase':selected,'expectedCases':(cases[cases.index(exact):] if selected.startswith('from:') else [selected]) if selected else cases,
               'sourceRaw':{'path':str(args.source),'sha256':hashlib.sha256(args.source.read_bytes()).hexdigest()},
               'decodedSource':{'encoding':'utf-8-sig with Python universal-newline decoding','sha256':hashlib.sha256(source.encode()).hexdigest()},
               'senderSha256':hashlib.sha256(sender.encode()).hexdigest(),
               'readerSha256':hashlib.sha256(original_reader.encode()).hexdigest(),
               'readerAdaptations':[{'from':'readSharedSystemPower(','to':'productionReadSharedSystemPower(','count':1}],
               'senderBodyAdaptations':0,'compiledSha256':hashlib.sha256(cpp.encode()).hexdigest(),'copiedHeaders':copied,
               'realSleep':False,'realPrivileges':False,'legacyParentExecuted':False}
    (out/'TASK.json').write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8')
    vcvars = r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    sdk = r'C:\Program Files (x86)\Windows Kits\10'
    version = '10.0.26100.0'
    def quote(value):
        value = str(value)
        if any(c in value for c in ['"','%','\r','\n']):
            raise RuntimeError('unsupported sender build path')
        return '"'+value+'"'
    include = ';'.join(str(Path(sdk)/'Include'/version/name) for name in ['ucrt','shared','um','winrt','cppwinrt'])
    libs = ';'.join(str(Path(sdk)/'Lib'/version/name/'x64') for name in ['ucrt','um'])
    build = '\r\n'.join(['@echo off','call '+quote(vcvars)+' >nul','if errorlevel 1 exit /b 1',
        'set "INCLUDE='+include+';%INCLUDE%"','set "LIB='+libs+';%LIB%"',
        'cl /nologo /std:c++20 /EHsc /utf-8 /O1 /W4 /WX /I'+quote(out)+' '+quote(out/'sender.cpp')+
        ' /sourceDependencies '+quote(out/'dependencies.json')+' /Fo'+quote(out/'sender.obj')+' /Fe'+quote(out/'sender.exe')+' /link /VERBOSE:LIB',
        'exit /b %errorlevel%',''])
    (out/'build.cmd').write_bytes(build.encode('utf-8'))
    result = {'pass':False,'selection':binding['selection'],'selectedCase':selected,'processes':[]}
    def execute(name, command, limit):
        before = time.perf_counter()
        child = subprocess.run(command,cwd=out,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=limit,
                               creationflags=subprocess.CREATE_NO_WINDOW)
        (out/(name+'.stdout.log')).write_bytes(child.stdout)
        (out/(name+'.stderr.log')).write_bytes(child.stderr)
        result['processes'].append({'name':name,'command':command,'exitCode':child.returncode,'elapsedMs':(time.perf_counter()-before)*1000})
        return child
    try:
        compiled = execute('compile',[os.environ.get('ComSpec',r'C:\Windows\System32\cmd.exe'),'/d','/c',str(out/'build.cmd')],45)
        if compiled.returncode:
            raise RuntimeError('actual sender compile failed')
        native = execute('behavior',[str(out/'sender.exe')]+([selected] if selected else []),5)
        result['native'] = json.loads(native.stdout.decode('utf-8'))
        if [row['name'] for row in result['native']['cases']] != binding['expectedCases']:
            raise RuntimeError('actual sender selected cases differ')
        result['nativeExitCode'] = native.returncode
        result['pass'] = native.returncode == 0
    except Exception as error:
        result['error'] = str(error)
    result['elapsedMs'] = (time.perf_counter()-started)*1000
    (out/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'pass':result['pass'],'cases':len(result.get('native',{}).get('cases',[])),'error':result.get('error')}))
    return 0 if result['pass'] else 1
