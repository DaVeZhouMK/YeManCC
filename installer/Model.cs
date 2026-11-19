using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;

namespace YeMan.Setup {
 public sealed class Blob { public string Path; public string Sha256; public long Length; }
 public sealed class Bundle { public int Schema; public string Version; public string CoreVersion; public Blob[] Files; }
 public sealed class Plan {
  public const string InstallRoot = @"C:\SOFT\YeMan";
  public string Root = InstallRoot;
  public string Mode = "install";
  public string ExpectedInstalledVersion;
  // Legacy program-download and reset requests are explicitly rejected.
  public bool Online;
  public bool ResetSettings;
  public bool DesktopShortcut = true;
  public bool DefenderExclusions = true;
  public static void Validate(Plan plan) {
   if(plan==null)throw new IOException("缺少安装计划");
   if(plan.Online)throw new IOException("YMCC 仅使用包内版本，不支持在线更新");
   if((plan.Mode!="install"&&plan.Mode!="first")||plan.ResetSettings)throw new IOException("此安装器支持本地安装或更新，不支持重新设置");
   if(!string.IsNullOrEmpty(plan.ExpectedInstalledVersion)&&plan.ExpectedInstalledVersion!="unknown")Config.StrictVersion(plan.ExpectedInstalledVersion);
   if(!string.Equals(SafeFiles.Full(plan.Root),InstallRoot,StringComparison.OrdinalIgnoreCase))throw new IOException("此版本固定安装到 C:\\SOFT\\YeMan，暂不支持更换位置");
   plan.Root=InstallRoot;
  }
 }
 public sealed class Receipt {
  public string OperationId; public string Phase; public string Root; public string Version;
  public string PreviousVersion;
  public string CoreVersion; public string StartedUtc; public string FinishedUtc; public string Error;
  public string ErrorType,ErrorDetails;public int? ErrorHResult;
  public bool RebootRequired; public string Backup; public string SystemActions = "NOT_RUN";
  public List<string> Events = new List<string>();
  public DefenderRecord Defender = new DefenderRecord();
  public HidHideRecord HidHide = new HidHideRecord();
  public bool DesktopShortcutRequested,DesktopShortcutVerified;
  public string DesktopShortcutPath;
  public List<string> RemovedHidHideDesktopShortcuts = new List<string>();
 }
 public sealed class InstallAssessment {
  public string InstalledVersion; public string PackageVersion; public string Action; public string Label; public string Error; public bool Allowed;
 }
 public static class Json {
  public static string Write(object o) { return new JavaScriptSerializer { MaxJsonLength = 16000000 }.Serialize(o); }
  public static T Read<T>(string s) { return new JavaScriptSerializer { MaxJsonLength = 16000000 }.Deserialize<T>(s); }
  public static void Save(string p, object o) {
   string temp=p+".tmp-"+Guid.NewGuid().ToString("N");
   try{byte[] bytes=new UTF8Encoding(false).GetBytes(Write(o));using(var stream=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)){stream.Write(bytes,0,bytes.Length);stream.Flush(true);}SafeFiles.SharingIo(()=>{if(File.Exists(p))File.Replace(temp,p,null);else File.Move(temp,p);});WorkSecurity.Seal(p);}
   finally{if(File.Exists(temp))File.Delete(temp);}
  }
 }
 public static class Config {
  public sealed class SettingsPath {public string path;public string backup;}
  public sealed class SettingsBaseline {public int formatVersion;public SettingsPath document;public SettingsPath[] legacyImports;public SettingsPath[] independentUserFiles;}
  public static HashSet<string> MutableProgramPaths(string app) {
   var paths=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   string file=Path.Combine(app,@"PowerControl\settings-baseline.json");
   if(!File.Exists(file))return paths;
   var baseline=Json.Read<SettingsBaseline>(File.ReadAllText(file));
   if(baseline==null||baseline.formatVersion!=1||baseline.document==null||baseline.document.path!="yeman-settings.json")throw new IOException("包内设置保留清单无效");
   var entries=new List<SettingsPath>{baseline.document};
   if(baseline.legacyImports!=null)entries.AddRange(baseline.legacyImports);
   if(baseline.independentUserFiles!=null)entries.AddRange(baseline.independentUserFiles);
   foreach(var entry in entries) {
    if(entry==null||string.IsNullOrEmpty(entry.path))throw new IOException("包内设置保留路径无效");
    foreach(string relative in new[]{entry.path,entry.backup})if(!string.IsNullOrEmpty(relative))paths.Add("PowerControl\\"+SafeFiles.EntryName(relative).Replace('/','\\'));
   }
   return paths;
  }
  public static Version StrictVersion(string s) {
   if (s == null || !System.Text.RegularExpressions.Regex.IsMatch(s, @"^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$")) throw new InvalidDataException("版本号格式无效");
   return new Version(s);
  }
 }
}
