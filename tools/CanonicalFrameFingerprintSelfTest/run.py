from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys,datetime
p=argparse.ArgumentParser();p.add_argument('--before',required=True);p.add_argument('--output',required=True);args=p.parse_args()
HERE=Path(__file__).resolve().parent;PROJECT=HERE.parent.parent;OUT=Path(args.output).resolve();OUT.mkdir(parents=True,exist_ok=True)
BEFORE=Path(args.before).resolve();beforeBytes=BEFORE.read_bytes();source=beforeBytes.decode('utf-8-sig');helperPath=PROJECT/'InputHost/CanonicalFrameFingerprintMemo.cs';helperBytes=helperPath.read_bytes();helper=helperBytes.decode('utf-8-sig')
def extract(name):
 match=re.search(r'^    private static (?:string|void) '+name+r'\([^\n]*\)',source,re.M)
 if not match:raise ValueError('missing exact before function '+name)
 start=match.start();op=source.index('{',match.end());depth=1;i=op+1
 while depth:
  if source[i]=='{':depth+=1
  elif source[i]=='}':depth-=1
  i+=1
 return source[start:i]
legacy=extract('CanonicalFrameHash');append=extract('AppendFrameHashBits')
shaBlock=re.compile(r'        Span<byte> hash = stackalloc byte\[32\];\r?\n        SHA256.HashData\(canonical\[\.\.count\], hash\);\r?\n        return Convert.ToHexStringLower\(hash\);')
assert len(shaBlock.findall(legacy))==1
memo=shaBlock.sub('        return CanonicalFrameFingerprintMemo.GetOrCompute(canonical[..count]);',legacy)
legacyPublic=legacy.replace('private static string CanonicalFrameHash','internal static string Compute',1);memoPublic=memo.replace('private static string CanonicalFrameHash','internal static string Compute',1)
code='using System;\nusing System.Security.Cryptography;\n\ninternal static class LegacyCanonical\n{\n'+legacyPublic+'\n'+append+'\n}\n\ninternal static class MemoizedCanonical\n{\n'+memoPublic+'\n'+append+'\n}\n'
(OUT/'GeneratedCanonical.cs').write_text(code,'utf-8')
assert helper.count('SHA256.HashData(canonical, hash);')==1
fault=helper.replace('internal static class CanonicalFrameFingerprintMemo','internal static class CanonicalFrameFingerprintMemoFaultFixture',1).replace('SHA256.HashData(canonical, hash);','FingerprintFaultInjection.HashData(canonical, hash);',1)
(OUT/'GeneratedFaultMemo.cs').write_text(fault,'utf-8')
sha=lambda x:hashlib.sha256(x).hexdigest()
manifest={'schema':1,'createdAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'beforePath':str(BEFORE),'beforeProgramSha256':sha(beforeBytes),'beforeCanonicalFunctionSha256':sha(legacy.encode()),'beforeAppendBitsFunctionSha256':sha(append.encode()),'helperPath':str(helperPath),'helperSha256':sha(helperBytes),'generatedCanonicalSha256':sha(code.encode()),'faultFixtureSha256':sha(fault.encode()),'faultFixtureTransformation':'exact helper source; only class name and SHA target replaced','productOrHardwareAccess':False,'productionProgramEditedByRunner':False}
currentProgram=(PROJECT/'InputHost/Program.cs').read_bytes().decode('utf-8-sig')
currentMatch=re.search(r'^    private static string CanonicalFrameHash\([^\n]*\)\r?\n    \{.*?\r?\n    \}',currentProgram,re.M|re.S)
if currentMatch and 'CanonicalFrameFingerprintMemo.GetOrCompute' in currentMatch.group():
 expected=memo.replace('Only the returned 64-character hash is allocated.','Byte-identical frames reuse only a successful hash.')
 assert currentMatch.group()==expected,'integrated function differs from measured candidate'
 manifest['actualIntegratedFunctionMatchesCandidate']=True
 manifest['actualIntegratedFunctionSha256']=sha(currentMatch.group().encode())
else: manifest['actualIntegratedFunctionMatchesCandidate']=False
manifest['toolFiles']=[{'path':str(x),'sha256':sha(x.read_bytes())} for x in [HERE/'Program.cs',HERE/'CanonicalFrameFingerprintSelfTest.csproj',Path(__file__)]]
(OUT/'input-source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n','utf-8')
env=os.environ.copy();env.update({'DOTNET_CLI_HOME':str(OUT/'dotnet-home'),'NUGET_PACKAGES':str(OUT/'nuget'),'DOTNET_CLI_TELEMETRY_OPTOUT':'1','DOTNET_SKIP_FIRST_TIME_EXPERIENCE':'1','DOTNET_GENERATE_ASPNET_CERTIFICATE':'false','TEMP':str(OUT/'temp'),'TMP':str(OUT/'temp')})
(OUT/'temp').mkdir(exist_ok=True)
cmd=['dotnet','build',str(HERE/'CanonicalFrameFingerprintSelfTest.csproj'),'-c','Release','--nologo','-p:EvidenceRoot='+str(OUT),'-p:BaseIntermediateOutputPath='+str(OUT/'obj')+os.sep,'-p:OutputPath='+str(OUT/'bin')+os.sep,'-p:RestoreIgnoreFailedSources=true','-p:NuGetAudit=false']
with (OUT/'build.log').open('w',encoding='utf-8') as f:result=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,env=env)
if result.returncode:
 print((OUT/'build.log').read_text('utf-8'));sys.exit(result.returncode)
binary=OUT/'bin/CanonicalFrameFingerprintSelfTest.dll'
with (OUT/'run.log').open('w',encoding='utf-8') as f:result=subprocess.run(['dotnet',str(binary),str(OUT)],stdout=f,stderr=subprocess.STDOUT,env=env)
manifest['binarySha256']=sha(binary.read_bytes());manifest['exitCode']=result.returncode;manifest['helperUnchangedDuringRun']=helperBytes==helperPath.read_bytes();manifest['beforeUnchangedDuringRun']=beforeBytes==BEFORE.read_bytes();(OUT/'run-binding.json').write_text(json.dumps(manifest,indent=2)+'\n','utf-8')
if result.returncode:
 print((OUT/'run.log').read_text('utf-8'));sys.exit(result.returncode)
r=json.loads((OUT/'results.json').read_text('utf-8'));print('CORRECTNESS',r['correctness']);print('DYNAMIC_GATE',r['benchmarks']['dynamicGatePassed'])
for x in r['benchmarks']['cases']:print(x['name'],'old_ns',round(x['oldMedianNs'],2),'new_ns',round(x['newMedianNs'],2),'ratio',round(x['ratio'],3),'old_bytes',x['oldBytesPerCall'],'new_bytes',x['newBytesPerCall'])
print('EVIDENCE',OUT)
