"""Actual complete Host response methods; fixture startup only, no Host constructor or I/O."""
from pathlib import Path
import argparse,hashlib,json,datetime,subprocess,re,importlib.util
ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('extract',ROOT/'tools/virtual_wire_number_perf_selftest.py');ex=importlib.util.module_from_spec(spec);spec.loader.exec_module(ex)
H=lambda b:hashlib.sha256(b).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--mode',choices=['candidate','final'],default='candidate');a=ap.parse_args()
 out=a.output/(a.mode+'-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));host=out/'Host';host.mkdir(parents=True)
 inputs=[]
 for p in (ROOT/'InputHost').rglob('*'):
  if not p.is_file() or p.suffix not in ['.cs','.csproj'] or any(x in p.parts for x in ['bin','obj']):continue
  rel=p.relative_to(ROOT/'InputHost');raw=p.read_bytes();dst=host/rel;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(raw);inputs.append({'path':rel.as_posix(),'sha256':H(raw),'bytes':len(raw)})
 current=(ROOT/'InputHost/Program.cs').read_text(encoding='utf-8-sig');baseline=(a.output.parent/'before/InputHost/Program.cs').read_text(encoding='utf-8-sig')
 signature='private string SerializeResponse(CommandEnvelope? envelope, bool ok, string status, string detail, bool? firstFrame, string? releaseDisposition)'
 before=ex.extract(baseline,signature)[2];actual=ex.extract(current,signature)[2]
 start=before.index('            var response = JsonSerializer.Serialize(new');end=before.index('            });',start)+len('            });');old=before[start:end]
 rows=[]
 for line in old.splitlines()[2:-1]:
  val=line.strip().rstrip(',');pair=val.split('=',1);name=pair[0].strip();value=pair[1].strip() if len(pair)==2 else name;rows.append((name,value))
 types={'protocolVersion':'int','ok':'bool','sequence':'ulong','epoch':'ulong','powerGeneration':'ulong','configRevision':'ulong','assetVerified':'bool','virtualEndpointReconciled':'bool','targetPnpAdmitted':'bool','targetPnpRootCount':'int','targetPnpPrimaryRootCount':'int','targetPnpXboxCompanionRootCount':'int','targetPnpHidChildCount':'int','targetPnpAllStarted':'bool','firstFrame':'bool?','releaseDisposition':'string?','submitElapsedUs':'long'}
 declarations=',\n    '.join(types.get(n,'string')+' '+n for n,v in rows)
 plain='        private readonly record struct ReplyReflectionFields(\n    '+declarations+');\n'
 schema='using System;\nusing System.Text.Json;\nusing System.Text.Json.Serialization;\n\n[JsonConverter(typeof(HostResponseFieldsJsonConverter))]\ninternal readonly record struct HostResponseFields(\n    '+declarations+');\n\ninternal sealed class HostResponseFieldsJsonConverter : JsonConverter<HostResponseFields>\n{\n    public HostResponseFieldsJsonConverter() {}\n    public override HostResponseFields Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) => throw new NotSupportedException();\n    public override void Write(Utf8JsonWriter writer, HostResponseFields value, JsonSerializerOptions options)\n    {\n        writer.WriteStartObject();\n'
 for name,val in rows:
  typ=types.get(name,'string')
  if typ=='bool?':schema+='        if (value.'+name+'.HasValue) writer.WriteBoolean("'+name+'", value.'+name+'.Value);\n        else writer.WriteNull("'+name+'");\n'
  else:
   method='WriteBoolean' if typ=='bool' else 'WriteNumber' if typ in ['int','long','ulong'] else 'WriteString';schema+='        writer.'+method+'("'+name+'", value.'+name+');\n'
 schema+='        writer.WriteEndObject();\n    }\n}\n'
 # JsonSerializer caches encoded names; avoid re-encoding the same 32 literal
 # protocol keys in each custom Write. These are immutable metadata, not frames.
 encoded=''.join('    private static readonly JsonEncodedText n_'+n+' = JsonEncodedText.Encode("'+n+'");\n' for n,v in rows)
 schema=schema.replace('    public HostResponseFieldsJsonConverter() {}',encoded+'    public HostResponseFieldsJsonConverter() {}')
 for n,v in rows:
  schema=schema.replace('writer.WriteNumber("'+n+'",','writer.WriteNumber(n_'+n+',').replace('writer.WriteBoolean("'+n+'",','writer.WriteBoolean(n_'+n+',').replace('writer.WriteString("'+n+'",','writer.WriteString(n_'+n+',').replace('writer.WriteNull("'+n+'")','writer.WriteNull(n_'+n+')')
 replacement=lambda name:'            var response = JsonSerializer.Serialize(new '+name+'(\n                '+',\n                '.join(v for n,v in rows)+'));'
 candidate=before[:start]+replacement('HostResponseFields')+before[end:]
 if a.mode=='final':
  assert actual==candidate,'Final production response differs from isolated candidate'
  assert (ROOT/'InputHost/HostResponseFields.cs').read_text(encoding='utf-8-sig')==schema,'Final schema differs from candidate'
 else:(host/'HostResponseFields.cs').write_text(schema,encoding='utf-8')
 methods=before.replace('SerializeResponse(', 'SerializeResponseBeforeR11(',1)+'\n'+before.replace(old,replacement('ReplyReflectionFields'),1).replace('SerializeResponse(','SerializeResponseReflectionR11(',1)
 if a.mode=='candidate':methods+='\n'+candidate.replace('SerializeResponse(','SerializeResponseWriterR11(',1)
 writerMethod='SerializeResponse' if a.mode=='final' else 'SerializeResponseWriterR11'
 bridge=plain+methods+r"""
        internal static object FixtureReplySession(ReplySeed seed) {
            var s=(HostSession)System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(typeof(HostSession));
            typeof(HostSession).GetField("_hostInstanceId",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic)!.SetValue(s,seed.HostId);
            s._virtualEndpoint=new VirtualEndpointObservation(true,seed.Primary,seed.Companion,seed.Child,seed.SdRoots,seed.SdChild,seed.Started,seed.EndpointDetail!);
            s._assetVerified=seed.Asset;s._virtualEndpointReconciled=seed.Reconciled;s._targetPnpAdmitted=seed.Admitted;s._lastSubmitElapsedUs=seed.SubmitUs;
            return s;
        }
        internal static bool FixtureReplyFlag(object s)=>((HostSession)s)._reportChainCapabilityAdvertised1410;
        internal static string FixtureReply(object s,object? e,int variant,bool ok,string status,string detail,bool? first,string? release) {
            var session=(HostSession)s;var envelope=(CommandEnvelope?)e;
            return variant switch {0=>session.SerializeResponseBeforeR11(envelope,ok,status,detail,first,release),1=>session.SerializeResponseReflectionR11(envelope,ok,status,detail,first,release),2=>session.WRITER_METHOD(envelope,ok,status,detail,first,release),_=>throw new ArgumentException()};
        }
""".replace('WRITER_METHOD',writerMethod)
 classAt=current.index('    private sealed class HostSession');opening=current.index('{',classAt);patched=current[:opening+1]+'\n'+bridge+'\n'+current[opening+1:]
 outer=r"""
    internal static object FixtureReplySession(ReplySeed s)=>HostSession.FixtureReplySession(s);
    internal static bool FixtureReplyFlag(object s)=>HostSession.FixtureReplyFlag(s);
    internal static string FixtureReply(object s,object? e,int v,bool ok,string status,string detail,bool? first,string? release)=>HostSession.FixtureReply(s,e,v,ok,status,detail,first,release);
    internal static object? FixtureReplyEnvelope(ReplySeed s)=>s.NullEnvelope?null:new CommandEnvelope(s.Command!,s.RequestId!,s.Sequence,s.RunId!,s.Epoch,s.Power,s.TargetId!,s.Owner!,s.Persona!,s.Profile!,s.Nonce!,s.Revision,s.Hash!){ReportChainPrepareSpecified1410=s.ReportSpecified};
"""
 classAt=patched.index('internal static partial class Program');opening=patched.index('{',classAt);patched=patched[:opening+1]+'\n'+outer+'\n'+patched[opening+1:];(host/'Program.cs').write_text(patched,encoding='utf-8')
 fixture=(ROOT/'tools/inputhost_response_local_perf_selftest.cs').read_bytes();(host/'ReplyFixture.cs').write_bytes(fixture)
 project=(host/'YeManInputHost.csproj').read_text(encoding='utf-8-sig');project=re.sub(r'<HintPath>(.*?)</HintPath>',lambda m:'<HintPath>'+str((ROOT/'InputHost'/m[1].replace('$(MSBuildThisFileDirectory)','')).resolve()).replace('&','&amp;')+'</HintPath>',project,flags=re.S);project=project.replace('<AssemblyName>YeManInputHost</AssemblyName>','<AssemblyName>ResponseLocalSelftest</AssemblyName>').replace('<PropertyGroup>','<PropertyGroup>\n<StartupObject>ReplyLocalFixture</StartupObject><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion><RollForward>Disable</RollForward>');(host/'YeManInputHost.csproj').write_text(project,encoding='utf-8')
 references=[{'path':m.group(1).replace('&amp;','&'),'sha256':H(Path(m.group(1).replace('&amp;','&')).read_bytes())} for m in re.finditer(r'<HintPath>(.*?)</HintPath>',project,flags=re.S)]
 manifest={'mode':a.mode,'sourceFiles':inputs,'beforeMethodSha256':H(before.encode()),'candidateMethodSha256':H(candidate.encode()),'currentActualMethodSha256':H(actual.encode()),'schemaSha256':H(schema.encode()),'fixtureSha256':H(fixture),'runnerSha256':H(Path(__file__).read_bytes()),'propertyOrder':[n for n,v in rows],'propertyTypes':{n:types.get(n,'string') for n,v in rows},'references':references,'finalUsesActualProductionMethod':a.mode=='final','noProductConstructorOrMainOrPipeOrDeviceCalls':True,'staticDataOnlyProgramTypeInitializationMayRun':True}
 (out/'candidate-schema.cs').write_text(schema,encoding='utf-8');(out/'candidate-response-method.cs').write_text(candidate,encoding='utf-8');(out/'original-response-method.cs').write_text(before,encoding='utf-8');(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
 def run(args,log,timeout=180):
  x=subprocess.run(['dotnet',*args],cwd=host,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout,env=__import__('os').environ|{'DOTNET_TieredCompilation':'1','DOTNET_TieredPGO':'1','DOTNET_CLI_TELEMETRY_OPTOUT':'1'});(out/log).write_text(x.stdout+x.stderr,encoding='utf-8');assert x.returncode==0,log+'\n'+x.stdout+x.stderr
 empty=out/'empty-nuget-source';empty.mkdir();run(['restore','YeManInputHost.csproj','--source',str(empty.resolve()),'-p:NuGetAudit=false'],'restore.log');profiles=[]
 for config in ['Release','Debug']:
  run(['build','YeManInputHost.csproj','-c',config,'--no-restore','--nologo'],'build-'+config+'.log');dir=out/config;run([str((host/'bin'/config/'net10.0-windows/ResponseLocalSelftest.dll').resolve()),str(dir.resolve()),'benchmark' if a.mode=='candidate' and config=='Release' else 'verify'],'run-'+config+'.log',240);d=json.loads((dir/'result.json').read_text());assert d['pass'];profiles.append({'config':config,**d});print(config,d['checks'],'checks')
 assert all(H((ROOT/'InputHost'/r['path']).read_bytes())==r['sha256'] for r in inputs),'Source drift';assert all(H(Path(r['path']).read_bytes())==r['sha256'] for r in references),'Reference drift'
 manifest['sourceStableDuringRun']=True;result={'pass':True,'runDirectory':str(out.resolve()),'manifest':manifest,'profiles':profiles};(out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');(a.output/(a.mode+'-result.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(out)
if __name__=='__main__':main()
