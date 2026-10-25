#!/usr/bin/env python3
"""Compile the actual prerequisite consumer; never run normal setup or SDK methods."""
from pathlib import Path
import argparse,datetime,hashlib,json,shutil,subprocess,sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',required=True)
parser.add_argument('--legacy-source',required=True,help='Preserved pre-fix production Program.cs, not an invented copy')
parser.add_argument('--legacy-auto-source',help='Preserved AutoSetup.cs accompanying the historical Program consumer')
parser.add_argument('--configuration',choices=['Release','Checked'],default='Release')
parser.add_argument('--prior-source-dir',help='Preserved prior SDK200 Program/helper source root for obsolete-prepare regression')
parser.add_argument('--sdk',help='Approved original SDK binary; defaults to the shared repo reference')
parser.add_argument('--inventory-sample',help='Preserved read-only pnputil XML bytes; never executes the inventory command')
args=parser.parse_args()
repo=Path(__file__).resolve().parent.parent
out=Path(args.out).resolve();out.mkdir(parents=True,exist_ok=False)
legacy=Path(args.legacy_source).resolve()
legacy_auto=Path(args.legacy_auto_source).resolve() if args.legacy_auto_source else legacy.with_name('AutoSetup.cs')
if not legacy_auto.is_file():raise RuntimeError('Historical AutoSetup.cs is required; provide --legacy-auto-source from a preserved production snapshot')
sdk=Path(args.sdk).resolve() if args.sdk else repo/'deps/hidmaestro/HIDMaestro.Core.dll'
fixture=repo/'tools/fixtures/gamepad_package_preparation'
sha=lambda b:hashlib.sha256(b).hexdigest()
read=lambda p:p.read_text(encoding='utf-8-sig')
commands=[];bindings=[]
inputs={'Program.cs':repo/'GamepadPrerequisites/Program.cs',
        'AutoSetup.cs':repo/'GamepadPrerequisites/AutoSetup.cs',
        'HidMaestroPackagePreparation.cs':repo/'GamepadPrerequisites/HidMaestroPackagePreparation.cs',
        'ProcessEfficiency.cs':repo/'native/ProcessEfficiency.cs',
        'Fixture.cs':fixture/'Fixture.cs'}
for role,p in list(inputs.items())+[('LegacyProgram.cs',legacy),('LegacyAutoSetup.cs',legacy_auto),('LegacyFixture.cs',fixture/'LegacyFixture.cs'),('pinned-sdk',sdk)]:
 bindings.append({'role':role,'path':str(p),'sha256':sha(p.read_bytes()),'bytes':p.stat().st_size})
if args.inventory_sample:
 sample=Path(args.inventory_sample).resolve()
 bindings.append({'role':'captured-readonly-pnputil-inventory','path':str(sample),'sha256':sha(sample.read_bytes()),'bytes':sample.stat().st_size})
(out/'SOURCE-BINDINGS.json').write_text(json.dumps(bindings,indent=2)+'\n',encoding='utf-8')
def run(command,cwd,log):
 result=subprocess.run(command,cwd=cwd,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=60)
 Path(log).write_text(result.stdout+result.stderr,encoding='utf-8')
 commands.append({'command':command,'cwd':str(cwd),'exit':result.returncode,'output':str(log)})
 return result

def variant(name,mutation=None,is_legacy=False,is_prior=False):
 target=out/name;target.mkdir();source=target/'source';source.mkdir()
 files={k:v.read_bytes() for k,v in inputs.items()}
 startup='GamepadPackagePreparationFixture'
 if is_legacy:
  files['Program.cs']=legacy.read_bytes();files['AutoSetup.cs']=legacy_auto.read_bytes();files.pop('HidMaestroPackagePreparation.cs')
  files['Fixture.cs']=(fixture/'LegacyFixture.cs').read_bytes();startup='LegacyReadinessFixture'
 elif is_prior:
  prior=Path(args.prior_source_dir).resolve()
  files['Program.cs']=(prior/'GamepadPrerequisites/Program.cs').read_bytes()
  files['HidMaestroPackagePreparation.cs']=(prior/'GamepadPrerequisites/HidMaestroPackagePreparation.cs').read_bytes()
  files['Fixture.cs']=(fixture/'PreviousPrepareFixture.cs').read_bytes();startup='PreviousPrepareFixture'
 elif mutation:
  filename,old,new=mutation
  text=files[filename].decode('utf-8-sig')
  if text.count(old)!=1:raise RuntimeError('Mutation must bind one exact production location: '+old)
  files[filename]=text.replace(old,new).encode('utf-8')
 for filename,data in files.items():(source/filename).write_bytes(data)
 project=source/'Fixture.csproj'
 project.write_text(f'''<Project Sdk="Microsoft.NET.Sdk">
 <PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework>
 <ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><PlatformTarget>x64</PlatformTarget>
 <AssemblyName>FixtureGate</AssemblyName><StartupObject>{startup}</StartupObject>
 <CheckForOverflowUnderflow>true</CheckForOverflowUnderflow></PropertyGroup>
</Project>\n''',encoding='utf-8')
 build=run(['dotnet','build',str(project),'-c',args.configuration,'--nologo','--artifacts-path',str(target/'artifacts'),'-o',str(target/'bin')],repo,target/'build.log')
 if build.returncode:raise RuntimeError(f'{name} compile failed; see {target}/build.log')
 command=['dotnet',str(target/'bin/FixtureGate.dll')]
 if not is_legacy:command +=[str(sdk),str(target/'evidence')]
 if args.inventory_sample and not is_legacy and not is_prior:command +=[str(sample)]
 result=run(command,repo,target/'run.log')
 row={'variant':name,'exit':result.returncode,'sourceManifest':str(target/'compiled-source-manifest.json')}
 (target/'compiled-source-manifest.json').write_text(json.dumps([{'path':str(p),'sha256':sha(p.read_bytes())} for p in sorted(source.iterdir()) if p.is_file()],indent=2)+'\n',encoding='utf-8')
 if is_prior:
  if result.returncode!=1 or 'EXPECTED_PREVIOUS_SDK200_PREPARE_FAILURE' not in result.stdout:raise RuntimeError('Prior SDK200 blocked-prepare production fault did not reproduce')
  row['status']='EXPECTED_PREVIOUS_SDK200_PREPARE_FAILURE_REPRODUCED';row['observation']=json.loads(result.stdout.strip())
 elif is_legacy:
  if result.returncode!=1 or 'EXPECTED_OLD_CODE_FAILURE' not in result.stdout:raise RuntimeError('Known old production readiness fault did not reproduce')
  row['status']='EXPECTED_OLD_CODE_FAILURE_REPRODUCED';row['observation']=json.loads(result.stdout.strip())
 else:
  row['result']=str(target/'evidence/RESULT.json')
  row['receipt']=json.loads(Path(row['result']).read_text(encoding='utf-8'))
  if mutation:
   if result.returncode!=1 or row['receipt'].get('status')!='FAIL':raise RuntimeError(f'{name} mutation not caught as assertion failure')
   row['status']='COMPILED_PRODUCTION_MUTATION_CAUGHT'
  else:
   if result.returncode or row['receipt'].get('status')!='PASS':raise RuntimeError('Actual production consumer fixture failed')
   row['status']='PASS_ACTUAL_PRODUCTION_CONSUMER'
 return row

try:
 rows=[variant('old-policy-red',is_legacy=True),variant('actual-consumer-green'),
       variant('mutation-legacy-as-current',('HidMaestroPackagePreparation.cs','r.Classification == "current" &&','(r.Classification == "current" || r.Classification == "known-legacy") &&')),
       variant('mutation-raw-CAT-required',('HidMaestroPackagePreparation.cs','var ready = inventory.AtLeastCurrentComplete;','var ready = inventory.AtLeastCurrentComplete && staging.Status == "CURRENT_RAW_DRIVER_INF_PRESENT" && staging.Infs.All(i => i.Catalogs.All(c => c.Status == "present-unverified"));')),
       variant('mutation-old-dispatch-restored',('Program.cs','return review.Inventory.PreparationKnown ? "PREPARE_OFFICIAL" : Decide(facts,true);','return review.Status;')),
       variant('mutation-postcondition-bypassed',('HidMaestroPackagePreparation.cs','!after.StoreReady','false')),
       variant('mutation-observed-pnputil-schema-rejected',('HidMaestroPackagePreparation.cs','if (!legacyFormat && !pnputilFormat)','if (!legacyFormat)')),
       variant('mutation-complete-pair-vetoed-by-extra-version',('HidMaestroPackagePreparation.cs','if (unresolved && !atLeast)','if (unresolved)')),
       variant('mutation-unverified-extra-allows-maintenance',('HidMaestroPackagePreparation.cs','PreparationKnown=rows.Count>0 && !unresolved','PreparationKnown=rows.Count>0')),
       variant('mutation-deferred-collapsed-to-dependency-failure',('HidMaestroPackagePreparation.cs','? "DRIVER_MAINTENANCE_DEFERRED" : decision;','? decision : decision;')),
       variant('mutation-deferred-without-current-pair',('HidMaestroPackagePreparation.cs','decision == "CLOSE_CONTROLLER_APPS_FIRST" && review.StoreReady','decision == "CLOSE_CONTROLLER_APPS_FIRST"')),
       variant('mutation-controller-client-guard-bypassed',('Program.cs','if (facts.ClientsRunning) return "CLOSE_CONTROLLER_APPS_FIRST";','if (false) return "CLOSE_CONTROLLER_APPS_FIRST";'))]
 priorPrepare=variant('prior-sdk200-prepare-red',is_prior=True) if args.prior_source_dir else None
 drift=[b for b in bindings if sha(Path(b['path']).read_bytes())!=b['sha256']]
 if drift:raise RuntimeError('Source drift during fixture run: '+str(drift))
 report={'status':'PASS_ACTUAL_PREREQUISITE_CONSUMER_OLD_FAULT_AND_CONTROLS','configuration':args.configuration,'sdkVersion':'2.0.0',
         'createdAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'cases':rows,
         'sourceDrift':drift,'previousSdk200Prepare':priorPrepare,'sdkMethodsInvoked':0,'deviceDriverRegistryCertificateOperations':False,
         'normalSetupExecuted':False,'realSdkLoadedForMetadataOnly':True}
 (out/'RESULT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'status':report['status'],'result':str(out/'RESULT.json'),'actualCases':rows[1]['receipt']['cases'],
                   'actualChecks':rows[1]['receipt']['checks'],'controlFailures':[len(r['receipt']['failures']) for r in rows[2:]]},ensure_ascii=False))
except Exception as exc:
 (out/'RESULT.json').write_text(json.dumps({'status':'ERROR','error':str(exc)},indent=2)+'\n',encoding='utf-8')
 print(str(exc),file=sys.stderr);sys.exit(1)
finally:
 (out/'COMMANDS.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
