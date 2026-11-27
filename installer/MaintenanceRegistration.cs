using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Security.AccessControl;
using System.Security.Principal;

namespace YeMan.Setup {
 public sealed class ProgramInventory {
  public int Schema = 1;
  public string[] Files;
 }

 internal static class MaintenanceRegistration {
  internal const string MutexName = @"Global\YeManCC.CoreSetup.v1";
  internal const string Entry = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\YeManCC.Core";
  internal const string Launcher = "启动YeManCC.exe";
  internal const string MainImage = @"YeManCC\YeManCC.exe";
  internal const string ExeName = "YeManCC-Uninstall.exe";
  internal static string State { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "YeManCC-Maintenance"); } }
  internal static string Bin { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "YeManCC Maintenance"); } }
  internal static string Inventory { get { return Path.Combine(State, "program-inventory.json"); } }
  internal static string Exe { get { return Path.Combine(Bin, ExeName); } }

  internal static string ProgramPath(string name) {
   var path = SafeFiles.EntryName(name).Replace('/', '\\');
   if (path.StartsWith("YeManCC\\", StringComparison.OrdinalIgnoreCase) || path.StartsWith("PowerControl\\", StringComparison.OrdinalIgnoreCase) ||
       path.Equals(Launcher, StringComparison.OrdinalIgnoreCase) || path.Equals(@".ymcc-setup\state.json", StringComparison.OrdinalIgnoreCase)) return path;
   throw new InvalidDataException("Not a YeManCC program path: " + name);
  }

  internal static ProgramInventory ReadInventory() {
   WorkSecurity.AssertDirectory(State);
   WorkSecurity.AssertFile(Inventory);
   if (new FileInfo(Inventory).Length > 4 * 1024 * 1024) throw new InvalidDataException("Program inventory is too large");
   var item = Json.Read<ProgramInventory>(File.ReadAllText(Inventory));
   if (item == null || item.Schema != 1 || item.Files == null || item.Files.Length > 24000) throw new InvalidDataException("Unknown program inventory");
   return item;
  }

  internal static IEnumerable<string> BundledPaths() {
   using (var resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("maintenance.files")) {
    if (resource == null || resource.Length > 4 * 1024 * 1024) throw new InvalidDataException("Bundled inventory unavailable");
    using (var reader = new StreamReader(resource)) {
     string path;
     while ((path = reader.ReadLine()) != null) yield return ProgramPath(path);
    }
   }
  }

  internal static void Publish(string app, string version, HashSet<string> mutable, Action<string> warn) {
   try { PublishEntry(version); }
   catch (Exception e) { warn("未能登记 Windows 卸载入口；程序安装不受影响。" + e.Message); }
   try { PublishInventory(app,mutable); }
   catch (Exception e) { warn("程序清单未能保存；卸载时使用随包清单。" + e.Message); }
  }

  static void PublishInventory(string app, HashSet<string> mutable) {
   var files = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   // Keep names from earlier committed packages, but never add scanned user files.
   try { foreach (var name in ReadInventory().Files) if (name != null) files.Add(ProgramPath(name)); }
   catch (IOException) { }
   catch (UnauthorizedAccessException) { }
   catch (ArgumentException) { }
   catch (InvalidOperationException) { }
   foreach (var name in SafeFiles.TreeFiles(app)) files.Add(ProgramPath(name.Substring(app.Length + 1)));
   files.ExceptWith(mutable);
   files.Add(Launcher);
   files.Add(@".ymcc-setup\state.json");

   WorkSecurity.Create(State);
   SafeFiles.NoLinks(Inventory);
   Json.Save(Inventory, new ProgramInventory { Files = new List<string>(files).ToArray() });
  }

  static void PublishEntry(string version) {
   var acl = WorkSecurity.Acl();
   acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.BuiltinUsersSid, null),
    FileSystemRights.ReadAndExecute, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
   SafeFiles.NoLinks(Bin);
   if (!Directory.Exists(Bin)) Directory.CreateDirectory(Bin, acl);
   WorkSecurity.AssertDirectory(Bin);
   PublishExecutable(Exe);
   if (MaintenanceUpdate.HasReleaseKey) PublishExecutable(Path.Combine(Bin,MaintenanceUpdate.UpdaterName));

   using (var hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
   using (var entry = hklm.CreateSubKey(Entry)) {
    entry.SetValue("DisplayName", "YeManCC 野蛮系统控制中心");
    entry.SetValue("DisplayVersion", version);
    entry.SetValue("Publisher", "YeManCC");
    entry.SetValue("DisplayIcon", "\"" + Exe + "\",0");
    entry.SetValue("InstallLocation", Plan.InstallRoot);
    entry.SetValue("UninstallString", "\"" + Exe + "\"");
    entry.SetValue("NoModify", 1, RegistryValueKind.DWord);
    entry.SetValue("NoRepair", 1, RegistryValueKind.DWord);
   }
   using (var hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
   using (var entry = hklm.OpenSubKey(Entry, false)) {
    if (entry == null || (entry.GetValue("UninstallString") as string) != ("\"" + Exe + "\"") ||
        (entry.GetValue("InstallLocation") as string) != Plan.InstallRoot || (entry.GetValue("DisplayVersion") as string) != version)
     throw new IOException("Windows 卸载入口回读不一致");
   }
  }

  static void PublishExecutable(string target) {
   SafeFiles.NoLinks(target);
   if (File.Exists(target)) WorkSecurity.AssertFile(target);
   var temporary = Path.Combine(Bin, Guid.NewGuid().ToString("N") + ".tmp");
   try {
    using (var resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("maintenance.exe")) {
     if (resource == null) throw new InvalidDataException("Missing bundled maintenance tool");
     using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
      resource.CopyTo(output);
      output.Flush(true);
     }
    }
    WorkSecurity.SealCore(temporary,true);
    WorkSecurity.AssertFile(temporary);
    if (File.Exists(target)) {
     // Re-running a full installer must not downgrade an already refreshed tool.
     if (MaintenanceUpdate.FileVersion(target) > MaintenanceUpdate.FileVersion(temporary)) return;
     File.Replace(temporary, target, null);
    } else File.Move(temporary, target);
    WorkSecurity.AssertFile(target);
   } finally { if (File.Exists(temporary)) File.Delete(temporary); }
  }
 }
}
