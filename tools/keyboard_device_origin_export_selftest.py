#!/usr/bin/env python3
"""Source-extracted native export adapter; only fake Steam/file/crypto boundaries."""
from pathlib import Path
import ast,datetime,hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/KeyboardDeviceOriginEvidence-20261015/export'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 run=OUT/('run-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+str(os.getpid()));run.mkdir(parents=True)
 result={'pass':False,'run':str(run),'profiles':[],'realInputOrSteamExecuted':False,'collectorReplaced':True}
 try:
  module=ast.parse((REPO/'tools/virtual_sample_tree_perf_selftest.py').read_text(encoding='utf-8-sig'));ctx={'re':re};exec(compile(ast.Module(body=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name in ['clean','match_brace','source_function']],type_ignores=[]),'<parser>','exec'),ctx)
  raw=(REPO/'native/main.cpp').read_bytes();s=raw.decode('utf-8-sig');sigs=['static void keyboardDeviceOriginObserveAtDrain(','static json keyboardModifierRawNameAtDrain(','struct KeyboardDeviceOriginEvidenceOps','static std::wstring exportKeyboardDeviceOriginEvidence(']
  blocks={sig:ctx['source_function'](s,sig) for sig in sigs}
  block='\n\n'.join(b+(';' if sig.startswith('struct ') else '') for sig,b in blocks.items())
  start=s.index('        const auto keyboardDeviceOrigins=exportKeyboardDeviceOriginEvidence();');end=s.index('        const auto msiOfficialModeEvidence=',start);attachment=s[start:end]
  assert attachment.count('exportKeyboardDeviceOriginEvidence()')==1 and s.count('exportKeyboardDeviceOriginEvidence()')==2
  assert 'current-keyboard-origins-not-saved' in attachment
  (run/'actual_export.inc').write_text(block+'\n',encoding='utf-8');(run/'actual_attach.inc').write_text(attachment,encoding='utf-8')
  forbidden=['prepareSession(', 'steamLiveRequest(', 'sdmTarget(', 'sdmStateLocked(', 'SendInput(', 'CreateProcess', 'StartEditing', 'PreviewConfig']
  assert not any(x in ctx['clean'](block+attachment) for x in forbidden)
  inputs={p:(REPO/p).read_bytes() for p in ['tools/keyboard_device_origin_export_selftest.cpp','tools/keyboard_device_origin_export_selftest.py','deps/json/json.hpp']}
  for p,b in inputs.items():(run/('fixture.cpp' if p.endswith('_selftest.cpp') else Path(p).name)).write_bytes(b)
  result['sourceHashes']={p:sha(b) for p,b in inputs.items()};result['functionHashes']={p:sha(b.encode()) for p,b in blocks.items()};result['attachmentSha256']=sha(attachment.encode());result['scopeAuditPassed']=True
  for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
   exe=run/(name+'.exe');im=run/(name+'-imports.txt');cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(exe),'/link','/INCREMENTAL:NO','/NODEFAULTLIB:user32.lib']
   bat=run/(name+'.cmd');bat.write_text('@echo off\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\nif errorlevel 1 exit /b %errorlevel%\ndumpbin /imports "'+str(exe)+'" > "'+str(im)+'"\nexit /b %errorlevel%\n',encoding='utf-8')
   p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=120);(run/(name+'-compile.log')).write_text(p.stdout+p.stderr,encoding='utf-8');assert p.returncode==0,'compile failed: '+str(run)
   imp=im.read_text(errors='replace').lower();banned=[x for x in ['user32.dll','win32u.dll','sendinput','advapi32.dll','getasynckeystate','getkeyboardstate','ole32.dll','shell32.dll','shgetknownfolderpath','cfgmgr32.dll'] if x in imp];save(run/(name+'-import-audit.json'),{'pass':not banned,'forbiddenMatches':banned});assert not banned,'real input/Steam/crypto import'
   p=subprocess.run([str(exe)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=15);(run/(name+'-stdout.json')).write_text(p.stdout,encoding='utf-8');(run/(name+'-stderr.txt')).write_text(p.stderr,encoding='utf-8');assert p.returncode==0,'fixture failed: '+p.stderr
   row=json.loads(p.stdout);result['profiles'].append({'profile':name,**row});print(name+': '+str(row['checks'])+' actual adapter/attachment checks',flush=True)
  cur=(REPO/'native/main.cpp').read_text(encoding='utf-8-sig');assert all(ctx['source_function'](cur,sig)==b for sig,b in blocks.items()),'function drift';a=cur.index('        const auto keyboardDeviceOrigins=exportKeyboardDeviceOriginEvidence();');b=cur.index('        const auto msiOfficialModeEvidence=',a);assert cur[a:b]==attachment,'attachment drift';assert all((REPO/p).read_bytes()==b for p,b in inputs.items()),'input drift';result['pass']=True
 except Exception as e:result['error']=str(e)
 save(run/'result.json',result);save(OUT/'latest.json',{'run':str(run),'pass':result['pass']});print('Evidence: '+str(run),flush=True);return 0 if result['pass'] else 1
if __name__=='__main__':raise SystemExit(main())
