using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.ServiceProcess;
using System.Text;
using System.Threading;
using Microsoft.Win32;

namespace YeMan.Setup {
 public sealed class HidHideSnapshot {
  public bool ServicePresent,ServiceRunning,ServiceDisabled,ServiceImageMatches,DriverFile,DriverPackage,ProductRegistered;
  public bool RepositoryEvidence,ClassFilter,ClientEvidence,MaintenancePresent;
  public List<string> PackageVersions=new List<string>();
  public List<string> CompletePackageVersions=new List<string>();
  public string ServiceState,Version;
  internal Version DiskDriverVersion {get{Version version;return DriverFile&&System.Version.TryParse(Version,out version)&&version!=new Version(0,0,0,0)?version:null;}}
  public bool Ready {get{return ServicePresent&&!ServiceDisabled&&ServiceImageMatches&&DriverPackage&&HidHide.Supported(DiskDriverVersion)&&CompletePackageVersions.Any(v=>{Version version;return System.Version.TryParse(v,out version)&&HidHide.Supported(version);});}}
  public bool Evidence {get{return ServicePresent||DriverFile||DriverPackage||ProductRegistered||RepositoryEvidence||ClassFilter||ClientEvidence||MaintenancePresent;}}
 }
 public sealed class HidHideRecord {
  public string Status="NOT_RUN",InstallerPath,ExpectedSha256,ActualSha256,Error,LogPath;
  public string LegacyUninstallReport,LegacyUninstallStatus;
  public int? LegacyUninstallExitCode;
  public string BundledPackageVersion=HidHide.PackageVersion,MinimumDriverVersion=HidHide.MinimumDriverVersion;
  public bool InstallAttempted,RebootRequired;
  public int? ExitCode;
  public HidHideSnapshot Before,After;
 }
 public sealed class HidHideUninstallReport {
  public string mode,status,finishedAt;
  public int? hidhideUninstallExit;
 }
 internal class HidHidePlatform {
  sealed class MaintenanceLease : IDisposable {
   readonly Mutex mutex;
   internal MaintenanceLease(Mutex mutex){this.mutex=mutex;}
   public void Dispose(){try{mutex.ReleaseMutex();}finally{mutex.Dispose();}}
  }
  internal virtual IDisposable TryEnterMaintenance(){return TryEnterMaintenance(HidHide.MaintenanceMutex);}
  internal static IDisposable TryEnterMaintenance(string name) {
   var mutex=new Mutex(false,name);bool held=false;
   try {
    try{held=mutex.WaitOne(0);}catch(AbandonedMutexException){mutex.ReleaseMutex();return null;}
    return held?new MaintenanceLease(mutex):null;
   }finally{if(!held)mutex.Dispose();}
  }
  [StructLayout(LayoutKind.Sequential)]struct InfContext {internal IntPtr Inf,CurrentInf;internal uint Section,Line;}
  [DllImport("setupapi.dll",CharSet=CharSet.Unicode)]static extern IntPtr SetupOpenInfFile(string path,string infClass,uint style,IntPtr errorLine);
  [DllImport("setupapi.dll",CharSet=CharSet.Unicode)]static extern bool SetupFindFirstLine(IntPtr inf,string section,string key,out InfContext context);
  [DllImport("setupapi.dll",CharSet=CharSet.Unicode)]static extern bool SetupGetStringField(ref InfContext context,uint index,StringBuilder value,uint size,IntPtr needed);
  [DllImport("setupapi.dll")]static extern void SetupCloseInfFile(IntPtr inf);
  internal static string ReadPackageVersion(string path) {
   const uint infStyleWin4=2;
   IntPtr inf=SetupOpenInfFile(path,null,infStyleWin4,IntPtr.Zero);
   if(inf==new IntPtr(-1))return null;
   try {
    InfContext line;Version version;var value=new StringBuilder(64);
    return SetupFindFirstLine(inf,"Version","DriverVer",out line)&&SetupGetStringField(ref line,2,value,(uint)value.Capacity,IntPtr.Zero)&&Version.TryParse(value.ToString().Trim(),out version)&&version.Revision>=0&&version!=new Version(0,0,0,0)?version.ToString():null;
   }finally{SetupCloseInfFile(inf);}
  }
  internal virtual HidHideSnapshot Observe() {
   var state=new HidHideSnapshot();
   string system=Environment.GetFolderPath(Environment.SpecialFolder.System),driver=Path.Combine(system,@"drivers\HidHide.sys");
   using(var hive=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64))
   using(var service=hive.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\HidHide")) {
    state.ServicePresent=service!=null;
    if(service!=null) {
     state.ServiceDisabled=Convert.ToInt32(service.GetValue("Start",4))==4;
     string image=Convert.ToString(service.GetValue("ImagePath","")).Trim('"');
     string windows=Environment.GetFolderPath(Environment.SpecialFolder.Windows);
     if(image.StartsWith(@"\SystemRoot\",StringComparison.OrdinalIgnoreCase))image=Path.Combine(windows,image.Substring(12));
     else if(image.StartsWith(@"System32\",StringComparison.OrdinalIgnoreCase))image=Path.Combine(windows,image);
     else if(image.StartsWith(@"\??\",StringComparison.Ordinal))image=image.Substring(4);
     image=Environment.ExpandEnvironmentVariables(image);
     state.ServiceImageMatches=Path.IsPathRooted(image)&&string.Equals(Path.GetFullPath(image),driver,StringComparison.OrdinalIgnoreCase)&&Convert.ToInt32(service.GetValue("Type",0))==1;
     using(var controller=new ServiceController("HidHide")){state.ServiceState=controller.Status.ToString();state.ServiceRunning=controller.Status==ServiceControllerStatus.Running;}
    }
   }
   state.DriverFile=File.Exists(driver);
   if(state.DriverFile){var version=FileVersionInfo.GetVersionInfo(driver);state.Version=new Version(version.FileMajorPart,version.FileMinorPart,version.FileBuildPart,version.FilePrivatePart).ToString();}
   string repository=Path.Combine(system,@"DriverStore\FileRepository");
   foreach(string folder in Directory.EnumerateDirectories(repository,"hidhide*",SearchOption.TopDirectoryOnly)) {
    // Keep partial package evidence distinct from a usable staged package.
    state.RepositoryEvidence=true;
    string inf=Path.Combine(folder,"HidHide.inf");
    bool hasInf=File.Exists(inf);
    string version=hasInf?ReadPackageVersion(inf):null;
    if(hasInf&&File.Exists(Path.Combine(folder,"HidHide.cat"))){state.DriverPackage=true;state.CompletePackageVersions.Add(version);}
    state.PackageVersions.Add(version);
   }
   foreach(var view in new[]{RegistryView.Registry64,RegistryView.Registry32})
   using(var hive=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,view))
   using(var uninstall=hive.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall")) {
    if(uninstall==null)continue;
    foreach(string name in uninstall.GetSubKeyNames())using(var product=uninstall.OpenSubKey(name)) {
     string display=product==null?"":Convert.ToString(product.GetValue("DisplayName",""));
     if(display.StartsWith("HidHide",StringComparison.OrdinalIgnoreCase)||string.Equals(display,"Nefarius Software Solutions e.U. HidHide (x64)",StringComparison.OrdinalIgnoreCase))state.ProductRegistered=true;
    }
   }
   state.ClientEvidence=File.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),@"Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe"));
   using(var hive=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64)) {
    // The runtime owner may deliberately leave no driver installed between reboots.
    using(var maintenance=hive.OpenSubKey(HidHide.MaintenanceKey))state.MaintenancePresent=HasMaintenanceRecord(maintenance);
    if(!state.Evidence)using(var classes=hive.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Class")) {
     if(classes==null)throw new IOException("无法确认 HidHide 类过滤器状态");
     foreach(string id in classes.GetSubKeyNames())using(var key=classes.OpenSubKey(id)) {
      if(key!=null&&(ContainsFilter(key.GetValue("UpperFilters"))||ContainsFilter(key.GetValue("LowerFilters")))){state.ClassFilter=true;break;}
     }
    }
   }
   return state;
  }
  internal static bool HasMaintenanceRecord(RegistryKey key){return key!=null&&key.GetValueNames().Contains("State",StringComparer.OrdinalIgnoreCase);}
  static bool ContainsFilter(object value){var list=value as string[];return list!=null?list.Contains("HidHide",StringComparer.OrdinalIgnoreCase):string.Equals(value as string,"HidHide",StringComparison.OrdinalIgnoreCase);}
  internal virtual void ValidateInstaller(string path,HidHideRecord record) {
   SafeFiles.NoLinks(path);
   if(!File.Exists(path))throw new IOException("包内 HidHide 安装程序缺失，请重新取得完整安装包");
   record.ActualSha256=SafeFiles.Hash(path);
   if(!string.Equals(record.ActualSha256,HidHide.InstallerSha256,StringComparison.OrdinalIgnoreCase))throw new IOException("HidHide 安装程序身份不符，已停止执行");
   Native.PublisherSignature(path,"O=Nefarius Software Solutions e.U.");
  }
  internal virtual int Install(string path,string logPath,Action<string> log) {
   using(var child=Process.Start(new ProcessStartInfo(path,HidHide.Arguments(logPath)){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Path.GetDirectoryName(path)})) {
    if(child==null)throw new IOException("HidHide 安装程序未能启动");
    // Never kill or replay an in-flight Windows Installer transaction.
    while(!child.WaitForExit(15000))try{log("HidHide 仍在安装，请等待 Windows 安装完成");}catch{}
    return child.ExitCode;
   }
  }
  internal virtual HidHideUninstallReport UninstallLegacy(string app,string work,HidHideRecord record) {
   // Run the bundled helper from protected setup staging, not the installed user tree.
   string stage=Path.Combine(work,"hidhide-maintenance"),redist=Path.Combine(stage,"PowerControl","redist");
   WorkSecurity.AssertDirectory(work);WorkSecurity.Create(stage);Directory.CreateDirectory(redist);SafeFiles.NoLinks(redist);
   foreach(string file in new[]{"HIDMaestroSetup.exe","HIDMaestroSetup.dll","HIDMaestroSetup.deps.json","HIDMaestroSetup.runtimeconfig.json","HidHide_1.5.230_x64.exe"}) {
    string source=Path.Combine(app,"PowerControl","redist",file);SafeFiles.NoLinks(source);
    File.Copy(source,Path.Combine(redist,file));
   }
   string report=Path.Combine(work,"HidHide-legacy-uninstall.json");record.LegacyUninstallReport=report;
   var start=new ProcessStartInfo(Path.Combine(redist,"HIDMaestroSetup.exe"),"--uninstall-legacy-hidhide --install-root \""+stage+"\" --report \""+report+"\"") {
    UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=redist
   };
   foreach(string key in start.EnvironmentVariables.Keys.Cast<string>().ToArray())
    if(key.StartsWith("DOTNET_",StringComparison.OrdinalIgnoreCase)||key.StartsWith("COREHOST_",StringComparison.OrdinalIgnoreCase)||key.StartsWith("CORECLR_",StringComparison.OrdinalIgnoreCase))start.EnvironmentVariables.Remove(key);
   using(var child=Process.Start(start)) {
    if(child==null)throw new IOException("HidHide 卸载助手未能启动");
    if(!child.WaitForExit(30000))return null;
    if(!File.Exists(report))throw new IOException("HidHide 卸载助手未生成记录，退出码 "+child.ExitCode);
    SafeFiles.NoLinks(report);
    var result=Json.Read<HidHideUninstallReport>(File.ReadAllText(report));
    if(result==null||result.mode!="uninstall-legacy-hidhide"||string.IsNullOrEmpty(result.finishedAt)||string.IsNullOrEmpty(result.status))
     throw new IOException("HidHide 卸载记录未完成");
    return result;
   }
  }
 }
 internal static class HidHide {
  // The official 1.5.230 product packages HidHide.sys 1.4.181.0. The
  // package and kernel driver use independent version numbers.
  internal const string PackageVersion="1.5.230";
  internal const string MinimumDriverVersion="1.4.181.0";
  internal const string MaintenanceKey=@"SOFTWARE\YeManCC\HidHideMaintenance";
  internal const string MaintenanceMutex=@"Global\YeManCC.GamepadPrerequisites.Install";
  internal const string InstallerSha256="F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6";
  static readonly Version MinimumDriver=new Version(MinimumDriverVersion);
  internal static bool Supported(Version version){return version!=null&&version>=MinimumDriver;}
  internal static string Arguments(string logPath){return "/exenoui /qn /norestart /L*v "+Native.Quote(logPath);}
  internal static void Prepare(string app,string work,HidHideRecord record,Action save,Action<string> log,Action<bool> reboot,HidHidePlatform platform) {
   record.InstallerPath=Path.Combine(app,@"PowerControl\redist\HidHide_1.5.230_x64.exe");
   record.ExpectedSha256=InstallerSha256;record.LogPath=Path.Combine(work,"HidHide-install.log");
   if(!TryDriver(()=>record.Before=platform.Observe(),record,save,log))return;
   if(record.Before.Evidence){KeepExisting(record,record.Before,save,log);return;}
   IDisposable lease=null;
   if(!TryDriver(()=>lease=platform.TryEnterMaintenance(),record,save,log))return;
   if(lease==null){record.Status="MAINTENANCE_BUSY";save();log(Warning(record));return;}
   using(lease)InstallFresh(record,save,log,reboot,platform);
  }
  static void InstallFresh(HidHideRecord record,Action save,Action<string> log,Action<bool> reboot,HidHidePlatform platform) {
   record.Status="VALIDATING_LOCAL_INSTALLER";save();
   if(!TryDriver(()=>platform.ValidateInstaller(record.InstallerPath,record),record,save,log))return;
   if(!TryDriver(()=>record.After=platform.Observe(),record,save,log))return;
   if(record.After.Evidence){KeepExisting(record,record.After,save,log);return;}
   record.Status="INSTALLING";record.InstallAttempted=true;save();log("正在从本地包安装 HidHide 驱动");
   record.After=null;
   if(!TryDriver(()=>record.ExitCode=platform.Install(record.InstallerPath,record.LogPath,log),record,save,log))return;
   int code=record.ExitCode.Value;
   record.RebootRequired=code==3010||code==1641;reboot(record.RebootRequired);
   record.Status="VERIFYING";save();
   if(!TryDriver(()=>{
    record.After=platform.Observe();
    if(code!=0&&code!=3010&&code!=1641)throw new IOException("HidHide 安装返回 "+code+"（0x"+code.ToString("X8")+"）");
    if(!record.RebootRequired&&!record.After.Ready)throw new IOException("HidHide 安装程序已退出，但驱动尚未确认就绪");
   },record,save,log))return;
   record.RebootRequired|=!record.After.ServiceRunning;
   record.Status=record.RebootRequired?"REBOOT_REQUIRED":"READY";reboot(record.RebootRequired);save();
   log(record.RebootRequired?"HidHide 安装要求重启":"HidHide 驱动已就绪");
  }
  internal static string LegacyVersion(HidHideSnapshot state) {
   var disk=state.DiskDriverVersion;
   if(disk!=null&&state.ServiceImageMatches)return !Supported(disk)?disk.ToString():null;
   Version staged;
   return state.PackageVersions.Count==1&&Version.TryParse(state.PackageVersions[0],out staged)&&!Supported(staged)?staged.ToString():null;
  }
  internal static string ExistingStatus(HidHideSnapshot state) {
   if(state.MaintenancePresent)return "MAINTENANCE_PENDING";
   if(state.Ready)return "EXISTING_UNCHANGED";
   if(LegacyVersion(state)!=null)return "EXISTING_LEGACY";
   return "EXISTING_UNVERIFIED";
  }
  internal static RuntimeObservation Detection(HidHideSnapshot state) {
   var row=new RuntimeObservation{name="HidHide 手柄驱动",ready=false};
   if(!state.Evidence)return row;
   switch(ExistingStatus(state)) {
    case "EXISTING_UNCHANGED":row.ready=true;row.badge="保持现状";break;
    case "EXISTING_LEGACY":row.badge="旧版，待修复";break;
    case "MAINTENANCE_PENDING":row.badge="维护未完成";break;
    default:row.badge="状态待确认";break;
   }
   return row;
  }
  internal static string Warning(HidHideRecord record) {
   switch(record.Status) {
    case "EXISTING_LEGACY":return "检测到旧版 HidHide（"+LegacyVersion(record.After??record.Before)+"），虚拟手柄可能不可用。请启动带有 HidHide 修复的 YeManCC 接续处理。";
    case "LEGACY_UNINSTALL_REBOOT":return "旧版 HidHide 已卸载。请重启 Windows，再打开 YeManCC 完成手柄驱动更新。";
    case "LEGACY_UNINSTALL_IN_PROGRESS":return "旧版 HidHide 卸载仍在后台进行。请稍后在 YeManCC 中查看手柄驱动状态。";
    case "LEGACY_UNINSTALL_SKIPPED":return "暂未卸载旧版 HidHide。请在 YeManCC 中查看手柄驱动状态。";
    case "LEGACY_UNINSTALL_FAILED":return "旧版 HidHide 未能卸载。YeManCC 已安装；虚拟手柄可能暂不可用。";
    case "MAINTENANCE_PENDING":return "HidHide 维护未完成。请按 YeManCC 的提示继续，安装器未重复安装驱动。";
    case "MAINTENANCE_BUSY":return "HidHide 的维护状态未能确认，已跳过补装。请在 YeManCC 中检查手柄驱动。";
    case "EXISTING_UNVERIFIED":return "已有 HidHide 的状态无法确认，已保留。虚拟手柄可能不可用。";
    case "FAILED":return record.InstallAttempted?"HidHide 未能安装，可能影响虚拟手柄。":"HidHide 检查未完成，未运行驱动安装程序。";
    default:return null;
   }
  }
  static void KeepExisting(HidHideRecord record,HidHideSnapshot state,Action save,Action<string> log){
   record.Status=ExistingStatus(state);
   if(record.Status=="EXISTING_LEGACY"&&state.DiskDriverVersion==new Version(1,1,50,0)&&state.ServiceImageMatches&&
    state.PackageVersions.Count!=0&&state.PackageVersions.All(v=>v=="1.1.50.0"))record.Status="LEGACY_UNINSTALL_QUEUED";
   save();log(record.Status=="LEGACY_UNINSTALL_QUEUED"?"检测到旧版 HidHide，将在主程序安装后尝试卸载":Warning(record)??"发现已有 HidHide，保持原状");
  }
  internal static void UninstallLegacy(string app,string work,HidHideRecord record,HidHidePlatform platform) {
   var result=platform.UninstallLegacy(app,work,record);
   if(result==null){record.Status="LEGACY_UNINSTALL_IN_PROGRESS";return;}
   record.LegacyUninstallStatus=result.status;record.LegacyUninstallExitCode=result.hidhideUninstallExit;
   if(result.status=="REBOOT_REQUIRED"&&(result.hidhideUninstallExit==0||result.hidhideUninstallExit==3010||result.hidhideUninstallExit==1641)) {
    record.RebootRequired=true;record.Status="LEGACY_UNINSTALL_REBOOT";
   }else if(result.status=="MAINTENANCE_PENDING")record.Status="MAINTENANCE_PENDING";
   else if(new[]{"SETUP_ALREADY_RUNNING","MAINTENANCE_BUSY","UPGRADE_CLOSE_APPS","UNINSTALL_IDENTITY_UNVERIFIED"}.Contains(result.status))
    record.Status="LEGACY_UNINSTALL_SKIPPED";
   else record.Status="LEGACY_UNINSTALL_FAILED";
  }
  static bool TryDriver(Action action,HidHideRecord record,Action save,Action<string> log){
   try{action();return true;}
   catch(Exception e){
    if(!(e is IOException||e is UnauthorizedAccessException||e is InvalidOperationException||e is System.ComponentModel.Win32Exception||e is System.Security.SecurityException||e is ArgumentException||e is FormatException))throw;
    record.Error=e.Message;record.Status="FAILED";save();log("HidHide 未完成："+e.Message);return false;
   }
  }
 }
}
