#!/usr/bin/env python3
"""Compile exact official SDK 2 policy excerpts with private fake OS terminals."""
from pathlib import Path
import argparse,hashlib,json,subprocess,datetime,sys
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',required=True)
parser.add_argument('--sdk-source-dir',required=True,help='Actual pinned SDK decompilation containing DriverDeploy and DriverBuilder')
parser.add_argument('--create-source-dir',required=True,help='Actual pinned SDK decompilation containing DeviceOrchestrator, HMContext, ControllerLease')
a=parser.parse_args();repo=Path(__file__).resolve().parent.parent
out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
src=Path(a.sdk_source_dir).resolve();create=Path(a.create_source_dir).resolve()
source=(src/'HIDMaestro.Internal.DriverDeploy.cs').read_text(encoding='utf-8-sig')
builder=(src/'HIDMaestro.Internal.DriverBuilder.cs').read_text(encoding='utf-8-sig')
commands=[];excerpts=[]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def take(text,marker,origin):
 start=text.index(marker);begin=text.rfind('\n',0,start)+1
 end=text.index('\n\t}',start)+4
 block=text[begin:end]
 excerpts.append({'marker':marker,'origin':str(origin),'line':text[:begin].count('\n')+1,'text':block,'sha256':hashlib.sha256(block.encode()).hexdigest()})
 return block
members=[]
for marker in ['internal class DeployMachine','internal readonly record struct DriverStamp','internal static DriverStamp? InstalledDriver()',
 'internal static DriverStamp EmbeddedDriver()','internal static List<string> OlderThanBuild()','internal static List<string> MissingOfBuild()',
 'internal static Mutex Acquire()','internal static HMDriverInstallOutcome Run(','internal static bool EnsureForCreate(','internal static void ForgetConfirmed()']:
 members.append(take(source,marker,src/'HIDMaestro.Internal.DriverDeploy.cs'))
full=take(builder,'public static bool FullDeploy(',src/'HIDMaestro.Internal.DriverBuilder.cs')
actual='''using System;using System.Collections.Generic;using System.Linq;using System.Threading;
namespace HIDMaestro.Internal;
internal static class DriverDeploy {
 internal static DeployMachine Machine=new DeployMachine();
 private static string? s_confirmedManifest;
 internal static List<DriverStamp> InstalledStamps(string inf)=>throw new Exception("unsubstituted terminal");
 internal static DriverStamp EmbeddedStamp(string inf)=>throw new Exception("unsubstituted terminal");
'''+ '\n'.join(members)+'\n}\n'
wrapper='namespace HIDMaestro.Internal;internal static partial class DriverBuilder {\n'+full+'\n}\n'
(out/'EXTRACTED-OFFICIAL-SOURCE.json').write_text(json.dumps(excerpts,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
inputs=[src/'HIDMaestro.Internal.DriverDeploy.cs',src/'HIDMaestro.Internal.DriverBuilder.cs',
 create/'HIDMaestro.Internal.DeviceOrchestrator.cs',create/'HIDMaestro.HMContext.cs',create/'HIDMaestro.Internal.ControllerLease.cs',
 repo/'tools/fixtures/gamepad_official_policy/Fixture.cs']
bindings=[{'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size} for p in inputs]
# Bind actual call-chain positions. This proves preparation-gate consumption,
# not ownership of the entire PnP interval after EnsureForCreate returns.
chain={}
for p,needles in [(inputs[2],['DriverDeploy.EnsureForCreate(identity.Token)','DeviceNodeCreator.CreateDeviceNode']),
                 (inputs[3],['ControllerLease controllerLease = AcquireLease','instanceId = SetupDevices(','SetupDevices = DeviceOrchestrator.SetupController']),
                 (inputs[4],['MachineClaims.AddController'])]:
 text=p.read_text(encoding='utf-8-sig');rows=[]
 for needle in needles:
  at=text.index(needle);rows.append({'text':needle,'line':text[:at].count('\n')+1})
 chain[str(p)]=rows
assert chain[str(inputs[2])][0]['line']<chain[str(inputs[2])][1]['line']
assert chain[str(inputs[3])][0]['line']<chain[str(inputs[3])][1]['line']
(out/'CALL-CHAIN.json').write_text(json.dumps({'scope':'SDK create preparation uses same Deploy gate; live lease record protects post-gate PnP','files':chain},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def run(cmd,log):
 r=subprocess.run(cmd,cwd=repo,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=45)
 log.write_text(r.stdout+r.stderr,encoding='utf-8');commands.append({'command':cmd,'exit':r.returncode,'log':str(log)});return r

def build(name,negative=False):
 folder=out/name;folder.mkdir();s=folder/'source';s.mkdir()
 text=actual
 if negative:
  old='bool flag = holders.Count > 0 || list.Count > 0;';assert text.count(old)==1
  text=text.replace(old,'bool flag = false;')
 (s/'ActualPolicy.cs').write_text(text,encoding='utf-8')
 (s/'ActualFullDeploy.cs').write_text(wrapper,encoding='utf-8')
 (s/'Fixture.cs').write_bytes(inputs[-1].read_bytes())
 project=s/'OfficialPolicy.csproj';project.write_text('''<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><Nullable>enable</Nullable><ImplicitUsings>enable</ImplicitUsings><CheckForOverflowUnderflow>true</CheckForOverflowUnderflow><StartupObject>OfficialPolicyFixture</StartupObject><AssemblyName>OfficialPolicy</AssemblyName></PropertyGroup></Project>''',encoding='utf-8')
 r=run(['dotnet','build',str(project),'-c','Release','--nologo','--artifacts-path',str(folder/'artifacts'),'-o',str(folder/'bin')],folder/'build.log')
 if r.returncode:raise RuntimeError('Compile failed: '+str(folder/'build.log'))
 r=run(['dotnet',str(folder/'bin/OfficialPolicy.dll'),str(folder/'evidence')],folder/'run.log')
 result=json.loads((folder/'evidence/RESULT.json').read_text(encoding='utf-8'))
 if negative:
  if r.returncode!=1 or result['status']!='FAIL':raise RuntimeError('Owner-protection mutation not caught')
 else:
  if r.returncode or result['status']!='PASS':raise RuntimeError('Actual policy fixture failed')
 return {'variant':name,'receipt':result,'result':str(folder/'evidence/RESULT.json')}
try:
 green=build('actual-policy-green');negative=build('live-owner-bypass-control',True)
 assert all(sha(Path(r['path']))==r['sha256'] for r in bindings)
 result={'status':'PASS_ACTUAL_OFFICIAL_POLICY_SHARED_PRIVATE_MUTEX_AND_OWNER_GUARDS','createdAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),
 'green':green,'negative':negative,'bindings':bindings,'actualSdkAssemblyMethodsInvoked':False,
 'privateLeafSubstitutions':['AdminNamespace.Name unique Local mutex','TimeoutScale bounded private waits','DeployMachine OS/driver/store/census effects'],
 'scope':'Actual Run, EnsureForCreate, Acquire and DeployMachine.Lock plus exact source call-chain; not a hardware or full-PnP-exclusive claim'}
 (out/'RESULT.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'status':result['status'],'cases':green['receipt']['cases'],'checks':green['receipt']['checks'],'negativeAssertions':len(negative['receipt']['failures']),'result':str(out/'RESULT.json')}))
except Exception as e:
 (out/'RESULT.json').write_text(json.dumps({'status':'ERROR','error':str(e)},indent=2)+'\n',encoding='utf-8');print(str(e),file=sys.stderr);sys.exit(1)
finally:(out/'COMMANDS.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
