using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading;

namespace YeMan.Setup {
 public static class Resources {
  internal static long PackedLength {get{using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("core.payload")){if(stream==null)throw new IOException("安装器缺少核心资源");return stream.Length;}}}

  public static string UnpackOne(string output,string name) {
   if(name!=Runtimes.WebViewBootstrapper)throw new InvalidDataException("仅支持 WebView2 小型引导程序");
   SafeFiles.NoLinks(output);Directory.CreateDirectory(output);
   using(var stream=System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("core.payload"))
   using(var zip=new ZipArchive(stream,ZipArchiveMode.Read)) {
    var metadata=zip.GetEntry("bundle.json");if(metadata==null||metadata.Length>65536)throw new InvalidDataException("资源清单无效");
    byte[] bytes;using(var input=metadata.Open())using(var memory=new MemoryStream()){input.CopyTo(memory);bytes=memory.ToArray();}
    using(var input=new MemoryStream(bytes))if(SafeFiles.Hash(input)!=BuildInfo.ManifestSha256)throw new InvalidDataException("资源清单校验失败");
    var bundle=Json.Read<Bundle>(Encoding.UTF8.GetString(bytes).TrimStart('\ufeff'));
    Blob selected=null;foreach(var file in bundle.Files)if(file.Path==name){if(selected!=null)throw new InvalidDataException("重复的环境资源");selected=file;}
    if(selected==null||selected.Length>5L*1024*1024)throw new InvalidDataException("缺少有效的 WebView2 小型引导程序");
    var entry=zip.GetEntry(name);if(entry==null||entry.Length!=selected.Length)throw new InvalidDataException("本地界面环境文件无效");
    string path=Path.Combine(output,name);using(var input=entry.Open())using(var file=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.None))input.CopyTo(file);
    SafeFiles.Verify(path,selected);return path;
   }
  }
  public static Bundle Unpack(string output,Action<string> log) {
   SafeFiles.NoLinks(output);Directory.CreateDirectory(output);
   using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("core.payload")) {
    if(stream==null)throw new InvalidDataException("安装器缺少核心资源");
    using(var zip=new ZipArchive(stream,ZipArchiveMode.Read)) {
     var entry=zip.GetEntry("bundle.json");if(entry==null||entry.Length>65536)throw new InvalidDataException("资源清单无效");
     string text;using(var input=entry.Open())using(var data=new MemoryStream()) {
      input.CopyTo(data);data.Position=0;if(SafeFiles.Hash(data)!=BuildInfo.ManifestSha256)throw new InvalidDataException("资源清单校验失败");text=Encoding.UTF8.GetString(data.ToArray()).TrimStart('\ufeff');
     }
     var b=Json.Read<Bundle>(text);if(b.Schema!=1||b.Files==null||b.Files.Length!=8)throw new InvalidDataException("不支持的本地核心资源清单");Config.StrictVersion(b.Version);
     var expected=new Dictionary<string,Blob>(StringComparer.OrdinalIgnoreCase);
     foreach(var f in b.Files){SafeFiles.EntryName(f.Path);if(f.Path.Contains("/")||f.Path.Contains("\\")||expected.ContainsKey(f.Path))throw new InvalidDataException("资源清单路径冲突");expected.Add(f.Path,f);}
     foreach(string required in new[]{"program.zip","hw.zip","rtss.zip","desktop.exe","vc-x64.exe","vc-x86.exe",Runtimes.WebViewBootstrapper,"launcher.exe"})if(!expected.ContainsKey(required))throw new InvalidDataException("缺少核心资源："+required);
     if(expected[Runtimes.WebViewBootstrapper].Length>5L*1024*1024)throw new InvalidDataException("WebView2 只能随包携带小型引导程序");
     if(zip.Entries.Count!=expected.Count+1)throw new InvalidDataException("资源数量不匹配");
     var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
     foreach(var e in zip.Entries) {
      if(!seen.Add(e.FullName))throw new InvalidDataException("资源路径重复");if(e.FullName=="bundle.json")continue;
      Blob f;if(!expected.TryGetValue(e.FullName,out f)||e.Length!=f.Length)throw new InvalidDataException("未知或损坏资源");
      log("核验本地资源："+f.Path);string p=Path.Combine(output,f.Path);
      using(var input=e.Open())using(var file=new FileStream(p,FileMode.CreateNew,FileAccess.Write,FileShare.None))input.CopyTo(file);
      SafeFiles.Verify(p,f);
     }
     Json.Save(Path.Combine(output,"bundle.json"),b);return b;
    }
   }
  }
 }
 public sealed class Engine {
  readonly Action<string> progress;readonly InstallPlatform platform; public Receipt Result; string receiptPath;
  public Engine(Action<string> report):this(new InstallPlatform(),report){}
  internal Engine(InstallPlatform boundary,Action<string> report){platform=boundary;progress=report;}
  void Log(string s){if(Result!=null)Result.Events.Add(DateTime.UtcNow.ToString("o")+" "+s);try{progress(s);}catch{}if(receiptPath!=null&&Result!=null)platform.Save(receiptPath,Result);}
  public static string InstalledVersion(string root) {
   string p=Path.Combine(root,@"YeManCC\YeManCC.exe");if(!File.Exists(p))return "";
   try {string s=FileVersionInfo.GetVersionInfo(p).ProductVersion;Config.StrictVersion(s);return s;} catch{return "unknown";}
  }
  public static void ValidateRoot(string root) {
   root=SafeFiles.Full(root);SafeFiles.NoLinks(root);
   if(root.Length<4||root.Length>200||root==Path.GetPathRoot(root).TrimEnd('\\'))throw new IOException("请选择较短的专用安装目录（最长 200 字符）");
   string windows=Environment.GetFolderPath(Environment.SpecialFolder.Windows);
   if(string.Equals(root,windows,StringComparison.OrdinalIgnoreCase)||SafeFiles.Under(root,windows))throw new IOException("不能安装到 Windows 系统目录");
   foreach(string special in new[]{Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData)})
    if(string.Equals(root,special,StringComparison.OrdinalIgnoreCase))throw new IOException("请选择独立子目录");

  }
  internal static bool ValidateTarget(string root) {
   if(File.Exists(root))throw new IOException("安装路径被同名文件占用，已保护原内容");
   ValidateRoot(root);
   string main=Path.Combine(root,@"YeManCC\YeManCC.exe");SafeFiles.NoLinks(main);
   return File.Exists(main);
  }
  public static InstallAssessment DetectInstall(string root,string selected) {
   var status=new InstallAssessment{PackageVersion=selected,Action="blocked"};
   try {
    bool existing=ValidateTarget(root);status.Allowed=true;
    status.Action=existing?"overwrite":"install";
    status.Label=existing?"覆盖程序文件，保留设置":"安装 YMCC "+selected;
   }catch(Exception error){status.Error=error.Message;status.Label="包内 "+selected;}
   return status;
  }
  static bool Pending(Receipt receipt){return receipt.Phase=="preparing"||receipt.Phase=="applying"||receipt.Phase=="repair-required";}
  public static InstallAssessment AssessInstall(){return AssessInstall(new InstallPlatform());}
  internal static InstallAssessment AssessInstall(InstallPlatform boundary) {
   var status=DetectInstall(boundary.Root,BuildInfo.ProgramVersion);
   // Only the elevated engine needs to read protected recovery records.
   if(!status.Allowed||!boundary.Admin)return status;
   if(!Directory.Exists(boundary.Parent))return status;
   try {
    boundary.AssertDirectory(boundary.Parent);
    foreach(string op in Directory.GetDirectories(boundary.Parent,"op-*")) {
     boundary.AssertDirectory(op);string path=Path.Combine(op,"result.json");if(!File.Exists(path))continue;
     boundary.AssertFile(path);var receipt=Json.Read<Receipt>(File.ReadAllText(path));
     if(receipt!=null&&Pending(receipt)&&string.Equals(receipt.Root,boundary.Root,StringComparison.OrdinalIgnoreCase)) {
      status.Action="recover";status.Label="恢复上次改动后继续安装";return status;
     }
    }
   }catch(Exception error){status.Allowed=false;status.Action="blocked";status.Error="安装记录需检查："+error.Message;}
   return status;
  }
  void InstallComponent(FileTransaction tx,string label,string source,string target,Predicate<string> preserve) {
   try {
    SafeFiles.NoLinks(target);
    platform.ClosePrograms(target,Log);
    Log("正在准备 "+label+" 调度依赖");
    tx.Tree(source,target,true,preserve,true);
   }catch(Exception e){
    if(!(e is InstallTargetBusyException||e is UnsafeInstallPathException||e is UnauthorizedAccessException))throw;
    tx.RestoreTree(target);
    Result.Warnings.Add(label+" 暂时无法更新，已跳过。");Log(label+" 未更新："+e.Message+(e.InnerException==null?"":" · "+e.InnerException.Message));
   }
  }
  public static string SecureWork(string parent) {
   WorkSecurity.Create(parent);
   var security=new DirectorySecurity();security.SetAccessRuleProtection(true,false);security.SetOwner(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null));
   security.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null),FileSystemRights.FullControl,InheritanceFlags.ContainerInherit|InheritanceFlags.ObjectInherit,PropagationFlags.None,AccessControlType.Allow));
   security.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null),FileSystemRights.FullControl,InheritanceFlags.ContainerInherit|InheritanceFlags.ObjectInherit,PropagationFlags.None,AccessControlType.Allow));
   Directory.SetAccessControl(parent,security);string work=Path.Combine(parent,"op-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(work,security);return work;
  }
  static void CheckFrameworks(string app) {
   foreach(var p in Directory.GetFiles(app,"*.runtimeconfig.json",SearchOption.AllDirectories)) {
    var doc=Json.Read<Dictionary<string,object>>(File.ReadAllText(p));
    object options;if(!doc.TryGetValue("runtimeOptions",out options))throw new IOException("运行时声明无效："+Path.GetFileName(p));
    string text=File.ReadAllText(p);
    foreach(System.Text.RegularExpressions.Match m in System.Text.RegularExpressions.Regex.Matches(text,"\\\"version\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"")) {
     Version v;if(!Version.TryParse(m.Groups[1].Value,out v)||v.Major!=10||v>Runtimes.BundledDesktopVersion)throw new IOException("所选 YMCC 需要另一等级的运行环境，请换用配套核心安装器："+m.Groups[1].Value);
    }
   }
  }
  public Receipt Execute(Plan plan) {
   Plan.Validate(plan);
   if(!platform.Admin)throw new IOException("安装需要 Windows 管理员权限");
   using(var mutex=new Mutex(false,platform.MutexName)) {
    bool entered=false;try{entered=mutex.WaitOne(0);}catch(AbandonedMutexException){entered=true;}if(!entered)throw new IOException("另一个设置中心正在处理，请稍后再试");
    try{return ExecuteLocked(plan);}finally{mutex.ReleaseMutex();}
   }
  }
  Receipt ExecuteLocked(Plan plan) {
   Plan.Validate(plan);RecoverInterrupted();string root=platform.Root;
   bool existing=ValidateTarget(root);
   string hw=platform.Hw;
   string rtss=platform.Rtss;
   platform.Space(platform.Parent,Resources.PackedLength+64L*1024*1024);
   string work=platform.Work();
   Result=new Receipt {OperationId=Path.GetFileName(work),Phase="preparing",Root=root,StartedUtc=DateTime.UtcNow.ToString("o"),Backup=Path.Combine(work,"backup"),DesktopShortcutRequested=plan.DesktopShortcut};
   string receipt=Path.Combine(work,"result.json");receiptPath=receipt;platform.Save(receipt,Result);FileTransaction tx=null;
   var defender=new DefenderExclusions(platform.Defender,Result.Defender,work,()=>platform.Save(receipt,Result),Log);
   try {
    string resources=Path.Combine(work,"resources");var bundle=platform.Unpack(resources,Log);Result.Version=bundle.Version;Result.CoreVersion=bundle.CoreVersion;
    long expanded=0;foreach(string file in new[]{"program.zip","hw.zip","rtss.zip"})expanded=checked(expanded+ExpandedSize(Path.Combine(resources,file)));
    long needed=checked(expanded*2+512L*1024*1024);
    platform.Space(root,needed);platform.Space(work,needed);
    defender.Prepare(plan.DefenderExclusions);
    string version=bundle.Version;string program=Path.Combine(resources,"program.zip");
    string app=Path.Combine(work,"app");Log("正在展开 YMCC 程序并检查目录");
    SafeFiles.Extract(program,app,n=>n.StartsWith("YeManCC/",StringComparison.OrdinalIgnoreCase)||n.StartsWith("PowerControl/",StringComparison.OrdinalIgnoreCase),
     n=>n.Equals(MaintenanceUpdate.PackageEntry,StringComparison.OrdinalIgnoreCase));
    if(!File.Exists(Path.Combine(app,@"YeManCC\YeManCC.exe"))||!Directory.Exists(Path.Combine(app,"PowerControl")))throw new IOException("YMCC 程序包缺少核心目录");
    string incoming=platform.Version(app);if(incoming!=version)throw new IOException("包内程序与清单不一致");
    var mutableSettings=Config.MutableProgramPaths(app);
    string hwStage=Path.Combine(work,"hw"),rtStage=Path.Combine(work,"rtss");
    SafeFiles.Extract(Path.Combine(resources,"hw.zip"),hwStage,n=>true);SafeFiles.Extract(Path.Combine(resources,"rtss.zip"),rtStage,n=>true);
    ValidateTarget(root);Result.Version=version;
    platform.BackupDirectory(Result.Backup);tx=new FileTransaction(Result.Backup,work);Result.Phase="applying";platform.Save(receipt,Result);
    Result.SystemActions="RUNNING";
    var system=platform.Compatibility();
    if(system.Allowed)platform.Runtimes(resources,work,plan.DownloadWebView,Log,b=>Result.RebootRequired|=b,s=>Result.Warnings.Add(s));
    else {Result.Warnings.Add("当前系统未验证：已跳过运行库和驱动安装。");Log(system.Error);}
    ValidateTarget(root);platform.ClosePrograms(root,Log);
    Directory.CreateDirectory(root);SafeFiles.NoLinks(root);Directory.CreateDirectory(Path.Combine(root,".ymcc-setup"));
    Log((existing?"正在覆盖安装 YMCC ":"正在安装 YMCC ")+version);tx.Tree(app,root,true,p=>mutableSettings.Contains(p));
    InstallComponent(tx,"HWiNFO",hwStage,hw,null);
    InstallComponent(tx,"RTSS",rtStage,rtss,p=>p.StartsWith("Profiles\\",StringComparison.OrdinalIgnoreCase));
    tx.Put(Path.Combine(resources,"launcher.exe"),Path.Combine(root,"启动YeManCC.exe"));
    if(system.Allowed)platform.HidHide(app,work,Result.HidHide,()=>platform.Save(receipt,Result),Log,b=>Result.RebootRequired|=b);
    string hidHideWarning=YeMan.Setup.HidHide.Warning(Result.HidHide);
    if(hidHideWarning!=null)Result.Warnings.Add(hidHideWarning);
    if(Result.HidHide.InstallAttempted)platform.RemoveHidHideShortcuts(tx,Result.RemovedHidHideDesktopShortcuts,()=>platform.Save(receipt,Result),Log,s=>Result.Warnings.Add(s));
    Result.StartMenuShortcutPath=TryPublishShortcut(tx,()=>platform.StartMenuFile,()=>platform.StartMenuShortcut(tx,root,work),"公共开始菜单");
    Result.StartMenuShortcutVerified=Result.StartMenuShortcutPath!=null;
    if(plan.DesktopShortcut){
     Result.DesktopShortcutPath=TryPublishShortcut(tx,()=>platform.ShortcutFile,()=>platform.Shortcut(tx,root,work),"公共桌面");
     Result.DesktopShortcutVerified=Result.DesktopShortcutPath!=null;
    }
    if(!File.Exists(Path.Combine(root,@"YeManCC\YeManCC.exe")))throw new IOException("主程序文件未完成写入");
    defender.Finish();tx.VerifyApplied();
    // Publish the completion marker only after every installed file and environment check.
    string marker=Path.Combine(root,".ymcc-setup");Directory.CreateDirectory(marker);
    Func<object> state=()=>new {Schema=1,Version=Result.Version,CoreVersion=bundle.CoreVersion,Root=root,RebootRequired=Result.RebootRequired,Warnings=Result.Warnings,CompletedUtc=DateTime.UtcNow.ToString("o")};
    string stateTemp=Path.Combine(work,"state.json"),installedState=Path.Combine(marker,"state.json");Json.Save(stateTemp,state());tx.Put(stateTemp,installedState);
    Result.Phase=Result.HidHide.Status=="LEGACY_UNINSTALL_QUEUED"?"program-committed":(Result.RebootRequired?"reboot-required":"completed");
    Result.SystemActions="PROGRAM_INSTALLED; HARDWARE_NOT_RUN";
    platform.Save(receipt,Result);
    // Driver removal is outside the committed program transaction and cannot roll it back.
    tx=null;
    int committedWarnings=Result.Warnings.Count;
    bool legacyQueued=Result.HidHide.Status=="LEGACY_UNINSTALL_QUEUED";
     try{platform.PublishMaintenance(app,version,mutableSettings,s=>{Result.Warnings.Add(s);TryLog(s);});}
     catch(Exception e){Result.Warnings.Add("YMCC 已安装，未能登记 Windows 卸载入口。");TryLog("卸载入口："+e.Message);}
    if(legacyQueued) {
     TryLog("YeManCC 已安装，正在尝试卸载旧版 HidHide");
     try{platform.UninstallLegacyHidHide(app,work,Result.HidHide);}
     catch(Exception e){Result.HidHide.Status="LEGACY_UNINSTALL_FAILED";Result.HidHide.Error=e.Message;TryLog("HidHide 卸载未完成："+e.Message);}
     Result.RebootRequired|=Result.HidHide.RebootRequired;
     string warning=YeMan.Setup.HidHide.Warning(Result.HidHide);if(warning!=null)Result.Warnings.Add(warning);
    }
    if(legacyQueued||Result.Warnings.Count!=committedWarnings)
     try{SafeFiles.NoLinks(installedState);Json.Save(installedState,state());}catch(Exception e){Result.Warnings.Add("未能更新本地安装摘要，请以本次完成页为准。");TryLog(e.Message);}
    Result.SystemActions=Result.Warnings.Count==0?"PROGRAM_INSTALLED; NO_COMPONENT_WARNINGS; HARDWARE_NOT_RUN":"PROGRAM_INSTALLED; COMPONENTS_PENDING; HARDWARE_NOT_RUN";Result.Phase=Result.RebootRequired?"reboot-required":"completed";TryLog(Result.RebootRequired?"YMCC 已安装，请自行重启":Result.Warnings.Count==0?"YMCC 和基础环境已准备好":"YMCC 已安装，部分配套组件待处理");
   }catch(Exception e){Result.Error=e.Message;Result.ErrorType=e.GetType().FullName;Result.ErrorHResult=e.HResult;Result.ErrorDetails=e.ToString();Result.Phase="failed";TryLog("未完成："+e.Message);
    try{if(tx!=null){TryLog("正在恢复本次改动的文件");tx.Rollback();}}
    catch(Exception re){Result.Phase="repair-required";Result.Error+="\r\n"+re.Message;}
    try{defender.Rollback();}catch(Exception re){Result.Phase="repair-required";Result.Error+="\r\n"+re.Message;}
   }finally{
    Result.FinishedUtc=DateTime.UtcNow.ToString("o");
    try{platform.Save(receipt,Result);}catch(Exception error){
     if(Result.Phase=="completed"||Result.Phase=="reboot-required")Result.Warnings.Add("安装已完成，但未能保存最终记录。"+error.Message);
     else {Result.Phase="repair-required";Result.Error=(Result.Error??"")+"\r\n安装记录未能保存："+error.Message+"。请保留记录目录后重新运行安装器";}
    }
    if(Result.Phase=="completed"||Result.Phase=="reboot-required"||Result.Phase=="failed")Cleanup(work);
   }
   TryLog("记录与备份："+receipt);return Result;
  }
  void TryLog(string message){try{Log(message);}catch{}}
  static long ExpandedSize(string path){long size=0;using(var archive=ZipFile.OpenRead(path))foreach(var entry in archive.Entries)size=checked(size+entry.Length);return size;}
  void Cleanup(string work) {
   foreach(string name in new[]{"resources","app","hw","rtss","hidhide-maintenance"}) {
    if(name=="hidhide-maintenance"&&Result.HidHide.Status=="LEGACY_UNINSTALL_IN_PROGRESS")continue;
    string folder=Path.Combine(work,name);
    try {if(!SafeFiles.Under(folder,work))throw new IOException("临时目录越界");SafeFiles.NoLinks(folder);if(Directory.Exists(folder)){foreach(string p in SafeFiles.TreeFiles(folder)){}Directory.Delete(folder,true);}}
    catch(Exception e){TryLog("临时资源清理未完成："+e.Message);}
   }
   string web=Path.Combine(work,"WebView2-x64.exe");try{if(File.Exists(web)){SafeFiles.NoLinks(web);File.Delete(web);}}catch(Exception e){TryLog("临时下载清理未完成："+e.Message);}
  }
  internal int RecoverInterrupted() {
   string parent=platform.Parent;
   int recovered=0;if(!Directory.Exists(parent))return 0;platform.AssertDirectory(parent);
   foreach(string op in Directory.GetDirectories(parent,"op-*")) {
    platform.AssertDirectory(op);string path=Path.Combine(op,"result.json");
    if(!File.Exists(path)){if(Directory.GetFileSystemEntries(op).Length==0){Directory.Delete(op,false);continue;}throw new IOException("先前操作缺少状态，需检查："+op);}
    platform.AssertFile(path);var receipt=Json.Read<Receipt>(File.ReadAllText(path));
    if(receipt==null)throw new IOException("先前安装记录无效："+op);
    if(!Pending(receipt))continue;
    if(!string.Equals(receipt.Root,platform.Root,StringComparison.OrdinalIgnoreCase))throw new IOException("安装恢复目标与固定位置不一致："+op);
    try{progress("发现上次中断的安装，先恢复本次改动："+op);}catch{}
    var errors=new List<string>();string backup=Path.Combine(op,"backup");
    try{string journal=Path.Combine(backup,"file-journal.json");if(Directory.Exists(backup)){platform.AssertDirectory(backup);if(File.Exists(journal))platform.AssertFile(journal);}if(!File.Exists(journal)&&(receipt.Phase=="applying"||(Directory.Exists(backup)&&Directory.GetFileSystemEntries(backup).Length>0)))throw new IOException("本次恢复缺少文件事务日志，已保留原文件和备份："+op);FileTransaction.Recover(backup,target=>SafeFiles.Under(target,platform.Root)||SafeFiles.Under(target,platform.Hw)||SafeFiles.Under(target,platform.Rtss)||IsShortcutTarget(target,()=>platform.ShortcutFile)||IsShortcutTarget(target,()=>platform.StartMenuFile)||(receipt.RemovedHidHideDesktopShortcuts!=null&&receipt.RemovedHidHideDesktopShortcuts.Contains(target)&&DesktopShortcuts.IsHidHideDesktopPath(target,platform.Desktops)),targets=>{
     foreach(string root in new[]{platform.Root,platform.Hw,platform.Rtss})if(Array.Exists(targets,t=>SafeFiles.Under(t,root)))platform.ClosePrograms(root,s=>{try{progress(s);}catch{}});
    });}catch(Exception error){errors.Add(error.Message);}
    try{if(receipt.Defender!=null)new DefenderExclusions(platform.Defender,receipt.Defender,op,()=>platform.Save(path,receipt),s=>{try{progress(s);}catch{}}).Rollback();}catch(Exception error){errors.Add(error.Message);}
    receipt.Phase=errors.Count==0?"rolled-back":"repair-required";receipt.FinishedUtc=DateTime.UtcNow.ToString("o");if(errors.Count>0)receipt.Error=string.Join("；",errors);
    platform.Save(path,receipt);if(errors.Count>0)throw new IOException("先前安装恢复未完成，已保留现场："+receipt.Error);recovered++;
   }
   return recovered;
  }
  string TryPublishShortcut(FileTransaction tx,Func<string> location,Action publish,string label) {
   string path=null;
   try{path=location();publish();return path;}
   catch(Exception e){
    if(!DesktopShortcuts.IsShortcutFailure(e))throw;
    if(path!=null)tx.RestorePaths(p=>string.Equals(p,path,StringComparison.OrdinalIgnoreCase));
    Result.Warnings.Add("未能创建"+label+"快捷方式，可从安装目录启动。");Log(label+"快捷方式未完成："+e.Message);
    return null;
   }
  }
  static bool IsShortcutTarget(string path,Func<string> location) {
   try{return string.Equals(Path.GetFullPath(path),location(),StringComparison.OrdinalIgnoreCase);}
   catch(Exception e){if(!DesktopShortcuts.IsShortcutFailure(e))throw;return false;}
  }
 }
}
