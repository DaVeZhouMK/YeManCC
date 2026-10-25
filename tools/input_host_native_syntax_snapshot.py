#!/usr/bin/env python3
"""Compile the complete production native TU from a coherent immutable source snapshot.

A snapshot PASS is scoped to its retained bytes. Current-tree drift is reported
separately and never promoted to whole-current-tree or hardware acceptance.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,subprocess,sys,traceback
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
AUTH='tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
SELF='tools/input_host_native_syntax_snapshot.py'
sha=lambda b:hashlib.sha256(b).hexdigest()
def save(p,v):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def need(v,s):
 if not v:raise RuntimeError(s)
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',required=True,type=Path);a=ap.parse_args();out=a.out.resolve()
 need(a.out.is_absolute() and not out.exists() and not out.is_relative_to(ROOT.resolve()),'fresh absolute evidence outside source required');out.mkdir(parents=True)
 receipt={'pass':False,'snapshotCompiled':False,'currentTreeAccepted':False,'hardwareAccepted':False,'fullLifecycleAccepted':False,'status':'PREPARING','startedAt':datetime.datetime.now().astimezone().isoformat()}
 try:
  names=[]
  for folder in ['native','capabilities','deps/webview2/build/native/include']:
   names.extend(str(p.relative_to(ROOT)).replace('\\','/') for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix.lower() in ('.h','.hpp','.cpp','.inc','.inl','.tpp','.idl','.rc'))
  names=sorted(set([*names,'deps/json/json.hpp','native/build_native.bat',AUTH,SELF]))
  original={n:(ROOT/n).read_bytes() for n in names};hashes={n:sha(b) for n,b in original.items()};snapshot=out/'source'
  for n,b in original.items():p=snapshot/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  captureDrift=[n for n,h in hashes.items() if not (ROOT/n).is_file() or sha((ROOT/n).read_bytes())!=h]
  receipt.update(sourceHashes=hashes,snapshotRoot=str(snapshot),sourceCaptureDrift=captureDrift,sourceCount=len(hashes));save(out/'SOURCE-MANIFEST.json',receipt)
  need(not captureDrift,'source changed during snapshot capture; snapshot not compiled')
  tools=json.loads(original[AUTH]);compiler=Path(tools['native']['compiler']);declared=next(row for row in tools['files'] if Path(row['path'])==compiler)
  need(sha(compiler.read_bytes())==declared['sha256'],'compiler identity changed')
  build=original['native/build_native.bat'].decode('utf-8-sig');line=next(r for r in build.splitlines() if r.startswith('cl ') and ' /c main.cpp ' in r)
  defines=list(dict.fromkeys(re.findall(r'/D[^\s]+',line)));defines.append('/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1')
  env={k:v for k,v in os.environ.items() if re.fullmatch(r'(?:SystemRoot|windir|SystemDrive|ComSpec|OS|PROCESSOR_ARCHITECTURE|NUMBER_OF_PROCESSORS|ProgramFiles|ProgramFiles\(x86\)|ProgramData|LOCALAPPDATA|APPDATA|USERPROFILE|USERNAME|HOMEDRIVE|HOMEPATH|PATHEXT)',k,re.I)}
  windows=Path(env.get('SystemRoot','C:/Windows'));temp=out/'tool-temp';temp.mkdir()
  env.update(PATH=';'.join(map(str,[compiler.parent,windows/'System32',windows])),INCLUDE=';'.join(tools['native']['include']),LIB=';'.join(tools['native']['lib']),TEMP=str(temp),TMP=str(temp),VSLANG='1033')
  argv=[str(compiler),'/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj',*defines,'/Zs',str(snapshot/'native/main.cpp'),'/I'+str(snapshot/'deps/webview2/build/native/include'),'/I'+str(snapshot/'deps/json'),'/I'+tools['native']['include'][-1]]
  receipt.update(command=argv,compiler={'path':str(compiler),'sha256':declared['sha256']},productionDefines=defines)
  proc=subprocess.run(argv,cwd=snapshot/'native',env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
  raw=proc.stdout+proc.stderr
  try:text=raw.decode('utf-8')
  except UnicodeDecodeError:text=raw.decode('mbcs',errors='replace')
  (out/'build.log').write_text(text,encoding='utf-8')
  snapshotDrift=[n for n,h in hashes.items() if not (snapshot/n).is_file() or sha((snapshot/n).read_bytes())!=h]
  currentDrift=[n for n,h in hashes.items() if not (ROOT/n).is_file() or sha((ROOT/n).read_bytes())!=h]
  receipt.update(exitCode=proc.returncode,snapshotDrift=snapshotDrift,currentTreeDrift=currentDrift,snapshotCompiled=proc.returncode==0 and not snapshotDrift,
   mainSha256=hashes['native/main.cpp'],currentMainSha256=sha((ROOT/'native/main.cpp').read_bytes()),currentMatchesSnapshot=not currentDrift)
  receipt['pass']=receipt['snapshotCompiled'];receipt['status']='PASS_EXACT_NATIVE_SNAPSHOT' if receipt['pass'] else 'FAIL_NATIVE_SNAPSHOT_COMPILE'
  need(sha(compiler.read_bytes())==declared['sha256'],'compiler changed during syntax build')
 except Exception as e:receipt['error']=str(e);receipt['traceback']=traceback.format_exc();receipt['status']='FAIL';receipt['pass']=False
 receipt['finishedAt']=datetime.datetime.now().astimezone().isoformat();save(out/'RESULT.json',receipt)
 print(json.dumps({k:receipt.get(k) for k in ['pass','status','exitCode','sourceCount','sourceCaptureDrift','snapshotDrift','currentTreeDrift','error']},ensure_ascii=False));print(out);return 0 if receipt['pass'] else 1
if __name__=='__main__':raise SystemExit(main())
