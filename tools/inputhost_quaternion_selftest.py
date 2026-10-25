#!/usr/bin/env python3
"""Source-exact SDK-free full-parser quaternion regression; never runs InputHost Main."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,subprocess,sys
REPO=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,o):p.write_text(json.dumps(o,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def member(text,marker,semi=False):
    assert text.count(marker)==1,marker
    start=text.index(marker);brace=text.find('{',start);end=text.find(';',start)
    if end>=0 and (brace<0 or end<brace):return text[start:end+1]
    depth=0
    for m in re.finditer(r'//[^\n]*|/\*.*?\*/|@"(?:""|[^"])*"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',text[brace:],re.S):
        if m[0]=='{':depth+=1
        elif m[0]=='}':
            depth-=1
            if depth==0:
                end=brace+m.end()
                if semi:
                    tail=re.match(r'\s*;',text[end:]);assert tail;end+=tail.end()
                return text[start:end]
    raise AssertionError('Unclosed '+marker)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--source',type=Path,default=REPO/'InputHost/Program.cs');a=ap.parse_args()
    out=a.out.resolve();assert out.is_relative_to((REPO.parents[2]/'Build/Validation').resolve()),'Output must be in workspace Build/Validation';assert not out.exists(),'Fresh output required; previous logs are retained';out.mkdir(parents=True)
    src=a.source.resolve();program=src.read_text(encoding='utf-8-sig');inputs={str(src):sha(src)};blocks=[];proof=[]
    def add(text,marker,origin,semi=False):
        body=member(text,marker,semi);blocks.append(body);proof.append({'source':str(origin),'marker':marker,'sha256Utf8Lf':hashlib.sha256(body.encode()).hexdigest(),'sourceExact':True})
    for n in ['BaseFields','FrameFields','HidHideFields']:add(program,'private static readonly HashSet<string> '+n,src,True)
    names=['private static bool TryParseEnvelope(string','private static CommandEnvelope AttachReportChainMetadata1410Safely(',
      'private static bool TryReadString(','private static bool TryReadUInt64(','private static bool TryReadInt32(',
      'private static bool TryReadShort(','private static bool TryReadByte(','private static bool TryReadAxis(',
      'private static bool TryReadTrigger(','private static void TryReadImuAxis(','private static bool IsSha256(',
      'private static string CanonicalFrameHash(','private static void AppendFrameHashBits(','private sealed record CommandEnvelope(']
    if 'private static bool TryParseEnvelopeDocument(' in program:names.append('private static bool TryParseEnvelopeDocument(')
    for m in names:add(program,m,src)
    for name,markers in [('ScreenTouchpadReport.cs',['private readonly record struct ScreenPad(','private static bool TryReadScreenPads(','private static bool TryReadScreenPad(']),('ScreenButtonReport.cs',['private static bool TryReadScreenButtons('])]:
        path=REPO/'InputHost'/name;inputs[str(path)]=sha(path);s=path.read_text(encoding='utf-8-sig')
        for m in markers:add(s,m,path)
    release=REPO/'InputHost/MsiSteamDeckReleaseFence1726.cs';inputs[str(release)]=sha(release);assert 'public const string PrepareField = "msiSteamDeckReleaseFence";' in release.read_text(encoding='utf-8-sig')
    pre='using System.Text;using System.Text.Json;using System.Security.Cryptography;using YeManInputHost.Diagnostics;\ninternal static class ActualParser { const int ProtocolVersion=1; const int MaxMessageBytes=16*1024;\n[ThreadStatic] private static Dictionary<string,JsonElement>? t_fieldCache;\nprivate static class MsiSteamDeckReleaseFence1726 {internal const string PrepareField="msiSteamDeckReleaseFence";}\n'
    adapter="""
internal static Observation Parse(string message) {
 var ok=TryParseEnvelope(message,out var e,out var error);
 return new(ok,error,e is null?null:[e.ImuQuatW,e.ImuQuatX,e.ImuQuatY,e.ImuQuatZ],
 e is null?null:[e.ImuGyroX,e.ImuGyroY,e.ImuGyroZ,e.ImuAccelX,e.ImuAccelY,e.ImuAccelZ],e?.ImuAdmitted??false,e?.LtRaw??0,e?.RtRaw??0);
}
}
"""
    (out/'ActualParser.cs').write_text(pre+'\n'.join(blocks)+adapter,encoding='utf-8')
    files=['ActualParser.cs','inputhost_quaternion_selftest.cs']
    fixture=REPO/'tools/inputhost_quaternion_selftest.cs';inputs[str(fixture)]=sha(fixture);(out/fixture.name).write_bytes(fixture.read_bytes())
    for name in ['MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs']:
        path=REPO/'InputHost'/name;inputs[str(path)]=sha(path);(out/name).write_bytes(path.read_bytes());files.append(name)
    project='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><ImportDirectoryBuildProps>false</ImportDirectoryBuildProps><ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets></PropertyGroup><ItemGroup>'+''.join('<Compile Include="'+f+'" />' for f in files)+'</ItemGroup></Project>'
    (out/'QuaternionFixture.csproj').write_text(project,encoding='utf-8');(out/'NuGet.Config').write_text('<configuration><packageSources><clear /></packageSources></configuration>',encoding='utf-8')
    save(out/'source-extraction.json',{'inputs':inputs,'members':proof,'onlyReleaseConstantStubbed':True,'productsSdkOrDevicesRun':False})
    env=dict(os.environ);env.update({'DOTNET_CLI_UI_LANGUAGE':'en-US','DOTNET_CLI_HOME':str(out/'cli-home'),'DOTNET_SKIP_FIRST_TIME_EXPERIENCE':'1','DOTNET_CLI_TELEMETRY_OPTOUT':'1'})
    jobs=[];failure=None
    try:
      for profile in ['Release','Debug']:
        commands=[(profile+'-build',['dotnet','build','QuaternionFixture.csproj','-c',profile,'--configfile','NuGet.Config','--nologo','-v','minimal','-p:UseSharedCompilation=false','-nodeReuse:false']), (profile+'-fixture',['dotnet',str(out/'bin'/profile/'net10.0-windows/QuaternionFixture.dll'),str(out/(profile+'-result.json'))])]
        for name,cmd in commands:
          with (out/(name+'-stdout.log')).open('w',encoding='utf-8') as stdout,(out/(name+'-stderr.log')).open('w',encoding='utf-8') as stderr:
            proc=subprocess.Popen(cmd,cwd=out,env=env,stdout=stdout,stderr=stderr)
            try:code=proc.wait(timeout=180)
            except BaseException:proc.kill();proc.wait();raise
          jobs.append({'name':name,'command':cmd,'pid':proc.pid,'terminal':True,'exitCode':code});save(out/'jobs.json',jobs);assert code==0,(name,code)
    except BaseException as ex:failure=repr(ex)
    current=all(Path(n).exists() and sha(Path(n))==v for n,v in inputs.items())
    result={'pass':failure is None and current,'failure':failure,'sourceInputsStillMatch':current,'sourceProgramSha256':inputs[str(src)],'jobs':jobs,'allJobsTerminal':all(x['terminal'] for x in jobs),'productsSdkOrDevicesRun':False}
    save(out/'result.json',result);print(json.dumps(result));return 0 if result['pass'] else 1
if __name__=='__main__':sys.exit(main())
