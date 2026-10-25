from pathlib import Path
import argparse,hashlib,json,subprocess,os,sys

def extract_last(text):
 sig='private static void ValidateCurve(IReadOnlyList<FanNode> nodes)';start=text.rindex(sig);i=text.index('{',start);depth=1;end=i+1
 while depth:
  if text[end]=='{':depth+=1
  elif text[end]=='}':depth-=1
  end+=1
 return text[start:end]

def main():
 p=argparse.ArgumentParser();p.add_argument('--feature-result',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1];out=a.output.resolve();source=root/'FanLab/real-host/Program.cs'
 if out.exists():raise RuntimeError('Output must be new')
 out.mkdir(parents=True);raw=source.read_bytes();method=extract_last(raw.decode('utf-8-sig'));(out/'HostCurveExtract.cs.txt').write_text(method,encoding='utf-8');data=json.loads(a.feature_result.read_text(encoding='utf-8'));assert data['hostCurveCases'],'No effective feature curves to check'
 cs=r'''using System.Text.Json;
sealed record FanNode(double TempC,double DutyPercent);
sealed class FanApiException : Exception {public string Code{get;}public FanApiException(int status,string code,string message):base(message){Code=code;}}
static class HostContract {
 public static bool Accepts(IReadOnlyList<FanNode> nodes){try{ValidateCurve(nodes);return true;}catch(FanApiException e)when(e.Code=="INVALID_CURVE"){return false;}}
__HOST_CURVE_METHOD__
}
static class Program {
 static FanNode[] Curve(JsonElement value)=>value.EnumerateArray().Select(n=>new FanNode(n.GetProperty("tempC").GetDouble(),n.GetProperty("dutyPercent").GetDouble())).ToArray();
 static int Main(string[] args){using var doc=JsonDocument.Parse(File.ReadAllText(args[0]));var results=new List<object>();var failed=0;var checks=0;
 foreach(var row in doc.RootElement.GetProperty("hostCurveCases").EnumerateArray()){
  var name=row.GetProperty("name").GetString();var input=HostContract.Accepts(Curve(row.GetProperty("input")));var output=HostContract.Accepts(Curve(row.GetProperty("output")));var expected=row.GetProperty("inputAccepted").GetBoolean();checks+=2;if(input!=expected)failed++;if(!output)failed++;
  results.Add(new{name,inputAccepted=input,expectedInputAccepted=expected,effectiveCurveAccepted=output});
 }
 Console.WriteLine(JsonSerializer.Serialize(new{ok=failed==0,checks,failed,results,hardwareCalls=0,scope="exact HostEngine ValidateCurve; synthetic normalized settings only; standalone net10 fixture, no Host/HC/device loaded"}));return failed==0?0:1;}
}
'''.replace('__HOST_CURVE_METHOD__',method)
 (out/'Program.cs').write_text(cs,encoding='utf-8');(out/'HostCurveContract.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="Program.cs" /></ItemGroup></Project>',encoding='utf-8');(out/'NuGet.Config').write_text('<configuration><packageSources><clear /></packageSources></configuration>',encoding='utf-8')
 env=dict(os.environ,DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER='1',DOTNET_CLI_TELEMETRY_OPTOUT='1',DOTNET_NOLOGO='1',DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1');r=subprocess.run(['dotnet','build',str(out/'HostCurveContract.csproj'),'-c','Release','-o',str(out/'bin'),'--disable-build-servers','-p:UseSharedCompilation=false','-v:q'],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',env=env);(out/'build.out').write_text(r.stdout+r.stderr,encoding='utf-8')
 if r.returncode:print(r.stdout+r.stderr);return 2
 r=subprocess.run(['dotnet',str(out/'bin/HostCurveContract.dll'),str(a.feature_result.resolve())],capture_output=True,text=True,encoding='utf-8',env=env);(out/'run.out').write_text(r.stdout+r.stderr,encoding='utf-8');v=json.loads(r.stdout);assert r.returncode in(0,1);assert extract_last(source.read_bytes().decode('utf-8-sig'))==method,'Host validator drift'
 v.update({'sourceSha256AtExtraction':hashlib.sha256(raw).hexdigest(),'methodSha256':hashlib.sha256(method.encode()).hexdigest(),'featureReportSha256':hashlib.sha256(a.feature_result.read_bytes()).hexdigest()});(out/'result.json').write_text(json.dumps(v,indent=2),encoding='utf-8');print(json.dumps({k:v[k]for k in ['ok','checks','failed']}));return r.returncode
if __name__=='__main__':sys.exit(main())
