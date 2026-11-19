using System;
using System.Diagnostics;
using System.IO;
using System.ServiceProcess;
using Microsoft.Win32;

namespace YeMan.Setup {
 public sealed class HidHideSnapshot {
  public bool ServicePresent,ServiceRunning,ServiceDisabled,ServiceImageMatches,DriverFile,DriverPackage,ProductRegistered;
  public string ServiceState,Version;
  public bool Ready {get{Version version;return ServicePresent&&!ServiceDisabled&&ServiceImageMatches&&DriverFile&&DriverPackage&&System.Version.TryParse(Version,out version)&&version>=new Version(HidHide.MinimumDriverVersion);}}
  public bool Evidence {get{return ServicePresent||DriverFile||DriverPackage||ProductRegistered;}}
 }
 public sealed class HidHideRecord {
  public string Status="NOT_RUN",InstallerPath,ExpectedSha256,ActualSha256,Error,LogPath;
  public string BundledPackageVersion=HidHide.PackageVersion,MinimumDriverVersion=HidHide.MinimumDriverVersion;
  public bool InstallAttempted,RebootRequired;
  public int? ExitCode;
  public HidHideSnapshot Before,After;
 }
 internal class HidHidePlatform {
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
   foreach(string folder in Directory.GetDirectories(repository,"hidhide*",SearchOption.TopDirectoryOnly)) {
    // Keep partial package evidence distinct from a usable staged package.
    state.ProductRegistered=true;
    if(File.Exists(Path.Combine(folder,"HidHide.inf"))&&File.Exists(Path.Combine(folder,"HidHide.cat")))state.DriverPackage=true;
   }
   foreach(var view in new[]{RegistryView.Registry64,RegistryView.Registry32})
   using(var hive=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,view))
   using(var uninstall=hive.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall")) {
    if(uninstall==null)continue;
    foreach(string name in uninstall.GetSubKeyNames())using(var product=uninstall.OpenSubKey(name)) {
     if(product!=null&&Convert.ToString(product.GetValue("DisplayName","")).StartsWith("HidHide",StringComparison.OrdinalIgnoreCase))state.ProductRegistered=true;
    }
   }
   return state;
  }
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
 }
 internal static class HidHide {
  // The official 1.5.230 product packages HidHide.sys 1.4.181.0. The
  // package and kernel driver use independent version numbers.
  internal const string PackageVersion="1.5.230";
  internal const string MinimumDriverVersion="1.4.181.0";
  internal const string InstallerSha256="F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6";
  internal static string Arguments(string logPath){return "/exenoui /qn /norestart /L*v "+Native.Quote(logPath);}
  internal static void Prepare(string app,string work,HidHideRecord record,Action save,Action<string> log,Action<bool> reboot,HidHidePlatform platform) {
   record.InstallerPath=Path.Combine(app,@"PowerControl\redist\HidHide_1.5.230_x64.exe");
   record.ExpectedSha256=InstallerSha256;record.LogPath=Path.Combine(work,"HidHide-install.log");
   try {
    record.Before=platform.Observe();
    if(record.Before.Ready){record.Status="EXISTING_READY";save();log("HidHide 已安装，复用现有驱动");return;}
    if(record.Before.Evidence)throw new IOException("检测到 HidHide 已有或残缺安装，请先重启；仍异常时修复现有 HidHide，安装器不会覆盖或卸载它");
    record.Status="VALIDATING_LOCAL_INSTALLER";save();platform.ValidateInstaller(record.InstallerPath,record);
    var latest=platform.Observe();
    if(latest.Ready){record.After=latest;record.Status="EXISTING_READY";save();log("HidHide 已由其他安装完成，复用现有驱动");return;}
    if(latest.Evidence)throw new IOException("HidHide 状态在准备期间发生变化，请等待其他安装完成后重试");
    record.Status="INSTALLING";record.InstallAttempted=true;save();log("正在从本地包安装 HidHide 驱动");
    int code=platform.Install(record.InstallerPath,record.LogPath,log);record.ExitCode=code;record.Status="VERIFYING";save();
    record.After=platform.Observe();
    if(code!=0&&code!=3010&&code!=1641)throw new IOException("HidHide 安装返回 "+code+"（0x"+code.ToString("X8")+"），请检查安装日志后重试");
    if(!record.After.Ready) {
     if(code==3010||code==1641){record.RebootRequired=true;reboot(true);throw new RuntimeRestartRequiredException("HidHide 安装要求重启，驱动尚未确认就绪。请重启后重新运行安装器");}
     throw new IOException("HidHide 安装程序已退出，但驱动、服务及驱动包未全部确认，请保留安装日志");
    }
    record.RebootRequired=code==3010||code==1641||!record.After.ServiceRunning;
    record.Status=record.RebootRequired?"REBOOT_REQUIRED":"READY";reboot(record.RebootRequired);save();
    log(record.RebootRequired?"HidHide 已安装，请重启后使用手柄功能":"HidHide 驱动已准备好");
   }catch(Exception error){record.Error=error.Message;record.Status=error is RuntimeRestartRequiredException?"REBOOT_REQUIRED":"FAILED";save();throw;}
  }
 }
}
