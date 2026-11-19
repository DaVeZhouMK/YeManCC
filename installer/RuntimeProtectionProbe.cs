using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using Microsoft.Win32.SafeHandles;

namespace YeMan.Setup {
 internal static class RuntimeProtectionProbe {
  internal enum Scope { Ancestor, SearchDirectory, Image }
  internal sealed class Observation {
   public string Path, Role, Reason;
   public bool Protected;
  }

  static readonly SecurityIdentifier ServicingOwner = new SecurityIdentifier("S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464");
  static bool Trusted(SecurityIdentifier sid) {
   return sid != null && (sid.IsWellKnown(WellKnownSidType.LocalSystemSid) ||
    sid.IsWellKnown(WellKnownSidType.BuiltinAdministratorsSid) || sid.Equals(ServicingOwner));
  }

  internal static string Refusal(RawSecurityDescriptor descriptor, Scope role) {
   if (descriptor == null || (descriptor.ControlFlags & ControlFlags.DiscretionaryAclPresent) == 0 || descriptor.DiscretionaryAcl == null)
    return "missing-or-null-dacl";
   if (!Trusted(descriptor.Owner)) return "untrusted-owner";
   const int genericWriteAllOrMaximumAllowed = 0x52000000;
   int dangerous = (int)(FileSystemRights.Delete | FileSystemRights.ChangePermissions | FileSystemRights.TakeOwnership |
    FileSystemRights.WriteAttributes | FileSystemRights.WriteExtendedAttributes) | genericWriteAllOrMaximumAllowed;
   if (role != Scope.Image) dangerous |= (int)FileSystemRights.DeleteSubdirectoriesAndFiles;
   // Adding a sibling cannot replace a protected ancestor. Adding a file to a loader search directory matters.
   if (role != Scope.Ancestor) dangerous |= (int)(FileSystemRights.WriteData | FileSystemRights.AppendData);
   foreach (GenericAce entry in descriptor.DiscretionaryAcl) {
    if ((entry.AceFlags & AceFlags.InheritOnly) != 0) continue;
    var ace = entry as CommonAce;
    if (ace == null || ace.IsCallback) return "unsupported-dacl-entry";
    if (ace.AceQualifier == AceQualifier.AccessDenied) continue;
    if (ace.AceQualifier != AceQualifier.AccessAllowed) return "unsupported-dacl-entry";
    if (!Trusted(ace.SecurityIdentifier) && (ace.AccessMask & dangerous) != 0) return "untrusted-mutation-grant";
   }
   return null;
  }

  [StructLayout(LayoutKind.Sequential)]
  struct FileInfo {
   internal FileAttributes Attributes;
   internal System.Runtime.InteropServices.ComTypes.FILETIME Creation, Access, Write;
   internal uint Volume, SizeHigh, SizeLow, LinkCount, IndexHigh, IndexLow;
  }

  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
  static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
  [DllImport("kernel32.dll", SetLastError = true)]
  static extern bool GetFileInformationByHandle(SafeFileHandle handle, out FileInfo info);
  [DllImport("advapi32.dll")]
  static extern uint GetSecurityInfo(SafeFileHandle handle, int type, uint information, IntPtr owner, IntPtr group, IntPtr dacl, IntPtr sacl, out IntPtr descriptor);
  [DllImport("advapi32.dll")]
  static extern uint GetSecurityDescriptorLength(IntPtr descriptor);
  [DllImport("kernel32.dll")]
  static extern IntPtr LocalFree(IntPtr memory);

  static string ReadPermissions(SafeFileHandle handle, Scope role) {
   IntPtr descriptor;
   const int seFileObject = 1;
   const uint ownerAndDacl = 0x5;
   uint error = GetSecurityInfo(handle, seFileObject, ownerAndDacl, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, out descriptor);
   try {
    if (error != 0) return error == 5 ? "descriptor-access-denied" : "object-or-descriptor-unavailable";
    if (descriptor == IntPtr.Zero) return "descriptor-invalid";
    uint length = GetSecurityDescriptorLength(descriptor);
    if (length < 20 || length > 128 * 1024) return "descriptor-invalid";
    var bytes = new byte[length];
    Marshal.Copy(descriptor, bytes, 0, bytes.Length);
    return Refusal(new RawSecurityDescriptor(bytes, 0), role);
   } finally { if (descriptor != IntPtr.Zero) LocalFree(descriptor); }
  }

  static Observation Read(string path, Scope role, List<SafeFileHandle> held) {
   var row = new Observation { Path = path, Role = role.ToString() };
   SafeFileHandle handle = null;
   try {
    const uint readControlAndAttributes = 0x20080, shareReadAndWrite = 3, openExisting = 3;
    const uint openReparseAndBackupSemantics = 0x02200000;
    handle = CreateFileW(path, readControlAndAttributes, shareReadAndWrite, IntPtr.Zero, openExisting, openReparseAndBackupSemantics, IntPtr.Zero);
    if (handle.IsInvalid) row.Reason = Marshal.GetLastWin32Error() == 5 ? "descriptor-access-denied" : "object-or-descriptor-unavailable";
    else {
     FileInfo info;
     if (!GetFileInformationByHandle(handle, out info)) row.Reason = "object-or-descriptor-unavailable";
     else if ((info.Attributes & FileAttributes.ReparsePoint) != 0) row.Reason = "reparse-point";
     else if (((info.Attributes & FileAttributes.Directory) != 0) != (role != Scope.Image)) row.Reason = "object-type-mismatch";
     else if (role == Scope.Image && info.LinkCount != 1) row.Reason = "unexpected-image-link-count";
     else {
      row.Reason = ReadPermissions(handle, role);
      if (role != Scope.Image) { held.Add(handle); handle = null; }
     }
    }
   } catch (UnauthorizedAccessException) { row.Reason = "descriptor-access-denied"; }
   catch (IOException) { row.Reason = "object-or-descriptor-unavailable"; }
   catch (System.Security.SecurityException) { row.Reason = "descriptor-access-denied"; }
   catch (ArgumentException) { row.Reason = "descriptor-invalid"; }
   finally { if (handle != null) handle.Dispose(); }
   row.Protected = row.Reason == null;
   return row;
  }

  static bool CanInspectChild(Observation parent) {
   return parent.Reason == null || parent.Reason == "untrusted-owner" ||
    parent.Reason == "untrusted-mutation-grant" || parent.Reason == "unsupported-dacl-entry" || parent.Reason == "missing-or-null-dacl";
  }
  static Observation ReadChild(Observation parent, string child, Scope role, List<SafeFileHandle> held) {
   return CanInspectChild(parent) ? Read(child, role, held) : new Observation { Path = child, Role = role.ToString(), Reason = "ancestor-not-inspectable" };
  }
  internal static List<Observation> InspectFixedInstallation() {
   return InspectLayout(Plan.InstallRoot);
  }
  internal static List<Observation> InspectLayout(string install) {
   var ancestors = new Stack<string>();
   for (string p = install; !string.IsNullOrEmpty(p); p = Path.GetDirectoryName(p)) ancestors.Push(p);
   var rows = new List<Observation>();
   var held = new List<SafeFileHandle>();
   // These handles belong only to this diagnostic, not to a later activation.
   try {
    rows.Add(Read(ancestors.Pop(), Scope.Ancestor, held));
    while (ancestors.Count > 0) rows.Add(ReadChild(rows[rows.Count - 1], ancestors.Pop(), Scope.Ancestor, held));
    var installRow = rows[rows.Count - 1];
    var app = ReadChild(installRow, Path.Combine(install, "YeManCC"), Scope.SearchDirectory, held);
    rows.Add(app);
    rows.Add(ReadChild(installRow, Path.Combine(install, "PowerControl"), Scope.SearchDirectory, held));
    rows.Add(ReadChild(app, Path.Combine(app.Path, "YeManCC.exe"), Scope.Image, held));
    return rows;
   } finally {
    for (int i = held.Count - 1; i >= 0; --i) held[i].Dispose();
   }
  }

  // A path snapshot is useful for diagnosis but never authorizes registration or proves the loader closure.
  internal static object DiagnosticSnapshot() {
   var rows = InspectFixedInstallation();
   return new { Status = "NOT_QUALIFIED", TaskRegistrationAllowed = false, RuntimeClosureVerified = false,
    FixedPathsPassSnapshot = rows.TrueForAll(row => row.Protected), Paths = rows };
  }
 }
}
