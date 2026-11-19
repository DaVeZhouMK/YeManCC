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
  public static bool KeepNewer(string installed,string selected) {return installed.Length>0&&Config.StrictVersion(installed)>Config.StrictVersion(selected);}
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
  public static void ValidateFirstInstallRoot(string root) {Plan.Validate(new Plan{Root=root});ValidateEmptyRoot(root);}
  public static string ValidateInstallRoot(string root,string selected) {return CheckInstallRoot(root,selected,InstalledVersion);}
  internal static string CheckInstallRoot(string root,string selected,Func<string,string> version) {
   if(File.Exists(root))throw new IOException("安装路径被同名文件占用，已保护原内容");
   ValidateRoot(root);Config.StrictVersion(selected);
   string main=Path.Combine(root,@"YeManCC\YeManCC.exe");SafeFiles.NoLinks(main);
   string current=File.Exists(main)?version(root):"";
   if(current.Length==0){ValidateEmptyRoot(root);return "";}
   if(current=="unknown")throw new IOException("无法识别现有 YMCC 版本，已保留原文件，请先检查程序");
   if(KeepNewer(current,selected))throw new IOException("当前 YMCC "+current+" 高于包内 "+selected+"，已停止以避免降级");
   return current;
  }
  public static InstallAssessment DetectInstall(string root,string selected) {
   var status=new InstallAssessment{InstalledVersion="",PackageVersion=selected,Action="blocked"};
   try {
    SafeFiles.NoLinks(Path.Combine(root,@"YeManCC\YeManCC.exe"));status.InstalledVersion=InstalledVersion(root);
    string current=ValidateInstallRoot(root,selected);status.InstalledVersion=current;status.Allowed=true;
    status.Action=current.Length==0?"install":current==selected?"reinstall":"update";
    status.Label=current.Length==0?"安装 YMCC "+selected:current==selected?"YMCC "+current+" · 同版重新安装，保留设置":"YMCC "+current+" → "+selected+" · 保留设置";
   }catch(Exception error){status.Error=error.Message;status.Label="当前 "+(status.InstalledVersion=="unknown"?"版本未知":status.InstalledVersion.Length==0?"未安装":status.InstalledVersion)+" · 包内 "+selected;}
   return status;
  }
  static bool Pending(Receipt receipt){return receipt.Phase=="preparing"||receipt.Phase=="applying"||receipt.Phase=="repair-required";}
  public static InstallAssessment AssessInstall(){return AssessInstall(new InstallPlatform());}
  internal static InstallAssessment AssessInstall(InstallPlatform boundary) {
   var status=DetectInstall(boundary.Root,BuildInfo.ProgramVersion);
   if(!Directory.Exists(boundary.Parent))return status;
   try {
    boundary.AssertDirectory(boundary.Parent);
    foreach(string op in Directory.GetDirectories(boundary.Parent,"op-*")) {
     boundary.AssertDirectory(op);string path=Path.Combine(op,"result.json");if(!File.Exists(path))continue;
     boundary.AssertFile(path);var receipt=Json.Read<Receipt>(File.ReadAllText(path));
     if(receipt!=null&&Pending(receipt)&&string.Equals(receipt.Root,boundary.Root,StringComparison.OrdinalIgnoreCase)) {
      status.Action="recover";status.Allowed=true;status.Error=null;status.Label="上次安装未完成 · 恢复后确认版本";return status;
     }
    }
   }catch(UnauthorizedAccessException) {
    if(!boundary.Admin&&!status.Allowed&&!(status.InstalledVersion!="unknown"&&KeepNewer(status.InstalledVersion,BuildInfo.ProgramVersion))) {
     status.Action="recover";status.Allowed=true;status.Error=null;status.Label="已有安装记录 · 由管理员检查后继续";
    }
   }catch(Exception error){status.Allowed=false;status.Action="blocked";status.Error="安装记录需检查："+error.Message;}
   return status;
  }
  string ValidateTarget(Plan plan,string selected,string expectedVersion,string expectedHash) {
   string current=CheckInstallRoot(platform.Root,selected,platform.Version);
   if(plan.Mode=="first"&&current.Length>0)throw new IOException("首次安装请求不能覆盖现有 YMCC，请重新打开安装器检测版本");
   if(expectedVersion!=null&&current!=expectedVersion)throw new IOException("检测后 YMCC 版本已变化，请重新打开安装器确认版本");
   if(expectedHash!=null&&SafeFiles.Hash(Path.Combine(platform.Root,@"YeManCC\YeManCC.exe"))!=expectedHash)throw new IOException("检测后 YMCC 程序已变化，请重新打开安装器确认");
   return current;
  }
  internal static void ValidateEmptyRoot(string root) {
   if(File.Exists(root))throw new IOException("安装路径被同名文件占用，已保护原内容");
   ValidateRoot(root);
   if(InstalledVersion(root).Length>0||File.Exists(Path.Combine(root,@"PowerControl\yeman-settings.json"))||File.Exists(Path.Combine(root,@".ymcc-setup\state.json")))throw new IOException("固定位置已有 YMCC 安装或设置，此安装器仅用于首次安装，已保护原文件。");
   if(Directory.Exists(root)) {
    // Existing system assets can share the portable root. Tree() only writes package members.
    // Still validate every path before admission; a stale marker grants no ownership.
    foreach(string item in SafeFiles.TreeFiles(root))if(SafeFiles.Under(item,Path.Combine(root,".ymcc-setup")))throw new IOException("安装记录目录含未识别文件，请保留并检查："+item);
   }
  }
  internal static void Busy(string[] roots) {
   // Check named applications and all executables under the affected trees.
   foreach(var p in Process.GetProcesses())using(p) {
    if(p.Id==Process.GetCurrentProcess().Id)continue;
    bool known=p.ProcessName.Equals("YeManCC",StringComparison.OrdinalIgnoreCase)||p.ProcessName.Equals("RTSS",StringComparison.OrdinalIgnoreCase)||p.ProcessName.Equals("HWiNFO64",StringComparison.OrdinalIgnoreCase)||p.ProcessName.StartsWith("YeManInputHost",StringComparison.OrdinalIgnoreCase);
    string image=null;try{image=p.MainModule.FileName;}catch{if(known)throw new IOException("请先关闭 "+p.ProcessName+"，然后重新开始");}
    if(known||(image!=null&&Array.Exists(roots,r=>SafeFiles.Under(image,r))))throw new IOException("请先关闭 "+p.ProcessName+"，再安装；设置中心不会结束现有进程");
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
     Version v;if(!Version.TryParse(m.Groups[1].Value,out v)||v.Major!=10||v>new Version(10,0,12))throw new IOException("所选 YMCC 需要另一等级的运行环境，请换用配套核心安装器："+m.Groups[1].Value);
    }
   }
  }
  static bool Newer(string file,Version minimum) {if(!File.Exists(file))return false;var f=FileVersionInfo.GetVersionInfo(file);var v=new Version(f.FileMajorPart,f.FileMinorPart,f.FileBuildPart,f.FilePrivatePart);return v>minimum;}
  public Receipt Execute(Plan plan) {
   Plan.Validate(plan);
   platform.Compatible();
   if(!platform.Admin)throw new IOException("安装需要 Windows 管理员权限");
   using(var mutex=new Mutex(false,platform.MutexName)) {
    bool entered=false;try{entered=mutex.WaitOne(0);}catch(AbandonedMutexException){entered=true;}if(!entered)throw new IOException("另一个设置中心正在处理，请稍后再试");
    try{return ExecuteLocked(plan);}finally{mutex.ReleaseMutex();}
   }
  }
  Receipt ExecuteLocked(Plan plan) {
   Plan.Validate(plan);int recovered=RecoverInterrupted();string root=platform.Root;
   if(recovered>0) {
    string restored=CheckInstallRoot(root,BuildInfo.ProgramVersion,platform.Version);
    if(plan.ExpectedInstalledVersion!=null&&restored!=plan.ExpectedInstalledVersion)return new Receipt{Root=root,Phase="confirmation-required",PreviousVersion=restored,Version=BuildInfo.ProgramVersion,SystemActions="PREVIOUS_TRANSACTION_RECOVERED; NEW_INSTALL_NOT_STARTED"};
   }
   string previousVersion=ValidateTarget(plan,BuildInfo.ProgramVersion,plan.ExpectedInstalledVersion,null);
   string previousHash=previousVersion.Length==0?null:SafeFiles.Hash(Path.Combine(root,@"YeManCC\YeManCC.exe"));
   SafeFiles.NoLinks(root);
   string hw=platform.Hw;
   string rtss=platform.Rtss;
   SafeFiles.NoLinks(hw);SafeFiles.NoLinks(rtss);platform.Idle(new[]{root,hw,rtss});
   platform.Space(platform.Parent,Resources.PackedLength+64L*1024*1024);
   string work=platform.Work();
   Result=new Receipt {OperationId=Path.GetFileName(work),Phase="preparing",Root=root,PreviousVersion=previousVersion,StartedUtc=DateTime.UtcNow.ToString("o"),Backup=Path.Combine(work,"backup"),DesktopShortcutRequested=plan.DesktopShortcut};
   string receipt=Path.Combine(work,"result.json");receiptPath=receipt;platform.Save(receipt,Result);FileTransaction tx=null;
   var defender=new DefenderExclusions(platform.Defender,Result.Defender,work,()=>platform.Save(receipt,Result),Log);
   try {
    string resources=Path.Combine(work,"resources");var bundle=platform.Unpack(resources,Log);Result.Version=bundle.Version;Result.CoreVersion=bundle.CoreVersion;
    // Check all Microsoft payload signatures before performing any system action.
    foreach(string f in new[]{"desktop.exe","vc-x64.exe","vc-x86.exe",Runtimes.WebViewBootstrapper})platform.Signature(Path.Combine(resources,f));
    long expanded=0;foreach(string file in new[]{"program.zip","hw.zip","rtss.zip"})expanded=checked(expanded+ExpandedSize(Path.Combine(resources,file)));
    long needed=checked(expanded*2+TreeSize(root)+TreeSize(hw)+TreeSize(rtss)+512L*1024*1024);
    platform.Space(root,needed);platform.Space(hw,needed);platform.Space(rtss,needed);platform.Space(work,needed);
    defender.Prepare(plan.DefenderExclusions);
    string version=bundle.Version;string program=Path.Combine(resources,"program.zip");
    string app=Path.Combine(work,"app");Log("正在展开 YMCC 程序并检查目录");
    SafeFiles.Extract(program,app,n=>n.StartsWith("YeManCC/",StringComparison.OrdinalIgnoreCase)||n.StartsWith("PowerControl/",StringComparison.OrdinalIgnoreCase));
    if(!File.Exists(Path.Combine(app,@"YeManCC\YeManCC.exe"))||!Directory.Exists(Path.Combine(app,"PowerControl")))throw new IOException("YMCC 程序包缺少核心目录");
    string incoming=platform.Version(app);if(incoming!=version)throw new IOException("程序版本与清单不一致");CheckFrameworks(app);
    var mutableSettings=Config.MutableProgramPaths(app);
    string hwStage=Path.Combine(work,"hw"),rtStage=Path.Combine(work,"rtss");
    SafeFiles.Extract(Path.Combine(resources,"hw.zip"),hwStage,n=>true);SafeFiles.Extract(Path.Combine(resources,"rtss.zip"),rtStage,n=>true);
    ValidateTarget(plan,version,previousVersion,previousHash);Result.Version=version;
    platform.Idle(new[]{root,hw,rtss});
    platform.BackupDirectory(Result.Backup);tx=new FileTransaction(Result.Backup,work);Result.Phase="applying";platform.Save(receipt,Result);
    Result.SystemActions="RUNNING";platform.Runtimes(resources,work,Log,b=>Result.RebootRequired|=b);
    ValidateTarget(plan,version,previousVersion,previousHash);platform.Idle(new[]{root,hw,rtss});
    Directory.CreateDirectory(root);SafeFiles.NoLinks(root);Directory.CreateDirectory(Path.Combine(root,".ymcc-setup"));
    Log((previousVersion.Length==0?"正在安装 YMCC ":previousVersion==version?"正在重新安装 YMCC ":"正在更新 YMCC "+previousVersion+" → ")+version);tx.Tree(app,root,true,p=>mutableSettings.Contains(p));
    if(!Newer(Path.Combine(hw,"HWiNFO64.EXE"),new Version(BuildInfo.HwFileVersion))){Log("正在准备 HWiNFO "+BuildInfo.HwVersion+" 调度依赖");tx.Tree(hwStage,hw,true);}else Log("保留较新的 HWiNFO");
    if(!Newer(Path.Combine(rtss,"RTSS.exe"),new Version(7,3,8,28315))){Log("正在准备 RTSS 调度依赖");tx.Tree(rtStage,rtss,true,p=>p.StartsWith("Profiles\\",StringComparison.OrdinalIgnoreCase));}else Log("保留较新的 RTSS");
    tx.Put(Path.Combine(resources,"launcher.exe"),Path.Combine(root,"启动YeManCC.exe"));
    platform.HidHide(root,work,Result.HidHide,()=>platform.Save(receipt,Result),Log,b=>Result.RebootRequired|=b);
    platform.RemoveHidHideShortcuts(tx,Result.RemovedHidHideDesktopShortcuts,()=>platform.Save(receipt,Result),Log);
    if(plan.DesktopShortcut){Log("正在创建并验证 YMCC 桌面快捷方式");platform.Shortcut(tx,root,work);Result.DesktopShortcutPath=platform.ShortcutFile;Result.DesktopShortcutVerified=true;platform.Save(receipt,Result);}
    if(!File.Exists(Path.Combine(root,@"YeManCC\YeManCC.exe"))||!File.Exists(Path.Combine(hw,"HWiNFO64.EXE"))||!File.Exists(Path.Combine(rtss,"RTSS.exe"))||!platform.Ready())throw new IOException("最终核心文件或环境复核未完成");
    defender.Finish();tx.VerifyApplied();
    // Publish the completion marker only after every installed file and environment check.
    string marker=Path.Combine(root,".ymcc-setup");Directory.CreateDirectory(marker);
    var state=new {Schema=1,Version=Result.Version,CoreVersion=bundle.CoreVersion,Root=root,RebootRequired=Result.RebootRequired,CompletedUtc=DateTime.UtcNow.ToString("o")};
    string stateTemp=Path.Combine(work,"state.json");Json.Save(stateTemp,state);tx.Put(stateTemp,Path.Combine(marker,"state.json"));
    Result.SystemActions="RUNTIMES_AND_HIDHIDE_VERIFIED; HARDWARE_NOT_RUN";Result.Phase=Result.RebootRequired?"reboot-required":"completed";Log(Result.RebootRequired?"核心文件已准备好，请自行重启后再使用 YMCC":"核心文件和环境已准备好；硬件功能请在 YMCC 中确认");
   }catch(Exception e){Result.Error=e.Message;Result.ErrorType=e.GetType().FullName;Result.ErrorHResult=e.HResult;Result.ErrorDetails=e.ToString();Result.Phase="failed";TryLog("未完成："+e.Message);
    try{if(tx!=null){TryLog("正在恢复本次改动的文件");tx.Rollback();}}
    catch(Exception re){Result.Phase="repair-required";Result.Error+="\r\n"+re.Message;}
    try{defender.Rollback();}catch(Exception re){Result.Phase="repair-required";Result.Error+="\r\n"+re.Message;}
    if(e is RuntimeRestartRequiredException&&Result.Phase=="failed"){Result.RebootRequired=true;Result.Phase="blocked-reboot";Result.SystemActions="ENVIRONMENT_RESTART_PENDING; PROGRAM_NOT_COMPLETED";}
   }finally{
    Result.FinishedUtc=DateTime.UtcNow.ToString("o");
    try{platform.Save(receipt,Result);}catch(Exception error){Result.Phase="repair-required";Result.Error=(Result.Error??"")+"\r\n安装记录未能保存："+error.Message+"。请保留记录目录后重新运行安装器";}
    if(Result.Phase=="completed"||Result.Phase=="reboot-required"||Result.Phase=="failed"||Result.Phase=="blocked-reboot")Cleanup(work);
   }
   TryLog("记录与备份："+receipt);return Result;
  }
  void TryLog(string message){try{Log(message);}catch{}}
  static long TreeSize(string root){long size=0;if(Directory.Exists(root))foreach(string file in SafeFiles.TreeFiles(root))size=checked(size+new FileInfo(file).Length);return size;}
  static long ExpandedSize(string path){long size=0;using(var archive=ZipFile.OpenRead(path))foreach(var entry in archive.Entries)size=checked(size+entry.Length);return size;}
  void Cleanup(string work) {
   foreach(string name in new[]{"resources","app","hw","rtss"}) {
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
    platform.Idle(new[]{receipt.Root,platform.Hw,platform.Rtss});try{progress("发现上次中断的安装，先恢复本次改动："+op);}catch{}
    var errors=new List<string>();string backup=Path.Combine(op,"backup");
    try{string journal=Path.Combine(backup,"file-journal.json");if(Directory.Exists(backup)){platform.AssertDirectory(backup);if(File.Exists(journal))platform.AssertFile(journal);}if(!File.Exists(journal)&&(receipt.Phase=="applying"||(Directory.Exists(backup)&&Directory.GetFileSystemEntries(backup).Length>0)))throw new IOException("本次恢复缺少文件事务日志，已保留原文件和备份："+op);FileTransaction.Recover(backup,target=>SafeFiles.Under(target,platform.Root)||SafeFiles.Under(target,platform.Hw)||SafeFiles.Under(target,platform.Rtss)||string.Equals(Path.GetFullPath(target),platform.ShortcutFile,StringComparison.OrdinalIgnoreCase)||(receipt.RemovedHidHideDesktopShortcuts!=null&&receipt.RemovedHidHideDesktopShortcuts.Contains(target)&&DesktopShortcuts.IsHidHideDesktopPath(target,platform.Desktops)));}catch(Exception error){errors.Add(error.Message);}
    try{if(receipt.Defender!=null)new DefenderExclusions(platform.Defender,receipt.Defender,op,()=>platform.Save(path,receipt),s=>{try{progress(s);}catch{}}).Rollback();}catch(Exception error){errors.Add(error.Message);}
    receipt.Phase=errors.Count==0?"rolled-back":"repair-required";receipt.FinishedUtc=DateTime.UtcNow.ToString("o");if(errors.Count>0)receipt.Error=string.Join("；",errors);
    platform.Save(path,receipt);if(errors.Count>0)throw new IOException("先前安装恢复未完成，已保留现场："+receipt.Error);recovered++;
   }
   return recovered;
  }
  internal static void Shortcut(FileTransaction tx,string root,string work) {
   // Common desktop avoids writing into the account used for alternate UAC credentials.
   DesktopShortcuts.Create(tx,root,work,Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonDesktopDirectory),"YeManCC.lnk"));
  }
 }
}
