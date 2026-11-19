using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Management;
using Microsoft.Win32;

namespace YeMan.Setup {
 public sealed class DefenderEntry {
  public string Path;public bool Temporary,BeforeExact,BeforeEffective,Attempted,Added,Removed;
 }
 public sealed class DefenderRecord {
  public string Status="not-requested",Message="";
  public List<DefenderEntry> Entries=new List<DefenderEntry>();
 }
 // The production client uses only the OS Defender provider and its signed checker.
 // An explicit client allows isolated verification without changing host protection.
 public interface IDefenderClient {
  bool Active();string[] ExactPaths();bool IsExcluded(string path);void Add(string path);void Remove(string path);
 }
 public sealed class WindowsDefenderClient : IDefenderClient {
  readonly ManagementScope scope=new ManagementScope(@"\\.\root\Microsoft\Windows\Defender",new ConnectionOptions{Timeout=TimeSpan.FromSeconds(20)});
  string checker;
  public bool Active() {
   using(var query=new ManagementObjectSearcher(scope,new ObjectQuery("SELECT * FROM MSFT_MpComputerStatus"),new EnumerationOptions{Timeout=TimeSpan.FromSeconds(20)}))
   using(var values=query.Get())foreach(ManagementObject value in values)using(value) {
    var mode=value.Properties["AMRunningMode"];
    return Convert.ToBoolean(value["AMServiceEnabled"])&&Convert.ToBoolean(value["AntivirusEnabled"])&&mode!=null&&string.Equals(Convert.ToString(mode.Value),"Normal",StringComparison.OrdinalIgnoreCase);
   }return false;
  }
  public string[] ExactPaths() {
   using(var key=Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Policies\Microsoft\Windows Defender"))if(key!=null&&Convert.ToInt32(key.GetValue("HideExclusionsFromLocalAdmins",0))!=0)throw new IOException("Defender 策略隐藏排除项，无法安全记录原有项");
   using(var preferences=new ManagementClass(scope,new ManagementPath("MSFT_MpPreference"),null))
   using(var values=preferences.GetInstances())foreach(ManagementObject value in values)using(value)return (string[])value["ExclusionPath"]??new string[0];
   throw new IOException("无法读取 Defender 排除项");
  }
  public bool IsExcluded(string path) {
   // MpCmdRun returns 2/ERROR_FILE_NOT_FOUND for future unpack/staging paths.
   // Confirm their exact registration now; Finish() uses the real checker after final files exist.
   if(!File.Exists(path)&&!Directory.Exists(path))return Array.Exists(ExactPaths(),p=>string.Equals(p,path,StringComparison.OrdinalIgnoreCase));
   if(checker==null){checker=System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),@"Windows Defender\MpCmdRun.exe");SafeFiles.NoLinks(checker);Native.MicrosoftSignature(checker);}
   using(var process=Process.Start(new ProcessStartInfo(checker,"-CheckExclusion -Path "+Native.Quote(path)){UseShellExecute=false,CreateNoWindow=true})) {
    if(!process.WaitForExit(15000)){try{process.Kill();process.WaitForExit(2000);}catch{}throw new IOException("Defender 排除项有效性检查超时");}
    if(process.ExitCode==0)return true;if(process.ExitCode==1)return false;
    throw new IOException("Defender 无法核验排除项，返回 "+process.ExitCode);
   }
  }
  void Change(string method,string path) {
   using(var preferences=new ManagementClass(scope,new ManagementPath("MSFT_MpPreference"),null))
   using(var input=preferences.GetMethodParameters(method)) {
    input["ExclusionPath"]=new[]{path};
    using(var output=preferences.InvokeMethod(method,input,new InvokeMethodOptions{Timeout=TimeSpan.FromSeconds(20)})) {
     // Some Defender providers expose these methods as void. The caller always re-reads the actual exclusion list.
     if(output!=null){var code=output.Properties["ReturnValue"];if(code!=null&&code.Value!=null){uint result=Convert.ToUInt32(code.Value);if(result!=0)throw new IOException("Defender "+method+" 返回 0x"+result.ToString("X8")+"，可能受策略或权限限制");}}
    }
   }
  }
  public void Add(string path){Change("Add",path);}
  public void Remove(string path){Change("Remove",path);}
 }
 public sealed class DefenderExclusions {
  // Exact first-party core executables only; no folders, wildcards, process or extension exclusions.
  static readonly string[] CoreFiles={@"YeManCC\YeManCC.exe",@"YeManCC\YeManRecoveryService.exe",@"PowerControl\fan-host-v2\YeManFanHost.exe",@"PowerControl\feature-assets\virtual-gamepad\YeManInputHost.exe",@"PowerControl\light-setter\YeManLightSetter.exe",@"PowerControl\pawnio\YeManTdpCtl.exe"};
  readonly IDefenderClient client;readonly DefenderRecord record;readonly Action persist;readonly Action<string> log;readonly string work;
  public DefenderExclusions(IDefenderClient api,DefenderRecord state,string operation,Action save,Action<string> report){client=api;record=state;work=operation;persist=save;log=report;}
  public static List<DefenderEntry> Targets(string work) {
   var result=new List<DefenderEntry>();foreach(string file in CoreFiles){result.Add(new DefenderEntry{Path=System.IO.Path.Combine(Plan.InstallRoot,file)});result.Add(new DefenderEntry{Path=System.IO.Path.Combine(work,"app",file),Temporary=true});}
   result.Add(new DefenderEntry{Path=System.IO.Path.Combine(Plan.InstallRoot,"启动YeManCC.exe")});
   var final=new List<DefenderEntry>(result);foreach(var file in final)if(!file.Temporary)result.Add(new DefenderEntry{Path=FileTransaction.StagingPath(file.Path,work),Temporary=true});return result;
  }
  static bool Has(string[] paths,string path){return Array.Exists(paths,p=>string.Equals(p,path,StringComparison.OrdinalIgnoreCase));}
  void ValidateRecord() {
   var allowed=new Dictionary<string,bool>(StringComparer.OrdinalIgnoreCase);foreach(var e in Targets(work))allowed.Add(e.Path,e.Temporary);
   var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   if(record.Entries==null||record.Entries.Count>allowed.Count)throw new IOException("Defender 恢复记录无效");
   foreach(var e in record.Entries){bool temporary;if(e==null||e.Path==null||!seen.Add(e.Path)||!allowed.TryGetValue(e.Path,out temporary)||temporary!=e.Temporary)throw new IOException("Defender 恢复记录超出本安装器范围");}
  }
  public void Prepare(bool requested) {
   if(!requested){record.Status="not-requested";record.Message="未选择 Defender 排除项，保留现有防护设置";persist();return;}
   try {
    if(!client.Active()){record.Status="unavailable";record.Message="Defender 未处于主动防护状态，未修改排除项；第三方杀软请自行确认";persist();log(record.Message);return;}
    var original=client.ExactPaths();record.Entries=Targets(work);ValidateRecord();
    // Read every baseline before the first mutation. Broad/policy exclusions are preserved.
    foreach(var e in record.Entries){SafeFiles.NoLinks(e.Path);e.BeforeExact=Has(original,e.Path);e.BeforeEffective=client.IsExcluded(e.Path);}
    record.Status="applying";record.Message="正在添加 YMCC 必要程序的 Defender 排除项";persist();
    foreach(var e in record.Entries) {
     if(e.BeforeEffective||e.BeforeExact)continue;
     e.Attempted=true;persist();client.Add(e.Path);e.Added=true;persist();
     if(!Has(client.ExactPaths(),e.Path))throw new IOException("Defender 未保存新增排除项，可能受组织策略或权限限制");
    }
    record.Status="prepared";record.Message="YMCC 精确路径排除项已登记，文件安装后核验；临时项将在结束时撤销";persist();log(record.Message);
   }catch(Exception error) {
    record.Message="Defender 排除项未完成："+error.Message;log(record.Message);
    RemoveOwned(false);record.Status="warning";persist();
   }
  }
  public void Finish() {
   RemoveOwned(true);
   if(record.Status!="prepared")return;
   try {
    // The engine applies registered settings asynchronously. Use one bounded final check, not a delay per file.
    var deadline=DateTime.UtcNow.AddSeconds(15);
    while(true){bool complete=true;foreach(var e in record.Entries)if(!e.Temporary&&!client.IsExcluded(e.Path)){complete=false;break;}
     if(complete)break;if(DateTime.UtcNow>=deadline)throw new IOException("排除项尚未确认生效");System.Threading.Thread.Sleep(250);
    }
   }
   catch(Exception error){record.Status="warning";record.Message="程序文件已安装，但 Defender 最终核验未完成："+error.Message+"。请在 Windows 安全中心检查";persist();log(record.Message);return;}
   record.Status="verified";record.Message="Defender 已确认 YMCC 7 个核心程序路径受排除；安装临时排除项已清理";persist();log(record.Message);
  }
  public void Rollback(){RemoveOwned(false);if(record.Status!="not-requested"){record.Status="rolled-back";record.Message="本次新增的 Defender 排除项已撤销，原有项保留";persist();}}
  void RemoveOwned(bool temporaryOnly) {
   ValidateRecord();var errors=new List<string>();
   foreach(var e in record.Entries)if(e.Attempted&&!e.BeforeExact&&!e.BeforeEffective&&!e.Removed&&(!temporaryOnly||e.Temporary))try {
    if(Has(client.ExactPaths(),e.Path))client.Remove(e.Path);
    if(Has(client.ExactPaths(),e.Path))throw new IOException("新增排除项仍然存在");
    e.Removed=true;persist();
   }catch(Exception error){errors.Add(e.Path+"："+error.Message);}
   if(errors.Count>0){record.Status="cleanup-required";persist();throw new IOException("本次 Defender 排除项撤销未完成："+string.Join("；",errors));}
  }
 }
}
