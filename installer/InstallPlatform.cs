using System;
using System.Collections.Generic;
using System.IO;

namespace YeMan.Setup {
 // Internal OS boundary. The public EXE always creates this production platform;
 // isolated diagnostics compile the same engine with explicit inert OS ports.
 internal class InstallPlatform {
  internal virtual string Root {get{return Plan.InstallRoot;}}
  internal virtual string Hw {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"HWiNFO64");}}
  internal virtual string Rtss {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"RivaTuner Statistics Server");}}
  internal virtual string Parent {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),"YeManCC-CoreSetup");}}
  internal virtual string ShortcutFile {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonDesktopDirectory),"YeManCC.lnk");}}
  internal virtual string[] Desktops {get{return DesktopShortcuts.DesktopDirectories();}}
  internal virtual string MutexName {get{return @"Global\YeManCC.CoreSetup.v1";}}
  internal virtual bool Admin {get{return Native.IsAdmin;}}
  internal virtual void Compatible(){Native.RequireSupportedSystem();}
  internal virtual IDefenderClient Defender {get{return new WindowsDefenderClient();}}
  internal virtual void Idle(string[] roots){Engine.Busy(roots);}
  internal virtual string Work(){return Engine.SecureWork(Parent);}
  internal virtual void AssertDirectory(string path){WorkSecurity.AssertDirectory(path);}
  internal virtual void AssertFile(string path){WorkSecurity.AssertFile(path);}
  internal virtual void BackupDirectory(string path){WorkSecurity.Create(path);}
  internal virtual void Save(string path,Receipt receipt){Json.Save(path,receipt);}
  internal virtual Bundle Unpack(string output,Action<string> log){return Resources.Unpack(output,log);}
  internal virtual void Signature(string path){Native.MicrosoftSignature(path);}
  internal virtual string Version(string root){return Engine.InstalledVersion(root);}
  internal virtual void Runtimes(string resources,string work,Action<string> log,Action<bool> reboot){YeMan.Setup.Runtimes.Prepare(resources,work,log,reboot);}
  internal virtual bool Ready(){return YeMan.Setup.Runtimes.Ready();}
  internal virtual void HidHide(string app,string work,HidHideRecord record,Action save,Action<string> log,Action<bool> reboot){YeMan.Setup.HidHide.Prepare(app,work,record,save,log,reboot,new HidHidePlatform());}
  internal virtual void Shortcut(FileTransaction tx,string root,string work){Engine.Shortcut(tx,root,work);}
  internal virtual void RemoveHidHideShortcuts(FileTransaction tx,List<string> removed,Action save,Action<string> log){DesktopShortcuts.RemoveHidHide(tx,Desktops,DesktopShortcuts.HidHideTargets(),removed,save,log);}
  internal virtual void Space(string path,long required){RequireSpace(new DriveInfo(Path.GetPathRoot(Path.GetFullPath(path))).AvailableFreeSpace,required);}
  internal static void RequireSpace(long available,long required){if(available<required)throw new IOException("磁盘可用空间不足：本阶段至少需要 "+Math.Ceiling(required/1048576d)+" MiB，可用 "+Math.Floor(available/1048576d)+" MiB。请释放空间后重试");}
 }
}
