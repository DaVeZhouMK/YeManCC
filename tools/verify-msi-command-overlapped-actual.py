#!/usr/bin/env python3
"""Compile verbatim MSI IO/open methods against fake-only Win32. No product entry point."""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

EXPECTED_CURRENT = '61d1a3efa21696acbe3c4ad59557d6d6558cc2932887ed38944761bb50b8cab8'
EXPECTED_ORIGINAL = 'aa7a1d1c378a1f0430b2e16f0151bd03d08091d81d7b9d72469fb92aa685a1e8'
DEFAULT_ROOT = Path(r'G:\YeManCC-Work\Build\Validation\G3E-082309-input-chain-audit-20261014\msi-command-overlapped-fix')
UNITS = [
 ('constant','kMsiClawVendorId'),('constant','kMsiClawPidXInput'),
 ('constant','kMsiClawPidDInput'),('constant','kMsiClawPidTesting'),
 ('struct','MsiClawHidDiag'),('function','msiPathMentionsVendor'),
 ('function','msiClawOpenCommandHid'),('enum','MsiClawIoState'),
 ('function','msiClawIoStateName'),('struct','MsiClawWriteResult'),
 ('function','msiClawWriteFrame'),('constant','kMsiClawWriteWaitMs'),
 ('constant','kMsiClawWriteReapMs'),('struct','MsiClawIoSlot'),
 ('global','g_msiClawIoSlot'),('function','msiClawIoSlotFreeLocked'),
 ('function','msiClawIoSlotReapLocked'),('function','msiClawCommandWriteBounded'),
 ('struct','MsiClawCommandOutcome'),('function','msiClawOpenModeCommand'),
 ('function','msiClawCloseModeCommand'),('function','msiClawSwitchModeOnHandle'),
 ('function','msiClawSwitchMode')]
PROTECTED = [
 'msiClawKnownBoardAuthority','msiDinputMainSourceEligible','msiPathMentionsVendor',
 'msiClawWriteFrame','msiClawIoSlotFreeLocked','msiClawIoSlotReapLocked',
 'msiClawCloseModeCommand','msiClawSwitchModeOnHandle','msiClawSwitchMode',
 'msiTerminalReturnMissingTransport','msiTerminalReturnFacts','msiTerminalReturnNote',
 'msiTerminalReturnCancel','msiTerminalReturnNow','msiTerminalReturnEligibilityNote',
 'msiTerminalReturnSchedule','msiModeReadbackFacts','msiControllerProbeBeforeRearm',
 'msiMode1TerminalReason','msiMode1WritebackVerdict','msiDinputDeviceTargetAbsent',
 'msiReturnDebtBasisName','msiReturnDebtDischarge','msiXInputReAdmissionQualified',
 'msiDinputTryDischargeReturnDebt','msiClawRestoreXInputMode','msiTerminalReturnMaintenance']


def sha(b): return hashlib.sha256(b).hexdigest()


def body_end(text, begin):
    """Brace scan, respecting C++ ordinary strings/chars/comments and raw strings."""
    i, depth = begin, 0
    while i < len(text):
        if text.startswith('//', i):
            end = text.find('\n', i); i = len(text) if end < 0 else end; continue
        if text.startswith('/*', i):
            end = text.find('*/', i + 2)
            if end < 0: raise ValueError('unterminated comment')
            i = end + 2; continue
        raw = re.match(r'R"([^ ()\\\t\r\n]{0,16})\(', text[i:]) if text.startswith('R"', i) else None
        if raw:
            end = text.find(')' + raw[1] + '"', i + len(raw[0]))
            if end < 0: raise ValueError('unterminated raw string')
            i = end + len(raw[1]) + 2; continue
        if text[i] in '\"\'':
            quote = text[i]; i += 1
            while i < len(text):
                if text[i] == '\\': i += 2
                elif text[i] == quote: i += 1; break
                else: i += 1
            continue
        if text[i] == '{': depth += 1
        if text[i] == '}':
            depth -= 1
            if depth == 0: return i + 1
        i += 1
    raise ValueError('unterminated body')


def extract(text, kind, name):
    if kind == 'function':
        pattern = r'^static[^\n;{}]*\b' + re.escape(name) + r'\s*\('
    elif kind in ('struct', 'enum'):
        pattern = r'^' + kind + r'(?: class)?\s+' + re.escape(name) + r'\b'
    else:
        pattern = r'^static[^\n;]*\b' + re.escape(name) + r'\b[^\n;]*;'
    for match in re.finditer(pattern, text, re.M):
        start = match.start()
        if kind in ('constant','global'): end = match.end()
        else:
            brace = text.find('{', match.end())
            semi = text.find(';', match.end())
            if brace < 0 or (semi >= 0 and semi < brace): continue  # prototype
            end = body_end(text, brace)
            if kind in ('struct','enum'):
                assert text[end] == ';'; end += 1
        value = text[start:end]
        return value, {'symbol':name,'kind':kind,'lineStart':text.count('\n',0,start)+1,
                       'lineEnd':text.count('\n',0,end)+1,'utf8SliceSha256':sha(value.encode('utf-8'))}, (start,end)
    raise ValueError(f'cannot find {kind} {name}')


def run(command, env, cwd, log):
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True,
                            encoding='utf-8', errors='replace')
    log.write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        print(result.stdout + result.stderr)
        raise RuntimeError(f'{command[0]} returned {result.returncode}; {log}')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current',type=Path,default=Path(__file__).resolve().parent.parent/'native'/'main.cpp')
    parser.add_argument('--original',type=Path,default=DEFAULT_ROOT/'main.cpp.before-overlapped-fix')
    parser.add_argument('--output',type=Path,default=DEFAULT_ROOT/'actual-io-fixture')
    parser.add_argument('--expected-current-sha256',default=EXPECTED_CURRENT)
    args=parser.parse_args()
    allowed=(DEFAULT_ROOT/'actual-io-fixture').resolve()
    out=args.output.resolve()
    if out != allowed and allowed not in out.parents:
        raise ValueError('output must remain inside authorized actual-io-fixture directory')
    tools=Path(__file__).resolve().parent
    out.mkdir(parents=True,exist_ok=True)
    tool_inputs={name:(tools/name).read_bytes() for name in ['msi_command_overlapped_fake_win32.h','msi_command_overlapped_actual_fixture.cpp','verify-msi-command-overlapped-actual.py']}
    for name in ['msi_command_overlapped_fake_win32.h','msi_command_overlapped_actual_fixture.cpp']:
        (out/name).write_bytes(tool_inputs[name])
    sources={label:path.read_bytes() for label,path in [('original',args.original),('current',args.current)]}
    if sha(sources['current']) != args.expected_current_sha256: raise ValueError('current source hash differs; no stale-source receipt')
    if sha(sources['original']) != EXPECTED_ORIGINAL: raise ValueError('original source hash differs')
    texts={label:b.decode('utf-8-sig') for label,b in sources.items()}
    provenance={'sourceHashes':{k:sha(v) for k,v in sources.items()},
        'sourcePaths':{'original':str(args.original.resolve()),'current':str(args.current.resolve())},
        'extraction':'exact UTF-8 source slices; no body/signature/control substitutions; BOM excluded from text slices',
        'compiledUnits':{},'protectedChecks':[],
        'testToolHashes':{name:sha(data) for name,data in tool_inputs.items()},
        'testSnapshots':'fake boundary and harness copied byte-for-byte before compile; post-run live tools checked', 'boundary':'all Win32/SetupAPI/HID names implemented in fake header; no Windows SDK includes; YMCC_FAULT_INJECT undefined; no product main/WinMain/SDK linked'}
    generated=['#include "msi_command_overlapped_fake_win32.h"\n']
    for label in ['original','current']:
        generated.append('namespace '+label+' {\n'); provenance['compiledUnits'][label]=[]
        for kind,name in UNITS:
            value,meta,_=extract(texts[label],kind,name)
            provenance['compiledUnits'][label].append(meta)
            generated.append(f'#line {meta["lineStart"]} "{label}-main.cpp-extracted"\n'+value+'\n')
        generated.append('}\n')
    generated.append('#line 1 "msi_command_overlapped_actual_fixture.cpp"\n#include "msi_command_overlapped_actual_fixture.cpp"\n')
    for name in PROTECTED:
        old,om,_=extract(texts['original'],'function',name); new,nm,_=extract(texts['current'],'function',name)
        if old != new: raise ValueError('protected method changed: '+name)
        provenance['protectedChecks'].append({'symbol':name,'byteIdenticalUtf8':True,'original':om,'current':nm})
    for name in ['kMsiClawWriteWaitMs','kMsiClawWriteReapMs']:
        old,om,_=extract(texts['original'],'constant',name);new,nm,_=extract(texts['current'],'constant',name)
        if old != new: raise ValueError('timeout budget changed')
        provenance['protectedChecks'].append({'symbol':name,'byteIdenticalUtf8':True,'original':om,'current':nm})
    # These two policy methods contain only new diagnostic fields. Exact bodies must
    # match after removal of those specified telemetry rows; nothing else ignored.
    telemetry = re.compile(r'^        \{"commandOpenFlags", static_cast<unsigned long long>\(outcome\.diag\.selectedOpenFlags\)\},\r?\n|^        \{"commandOverlapped", \(outcome\.diag\.selectedOpenFlags & FILE_FLAG_OVERLAPPED\) != 0\},\r?\n',re.M)
    for name in ['msiClawRequestDInputMode','msiClawFinishXinputRestore']:
        old,om,_=extract(texts['original'],'function',name);new,nm,_=extract(texts['current'],'function',name)
        cleaned,n=telemetry.subn('',new)
        if n != 2 or old != cleaned: raise ValueError('policy changes beyond two diagnostic rows: '+name)
        provenance['protectedChecks'].append({'symbol':name,'byteIdenticalAfterExactlyTwoDiagnosticRowsRemoved':True,'removedRows':n,'original':om,'current':nm})
    old,_,_=extract(texts['original'],'function','msiClawOpenCommandHid');new,_,_=extract(texts['current'],'function','msiClawOpenCommandHid')
    normalized=new.replace('MsiClawHidDiag& diag, DWORD openFlags = FILE_ATTRIBUTE_NORMAL','MsiClawHidDiag& diag').replace('            openFlags, nullptr);','            FILE_ATTRIBUTE_NORMAL, nullptr);')
    normalized=re.sub(r'^                diag.selectedOpenFlags = openFlags;\r?\n','',normalized,flags=re.M)
    if normalized != old: raise ValueError('selector/order/fallback differs beyond flags plumbing')
    provenance['selectorCheck']='exact original body after only optional flags signature/argument and diagnostic assignment removed'
    # Entire source outside changed units must remain byte-identical.
    changed=[('struct','MsiClawHidDiag'),('function','msiClawOpenCommandHid'),('function','msiClawCommandWriteBounded'),('function','msiClawOpenModeCommand'),('function','msiClawRequestDInputMode'),('function','msiClawFinishXinputRestore')]
    residues={}
    for label,text in texts.items():
        for kind,name in changed:
            _,_,span=extract(text,kind,name)
            text=text[:span[0]]+f'/* EXACT_ALLOWED_CHANGED_UNIT:{name} */'+text[span[1]:]
        residues[label]=text
    if residues['original']!=residues['current']: raise ValueError('source changes outside enumerated contract/diagnostic units')
    provenance['wholeSourceOutsideSixChangedUnitsIdentical']=True
    cpp=out/'actual-source-extracted.cpp';cpp.write_text(''.join(generated),encoding='utf-8',newline='')
    (out/'source-diff.patch').write_text(''.join(difflib.unified_diff(texts['original'].splitlines(keepends=True),texts['current'].splitlines(keepends=True),fromfile='original-main.cpp',tofile='current-main.cpp')),encoding='utf-8',newline='')
    (out/'source-provenance.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # Use known installed compiler/CRT paths directly. No vcvars, registry,
    # service discovery, product build scripts or environment mutations outside child.
    vc=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207')
    sdk=Path(r'C:\Program Files (x86)\Windows Kits\10')
    sdk_version='10.0.26100.0'
    env=os.environ.copy()
    env['PATH']=str(vc/'bin'/'Hostx64'/'x64')+os.pathsep+env.get('PATH','')
    env['INCLUDE']=';'.join(str(q) for q in [vc/'include',sdk/'Include'/sdk_version/'ucrt'])
    env['LIB']=';'.join(str(q) for q in [vc/'lib'/'x64',sdk/'Lib'/sdk_version/'ucrt'/'x64',sdk/'Lib'/sdk_version/'um'/'x64'])
    command=[str(vc/'bin'/'Hostx64'/'x64'/'cl.exe'),'/nologo','/std:c++20','/EHsc','/utf-8','/MT','/W4','/O2',str(cpp),'/I'+str(out),'/Fo'+str(out/'actual-io-fixture.obj'),'/Fe'+str(out/'actual-io-fixture.exe'),'/Fd'+str(out/'actual-io-fixture.pdb'),'/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64']
    (out/'compile-command.json').write_text(json.dumps({'argv':command,'cwd':str(out),'includes':'fake header and C++ standard library only','productionObjects':[],'WindowsSdkHeadersIncluded':False,'YMCC_FAULT_INJECT':False},indent=2)+'\n',encoding='utf-8')
    run(command,env,out,out/'compile.log')
    result=run([str(out/'actual-io-fixture.exe')],env,out,out/'test-output.txt')
    passed=[line.split('\t')[1] for line in result.stdout.splitlines() if line.startswith('PASS\t')]
    count=re.search(r'SUMMARY\t(\d+)\tchecks=(\d+)',result.stdout)
    if not count or int(count[1])!=len(passed): raise ValueError('case count mismatch')
    for label,path in [('original',args.original),('current',args.current)]:
        if sha(path.read_bytes())!=provenance['sourceHashes'][label]: raise ValueError('source changed during compile/run: '+label)
    for name,data in tool_inputs.items():
        if (tools/name).read_bytes()!=data: raise ValueError('test tool changed during compile/run: '+name)
    receipt={'exitCode':0,'casesPassed':len(passed),'assertionChecks':int(count[2]),'caseNames':passed,
             'sourceHashes':provenance['sourceHashes'],'postRunSourceHashesMatched':True,'postRunToolHashesMatched':True,
             'testToolHashes':provenance['testToolHashes'],
             'actualMethodsExtractedVerbatim':len(UNITS)*2,'protectedChecksPassed':len(provenance['protectedChecks']),
             'fakeCallsOnly':True,'realHardwareCalls':0,'realSleeps':0,'productEntryPointsCompiled':False,
             'originalSyncFlaw':'fake WriteFile throws SYNC_HANDLE_BLOCKS_BEFORE_WAIT_BUDGET; waitCalls/cancelCalls=0',
             'scopeLimit':'fake contract/resource regression only; no hardware error433 root cause or live return/Steam/name acceptance proof'}
    (out/'test-summary.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    artifacts=[p for p in out.iterdir() if p.is_file() and p.name not in ['artifact-hashes.json','SHA256SUMS.txt']]
    artifacts += [tools/'msi_command_overlapped_fake_win32.h',tools/'msi_command_overlapped_actual_fixture.cpp',Path(__file__).resolve()]
    hashes=[{'path':str(p.resolve()),'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())} for p in sorted(artifacts,key=lambda p:str(p))]
    (out/'artifact-hashes.json').write_text(json.dumps({'sourceHashes':provenance['sourceHashes'],'artifacts':hashes},indent=2)+'\n',encoding='utf-8')
    (out/'SHA256SUMS.txt').write_text('\n'.join(q['sha256']+'  '+q['path'] for q in hashes)+'\n',encoding='utf-8')
    print(json.dumps(receipt,ensure_ascii=False,indent=2))


if __name__=='__main__':
    try: main()
    except Exception as error:
        print('FAIL: '+str(error),file=sys.stderr);sys.exit(1)
