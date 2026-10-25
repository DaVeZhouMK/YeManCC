"""Actual FanHost ControllerManager maintenance island, with inert reflection targets."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,os,sys
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--source',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1];source=a.source or root/'FanLab/real-host/Program.cs';raw=source.read_bytes();text=raw.decode('utf-8-sig');out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
def block(s,start):
 i=s.index('{',start);end=i+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
method=block(text,text.index('    public bool StopHcControllerCpuLoops('));start=method.index('            var controllerManager = FindType("HandheldCompanion.Managers.ControllerManager");');stop=method.index('        }\n        catch',start) if '\r\n'not in method else method.index('        }\r\n        catch',start);island=method[start:stop]
cache=re.findall(r'    private static readonly ConcurrentDictionary<Type, object> manualSlotModeMetadata = new\(\);',text);assert len(cache)<=1
hc=root/'deps/handheldcompanion-runtime/source/Managers/ControllerManager.cs';hcRaw=hc.read_bytes();hcText=hcRaw.decode('utf-8-sig');enum=block(hcText,hcText.index('    public enum ControllerSlotManagementMode'))
assert island.count('Enum.Parse(')==1 and island.count('modeField.GetValue(null)')==1 and island.count('modeField.SetValue(null, manual)')==1
metered=island.replace('Enum.Parse(','FixtureEnumParse(',1).replace('modeField.GetValue(null)','FixtureModeRead(modeField)',1).replace('modeField.SetValue(null, manual)','FixtureModeWrite(modeField, manual)',1)
assert metered.replace('FixtureEnumParse(','Enum.Parse(',1).replace('FixtureModeRead(modeField)','modeField.GetValue(null)',1).replace('FixtureModeWrite(modeField, manual)','modeField.SetValue(null, manual)',1)==island
fixture=root/'tools/fan_hc_maintenance_constant_selftest.cs';template=fixture.read_text(encoding='utf-8');generated=template.replace('@@HC_ENUM@@',enum).replace('@@CACHE@@',cache[0]if cache else'').replace('@@CLEAR@@','manualSlotModeMetadata.Clear();'if cache else'').replace('@@ISLAND@@',metered);assert '@@'not in generated
h=out/'harness';h.mkdir();(h/'Program.cs').write_text(generated,encoding='utf-8');(h/'Gate.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup></Project>',encoding='utf-8')
(out/'island.cs.txt').write_text(island,encoding='utf-8');(out/'source-bindings.json').write_text(json.dumps({'source':str(source),'sourceSha256':hashlib.sha256(raw).hexdigest(),'hcEnumSource':str(hc),'hcSourceSha256':hashlib.sha256(hcRaw).hexdigest(),'islandSha256':hashlib.sha256(island.encode()).hexdigest(),'fixtureSha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'meteringRestoredExactly':True},indent=2),encoding='utf-8')
env=os.environ.copy();env['DOTNET_CLI_TELEMETRY_OPTOUT']='1'
r=subprocess.run(['dotnet','build',str(h/'Gate.csproj'),'-c','Release','--nologo'],capture_output=True,encoding='utf-8',errors='replace',env=env,creationflags=subprocess.CREATE_NO_WINDOW);(out/'build.log').write_text(r.stdout+r.stderr,encoding='utf-8')
if r.returncode:print(r.stdout+r.stderr);raise SystemExit(2)
r=subprocess.run(['dotnet',str(h/'bin/Release/net10.0/Gate.dll')],capture_output=True,encoding='utf-8',errors='replace',env=env,creationflags=subprocess.CREATE_NO_WINDOW);(out/'runtime.log').write_text(r.stdout+r.stderr,encoding='utf-8');data=json.loads(r.stdout);data['buildExit']=0;(out/'result.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8');assert source.read_bytes()==raw and hc.read_bytes()==hcRaw
print(json.dumps({'ok':data['ok'],'checks':data['checks'],'idleConstantParses':data['idleConstantParses'],'failed':[x['name']for x in data['results']if not x['ok']]}));raise SystemExit(r.returncode)
