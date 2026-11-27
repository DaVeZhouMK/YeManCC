using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using YeMan.Setup;

internal static class MachineWideInstallTest {
 sealed class Boundary : InstallPlatform {
  readonly string area;
  internal bool Elevated=true, RegistrationDenied, ProgramsUnavailable;
  internal int RecoveryReads, RegistryWrites, VersionReads;
  internal Boundary(string path) { area=path; Directory.CreateDirectory(area); }
  internal override string Root { get { return Path.Combine(area,"program"); } }
  internal override string Hw { get { return Path.Combine(area,"hw"); } }
  internal override string Rtss { get { return Path.Combine(area,"rtss"); } }
  internal override string Parent { get { return Path.Combine(area,"ops"); } }
  internal override string StartMenuFile { get { if(ProgramsUnavailable)throw new IOException("fixture folder unavailable"); return Path.Combine(area,"common-programs",DesktopShortcuts.YeManLinkName); } }
  internal override string ShortcutFile { get { return Path.Combine(area,"common-desktop",DesktopShortcuts.YeManLinkName); } }
  internal override string MutexName { get { return "YeManCC.ScopeTest."+Guid.NewGuid().ToString("N"); } }
  internal override bool Admin { get { return Elevated; } }
  internal override SystemObservation Compatibility() { return new SystemObservation { Allowed=true }; }
  internal override IDefenderClient Defender { get { return null; } }
  internal override void ClosePrograms(string path,Action<string> log) { }
  internal override void Space(string path,long bytes) { }
  internal override void AssertDirectory(string path) { RecoveryReads++; SafeFiles.NoLinks(path); }
  internal override void AssertFile(string path) { RecoveryReads++; SafeFiles.NoLinks(path); }
  internal override string Work() { return Directory.CreateDirectory(Path.Combine(Parent,"op-"+Guid.NewGuid().ToString("N"))).FullName; }
  internal override void BackupDirectory(string path) { Directory.CreateDirectory(path); }
  internal override string Version(string path) {
   if(Path.GetFileName(path)!="app")throw new InvalidOperationException("Existing executable version must not be read");
   VersionReads++; return BuildInfo.ProgramVersion;
  }
  internal override void Runtimes(string r,string w,bool online,Action<string> log,Action<bool> reboot,Action<string> warn) {
   if(online)throw new InvalidOperationException("Fixture must be offline");
  }
  internal override void HidHide(string app,string work,HidHideRecord record,Action save,Action<string> log,Action<bool> reboot) { }
  internal override void PublishMaintenance(string app,string version,HashSet<string> mutable,Action<string> warn) {
   RegistryWrites++;
   if(RegistrationDenied)throw new UnauthorizedAccessException("fixture HKLM denied");
  }
  internal override Bundle Unpack(string path,Action<string> log) {
   Directory.CreateDirectory(path);
   Zip(Path.Combine(path,"program.zip"),new Dictionary<string,string> {
    {"YeManCC/YeManCC.exe","new core"}, {"PowerControl/yeman-settings.json","factory"},
    {"PowerControl/settings-baseline.json","{\"formatVersion\":1,\"document\":{\"path\":\"yeman-settings.json\"}}"}
   });
   Zip(Path.Combine(path,"hw.zip"),new Dictionary<string,string>{{"HWiNFO64.exe","mock HW"}});
   Zip(Path.Combine(path,"rtss.zip"),new Dictionary<string,string>{{"RTSS.exe","mock RTSS"}});
   Write(Path.Combine(path,"launcher.exe"),"mock launcher");
   return new Bundle { Version=BuildInfo.ProgramVersion, CoreVersion=BuildInfo.CoreVersion };
  }
 }
 static void Write(string path,string text) {
  Directory.CreateDirectory(Path.GetDirectoryName(path)); File.WriteAllText(path,text);
 }
 static void Zip(string path,Dictionary<string,string> files) {
  using(var zip=ZipFile.Open(path,ZipArchiveMode.Create))
   foreach(var row in files)using(var writer=new StreamWriter(zip.CreateEntry(row.Key).Open()))writer.Write(row.Value);
 }
 static void Check(bool ok,string name) {
  if(!ok)throw new Exception(name); Console.WriteLine("PASS: "+name);
 }
 static Receipt Run(Boundary b,bool desktop) {
  var r=new Engine(b,s=>{}).Execute(new Plan { DesktopShortcut=desktop, DefenderExclusions=false });
  if(r.Phase!="completed")throw new Exception(r.Phase+": "+r.ErrorDetails);
  return r;
 }
 static bool HasWarning(Receipt r,string token) { return r.Warnings.Exists(w=>w.Contains(token)); }
 static string Image(Boundary b) { return Path.Combine(b.Root,MaintenanceRegistration.MainImage); }
 [STAThread] static int Main(string[] args) {
  try {
   AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);
   AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);
   if(args.Length!=1||Directory.Exists(args[0])||File.Exists(args[0]))return 2;
   string dir=Path.GetFullPath(args[0]);
   var b=new Boundary(Path.Combine(dir,"green"));
   Write(Image(b),"a versionless portable executable");
   var settings=Path.Combine(b.Root,"PowerControl","yeman-settings.json");
   Write(settings,"user settings");
   Directory.CreateDirectory(b.Parent);
   b.Elevated=false;
   var assessment=Engine.AssessInstall(b);
   Check(assessment.Allowed&&assessment.Action=="overwrite"&&b.RecoveryReads==0&&b.VersionReads==0&&b.RegistryWrites==0,
    "standard-user preview admits an unregistered, versionless portable installation without reading protected records");
   b.Elevated=true; b.RegistrationDenied=true;
   var r=Run(b,false);
   Check(b.VersionReads==1&&b.RegistryWrites==1&&File.ReadAllText(Image(b))=="new core"&&File.ReadAllText(settings)=="user settings",
    "portable overwrite preserves settings and attempts registration only after installation");
   Check(r.StartMenuShortcutVerified&&r.StartMenuShortcutPath==b.StartMenuFile&&!File.Exists(b.ShortcutFile)&&!r.DesktopShortcutVerified,
    "desktop opt-out still creates the common Start menu entry");
   Check(HasWarning(r,"卸载入口")&&File.ReadAllText(Path.Combine(b.Root,".ymcc-setup","state.json")).Contains("卸载入口"),
    "HKLM refusal is a post-commit warning and is persisted in the installation summary");
   var link=DesktopShortcuts.Read(b.StartMenuFile);
   Check(link.Target==Path.Combine(b.Root,MaintenanceRegistration.Launcher)&&link.Icon==Image(b)&&link.WorkingDirectory==b.Root,
    "real shell link stores one shared launcher, working directory and product icon");

   var fresh=new Boundary(Path.Combine(dir,"fresh"));
   Check(Engine.AssessInstall(fresh).Action=="install","a clean directory needs no app registration");
   r=Run(fresh,true);
   Check(r.StartMenuShortcutVerified&&r.DesktopShortcutVerified&&File.Exists(fresh.StartMenuFile)&&File.Exists(fresh.ShortcutFile),
    "fresh install creates both common shortcuts when requested");
   r=Run(fresh,true);
   Check(r.StartMenuShortcutVerified&&r.DesktopShortcutVerified,"same-package reinstall can refresh owned links");

   var collision=new Boundary(Path.Combine(dir,"collision"));
   Write(collision.StartMenuFile,"foreign entry");
   r=Run(collision,true);
   Check(!r.StartMenuShortcutVerified&&r.DesktopShortcutVerified&&HasWarning(r,"公共开始菜单")&&File.ReadAllText(collision.StartMenuFile)=="foreign entry",
    "foreign same-name Start menu entry survives; core and desktop still install");
   var missing=new Boundary(Path.Combine(dir,"missing-programs")){ProgramsUnavailable=true};
   r=Run(missing,true);
   Check(!r.StartMenuShortcutVerified&&r.DesktopShortcutVerified&&HasWarning(r,"公共开始菜单")&&File.Exists(Image(missing)),
    "missing Common Programs does not fall back to current-user paths or block core installation");

   var interrupted=new Boundary(Path.Combine(dir,"recovery"));
   string work=interrupted.Work(),backup=Path.Combine(work,"backup"),dummy=Path.Combine(work,"source");
   Write(dummy,"test pending link");
   new FileTransaction(backup,work).Put(dummy,interrupted.StartMenuFile);
   Json.Save(Path.Combine(work,"result.json"),new Receipt{Root=interrupted.Root,Phase="applying"});
   Check(new Engine(interrupted,s=>{}).RecoverInterrupted()==1&&!File.Exists(interrupted.StartMenuFile),
    "interrupted recovery recognizes only the exact common Start menu link as an owned target");
   return 0;
  } catch(Exception e) { Console.Error.WriteLine(e); return 1; }
 }
}
