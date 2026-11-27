using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text.RegularExpressions;

namespace YeMan.Setup {
 public sealed class MaintenanceValue {
  public string type;
  public string text;
  public long? number;
 }

 public sealed class MaintenanceReceipt {
  public int schema;
  public string id;
  public string sid;
  public MaintenanceValue before;
  public MaintenanceValue after;
  public MaintenanceValue pending;
  public string taskPath;
  public string xml;
  public bool? enabled;
  public bool? removed;
 }

 internal static class MaintenanceRollback {
  sealed class Target {
   internal readonly string Id, Path, Name, Kind;
   internal readonly bool Machine;
   internal Target(string id, string path, string name, string kind, bool machine) { Id=id; Path=path; Name=name; Kind=kind; Machine=machine; }
  }
  static readonly Target[] Targets = {
   new Target("gaming-home", @"Software\Microsoft\Windows\CurrentVersion\GamingConfiguration", "GamingHomeApp", "string", false),
   new Target("gaming-start", @"Software\Microsoft\Windows\CurrentVersion\GamingConfiguration", "StartupToGamingHome", "dword", false),
   new Target("gaming-device", @"SOFTWARE\Microsoft\Windows NT\CurrentVersion\OEM", "DeviceForm", "dword", true),
   new Target("gamebar-controller", @"Software\Microsoft\GameBar", "UseNexusForGameBarEnabled", "dword", false),
   new Target("gpu-scheduling", @"SYSTEM\CurrentControlSet\Control\GraphicsDrivers", "HwSchMode", "dword", true)
  };

  internal static void Restore(RemovalResult result) {
   var root = Path.Combine(MaintenanceRegistration.State, "receipts-v1");
   if (!Directory.Exists(root)) { result.Warnings.Add("未找到系统设置原值记录，未猜测还原。"); return; }
   WorkSecurity.AssertDirectory(MaintenanceRegistration.State);
   WorkSecurity.AssertDirectory(root);
   using (var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
   using (var users = RegistryKey.OpenBaseKey(RegistryHive.Users, RegistryView.Registry64)) {
    var seen = 0;
    foreach (var path in Directory.EnumerateFiles(root, "*.json")) {
     if (++seen > 1024) { result.Warnings.Add("还原记录过多，其余已保留。"); break; }
     try { RestoreFile(root, path, machine, users, result); }
     catch (Exception e) { result.Warnings.Add(Path.GetFileName(path) + " 未还原：" + e.Message); }
    }
   }
  }

  static bool Valid(MaintenanceValue value, string expected, bool absent) {
   if (value == null) return false;
   if (value.type == "none") return absent;
   if (value.type != expected) return false;
   if (expected == "dword") return value.number.HasValue && value.number.Value >= 0 && value.number.Value <= UInt32.MaxValue;
   if (expected == "bytes") return value.text != null && value.text.Length <= 16384 &&
    value.text.Length % 2 == 0 && Regex.IsMatch(value.text, "^[0-9a-f]*$");
   return value.text != null && value.text.Length <= 4096 && value.text.IndexOf('\0') < 0;
  }
  static bool Equal(MaintenanceValue a, MaintenanceValue b) {
   return a != null && b != null && a.type == b.type &&
    (a.type == "none" || a.type == "dword" && a.number == b.number || (a.type == "string" || a.type == "bytes") && a.text == b.text);
  }
  static MaintenanceValue Read(RegistryKey key, string name) {
   object raw = key == null ? null : key.GetValue(name, null, RegistryValueOptions.DoNotExpandEnvironmentNames);
   if (raw == null) return new MaintenanceValue { type = "none" };
   var kind = key.GetValueKind(name);
   if (kind == RegistryValueKind.DWord && raw is int) return new MaintenanceValue { type = "dword", number = unchecked((uint)(int)raw) };
   if (kind == RegistryValueKind.String && raw is string) return new MaintenanceValue { type = "string", text = (string)raw };
   return new MaintenanceValue { type = "other" };
  }

  static void RestoreFile(string root, string record, RegistryKey machine, RegistryKey users, RemovalResult result) {
   WorkSecurity.AssertFile(record);
   if (new FileInfo(record).Length > 64 * 1024) throw new InvalidDataException("记录太长");
   var data = Json.Read<MaintenanceReceipt>(File.ReadAllText(record));
   if (data == null || data.schema != 1) throw new InvalidDataException("记录格式不受支持");
   if (data.id == "msi-task") {
    RestoreMsiTask(data,record);
    File.Delete(record);
    result.Restored.Add(data.taskPath);
    return;
   }
   if (data.id == "steam-earlystart") {
    ValidateReceipt(data, record, "bytes", false);
    RestoreEarlyStart(data);
    File.Delete(record);
    result.Restored.Add(data.id + " (" + data.sid + ")");
    return;
   }
   if (data.id == "earlystart-service") {
    ValidateReceipt(data, record, "string", true);
    RestoreEarlyStartService(data);
    File.Delete(record);
    result.Restored.Add(data.id);
    return;
   }
   var target = Array.Find(Targets, t => t.Id == data.id);
   if (target == null) throw new InvalidDataException("未知的还原项目");
   ValidateReceipt(data, record, target.Kind, target.Machine);
   if (!target.Machine) {
    var sid = new SecurityIdentifier(data.sid);
    if (sid.IsWellKnown(WellKnownSidType.LocalSystemSid) || sid.IsWellKnown(WellKnownSidType.LocalServiceSid) || sid.IsWellKnown(WellKnownSidType.NetworkServiceSid)) throw new InvalidDataException("不是用户配置记录");
    using (var profile = users.OpenSubKey(data.sid, false)) if (profile == null) throw new IOException("原用户配置单元未加载");
   }
   var hive = target.Machine ? machine : users;
   var path = target.Machine ? target.Path : data.sid + "\\" + target.Path;
   using (var key = hive.OpenSubKey(path, true)) {
    var current = Read(key, target.Name);
    if (!Equal(current, data.before)) {
     if (!Equal(current, data.after) && !Equal(current, data.pending)) throw new IOException("当前值已由其他程序或用户更改");
     if (key == null) throw new IOException("原设置键已不存在");
     if (data.before.type == "none") key.DeleteValue(target.Name, false);
     else if (data.before.type == "dword") key.SetValue(target.Name, unchecked((int)data.before.number.Value), RegistryValueKind.DWord);
     else key.SetValue(target.Name, data.before.text, RegistryValueKind.String);
     if (!Equal(Read(key,target.Name),data.before)) throw new IOException("恢复读回失败");
    }
   }
   File.Delete(record);
   result.Restored.Add(data.id + " (" + data.sid + ")");
  }

  static void ValidateReceipt(MaintenanceReceipt data, string record, string kind, bool machine) {
   if (!Valid(data.before, kind, true) || !Valid(data.after, kind, true) || data.pending != null && !Valid(data.pending, kind, true))
    throw new InvalidDataException("记录中的值无效");
   if (machine ? data.sid != "machine" : !Regex.IsMatch(data.sid ?? "", @"^S-1-\d(-\d{1,10}){1,13}$"))
    throw new InvalidDataException("记录的作用域无效");
   if (!Path.GetFileName(record).Equals(data.id + "." + data.sid + ".json", StringComparison.OrdinalIgnoreCase))
    throw new InvalidDataException("记录名称不匹配");
  }

  static void RestoreMsiTask(MaintenanceReceipt data,string record) {
   if (data.removed != true || !data.enabled.HasValue) throw new IOException("未确认任务由 YMCC 移除或缺少原启用状态");
   var path = data.taskPath ?? "";
   if (path.Length > 240 || !path.StartsWith("\\") || path.IndexOfAny(new[] {'/',':','\0','\r','\n'}) >= 0 || data.xml == null || data.xml.Length > 20000)
    throw new InvalidDataException("微星任务记录无效");
   foreach (var part in path.Substring(1).Split('\\')) if (part.Length == 0 || part == "." || part == "..") throw new InvalidDataException("微星任务路径无效");
   ulong hash = 14695981039346656037UL;
   foreach (var c in path) { hash ^= Char.ToLowerInvariant(c); hash = unchecked(hash*1099511628211UL); }
   if (Path.GetFileName(record) != "msi-task-" + hash.ToString(System.Globalization.CultureInfo.InvariantCulture) + ".json") throw new InvalidDataException("微星任务记录名称不匹配");
   int split = path.LastIndexOf('\\');
   var folderPath = split == 0 ? "\\" : path.Substring(0,split);
   var leaf = path.Substring(split+1);
   bool server = leaf.Equals("MSI_Center_M_Server",StringComparison.OrdinalIgnoreCase);
   if (!server && !leaf.Equals("MSI_Center_M_Updater",StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("不支持的微星任务");
   var expected = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),@"MSI\MSI Center M",server ? "MSI_Center_M_Server.exe" : "MSI Center M Updater.exe");
   SafeFiles.NoLinks(expected);
   if (!File.Exists(expected)) throw new IOException("未找到微星 OEM 程序，请使用 MSI 官方组件修复");
   dynamic scheduler = Activator.CreateInstance(Type.GetTypeFromProgID("Schedule.Service",true));
   dynamic folder = null, old = null, definition = null, action = null, restored = null;
   try {
    scheduler.Connect(null,null,null,null);
    folder = scheduler.GetFolder(folderPath);
    try { old = folder.GetTask(leaf); }
    catch (COMException e) { if (e.ErrorCode != unchecked((int)0x80070002) && e.ErrorCode != unchecked((int)0x80070003)) throw; }
    if (old != null) throw new IOException("微星任务已经存在，保留当前定义");
    definition = scheduler.NewTask(0);
    definition.XmlText = data.xml;
    int level = definition.Principal.RunLevel;
    if ((int)definition.Actions.Count != 1 || (int)definition.Principal.LogonType != 4 || (level != 0 && level != 1) ||
        !String.Equals((string)definition.Principal.GroupId,"S-1-5-32-545",StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("微星原任务的运行身份不在可安全还原范围");
    action = definition.Actions[1];
    if ((int)action.Type != 0 || !String.Equals(((string)action.Path).Trim('"'),expected,StringComparison.OrdinalIgnoreCase) ||
        !String.IsNullOrEmpty((string)action.WorkingDirectory) ||
        !String.Equals((string)action.Arguments ?? "",server ? "" : "MSI_Center_M",StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("微星原任务的启动动作未通过核对");
    definition.Settings.Enabled = data.enabled.Value;
    // Create only. If MSI recreated the task after the read, registration fails without overwriting it.
    restored = folder.RegisterTaskDefinition(leaf,definition,2,"S-1-5-32-545",null,4,null);
    if ((bool)restored.Enabled != data.enabled.Value) throw new IOException("微星任务启用状态读回失败");
   } finally {
    foreach (object item in new object[] {restored,action,definition,old,folder,scheduler}) if (item != null && Marshal.IsComObject(item)) Marshal.ReleaseComObject(item);
   }
  }

  static MaintenanceValue ReadEarlyStart(string path) {
   SafeFiles.NoLinks(path);
   try {
    using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read)) {
     if (f.Length > 8192) throw new IOException("启动文件超出记录范围");
     var bytes = new byte[f.Length];
     int count = 0;
     while (count < bytes.Length) {
      int read = f.Read(bytes, count, bytes.Length - count);
      if (read == 0) throw new IOException("启动文件已更改");
      count += read;
     }
     return new MaintenanceValue { type = "bytes", text = BitConverter.ToString(bytes).Replace("-", "").ToLowerInvariant() };
    }
   } catch (FileNotFoundException) { return new MaintenanceValue { type = "none" }; }
  }

  static void RestoreEarlyStart(MaintenanceReceipt data) {
   using (var actor = WindowsIdentity.GetCurrent()) if (actor.User.Value != data.sid)
    throw new IOException("请使用原用户账户还原 Steam 启动文件");
   var profile = SafeFiles.Full(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile));
   var file = Path.Combine(profile, ".earlystart");
   var live = ReadEarlyStart(file);
   if (Equal(live,data.before)) return;
   if (!Equal(live,data.after) && !Equal(live,data.pending)) throw new IOException("启动文件已由其他程序或用户更改");
   if (data.before.type == "none") {
    if (!Equal(live,ReadEarlyStart(file))) throw new IOException("启动文件已更改");
    File.Delete(file);
   } else {
    var bytes = new byte[data.before.text.Length/2];
    for (int i=0; i<bytes.Length; i++) bytes[i]=Convert.ToByte(data.before.text.Substring(i*2,2),16);
    var temp = Path.Combine(profile,".ymcc-uninstall-" + Guid.NewGuid().ToString("N") + ".tmp");
    try {
     using (var f = new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)) { f.Write(bytes,0,bytes.Length); f.Flush(true); }
     if (!Equal(live,ReadEarlyStart(file))) throw new IOException("启动文件已更改");
     if (live.type == "none") File.Move(temp,file); else File.Replace(temp,file,null);
    } finally { if (File.Exists(temp)) File.Delete(temp); }
   }
   if (!Equal(data.before,ReadEarlyStart(file))) throw new IOException("启动文件恢复读回失败");
  }

  [StructLayout(LayoutKind.Sequential)]
  struct ServiceConfig {
   public uint Type, Start, ErrorControl;
   public IntPtr Binary, Group;
   public uint Tag;
   public IntPtr Dependencies, Account, Display;
  }
  [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr OpenSCManager(string machine, string database, uint access);
  [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr OpenService(IntPtr manager, string name, uint access);
  [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool QueryServiceConfig(IntPtr service, IntPtr buffer, uint capacity, out uint needed);
  [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool ChangeServiceConfig(IntPtr service, uint type, uint start, uint error,
   string binary, string group, IntPtr tag, string dependencies, string account, string password, string display);
  [DllImport("advapi32.dll")] static extern bool CloseServiceHandle(IntPtr service);

  static MaintenanceValue ReadService(IntPtr service) {
   uint bytes;
   QueryServiceConfig(service, IntPtr.Zero, 0, out bytes);
   if (bytes < Marshal.SizeOf(typeof(ServiceConfig)) || bytes > 65536) throw new IOException("无法读取服务配置");
   var buffer = Marshal.AllocHGlobal((int)bytes);
   try {
    if (!QueryServiceConfig(service,buffer,bytes,out bytes)) throw new System.ComponentModel.Win32Exception();
    var config = (ServiceConfig)Marshal.PtrToStructure(buffer,typeof(ServiceConfig));
    var invariant = System.Globalization.CultureInfo.InvariantCulture;
    return new MaintenanceValue { type="string", text=config.Start.ToString(invariant)+"\n"+config.Type.ToString(invariant)+"\n"+Marshal.PtrToStringUni(config.Binary) };
   } finally { Marshal.FreeHGlobal(buffer); }
  }

  static void RestoreEarlyStartService(MaintenanceReceipt data) {
   string before = data.before.text;
   if (before == null || before.Length < 5 || before[0] < '2' || before[0] > '4' || before[1] != '\n' ||
       data.after.text != before && data.after.text != "2"+before.Substring(1) ||
       data.pending != null && data.pending.text != "2"+before.Substring(1)) throw new InvalidDataException("服务启动记录无效");
   var manager = OpenSCManager(null,null,1);
   if (manager == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
   try {
    var service = OpenService(manager,"EarlyStartService",3);
    if (service == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
    try {
     var current = ReadService(service);
     if (Equal(current,data.before)) return;
     if (!Equal(current,data.after) && !Equal(current,data.pending)) throw new IOException("服务已由其他程序或用户更改");
     if (!ChangeServiceConfig(service,UInt32.MaxValue,(uint)(before[0]-'0'),UInt32.MaxValue,null,null,IntPtr.Zero,null,null,null,null))
      throw new System.ComponentModel.Win32Exception();
     if (!Equal(ReadService(service),data.before)) throw new IOException("服务启动类型恢复读回失败");
    } finally { CloseServiceHandle(service); }
   } finally { CloseServiceHandle(manager); }
  }
 }
}
