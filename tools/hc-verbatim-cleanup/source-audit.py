"""Bounded repository source/name audit; no binary decompilation or target execution."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
REPO=Path(__file__).resolve().parents[2]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();assert not a.out.exists()
 names=subprocess.run(['git','ls-files','-c','-o','--exclude-standard','-z'],cwd=REPO,stdout=subprocess.PIPE,check=True).stdout.decode('utf-8').split('\0')
 pattern=re.compile(r'\b(?:ControllerState|ButtonState|AxisState|GyroState|InputUtils|AxisFlags|ButtonFlags|AxisLayoutFlags)\b|HcVerbatim')
 suffixes={'.cs','.csproj','.props','.targets','.json','.jsonc','.ts','.tsx','.js','.mjs','.cjs','.py','.ps1','.cpp','.h','.hpp','.c','.md','.txt','.yml','.yaml','.xml','.xaml','.config','.cmd','.bat','.csv','.sha256'}
 hits=[];scanned=[]
 for name in sorted(set(names)):
  p=REPO/name
  if not name or not p.is_file() or p.suffix.lower() not in suffixes or name.startswith('tools/hc-verbatim-cleanup/'):continue
  try:text=p.read_text(encoding='utf-8-sig')
  except UnicodeError:continue
  scanned.append({'path':name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
  for n,line in enumerate(text.splitlines(),1):
   if pattern.search(line):hits.append({'path':name,'line':n,'text':line})
 a.out.write_text(json.dumps({'scope':'Git tracked and nonignored untracked text inputs; tool itself excluded. Frozen build and normal package metadata are separately audited.','scannedTextFiles':scanned,'count':len(scanned),'hits':hits},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'files':len(scanned),'hits':len(hits)}))
if __name__=='__main__':main()
