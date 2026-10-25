#!/usr/bin/env python3
"""Extract real shortcut producer; validate packet pairing without input APIs."""
from pathlib import Path
import argparse,ast,datetime,hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/KeyboardShortcutEncoding-20261015'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
SIGNATURES=['static bool nativeKeyboardShortcutVirtualKey(','static DWORD nativeKeyboardShortcutInputFlags(','static void nativeKeyboardShortcutSend(']
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
    args=argparse.ArgumentParser();args.add_argument('--source',type=Path,default=REPO/'native/main.cpp');opts=args.parse_args()
    run=OUT/('run-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+str(os.getpid()));run.mkdir(parents=True)
    result={'pass':False,'source':str(opts.source),'run':str(run),'profiles':[],'actualOSInput':False,'productExecuted':False,'fieldAcceptance':False}
    try:
        module=ast.parse((REPO/'tools/virtual_sample_tree_perf_selftest.py').read_text(encoding='utf-8-sig'))
        scope={'re':re};exec(compile(ast.Module(body=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name in ['clean','match_brace','source_function']],type_ignores=[]),'<parser>','exec'),scope)
        raw=opts.source.read_bytes();functions={s:scope['source_function'](raw.decode('utf-8-sig'),s) for s in SIGNATURES}
        result['sourceSha256']=sha(raw);result['functionHashes']={s:sha(b.encode()) for s,b in functions.items()}
        (run/'actual_shortcut.inc').write_text('\n\n'.join(functions.values())+'\n',encoding='utf-8')
        for src,dest in [('tools/keyboard_shortcut_encoding_selftest.cpp','fixture.cpp'),('tools/keyboard_shortcut_encoding_selftest.py','runner.py'),('deps/json/json.hpp','json.hpp')]:
            (run/dest).write_bytes((REPO/src).read_bytes())
        for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
            exe=run/(name+'.exe');imports=run/(name+'-imports.txt')
            cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(exe),'/link','/INCREMENTAL:NO','/NODEFAULTLIB:user32.lib']
            bat=run/(name+'.cmd');bat.write_text('@echo off\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\nif errorlevel 1 exit /b %errorlevel%\ndumpbin /imports "'+str(exe)+'" > "'+str(imports)+'"\nexit /b %errorlevel%\n',encoding='utf-8')
            p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=120)
            (run/(name+'-compile.log')).write_text(p.stdout+p.stderr,encoding='utf-8');assert p.returncode==0,'compile failed: '+str(run)
            data=imports.read_text(errors='replace').lower();banned=[k for k in ['user32.dll','win32u.dll','sendinput','keybd_event','getasynckeystate','ole32.dll','shell32.dll'] if k in data]
            audit={'pass':not banned,'forbiddenImports':banned};save(run/(name+'-import-audit.json'),audit);assert audit['pass'],'unexpected input/UI import'
            p=subprocess.run([str(exe)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=15)
            (run/(name+'-stdout.json')).write_text(p.stdout,encoding='utf-8');(run/(name+'-stderr.txt')).write_text(p.stderr,encoding='utf-8')
            row={'profile':name,'exitCode':p.returncode,**json.loads(p.stdout)};result['profiles'].append(row)
            print(name+': '+('PASS' if row['pass'] else 'FAIL')+', '+str(row['checks'])+' checks, '+str(len(row['failures']))+' failures',flush=True)
            if not row['pass']:break
        current=opts.source.read_text(encoding='utf-8-sig');result['testedFunctionsUnchanged']=all(scope['source_function'](current,s)==b for s,b in functions.items())
        result['pass']=len(result['profiles'])==2 and all(x['pass'] for x in result['profiles']) and result['testedFunctionsUnchanged']
    except Exception as e:result['error']=str(e)
    save(run/'result.json',result);save(OUT/'latest.json',{'run':str(run),'pass':result['pass']})
    print('Evidence: '+str(run),flush=True)
    return 0 if result['pass'] else 1
if __name__=='__main__':raise SystemExit(main())
