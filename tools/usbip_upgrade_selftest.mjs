// Executes the original upgrade consumer and metadata reader. SDK setup and
// device calls are inert fake ports; the official SDK is read as metadata only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
// Explicit internal file-transition variant; the original three-argument path below is unchanged.
if (process.argv.slice(2).some(arg=>arg.startsWith('--case-filter'))) {
  assert.equal(process.argv.length,5,'Supply <absolute-fresh-out> --case-filter pending-file-transition');
  assert.equal(process.argv[3],'--case-filter','Expected exact --case-filter after output directory');
  assert.equal(process.argv[4],'pending-file-transition','Unknown USBIP case filter');
  const {runPendingFileTransition}=await import('./fixtures/usbip_pending_transition.mjs');
  await runPendingFileTransition({repo,out:process.argv[2]});
  process.exit(0);
}
assert.equal(process.argv.length,3,'Supply one fresh evidence directory');
const out=path.resolve(process.argv[2]);assert(!fs.existsSync(out)||fs.readdirSync(out).length===0,'Use fresh evidence');
assert(!out.startsWith(repo+path.sep),'Evidence outside source');fs.mkdirSync(out,{recursive:true});
const files=['GamepadPrerequisites/UsbipUpgrade.cs','GamepadPrerequisites/UsbipUpgradeSelfTest.cs','GamepadPrerequisites/AutoSetup.cs','GamepadPrerequisites/ModulePayloadBootstrap.cs','GamepadPrerequisites/Program.cs','GamepadPrerequisites/HidMaestroPackagePreparation.cs','GamepadPrerequisites/YeManGamepadPrerequisites.csproj','native/ProcessEfficiency.cs','native/UsbipMaintenanceState.cs','native/ModuleMaintenanceGate.cs','InputHost/UsbipMaintenanceGate.cs','tools/usbip_upgrade_selftest.mjs'];
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(path.join(repo,file))).digest('hex');
const sourceHashes=Object.fromEntries(files.map(file=>[file,hash(file)]));
const commands=[];
function run(name,exe,args,success=true){const startedAt=Date.now();const r=spawnSync(exe,args,{cwd:repo,encoding:'utf8',windowsHide:true,timeout:90000,maxBuffer:4<<20});fs.writeFileSync(path.join(out,name+'.log'),(r.stdout||'')+(r.stderr||''));commands.push({name,exe,args,exitCode:r.status,error:r.error?.message,elapsedMs:Date.now()-startedAt});if(success)assert.equal(r.status,0,name+': '+r.stdout+r.stderr);else assert.notEqual(r.status,0,'Specified old-version bypass must fail');return r;}
function project(dir,mutation){fs.mkdirSync(dir,{recursive:true});const includes=[];for(const file of files.filter(f=>f.endsWith('.cs'))){let source=path.join(repo,file);if(mutation&&file.endsWith(mutation==='pending-ready'?'/AutoSetup.cs':'/UsbipUpgrade.cs')){source=path.join(dir,path.basename(file));const original=fs.readFileSync(path.join(repo,file),'utf8');
let start,replacement;
if(mutation==='owned-install-record'){
  start='!string.Equals(row.GetProperty("coreSha256").GetString(), core, StringComparison.OrdinalIgnoreCase)';
  replacement='false /* negative: same-session foreign SDK record may be removed */';
}else if(mutation==='install-preflight'){
  start='if (now.Present || now.Installed is not null)';
  replacement='if (false /* negative: product appeared before installation */)';
}else if(mutation==='pending-ready'){
  start='if (usbipRebootPending || hidHide == "REBOOT_REQUIRED")';
  replacement='if (hidHide == "REBOOT_REQUIRED" /* negative: ignored active USBIP fence */)';
}else if(mutation==='first-install'){
  start='bool resume = runtime.ShouldResumeInstall(inventory);';
  replacement=start+'\n                if (!resume) return Send("not-installed", "negative: original first-install gap");';
}else if(mutation==='completion-inventory'){
  start='if (!inventory.Present || !inventory.Verified || inventory.Target != target ||\r\n                inventory.Installed != target || !inventory.DriverCurrent)';
  if(!original.includes(start))start=start.replaceAll('\r\n','\n');
  replacement='if (false /* negative: backend-only result accepted as complete */)';
}else{
  start='if (inventory.Installed > inventory.Target)';
  replacement='if (false /* specified negative: removed newer-version protection */)';
}
assert.equal(original.split(start).length,2,'Exact negative control seam: '+mutation);
fs.writeFileSync(source,original.replace(start,replacement));}includes.push(`<Compile Include="${source.replaceAll('&','&amp;')}" />`);}const file=path.join(dir,'Fixture.csproj');fs.writeFileSync(file,`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><PlatformTarget>x64</PlatformTarget><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup>${includes.join('')}</ItemGroup></Project>`);return file;}
const positive=path.join(out,'positive');run('positive-build','dotnet',['build',project(positive,false),'-c','Release','-v:q']);const dll=path.join(positive,'bin/Release/net10.0-windows/Fixture.dll');
const consumer=run('consumer','dotnet',[dll,'--selftest-usbip-upgrade']);const consumerResult=JSON.parse(consumer.stdout.trim());assert.equal(consumerResult.status,'PASS');assert.ok(consumerResult.checks>=48);assert.equal(consumerResult.hardwareIo,false);
run('prerequisite-preserved','dotnet',[dll,'--selftest']);
const uninstallWait=JSON.parse(run('uninstall-observer-budget','dotnet',[dll,'--selftest-usbip-owned-wait']).stdout.trim());assert.equal(uninstallWait.status,'PASS');assert.equal(uninstallWait.ownedChildExited,true);
const bootOne=JSON.parse(run('boot-identity-one','dotnet',[dll,'--selftest-usbip-boot-id']).stdout.trim()),bootTwo=JSON.parse(run('boot-identity-two','dotnet',[dll,'--selftest-usbip-boot-id']).stdout.trim());assert.equal(bootOne.status,'PASS');assert.equal(bootOne.bootIdentifier,bootTwo.bootIdentifier,'Actual Windows boot identity must persist across helper processes');
const sdkFile=path.join(repo,'deps/hidmaestro/HIDMaestro.Core.dll');const metadata=run('metadata','dotnet',[dll,'--selftest-usbip-sdk-contract',sdkFile]);const contract=JSON.parse(metadata.stdout.trim());assert.equal(contract.status,'PASS');assert.equal(contract.sdkMethodsInvoked,0);assert.equal(contract.hardwareIo,false);
const negative=path.join(out,'negative');run('negative-build','dotnet',['build',project(negative,true),'-c','Release','-v:q']);const newerRejected=run('negative-newer-version','dotnet',[path.join(negative,'bin/Release/net10.0-windows/Fixture.dll'),'--selftest-usbip-upgrade'],false);assert.match(newerRejected.stderr,/same\/newer preserved/);
for(const mutation of ['first-install','completion-inventory','pending-ready','owned-install-record','install-preflight']){
  const dir=path.join(out,'negative-'+mutation);
  run(mutation+'-negative-build','dotnet',['build',project(dir,mutation),'-c','Release','-v:q']);
  const rejected=run(mutation+'-negative-consumer','dotnet',[path.join(dir,'bin/Release/net10.0-windows/Fixture.dll'),mutation==='pending-ready'?'--selftest':'--selftest-usbip-upgrade'],false);
  const reason=mutation==='first-install'?/first install: verified absence installs/:mutation==='pending-ready'?/active USB\/IP install\/uninstall fence/:mutation==='owned-install-record'?/actual owned completion preserves foreign\/unknown records: foreign-core/:mutation==='install-preflight'?/actual install preflight: old-product-appeared/:/backend true with old\/absent\/unknown registration/;
  assert.match(rejected.stderr,reason,'Negative must fail at its specific original assertion');
}
const gateDir=path.join(out,'gate');fs.mkdirSync(gateDir,{recursive:true});
const entry=path.join(gateDir,'Entry.cs');fs.writeFileSync(entry,`
using YeManInputHost;
int checks=0;void Need(bool x,string n){if(!x)throw new Exception(n);checks++;}
string name="Local\\\\YMCC-Usbip-Gate-Test-"+Guid.NewGuid().ToString("N");
int creates=0;Mutex Create(){creates++;return new Mutex(false,name);}
using(var lease=UsbipMaintenanceGate.TryEnter(false,()=>throw new Exception("nonUSBIP must not probe"),()=>throw new Exception("nonUSBIP must not lock")))Need(lease is not null,"other personas bypass maintenance IO");
Need(UsbipMaintenanceGate.TryEnter(true,()=>true,Create) is null&&creates==0,"pending system uninstall refuses before creating SDK context");
var held=new ManualResetEventSlim(false);var release=new ManualResetEventSlim(false);
var worker=new Thread(()=>{using var m=new Mutex(false,name);m.WaitOne();held.Set();release.Wait();m.ReleaseMutex();});worker.Start();Need(held.Wait(2000),"owned maintenance mutex held");
Need(UsbipMaintenanceGate.TryEnter(true,()=>false,Create) is null,"other owner held gate returns immediately");release.Set();Need(worker.Join(2000),"owned lock thread ended");
int observations=0;Need(UsbipMaintenanceGate.TryEnter(true,()=>++observations>1,Create) is null,"late pending fence is checked again under mutex");
var active=UsbipMaintenanceGate.TryEnter(true,()=>false,Create);Need(active is not null,"normal preparation admitted");active!.Dispose();active.Dispose();
using(var next=UsbipMaintenanceGate.TryEnter(true,()=>false,Create))Need(next is not null,"idempotent disposal released owned mutex");
Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new{status="PASS",checks,hardwareIo=false}));
`);
const gateProject=path.join(gateDir,'Gate.csproj');fs.writeFileSync(gateProject,`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Compile Include="${path.join(repo,'InputHost/UsbipMaintenanceGate.cs')}" /><Compile Include="${path.join(repo,'native/UsbipMaintenanceState.cs')}" /><Compile Include="${path.join(repo,'native/ModuleMaintenanceGate.cs')}" /></ItemGroup></Project>`);
run('gate-build','dotnet',['build',gateProject,'-c','Release','-v:q']);const gate=JSON.parse(run('gate-consumer','dotnet',[path.join(gateDir,'bin/Release/net10.0-windows/Gate.dll')]).stdout.trim());assert.equal(gate.status,'PASS');
for(const file of files)assert.equal(hash(file),sourceHashes[file],'Source changed: '+file);
const result={status:'PASS',actualConsumer:'UsbipUpgrade.Execute',consumer:consumerResult,metadata:contract,gate,uninstallWait,bootIdentity:bootOne,negativeControl:'The original selftest rejects removing newer-version protection, reinstating the first-install gap, accepting backend-only completion, and ignoring an active pending fence, clearing a foreign-SDK record, and skipping final inventory admission',negativeControls:['newer-version','first-install','completion-inventory','pending-ready','owned-install-record','install-preflight'],commands,sourceHashes,physicalUpgradeVerified:false,systemDriverIo:false,scope:'Actual startup upgrade decision/order consumer and exact DLL metadata. No installer, SDK device method, real YMCC, driver uninstall or system sleep executed.'};fs.writeFileSync(path.join(out,'usbip-upgrade-result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({status:result.status,checks:consumerResult.checks,negativeRejected:true,physicalUpgradeVerified:false}));
