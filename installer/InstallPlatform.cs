using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace YeMan.Setup {
 // Internal OS boundary. The public EXE always creates this production platform;
 // isolated diagnostics compile the same engine with explicit inert OS ports.
 internal class InstallPlatform {
  internal virtual string Root {get{return Plan.InstallRoot;}}
  internal virtual string Hw {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"HWiNFO64");}}
  internal virtual string Rtss {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"RivaTuner Statistics Server");}}
  internal virtual string Parent {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),"YeManCC-CoreSetup");}}
  internal virtual string ShortcutFile {get{return DesktopShortcuts.CommonLink(Environment.SpecialFolder.CommonDesktopDirectory);}}
  internal virtual string StartMenuFile {get{return DesktopShortcuts.CommonLink(Environment.SpecialFolder.CommonPrograms);}}
  internal virtual string[] Desktops {get{return DesktopShortcuts.DesktopDirectories();}}
  internal virtual string MutexName {get{return MaintenanceRegistration.MutexName;}}
  internal virtual void PublishMaintenance(string app,string version,HashSet<string> mutable,Action<string> warn){
   if(Root.Equals(Plan.InstallRoot,StringComparison.OrdinalIgnoreCase)) MaintenanceRegistration.Publish(app,version,mutable,warn);
  }
  internal virtual bool Admin {get{return Native.IsAdmin;}}
  internal virtual SystemObservation Compatibility(){return Native.SystemSupport();}
  internal virtual IDefenderClient Defender {get{return new WindowsDefenderClient();}}
  internal virtual void ClosePrograms(string root,Action<string> log){InstalledProcesses.Close(root,string.Equals(root,Root,StringComparison.OrdinalIgnoreCase),log);}
  internal virtual string Work(){return Engine.SecureWork(Parent);}
  internal virtual void AssertDirectory(string path){WorkSecurity.AssertDirectory(path);}
  internal virtual void AssertFile(string path){WorkSecurity.AssertFile(path);}
  internal virtual void BackupDirectory(string path){WorkSecurity.Create(path);}
  internal virtual void Save(string path,Receipt receipt){Json.Save(path,receipt);}
  internal virtual Bundle Unpack(string output,Action<string> log){return Resources.Unpack(output,log);}
  internal virtual string Version(string root){return Engine.InstalledVersion(root);}
  internal virtual void Runtimes(string resources,string work,bool webOnline,Action<string> log,Action<bool> reboot,Action<string> warn){YeMan.Setup.Runtimes.Prepare(resources,work,webOnline,log,reboot,warn);}
  internal virtual void HidHide(string app,string work,HidHideRecord record,Action save,Action<string> log,Action<bool> reboot){YeMan.Setup.HidHide.Prepare(app,work,record,save,log,reboot,new HidHidePlatform());}
  internal virtual void UninstallLegacyHidHide(string app,string work,HidHideRecord record){YeMan.Setup.HidHide.UninstallLegacy(app,work,record,new HidHidePlatform());}
  internal virtual void Shortcut(FileTransaction tx,string root,string work){DesktopShortcuts.Create(tx,root,work,ShortcutFile);}
  internal virtual void StartMenuShortcut(FileTransaction tx,string root,string work){DesktopShortcuts.Create(tx,root,work,StartMenuFile);}
  internal virtual void RemoveHidHideShortcuts(FileTransaction tx,List<string> removed,Action save,Action<string> log,Action<string> warn){DesktopShortcuts.RemoveHidHide(tx,Desktops,DesktopShortcuts.HidHideTargets(),removed,save,log,warn);}
  internal virtual void Space(string path,long required){RequireSpace(new DriveInfo(Path.GetPathRoot(Path.GetFullPath(path))).AvailableFreeSpace,required);}
  internal static void RequireSpace(long available,long required){if(available<required)throw new IOException("磁盘可用空间不足：本阶段至少需要 "+Math.Ceiling(required/1048576d)+" MiB，可用 "+Math.Floor(available/1048576d)+" MiB。请释放空间后重试");}
 }
 internal sealed class InstallTargetBusyException : IOException {
  internal InstallTargetBusyException(string name,Exception cause=null):base("正在使用："+name,cause){}
 }
 internal static class InstalledProcesses {
  const uint Terminate=0x0001,QueryLimited=0x1000,Synchronize=0x00100000,Signaled=0,Timeout=258;
  [DllImport("kernel32.dll",SetLastError=true)]static extern IntPtr OpenProcess(uint access,bool inherit,int id);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool QueryFullProcessImageName(IntPtr handle,uint flags,StringBuilder path,ref int length);
  [DllImport("kernel32.dll",SetLastError=true)]static extern bool TerminateProcess(IntPtr handle,uint exitCode);
  [DllImport("kernel32.dll",SetLastError=true)]static extern uint WaitForSingleObject(IntPtr handle,uint timeout);
  [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr handle);
  delegate bool WindowVisitor(IntPtr window,IntPtr parameter);
  [DllImport("user32.dll")]static extern bool EnumWindows(WindowVisitor visitor,IntPtr parameter);
  [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr window,out int processId);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr window,StringBuilder name,int capacity);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern uint RegisterWindowMessage(string name);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool PostMessage(IntPtr window,uint message,IntPtr wparam,IntPtr lparam);
  internal static bool OwnsImage(string root,bool core,string image) {
   if(!core)return SafeFiles.Under(image,root);
   return SafeFiles.Under(image,Path.Combine(root,"YeManCC"))||SafeFiles.Under(image,Path.Combine(root,"PowerControl"))||string.Equals(Path.Combine(root,"启动YeManCC.exe"),image,StringComparison.OrdinalIgnoreCase);
  }
  internal static void Close(string root,bool core,Action<string> log,bool graceful=false,ISet<string> ownedFiles=null) {
   // Identify processes on the same handle used to terminate, never by name alone.
   if(!Directory.Exists(root))return;
   SafeFiles.NoLinks(root);
   int ownId;using(var self=Process.GetCurrentProcess())ownId=self.Id;
   var clock=Stopwatch.StartNew();
   var processes=Process.GetProcesses();
   try {
    // Main processes first so they cannot respawn their own helpers.
    foreach(bool main in new[]{true,false})foreach(var process in processes) {
     if(process.Id==ownId)continue;
     string name;try{name=process.ProcessName;}catch(InvalidOperationException){continue;}
     if(main!=IsMain(name))continue;
     IntPtr handle=OpenProcess(Terminate|QueryLimited|Synchronize,false,process.Id);
     if(handle==IntPtr.Zero)continue;
     try {
      var path=new StringBuilder(1024);int length=path.Capacity;
      if(!QueryFullProcessImageName(handle,0,path,ref length)||!OwnsImage(root,core,path.ToString()))continue;
      if(ownedFiles!=null&&!ownedFiles.Contains(path.ToString().Substring(root.Length+1)))continue;
      if(WaitForSingleObject(handle,0)!=Timeout)continue;
      try{SafeFiles.NoLinks(path.ToString());}catch(UnsafeInstallPathException){continue;}
      if(graceful&&String.Equals(path.ToString(),Path.Combine(root,MaintenanceRegistration.MainImage),StringComparison.OrdinalIgnoreCase)&&RequestExit(process.Id)) {
       if(WaitForSingleObject(handle,(uint)Math.Max(0,12000-clock.ElapsedMilliseconds))==Signaled){log("YeManCC 已正常退出");continue;}
       log("YeManCC 未在时限内正常退出，尝试结束进程");
      }
      if(WaitForSingleObject(handle,0)!=Timeout)continue;
      TerminateProcess(handle,0);
      uint wait=(uint)Math.Max(0,Math.Min(1500,(graceful?18000:6000)-clock.ElapsedMilliseconds));
      log(WaitForSingleObject(handle,wait)==Signaled?"已关闭 "+name:"未能关闭 "+name+"，尝试继续写入");
     }finally{CloseHandle(handle);}
    }
   }finally{foreach(var process in processes)process.Dispose();}
  }
  static bool RequestExit(int id) {
   var message=RegisterWindowMessage("YeManCC.Maintenance.RequestExit.v1");
   if(message==0)return false;
   bool sent=false;
   EnumWindows((window,parameter)=>{
    int pid;
    if(GetWindowThreadProcessId(window,out pid)==0||pid!=id)return true;
    var name=new StringBuilder(32);
    if(GetClassName(window,name,name.Capacity)==0||name.ToString()!="QQ")return true;
    sent=PostMessage(window,message,IntPtr.Zero,IntPtr.Zero);
    return !sent;
   },IntPtr.Zero);
   return sent;
  }
  static bool IsMain(string name){return name.Equals("RTSS",StringComparison.OrdinalIgnoreCase)||name.Equals("HWiNFO64",StringComparison.OrdinalIgnoreCase)||name.Equals("YeManCC",StringComparison.OrdinalIgnoreCase)||name.Equals("启动YeManCC",StringComparison.OrdinalIgnoreCase);}
 }
}
