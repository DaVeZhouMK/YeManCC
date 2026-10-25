"""Execute the exact production split-payload validator with explicit fixture roots."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--scratch',required=True);args=parser.parse_args()
repo=Path(__file__).resolve().parents[2];scratch=Path(args.scratch).resolve()
if scratch.exists():raise RuntimeError('Use new scratch directory')
scratch.mkdir(parents=True);project=scratch/'probe';project.mkdir();offline=scratch/'offline';offline.mkdir()
source=(repo/'FanLab/real-host/Program.cs').read_text(encoding='utf-8-sig')
start=source.index('    internal static void ValidateSplitPayloadManifestAt(')
end=source.index('\n    //',source.index('\n    }',start)+1)
method=source[start:end]
if method.count('internal static void ValidateSplitPayloadManifestAt')!=1:raise RuntimeError('Production method boundary changed')
# The complete method is copied verbatim; only its surrounding class and roots are test-owned.
wrapper='using System.IO;\nusing System.Text;\nusing System.Text.Json;\nusing System.Security.Cryptography;\ninternal static class ActualSplitGate\n{\n'+method+'\n    static string Sha256(string path)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)));\n}\n'
(project/'ActualSplitGate.cs').write_text(wrapper,encoding='utf-8')
shutil.copyfile(repo/'FanLab/DeviceCoreSharedDependencies.cs',project/'DeviceCoreSharedDependencies.cs')
(project/'SplitGateProbe.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup></Project>',encoding='utf-8')
(project/'Program.cs').write_bytes((repo/'tools/hc-shared-device-runtime/SplitGateProbe.cs.txt').read_bytes())
result=subprocess.run(['dotnet','build',str(project/'SplitGateProbe.csproj'),'--source',str(offline),'--nologo','-v:q'],capture_output=True,cwd=scratch)
(scratch/'build.log').write_bytes(result.stdout+result.stderr)
if result.returncode:raise RuntimeError('Split gate probe compile failed')
result=subprocess.run(['dotnet',str(project/'bin/Debug/net10.0/SplitGateProbe.dll'),str(repo),str(scratch/'fixture'),str(scratch/'result.json')],capture_output=True,cwd=scratch)
(scratch/'run.log').write_bytes(result.stdout+result.stderr)
if result.returncode:raise RuntimeError('Actual split gate failed; '+str(scratch/'run.log'))
record={'status':'passed','sourceMethodSha256':hashlib.sha256(method.encode()).hexdigest(),'sourceMethodCopiedVerbatim':True,'method':'ValidateSplitPayloadManifestAt','test':json.loads((scratch/'result.json').read_text()),'productHostExecuted':False,'hardwareExecuted':False}
(scratch/'verification.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');print(json.dumps({'status':'passed','checks':record['test']['checks']}))
