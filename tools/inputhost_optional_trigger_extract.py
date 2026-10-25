#!/usr/bin/env python3
"""Extract R49 real parser members into SDK-free verification fixtures. Never executes a product."""
from pathlib import Path
import argparse,hashlib,json,re
REPO=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def member(text,marker,semicolon=False):
    assert text.count(marker)==1,marker
    start=text.index(marker);brace=text.find('{',start);semi=text.find(';',start)
    if semi>=0 and (brace<0 or semi<brace):return text[start:semi+1]
    tokens=re.compile(r'//[^\n]*|/\*.*?\*/|@"(?:""|[^"])*"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',re.S);depth=0
    for m in tokens.finditer(text,brace):
        if m[0]=='{':depth+=1
        elif m[0]=='}':
            depth-=1
            if depth==0:
                end=m.end()
                if semicolon:
                    tail=re.match(r'\s*;',text[end:]);assert tail;end+=tail.end()
                return text[start:end]
    raise RuntimeError('Unclosed member '+marker)
def main(baseline,candidate,out):
    assert not out.exists(),'Use a fresh output directory; old failures/evidence remain'
    out.mkdir(parents=True)
    base=baseline.read_text(encoding='utf-8-sig');new=candidate.read_text(encoding='utf-8-sig')
    before='                TryReadTrigger(properties, "ltRaw", out ltRaw);\n                TryReadTrigger(properties, "rtRaw", out rtRaw);'
    after='                if (properties.ContainsKey("ltRaw")) TryReadTrigger(properties, "ltRaw", out ltRaw);\n                if (properties.ContainsKey("rtRaw")) TryReadTrigger(properties, "rtRaw", out rtRaw);'
    assert base.count(before)==1 and base.replace(before,after)==new,'Only the two missing-key guards may differ'
    screen=(REPO/'InputHost/ScreenTouchpadReport.cs').read_text(encoding='utf-8-sig');buttons=(REPO/'InputHost/ScreenButtonReport.cs').read_text(encoding='utf-8-sig')
    markers=['private static bool TryParseEnvelope(','private static CommandEnvelope AttachReportChainMetadata1410Safely(','private static bool TryReadString(','private static bool TryReadUInt64(','private static bool TryReadInt32(','private static bool TryReadShort(','private static bool TryReadByte(','private static bool TryReadAxis(','private static bool TryReadTrigger(','private static void TryReadImuAxis(','private static bool IsSha256(','private static string CanonicalFrameHash(','private static void AppendFrameHashBits(','private static byte TriggerToByte(','private sealed record CommandEnvelope(']
    proof=[]
    for name,src in [('BaselineParser',base),('CandidateParser',new)]:
        blocks=[member(src,'private static readonly HashSet<string> '+field,True) for field in ['BaseFields','FrameFields','HidHideFields']]
        for marker in markers:
            body=member(src,marker);blocks.append(body);proof.append({'variant':name,'marker':marker,'sha256':sha(body.encode())})
        for marker,content in [('private static bool TryReadScreenPads(',screen),('private static bool TryReadScreenPad(',screen),('private static bool TryReadScreenButtons(',buttons)]:blocks.append(member(content,marker))
        pad=re.search(r'private readonly record struct ScreenPad[^;]+;',screen)[0]
        prefix='using System.Text; using System.Text.Json; using System.Security.Cryptography; using YeManInputHost.Diagnostics;\n'
        scaffold='const int ProtocolVersion=1; const int MaxMessageBytes=16384; [ThreadStatic] private static Dictionary<string,JsonElement>? t_fieldCache;\nprivate static class MsiSteamDeckReleaseFence1726 { internal const string PrepareField="msiSteamDeckReleaseFence"; }\n'+pad+'\n'
        bridge='''internal static ParseOutcome Parse(string message) { var ok=TryParseEnvelope(message,out var e,out var error); return new(ok,error,e is null?null:JsonSerializer.Serialize(e,new JsonSerializerOptions { NumberHandling=System.Text.Json.Serialization.JsonNumberHandling.AllowNamedFloatingPointLiterals }),e?.Lt??0,e?.Rt??0,e?.LtRaw??0,e?.RtRaw??0,TriggerToByte(e?.LtRaw??0),TriggerToByte(e?.RtRaw??0)); }
internal static float ForCost(string message) { if(!TryParseEnvelope(message,out var e,out var error))throw new Exception(error);return e!.LtRaw+e.RtRaw; }
'''
        (out/(name+'.cs')).write_text(prefix+'internal static class '+name+' {\n'+scaffold+'\n'.join(blocks)+'\n'+bridge+'}\n',encoding='utf-8')
    for rel in ['InputHost/MsiVirtualReportChain1410.cs','InputHost/CanonicalFrameFingerprintMemo.cs','tools/inputhost_optional_trigger_selftest.cs']:(out/Path(rel).name).write_bytes((REPO/rel).read_bytes())
    files=['BaselineParser.cs','CandidateParser.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','inputhost_optional_trigger_selftest.cs']
    project='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><NuGetAudit>false</NuGetAudit><EnableDefaultCompileItems>false</EnableDefaultCompileItems><StartupObject>OptionalTriggerFixture</StartupObject></PropertyGroup><ItemGroup>'+''.join('<Compile Include="'+name+'" />' for name in files)+'</ItemGroup></Project>'
    (out/'ParserTriggerFixture.csproj').write_text(project,encoding='utf-8')
    (out/'source-extraction.json').write_text(json.dumps({'productionProgramBeforeSha256':sha(baseline.read_bytes()),'candidateProgramSha256':sha(candidate.read_bytes()),'members':proof,'releaseFenceOnlyConstantStubbed':True,'realSdkReferenced':False},indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'extracted':True,'out':str(out),'productExecuted':False}))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();main(a.baseline,a.candidate,a.out)
