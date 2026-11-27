using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace YeMan.Setup {
 internal static class MaintenanceProgram {
  [STAThread] static int Main(string[] args) {
   if (args.Length==2 && args[0]=="--refresh") return MaintenanceUpdate.Refresh(args[1]);
   bool quiet = args.Length == 1 && args[0] == "--quiet";
   if (args.Length != 0 && !quiet) return 2;
   try {
    if (!Native.IsAdmin) throw new UnauthorizedAccessException("需要管理员权限来卸载 YeManCC。");
    if (!quiet && MessageBox.Show("卸载 YeManCC？将保留个人配置和共享组件。", "YeManCC 野蛮系统控制中心", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return 1602;
    using (var mutex = new Mutex(false, MaintenanceRegistration.MutexName)) {
     bool acquired;
     try { acquired = mutex.WaitOne(0); } catch (AbandonedMutexException) { acquired = true; }
     if (!acquired) throw new IOException("正在安装或卸载 YeManCC，请稍后重试。");
     try {
      var operation = new CoreRemoval();
      var result = operation.Run();
      if (!quiet) MessageBox.Show(result.Summary(), "YeManCC 卸载", MessageBoxButtons.OK, result.Errors.Count > 0 ? MessageBoxIcon.Warning : MessageBoxIcon.Information);
      return result.Errors.Count == 0 ? 0 : 1;
     } finally { mutex.ReleaseMutex(); }
    }
   } catch (Exception e) {
    if (!quiet) MessageBox.Show(e.Message, "YeManCC 卸载", MessageBoxButtons.OK, MessageBoxIcon.Error);
    return 1;
   }
  }
 }

 public sealed class RemovalResult {
  public readonly List<string> Errors = new List<string>();
  public readonly List<string> Warnings = new List<string>();
  public readonly List<string> Removed = new List<string>();
  public readonly List<string> Restored = new List<string>();
  public int Schema = 1;
  public string Status;
  public string Utc;
  internal string Summary() {
   var lines = new List<string>(Errors.Count > 0 ? Errors : Warnings);
   if (lines.Count > 6) { lines.RemoveRange(6, lines.Count - 6); lines.Add("更多信息见卸载报告。"); }
   return (Errors.Count > 0 ? "YeManCC 卸载未完成。可重试。" : "YeManCC 已卸载。") +
    (lines.Count == 0 ? "" : "\r\n\r\n" + string.Join("\r\n", lines));
  }
 }

 internal sealed class CoreRemoval {
  const string Root = Plan.InstallRoot;
  static readonly Guid Balanced = new Guid("381b4222-f694-41f0-9685-ff5bb260df2e");
  static readonly Guid YeMan = new Guid("1cb8b882-a900-4b9f-9bac-99d151e64441");
  static readonly string[] Core = { MaintenanceRegistration.MainImage, MaintenanceRegistration.Launcher, @".ymcc-setup\state.json" };
  static readonly string[] TaskFolders = { @"\", @"\野蛮优化整合系统" };
  const string RunPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
  const string RunOncePath = @"Software\Microsoft\Windows\CurrentVersion\RunOnce";
  readonly RemovalResult result = new RemovalResult();

  void Step(string name, bool required, Action action) {
   try { action(); }
   catch (Exception e) { (required ? result.Errors : result.Warnings).Add(name + "：" + e.Message); }
  }

  internal RemovalResult Run() {
   var files = new HashSet<string>(Core, StringComparer.OrdinalIgnoreCase);
   Step("随包程序清单不可用", false, () => { foreach (var name in MaintenanceRegistration.BundledPaths()) files.Add(name); });
   Step("程序清单不可用，清理可识别的核心文件", false, () => {
    foreach (var name in MaintenanceRegistration.ReadInventory().Files)
     Step("一项程序清单无效", false, () => files.Add(MaintenanceRegistration.ProgramPath(name)));
   });
   Step("计划任务", true, RemoveTasks);
   Step("登录启动项", true, RemoveRegistryStartup);
   Step("关闭 YeManCC", true, () => InstalledProcesses.Close(Root, true, s => {
    if (s.Contains("未")) result.Warnings.Add(s);
   }, true, files));
   Step("系统设置还原", false, () => MaintenanceRollback.Restore(result));
   Step("系统电源方案", false, RestorePower);
   Step("桌面快捷方式", true, () => RemoveLinks(DesktopShortcuts.DesktopDirectories(), false));
   Step("公共开始菜单", true, () => RemoveLinks(new[] { Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms) }, false));
   Step("启动文件夹", true, () => RemoveLinks(new[] {
    Environment.GetFolderPath(Environment.SpecialFolder.Startup), Environment.GetFolderPath(Environment.SpecialFolder.CommonStartup)
   }, true));
   var folders = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach (var name in files) Step("程序文件 " + name, true, () => {
    string path = Path.Combine(Root, name);
    SafeFiles.NoLinks(path);
    if (!File.Exists(path)) return;
    File.Delete(SafeFiles.IoPath(path));
    result.Removed.Add(name);
    for (var dir = Path.GetDirectoryName(path); SafeFiles.Under(dir, Root); dir = Path.GetDirectoryName(dir)) folders.Add(dir);
   });
   var byDepth = new List<string>(folders);
   byDepth.Sort((a, b) => b.Length.CompareTo(a.Length));
   foreach (var folder in byDepth) Step("清理空目录 " + Path.GetFileName(folder), false, () => {
    SafeFiles.NoLinks(folder);
    if (Directory.Exists(folder) && Directory.GetFileSystemEntries(folder).Length == 0) Directory.Delete(folder, false);
   });
   if (result.Errors.Count == 0) Step("Windows 卸载入口", true, () => {
    using (var hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)) hklm.DeleteSubKey(MaintenanceRegistration.Entry, false);
   });
   result.Status = result.Errors.Count == 0 ? "PROGRAM_REMOVED" : "PROGRAM_REMOVAL_INCOMPLETE";
   result.Utc = DateTime.UtcNow.ToString("o");
   Step("卸载报告", false, () => {
    WorkSecurity.Create(MaintenanceRegistration.State);
    string path = Path.Combine(MaintenanceRegistration.State, "last-uninstall.json");
    SafeFiles.NoLinks(path);
    Json.Save(path, result);
   });
   return result;
  }

  internal static bool OwnsStartupImage(string image) {
   if (String.IsNullOrWhiteSpace(image)) return false;
   try {
    var path = SafeFiles.Full(image.Trim().Trim('"'));
    return String.Equals(path, Path.Combine(Root, MaintenanceRegistration.MainImage), StringComparison.OrdinalIgnoreCase) ||
     String.Equals(path, Path.Combine(Root, MaintenanceRegistration.Launcher), StringComparison.OrdinalIgnoreCase);
   } catch (ArgumentException) { return false; }
   catch (IOException) { return false; }
   catch (NotSupportedException) { return false; }
  }

  internal static bool OwnsTaskAction(string name,string image,string arguments) {
   if (OwnsStartupImage(image)) return true;
   if (String.IsNullOrWhiteSpace(image)) return false;
   string path;
   try { path=SafeFiles.Full(image.Trim().Trim('"')); }
   catch (ArgumentException) { return false; }
   catch (IOException) { return false; }
   catch (NotSupportedException) { return false; }
   if (name=="桌面模式-开机设置为桌面模式") return String.IsNullOrWhiteSpace(arguments) &&
    String.Equals(path,Path.Combine(Root,@"PowerControl\还原成非平板模式.vbs"),StringComparison.OrdinalIgnoreCase);
   if (name=="内存-开机自动内存清理并关闭") return String.IsNullOrWhiteSpace(arguments) &&
    String.Equals(path,Path.Combine(Root,@"PowerControl\MG-AUTO\清理内存.bat"),StringComparison.OrdinalIgnoreCase);
   if (name!="Xbox大屏游戏模式") return false;
   var windowsPowershell=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),@"WindowsPowerShell\v1.0\powershell.exe");
   var xboxArgs="-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+Path.Combine(Root,@"PowerControl\Repair-GamingHandheld.ps1")+"\"";
   return String.Equals(path,windowsPowershell,StringComparison.OrdinalIgnoreCase) &&
    String.Equals((arguments??"").Trim(),xboxArgs,StringComparison.OrdinalIgnoreCase);
  }

  void RemoveLinks(string[] directories, bool allLinks) {
   foreach (var dir in directories) Step("快捷方式目录 " + dir, true, () => {
    if (String.IsNullOrEmpty(dir) || !Directory.Exists(dir)) return;
    SafeFiles.NoLinks(dir);
    foreach (var link in Directory.EnumerateFiles(dir, allLinks ? "*.lnk" : DesktopShortcuts.YeManLinkName)) {
     string target = null;
     Step("读取快捷方式 " + Path.GetFileName(link), false, () => target = DesktopShortcuts.Read(link).Target);
     if (OwnsStartupImage(target)) Step("删除快捷方式 " + Path.GetFileName(link), true, () => {
      File.Delete(link); result.Removed.Add(link);
     });
    }
   });
  }

  void RemoveTasks() {
   dynamic service = Activator.CreateInstance(Type.GetTypeFromProgID("Schedule.Service", true));
   try {
    service.Connect(null, null, null, null);
    foreach (var dir in TaskFolders) Step("任务目录 " + dir, true, () => {
     dynamic folder;
     try { folder = service.GetFolder(dir); }
     catch (COMException e) {
      if (e.ErrorCode == unchecked((int)0x80070002) || e.ErrorCode == unchecked((int)0x80070003)) return;
      throw;
     }
     try {
      dynamic tasks = folder.GetTasks(1);
      try {
       int count = tasks.Count;
       if (count > 2048) throw new IOException("任务数量超过可检查范围");
       for (int i = count; i >= 1; i--) Step("检查计划任务", false, () => {
        dynamic task = tasks[i];
        dynamic def = null, actions = null, action = null;
        try {
         def = task.Definition; actions = def.Actions;
         if ((int)actions.Count != 1) return;
         action = actions[1];
         string name = task.Name;
         if ((int)action.Type != 0 || !OwnsTaskAction(name,(string)action.Path,(string)action.Arguments)) return;
         Step("删除计划任务 " + name, true, () => {
          folder.DeleteTask(name, 0);
          result.Removed.Add("Task: " + dir + " " + name);
         });
        } finally { Release(action); Release(actions); Release(def); Release(task); }
       });
      } finally { Release(tasks); }
     } finally { Release(folder); }
    });
   } finally { Release(service); }
  }

  static void Release(object value) { if (value != null && Marshal.IsComObject(value)) Marshal.ReleaseComObject(value); }

  void RemoveRegistryStartup() {
   foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 }) {
    using (var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view)) RemoveRun(machine, "");
    using (var users = RegistryKey.OpenBaseKey(RegistryHive.Users, view))
     foreach (var sid in users.GetSubKeyNames()) if (sid.StartsWith("S-1-5-21-") && !sid.EndsWith("_Classes")) RemoveRun(users, sid + "\\");
   }
  }

  void RemoveRun(RegistryKey hive, string prefix) {
   foreach (var path in new[] { RunPath, RunOncePath }) Step("检查自启 " + prefix + path, true, () => {
    using (var key = hive.OpenSubKey(prefix + path, true)) {
     if (key == null) return;
     foreach (var name in key.GetValueNames()) Step("检查自启 " + name, true, () => {
      var raw = key.GetValue(name, null, RegistryValueOptions.DoNotExpandEnvironmentNames) as string;
      if (raw == null) return;
      var command = raw.Trim();
      // Unquoted paths containing spaces are ambiguous; never delete those entries by guess.
      var end = command.StartsWith("\"") ? command.IndexOf('"', 1) : command.IndexOfAny(new[] { ' ', '\t' });
      var image = end < 0 ? command : command.Substring(0, end);
      if (OwnsStartupImage(image) && String.Equals(raw, key.GetValue(name, null, RegistryValueOptions.DoNotExpandEnvironmentNames) as string, StringComparison.Ordinal)) {
       key.DeleteValue(name, false); result.Removed.Add("Startup: " + prefix + name);
      }
     });
    }
   });
  }

  [DllImport("powrprof.dll")] static extern uint PowerSetActiveScheme(IntPtr user, ref Guid scheme);
  [DllImport("powrprof.dll")] static extern uint PowerGetActiveScheme(IntPtr user, out IntPtr scheme);
  [DllImport("powrprof.dll")] static extern uint PowerDeleteScheme(IntPtr user, ref Guid scheme);
  [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr pointer);
  static void RestorePower() {
   var plan = Balanced;
   var set = PowerSetActiveScheme(IntPtr.Zero, ref plan);
   if (set != 0) throw new IOException("无法切换到平衡，保留原电源计划 (" + set + ")");
   IntPtr active;
   var read = PowerGetActiveScheme(IntPtr.Zero, out active);
   if (read != 0 || active == IntPtr.Zero) throw new IOException("未确认平衡计划，保留野蛮电源计划");
   try { if ((Guid)Marshal.PtrToStructure(active, typeof(Guid)) != Balanced) throw new IOException("活动计划不是平衡，保留野蛮电源计划"); }
   finally { LocalFree(active); }
   var yeman = YeMan;
   var delete = PowerDeleteScheme(IntPtr.Zero, ref yeman);
   if (delete != 0 && delete != 2) throw new IOException("未能移除野蛮电源计划 (" + delete + ")");
  }
 }
}
