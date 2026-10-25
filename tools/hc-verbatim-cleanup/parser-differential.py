"""Execute only source-exact pure InputHost parsers, never Program/SDK/device code."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
REPO=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--attempt',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=False)
 runner=REPO/'tools/inputhost_quaternion_selftest.py';spec=importlib.util.spec_from_file_location('prior',runner);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 inputs=[];proof=[]
 for side,cls in [('baseline','BaselineParser'),('candidate','CandidateParser')]:
  root=a.attempt/side;src=root/'InputHost/Program.cs';program=src.read_text(encoding='utf-8-sig');blocks=[]
  def add(text,marker,source,semi=False):
   block=module.member(text,marker,semi);blocks.append(block);proof.append({'side':side,'source':str(source),'marker':marker,'sha256':hashlib.sha256(block.encode()).hexdigest(),'sourceExact':True})
  for n in ['BaseFields','FrameFields','HidHideFields']:add(program,'private static readonly HashSet<string> '+n,src,True)
  names=['private static bool TryGetVirtualOutputProfile(','private static bool TryReadImuQuaternion(','private static bool TryParseEnvelope(string','private static CommandEnvelope AttachReportChainMetadata1410Safely(','private static bool TryReadString(','private static bool TryReadUInt64(','private static bool TryReadInt32(','private static bool TryReadShort(','private static bool TryReadByte(','private static bool TryReadAxis(','private static bool TryReadTrigger(','private static void TryReadImuAxis(','private static bool IsSha256(','private static string CanonicalFrameHash(','private static void AppendFrameHashBits(','private sealed record CommandEnvelope(']
  if 'private static bool TryParseEnvelopeDocument(' in program:names.append('private static bool TryParseEnvelopeDocument(')
  for marker in names:add(program,marker,src)
  origins=[src]
  for name,markers in [('ScreenTouchpadReport.cs',['private readonly record struct ScreenPad(','private static bool TryReadScreenPads(','private static bool TryReadScreenPad(']),('ScreenButtonReport.cs',['private static bool TryReadScreenButtons('])]:
   f=root/'InputHost'/name;origins.append(f);text=f.read_text(encoding='utf-8-sig')
   for marker in markers:add(text,marker,f)
  release=root/'InputHost/MsiSteamDeckReleaseFence1726.cs';assert 'public const string PrepareField = "msiSteamDeckReleaseFence";' in release.read_text(encoding='utf-8-sig');origins.append(release)
  prefix='using System.Text;using System.Text.Json;using System.Security.Cryptography;using YeManInputHost.Diagnostics;\ninternal static class '+cls+' { const int ProtocolVersion=1; const int MaxMessageBytes=16*1024;\n[ThreadStatic] private static Dictionary<string,JsonElement>? t_fieldCache;\nprivate static class MsiSteamDeckReleaseFence1726 {internal const string PrepareField="msiSteamDeckReleaseFence";}\n'
  adapter='\npublic static string Snapshot(string message) { var ok=TryParseEnvelope(message,out var e,out var error); return JsonSerializer.Serialize(new { accepted=ok,error,envelope=e }); }\n}\n'
  (a.out/(cls+'.cs')).write_text(prefix+'\n'.join(blocks)+adapter,encoding='utf-8')
  for f in origins:inputs.append({'path':str(f),'sha256':sha(f)})
 for name in ['MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs']:
  b=a.attempt/'baseline/InputHost'/name;c=a.attempt/'candidate/InputHost'/name;assert b.read_bytes()==c.read_bytes();(a.out/name).write_bytes(b.read_bytes());inputs.extend([{'path':str(x),'sha256':sha(x)} for x in [b,c]])
 fixture=Path(__file__).with_name('ParserFixture.cs');(a.out/fixture.name).write_bytes(fixture.read_bytes());inputs.append({'path':str(fixture),'sha256':sha(fixture)})
 (a.out/'ParserFixture.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><NuGetAudit>false</NuGetAudit><ImportDirectoryBuildProps>false</ImportDirectoryBuildProps><ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets></PropertyGroup></Project>',encoding='utf-8');(a.out/'NuGet.Config').write_text('<configuration><packageSources><clear /></packageSources></configuration>',encoding='utf-8')
 save(a.out/'source-extraction.json',{'inputs':inputs,'members':proof,'inertReleaseConstantOnly':True,'productsSdkDevicesRun':False})
 env=dict(os.environ);env.update(DOTNET_CLI_UI_LANGUAGE='en-US',DOTNET_CLI_TELEMETRY_OPTOUT='1',DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1');jobs=[]
 for profile in ['Release','Debug']:
  commands=[('build',['dotnet','build','ParserFixture.csproj','-c',profile,'--configfile','NuGet.Config','--nologo','-v','minimal','-p:UseSharedCompilation=false','-nodeReuse:false']),('fixture',['dotnet',str(a.out/'bin'/profile/'net10.0-windows/ParserFixture.dll'),str(a.out/(profile+'-result.json'))])]
  for name,cmd in commands:
   with (a.out/(profile+'-'+name+'.log')).open('w',encoding='utf-8') as log:
    proc=subprocess.Popen(cmd,cwd=a.out,env=env,stdout=log,stderr=subprocess.STDOUT);code=proc.wait()
   jobs.append({'profile':profile,'name':name,'command':cmd,'pid':proc.pid,'exitCode':code,'terminal':True});save(a.out/'jobs.json',jobs)
   if code:raise RuntimeError((profile,name,code))
 stable=all(sha(Path(i['path']))==i['sha256'] for i in inputs);save(a.out/'result.json',{'pass':stable,'sourceStable':stable,'jobs':jobs,'productsSdkDevicesRun':False});assert stable
 print(json.dumps({'pass':True,'out':str(a.out)}))
if __name__=='__main__':main()
