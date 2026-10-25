import importlib.util,pathlib,json,sys,zipfile
source,evidence,package,binding=map(pathlib.Path,sys.argv[1:5]);evidence.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('home_verifier',source/'tools/gaming-home/verify-package.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
b=json.loads(binding.read_text(encoding='utf-8-sig'))
with zipfile.ZipFile(package) as z:original={n:z.read(n) for n in z.namelist()}
rows=[]
for name,expected in [('exe-bytes','blockmap hash mismatch YeManGamingHome.exe'),('blockmap-bytes','signed digest mismatch: AppxBlockMap.xml'),('extra-file','unbound extra ZIP file')]:
 payload=original.copy()
 if name=='exe-bytes':
  data=bytearray(payload['YeManGamingHome.exe']);data[-1]^=1;payload['YeManGamingHome.exe']=bytes(data)
 elif name=='blockmap-bytes':payload['AppxBlockMap.xml']+=b' '
 else:payload['unexpected.exe']=b'not executable'
 path=evidence/(name+'.msix')
 with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
  for n,data in payload.items():z.writestr(n,data)
 try:m.verify(path,b,source/'native/GamingHome')
 except ValueError as e:
  if expected not in str(e):raise
  rows.append({'name':name,'detected':True,'expectedFailure':str(e)})
 else:raise RuntimeError('tamper not rejected: '+name)
result={'status':'PASS','cases':rows,'packageInstalled':False};(evidence/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
