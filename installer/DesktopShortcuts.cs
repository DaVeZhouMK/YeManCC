using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

namespace YeMan.Setup {
 internal sealed class DesktopLink {
  internal string Target,Arguments,WorkingDirectory,Icon;internal int IconIndex;
 }
 internal static class DesktopShortcuts {
  [ComImport,Guid("00021401-0000-0000-C000-000000000046")] class ShellLink {}
  [ComImport,InterfaceType(ComInterfaceType.InterfaceIsIUnknown),Guid("000214F9-0000-0000-C000-000000000046")]
  interface IShellLinkW {
   void GetPath([Out,MarshalAs(UnmanagedType.LPWStr)]StringBuilder value,int size,IntPtr data,uint flags);
   void GetIDList(out IntPtr value);void SetIDList(IntPtr value);
   void GetDescription([Out,MarshalAs(UnmanagedType.LPWStr)]StringBuilder value,int size);void SetDescription([MarshalAs(UnmanagedType.LPWStr)]string value);
   void GetWorkingDirectory([Out,MarshalAs(UnmanagedType.LPWStr)]StringBuilder value,int size);void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)]string value);
   void GetArguments([Out,MarshalAs(UnmanagedType.LPWStr)]StringBuilder value,int size);void SetArguments([MarshalAs(UnmanagedType.LPWStr)]string value);
   void GetHotkey(out short value);void SetHotkey(short value);void GetShowCmd(out int value);void SetShowCmd(int value);
   void GetIconLocation([Out,MarshalAs(UnmanagedType.LPWStr)]StringBuilder value,int size,out int index);void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)]string value,int index);
   void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)]string value,uint reserved);void Resolve(IntPtr window,uint flags);void SetPath([MarshalAs(UnmanagedType.LPWStr)]string value);
  }
  [DllImport("shell32.dll",CharSet=CharSet.Unicode)] static extern void SHChangeNotify(uint change,uint flags,[MarshalAs(UnmanagedType.LPWStr)]string path,IntPtr other);
  internal static DesktopLink Read(string path) {
   SafeFiles.NoLinks(path);object native=new ShellLink();
   try {
    ((IPersistFile)native).Load(path,0);var link=(IShellLinkW)native;var text=new StringBuilder(32768);var result=new DesktopLink();
    // Read the stored path only. Never Resolve: inspection must not launch a
    // target, contact a network share or show a missing-target dialog.
    link.GetPath(text,text.Capacity,IntPtr.Zero,4);result.Target=text.ToString();text.Clear();
    link.GetArguments(text,text.Capacity);result.Arguments=text.ToString();text.Clear();
    link.GetWorkingDirectory(text,text.Capacity);result.WorkingDirectory=text.ToString();text.Clear();
    link.GetIconLocation(text,text.Capacity,out result.IconIndex);result.Icon=text.ToString();return result;
   }finally{Marshal.FinalReleaseComObject(native);}
  }
  static bool Same(string left,string right){return !string.IsNullOrEmpty(left)&&!string.IsNullOrEmpty(right)&&string.Equals(Path.GetFullPath(left).TrimEnd('\\'),Path.GetFullPath(right).TrimEnd('\\'),StringComparison.OrdinalIgnoreCase);}
  internal const string YeManLinkName="YeManCC.lnk";
  internal static string CommonLink(Environment.SpecialFolder folder) {
   string dir=Environment.GetFolderPath(folder);
   if(string.IsNullOrWhiteSpace(dir)||!Path.IsPathRooted(dir))throw new IOException("Windows 公共快捷方式目录不可用");
   return Path.Combine(dir,YeManLinkName);
  }
  internal static void Create(FileTransaction tx,string root,string work,string destination) {
   string target=Path.Combine(root,"启动YeManCC.exe"),icon=Path.Combine(root,@"YeManCC\YeManCC.exe");
   if(!File.Exists(target)||!File.Exists(icon))throw new IOException("YMCC 启动程序或图标文件缺失，无法创建桌面入口");
   SafeFiles.NoLinks(destination);
   if(File.Exists(destination)) {
    var previous=Read(destination);
    if(!Same(previous.Target,target)&&!Same(previous.Target,icon))throw new IOException("已有同名快捷方式，已保留");
   }
   string temporary=Path.Combine(work,"link-"+Guid.NewGuid().ToString("N"),YeManLinkName);
   Directory.CreateDirectory(Path.GetDirectoryName(temporary));object native=new ShellLink();
   try {
    var link=(IShellLinkW)native;link.SetPath(target);link.SetWorkingDirectory(root);link.SetArguments("");link.SetIconLocation(icon,0);link.SetDescription("YeManCC 控制中心");link.SetShowCmd(1);
    ((IPersistFile)native).Save(temporary,true);
   }finally{Marshal.FinalReleaseComObject(native);}
   tx.Put(temporary,destination);var installed=Read(destination);
   if(!Same(installed.Target,target)||!Same(installed.WorkingDirectory,root)||!Same(installed.Icon,icon)||installed.IconIndex!=0||!string.IsNullOrEmpty(installed.Arguments))throw new IOException("YMCC 桌面快捷方式回读不一致，安装尚未完成");
   if((File.GetAttributes(destination)&(FileAttributes.Hidden|FileAttributes.System))!=0)throw new IOException("YMCC 桌面快捷方式被隐藏，请检查桌面目录权限");
   Notify(destination,false);
  }
  internal static string[] DesktopDirectories() {
   var paths=new List<string>();foreach(var folder in new[]{Environment.SpecialFolder.CommonDesktopDirectory,Environment.SpecialFolder.DesktopDirectory}){
    string path=Environment.GetFolderPath(folder);if(!string.IsNullOrEmpty(path)&&!paths.Exists(p=>Same(p,path)))paths.Add(path);
   }return paths.ToArray();
  }
  internal static string[] HidHideTargets() {
   var targets=new List<string>();foreach(var folder in new[]{Environment.SpecialFolder.ProgramFiles,Environment.SpecialFolder.ProgramFilesX86}){
    string root=Environment.GetFolderPath(folder);if(string.IsNullOrEmpty(root))continue;
    foreach(string arch in new[]{"x64","x86"})targets.Add(Path.Combine(root,@"Nefarius Software Solutions\HidHide",arch,"HidHideClient.exe"));
   }return targets.ToArray();
  }
  internal static bool IsHidHideDesktopPath(string path,string[] desktops) {
   if(!Path.IsPathRooted(path)||!Path.GetFileName(path).StartsWith("HidHide",StringComparison.OrdinalIgnoreCase)||!Path.GetExtension(path).Equals(".lnk",StringComparison.OrdinalIgnoreCase))return false;
   foreach(string desktop in desktops)if(Same(Path.GetDirectoryName(path),desktop))return true;return false;
  }
  internal static bool IsShortcutFailure(Exception e){return e is IOException||e is UnauthorizedAccessException||e is COMException||e is System.ComponentModel.Win32Exception;}
  internal static void RemoveHidHide(FileTransaction tx,string[] desktops,string[] allowedTargets,List<string> removed,Action save,Action<string> log,Action<string> warn) {
   bool skipped=false;
   foreach(string desktop in desktops){
    if(!Directory.Exists(desktop))continue;
    string[] paths;
    try{SafeFiles.NoLinks(desktop);paths=Directory.GetFiles(desktop,"*.lnk",SearchOption.TopDirectoryOnly);}
    catch(Exception e){if(!IsShortcutFailure(e))throw;skipped=true;log("未能检查桌面："+e.Message);continue;}
    foreach(string path in paths){
     if(!IsHidHideDesktopPath(path,desktops))continue;
     bool owned=false;
     try{SafeFiles.NoLinks(path);var link=Read(path);foreach(string target in allowedTargets)if(Same(link.Target,target)){owned=true;break;}}
     catch(Exception e){if(!IsShortcutFailure(e))throw;skipped=true;log("未能读取 HidHide 快捷方式："+e.Message);continue;}
     if(!owned)continue;
     removed.Add(path);save();
     try{tx.Remove(path);Notify(path,true);}
     catch(Exception e){if(!IsShortcutFailure(e))throw;tx.RestorePaths(p=>Same(p,path));skipped=true;log("未能移除 HidHide 快捷方式："+e.Message);continue;}
     log("已移除 HidHide 桌面快捷方式");
    }
   }
   if(skipped)warn("未能清理 HidHide 桌面快捷方式，仅影响桌面图标。");
  }
  static void Notify(string path,bool deleted) {SHChangeNotify(deleted?4u:0x2000u,5,path,IntPtr.Zero);SHChangeNotify(0x1000,5,Path.GetDirectoryName(path),IntPtr.Zero);}
 }
}
