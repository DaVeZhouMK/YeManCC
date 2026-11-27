using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;

using System.Text;
using System.Web.Script.Serialization;

namespace YeMan.Setup {
 public sealed class RuntimeRestartRequiredException : IOException {public RuntimeRestartRequiredException(string message):base(message){}}
 public sealed class RuntimeObservation {public string name;public bool? ready;public string error,badge;}
 public sealed class SystemObservation {public bool Allowed;public string Label,Error;public int Major,Minor,Build;public ushort Architecture;public byte ProductType;}
 public static class Runtimes {
  public static RuntimeObservation Observe(string name,Func<bool> probe) {
   try{return new RuntimeObservation{name=name,ready=probe()};}
   catch(Exception error){return new RuntimeObservation{name=name,ready=null,error=error.Message};}
  }
  public const string WebViewBootstrapper="webview2-bootstrapper.exe";
  public const string WebViewId="{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
  public static bool WebView() {
   return WebAssets.RuntimeAvailable();
  }
  static string DotnetRoot {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"dotnet");}}
  static Version HighestVersion(string root,int major=-1,int minor=-1) {
   Version best=new Version(0,0);if(!Directory.Exists(root))return best;
   foreach(string path in Directory.GetDirectories(root)){Version version;if(Version.TryParse(Path.GetFileName(path),out version)&&(major<0||version.Major==major)&&(minor<0||version.Minor==minor)&&version>best)best=version;}
   return best;
  }
  static bool Image(string path,ushort machine=0,bool managed=false) {
   if(!File.Exists(path))return false;
   try {
    using(var stream=File.OpenRead(path))using(var reader=new BinaryReader(stream)) {
     if(stream.Length<64||reader.ReadUInt16()!=0x5a4d)return false;stream.Position=60;int offset=reader.ReadInt32();
     if(offset<64||offset>stream.Length-24)return false;stream.Position=offset;if(reader.ReadUInt32()!=0x4550)return false;
     ushort actual=reader.ReadUInt16();if(machine!=0?actual!=machine:actual!=0x8664&&actual!=0x14c)return false;
    }
    if(managed){var identity=AssemblyName.GetAssemblyName(path);if(!string.Equals(identity.Name,Path.GetFileNameWithoutExtension(path),StringComparison.Ordinal)||identity.ProcessorArchitecture!=ProcessorArchitecture.MSIL&&identity.ProcessorArchitecture!=ProcessorArchitecture.Amd64)return false;}
    return true;
   }catch(BadImageFormatException){return false;}catch(IOException){return false;}catch(UnauthorizedAccessException){return false;}
  }
  public static Version DesktopVersion() {
   string dotnet=DotnetRoot,root=Path.Combine(dotnet,@"shared\Microsoft.WindowsDesktop.App");Version version=HighestVersion(root,10),missing=new Version(0,0);
   if(version.Major!=10)return missing;string desktop=Path.Combine(root,version.ToString());
   foreach(string name in new[]{"PresentationFramework.dll","PresentationCore.dll","WindowsBase.dll","System.Windows.Forms.dll"})if(!Image(Path.Combine(desktop,name),managed:true))return missing;
   Version required;
   try {
    string manifest=Path.Combine(desktop,"Microsoft.WindowsDesktop.App.runtimeconfig.json");if(!File.Exists(manifest)||new FileInfo(manifest).Length>65536)return missing;
    var document=new JavaScriptSerializer{MaxJsonLength=65536}.Deserialize<Dictionary<string,object>>(File.ReadAllText(manifest));
    var options=(Dictionary<string,object>)document["runtimeOptions"];var framework=(Dictionary<string,object>)options["framework"];
    if(Convert.ToString(framework["name"])!="Microsoft.NETCore.App"||!Version.TryParse(Convert.ToString(framework["version"]),out required)||required.Major!=10||required.Minor!=version.Minor)return missing;
   }catch(Exception){return missing;}
   string coreRoot=Path.Combine(dotnet,@"shared\Microsoft.NETCore.App");Version core=HighestVersion(coreRoot,required.Major,required.Minor);if(core<required)return missing;
   string corePath=Path.Combine(coreRoot,core.ToString());
   if(!Image(Path.Combine(corePath,"coreclr.dll"),0x8664)||!Image(Path.Combine(corePath,"hostpolicy.dll"),0x8664)||!Image(Path.Combine(corePath,"System.Private.CoreLib.dll"),managed:true))return missing;
   string hostRoot=Path.Combine(dotnet,@"host\fxr");Version host=HighestVersion(hostRoot);
   if(host<required||!Image(Path.Combine(hostRoot,host.ToString(),"hostfxr.dll"),0x8664)||!Image(Path.Combine(dotnet,"dotnet.exe"),0x8664))return missing;
   return version;
  }
  public static bool VC(string arch) {
   return VC(arch,Environment.GetFolderPath(Environment.SpecialFolder.Windows));
  }
  internal static bool VC(string arch,string windows) {
   if(arch!="x64"&&arch!="x86")throw new IOException("不支持的 VC++ 架构");
   string folder=Path.Combine(windows,arch=="x64"?"System32":"SysWOW64");
   // Presence check only; the two architectures must not satisfy each other.
   return File.Exists(Path.Combine(folder,"msvcp140.dll"))&&File.Exists(Path.Combine(folder,"vcruntime140.dll"));
  }
  internal static string InstallArguments(Version installed,Version bundled,string component) {
   if(installed>bundled)throw new IOException(component+" 的较新版本 "+installed+" 文件不完整，请修复该版本后重试；包内 "+bundled+" 不会覆盖较新版本");
   return (installed==bundled?"/repair":"/install")+" /quiet /norestart";
  }
  public static readonly Version BundledDesktopVersion=new Version(BuildInfo.DesktopRuntimeVersion);
  public static bool DesktopReady(){return DesktopVersion()>=BundledDesktopVersion;}
  public static string Summary() {return (WebView()?"WebView2 已就绪":"WebView2 需联网补齐")+"\r\n.NET 10 桌面运行时 "+(DesktopReady()?"已就绪":"将从包内补齐")+" · VC++ "+(VC("x64")&&VC("x86")?"已就绪":"将从包内补齐");}
  public static bool Ready(){return WebView()&&VC("x64")&&VC("x86")&&DesktopReady();}
  public static SystemObservation AssessSystem(int major,int minor,int build,ushort architecture,byte productType) {
   var result=new SystemObservation{Major=major,Minor=minor,Build=build,Architecture=architecture,ProductType=productType};
   result.Label="Windows "+(major==10&&build>=22000?"11":major==10?"10":major+"."+minor)+" · "+(architecture==9?"x64":architecture==12?"ARM64":architecture==0?"x86":"未知架构")+" · "+build;
   if(architecture!=9)result.Error="此核心包包含 x64 调度组件和驱动，仅适用于原生 x64 Windows；ARM64 和 32 位系统请勿使用此包";
   else if(major!=10||minor!=0||build<19044)result.Error="此核心包的兼容基线是 Windows 10 21H2 或更高版本，或 Windows 11；较旧系统尚未适配，请先升级系统";
   else if(productType!=1)result.Error="此核心包面向 Windows 10/11 桌面系统，暂不支持 Windows Server 或未知系统类型";
   else result.Allowed=true;
   return result;
  }
  public static bool CheckExit(int code,bool available,string component) {
   bool restart=code==3010||code==1641;
   if(restart&&!available)throw new RuntimeRestartRequiredException(component+" 已要求重启。请保存工作并自行重启，然后重新运行安装器；YMCC 尚未安装完成");
   // A ready older installation cannot turn an arbitrary installer failure into success.
   // ERROR_PRODUCT_VERSION is the sole existing-version exception, with a fresh readiness check.
   bool existing=code==1638||code==unchecked((int)0x80070666);
   if(code!=0&&!restart&&!existing) {
    string reason=code==1618||code==unchecked((int)0x80070652)?"另一个 Windows 安装正在进行，请等它结束后重试":code==1602||code==1223||code==unchecked((int)0x800704C7)?"安装已取消，请重新运行安装器并确认权限请求":"安装失败，请查看本次运行库安装日志";
    throw new IOException(component+" 安装返回 "+code+"（0x"+code.ToString("X8")+"）："+reason);
   }
   if(!available)throw new IOException(component+" 安装返回 "+code+"（0x"+code.ToString("X8")+"），但所需环境仍不可用。请检查权限、网络或安装日志后重试");
   return restart;
  }
  public static bool Run(string path,string args,Func<bool> available=null,Action<string> log=null) {
   Native.MicrosoftSignature(path);
   using(var p=Process.Start(new ProcessStartInfo(path,args){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Path.GetDirectoryName(path)})) {
    // Keep the UI alive; never force-terminate a runtime installer in mid-write.
    while(!p.WaitForExit(15000))if(log!=null)try{log(Path.GetFileName(path)+" 仍在运行，请等待 Windows 安装完成");}catch{}
    int code=p.ExitCode;if(log!=null)try{log(Path.GetFileName(path)+" 返回 "+code+"，正在重新检测环境");}catch{}
    if(available!=null)return CheckExit(code,available(),Path.GetFileName(path));
    if(code!=0&&code!=3010&&code!=1641)throw new IOException(Path.GetFileName(path)+" 安装返回 "+code);return code==3010||code==1641;
   }
  }
  static void PrepareOne(string label,string installer,Func<string> arguments,Func<bool> ready,Action<string> log,Action<bool> reboot,Action<string> warn) {
   log("检查 "+label);
   try {
    if(ready())return;
    if(arguments==null){warn(label+" 未就绪，可联网后补装。");return;}
    reboot(Run(installer,arguments(),ready));
   }catch(Exception e){
    if(!(e is IOException||e is UnauthorizedAccessException||e is System.ComponentModel.Win32Exception||e is System.Security.SecurityException||e is InvalidOperationException))throw;
    bool restart=e is RuntimeRestartRequiredException;reboot(restart);
    warn(label+(restart?" 重启后再检查。":" 未能补齐，可能影响运行。"));
    log(label+" 未完成："+e.Message);
   }
  }
  public static void Prepare(string resource,string cache,bool webOnline,Action<string> log,Action<bool> reboot,Action<string> warn) {
   const string desktop=".NET 10 桌面运行时";
   PrepareOne(desktop,Path.Combine(resource,"desktop.exe"),()=>InstallArguments(HighestVersion(Path.Combine(DotnetRoot,@"shared\Microsoft.WindowsDesktop.App"),10),BundledDesktopVersion,desktop)+" /log "+Native.Quote(Path.Combine(cache,"runtime-desktop.log")),DesktopReady,log,reboot,warn);
   foreach(string arch in new[]{"x64","x86"})PrepareOne("VC++ "+arch,Path.Combine(resource,"vc-"+arch+".exe"),()=>"/install /quiet /norestart /log "+Native.Quote(Path.Combine(cache,"runtime-vc-"+arch+".log")),()=>VC(arch),log,reboot,warn);
   PrepareOne("WebView2",Path.Combine(resource,WebViewBootstrapper),webOnline?(Func<string>)(()=>"/silent /install"):null,WebView,log,reboot,warn);
  }
 }
}
