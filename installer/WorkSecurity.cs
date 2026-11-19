using System;using System.IO;using System.Security.AccessControl;using System.Security.Principal;
namespace YeMan.Setup {
 public static class WorkSecurity {
  static bool Trusted(SecurityIdentifier id){return id.IsWellKnown(WellKnownSidType.BuiltinAdministratorsSid)||id.IsWellKnown(WellKnownSidType.LocalSystemSid);}
  static void Check(FileSystemSecurity security) {
   if(!Trusted((SecurityIdentifier)security.GetOwner(typeof(SecurityIdentifier))))throw new IOException("准备记录的所有权不安全；请由管理员检查原准备目录");
   FileSystemRights writes=FileSystemRights.WriteData|FileSystemRights.AppendData|FileSystemRights.Delete|FileSystemRights.DeleteSubdirectoriesAndFiles|FileSystemRights.ChangePermissions|FileSystemRights.TakeOwnership;
   foreach(FileSystemAccessRule rule in security.GetAccessRules(true,true,typeof(SecurityIdentifier)))if(rule.AccessControlType==AccessControlType.Allow&&(rule.FileSystemRights&writes)!=0&&!Trusted((SecurityIdentifier)rule.IdentityReference))throw new IOException("准备记录允许普通账户写入，已停止恢复");
  }
  public static void AssertDirectory(string path){SafeFiles.NoLinks(path);Check(Directory.GetAccessControl(path));}
  public static void AssertFile(string path){SafeFiles.NoLinks(path);Check(File.GetAccessControl(path));}
  public static DirectorySecurity Acl(){var s=new DirectorySecurity();s.SetAccessRuleProtection(true,false);s.SetOwner(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null));foreach(var id in new[]{WellKnownSidType.BuiltinAdministratorsSid,WellKnownSidType.LocalSystemSid})s.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(id,null),FileSystemRights.FullControl,InheritanceFlags.ContainerInherit|InheritanceFlags.ObjectInherit,PropagationFlags.None,AccessControlType.Allow));return s;}
  public static void Create(string path){SafeFiles.NoLinks(path);if(Directory.Exists(path))AssertDirectory(path);else Directory.CreateDirectory(path,Acl());}
  // Only setup-owned machine records are sealed. User configuration remains user-owned.
  public static void Seal(string path){string root=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),"YeManCC-CoreSetup");if(!Native.IsAdmin||!SafeFiles.Under(path,root))return;SafeFiles.NoLinks(path);var s=new FileSecurity();s.SetAccessRuleProtection(true,false);s.SetOwner(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null));foreach(var id in new[]{WellKnownSidType.BuiltinAdministratorsSid,WellKnownSidType.LocalSystemSid})s.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(id,null),FileSystemRights.FullControl,AccessControlType.Allow));File.SetAccessControl(path,s);}
 }
}