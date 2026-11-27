using Microsoft.Win32;
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Text.RegularExpressions;

namespace YeMan.Setup {
 public sealed class MaintenanceRelease {
  public string Product, Version, Sha256;
  public int Schema;
 }

 internal static class MaintenanceUpdate {
  const string TrustResource = "maintenance.release-key";
  internal const string PackageEntry = "YeManCC/YeManCC-Maintenance-Update.zip";
  internal const string UpdaterName = "YeManCC-Maintenance-Update.exe";
  internal static bool HasReleaseKey { get { return Array.IndexOf(Assembly.GetExecutingAssembly().GetManifestResourceNames(),TrustResource)>=0; } }
  static string UpdaterPath { get { return Path.Combine(MaintenanceRegistration.Bin,UpdaterName); } }

  static byte[] ReadLimited(Stream stream,int limit) {
   if (stream == null) throw new InvalidDataException("Missing maintenance input");
   using (var output = new MemoryStream()) {
    var buffer = new byte[16384];
    int read;
    while ((read=stream.Read(buffer,0,buffer.Length))!=0) {
     if (output.Length+read>limit) throw new InvalidDataException("Oversize maintenance input");
     output.Write(buffer,0,read);
    }
    return output.ToArray();
   }
  }

  static byte[] ReadEntry(ZipArchive package,string name,int limit) {
   var entry = package.GetEntry(name);
   if (entry==null || entry.Length<1 || entry.Length>limit) throw new InvalidDataException("Invalid maintenance bundle entry");
   using (var stream = entry.Open()) return ReadLimited(stream,limit);
  }

  internal static MaintenanceRelease Verify(byte[] manifest,byte[] signature,byte[] payload,string publicKey) {
   using (var key = new RSACryptoServiceProvider()) {
    key.PersistKeyInCsp = false;
    key.FromXmlString(publicKey);
    if (!key.PublicOnly || key.KeySize<2048 || !key.VerifyData(manifest,"SHA256",signature)) throw new InvalidDataException("Maintenance publisher verification failed");
   }
   var release = Json.Read<MaintenanceRelease>(new UTF8Encoding(false,true).GetString(manifest));
   Version version;
   if (release==null || release.Schema!=1 || release.Product!="YeManCC.Uninstaller" || !Version.TryParse(release.Version,out version) ||
       version.Revision<0 || release.Sha256==null || !Regex.IsMatch(release.Sha256,@"\A[0-9a-fA-F]{64}\z")) throw new InvalidDataException("Invalid maintenance release");
   using (var sha=SHA256.Create()) if (!String.Equals(BitConverter.ToString(sha.ComputeHash(payload)).Replace("-",""),release.Sha256,StringComparison.OrdinalIgnoreCase))
    throw new InvalidDataException("Maintenance payload mismatch");
   return release;
  }

  internal static Version FileVersion(string path) {
   var info=FileVersionInfo.GetVersionInfo(path);
   return new Version(info.FileMajorPart,info.FileMinorPart,info.FileBuildPart,info.FilePrivatePart);
  }

  internal static int Refresh(string bundle) {
   if (!Native.IsAdmin || !HasReleaseKey || !String.Equals(Assembly.GetExecutingAssembly().Location,UpdaterPath,StringComparison.OrdinalIgnoreCase)) return 2;
   try {
    using (var mutex = new Mutex(false,MaintenanceRegistration.MutexName)) {
     bool locked;
     try { locked=mutex.WaitOne(0); } catch (AbandonedMutexException) { locked=true; }
     if (!locked) return 3;
     try { Apply(bundle); return 0; } finally { mutex.ReleaseMutex(); }
    }
   } catch { return 1; }
  }

  static void Apply(string bundle) {
   bundle=SafeFiles.Full(bundle);
   if (new DriveInfo(Path.GetPathRoot(bundle)).DriveType!=DriveType.Fixed) throw new InvalidDataException("Not a local maintenance bundle");
   SafeFiles.NoLinks(bundle);
   WorkSecurity.AssertDirectory(MaintenanceRegistration.Bin);
   WorkSecurity.AssertFile(UpdaterPath);
   string target=MaintenanceRegistration.Exe;
   WorkSecurity.AssertFile(target);
   using (var registry=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64))
   using (var entry=registry.OpenSubKey(MaintenanceRegistration.Entry,false))
    if (entry==null || (entry.GetValue("UninstallString") as string)!=("\""+target+"\"")) throw new InvalidDataException("Maintenance is not registered");
   byte[] manifest,signature,payload;
   using (var input=new FileStream(bundle,FileMode.Open,FileAccess.Read,FileShare.Read)) {
    if (input.Length>9*1024*1024) throw new InvalidDataException("Maintenance bundle too large");
    using (var package=new ZipArchive(input,ZipArchiveMode.Read)) {
     if (package.Entries.Count!=3) throw new InvalidDataException("Invalid maintenance bundle layout");
     manifest=ReadEntry(package,"release.json",4096);
     signature=ReadEntry(package,"release.sig",2048);
     payload=ReadEntry(package,"YeManCC-Uninstall.exe",8*1024*1024);
    }
   }
   string publicKey;
   using (var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream(TrustResource)) publicKey=Encoding.UTF8.GetString(ReadLimited(stream,8192));
   var release=Verify(manifest,signature,payload,publicKey);
   var next=Version.Parse(release.Version);
   var current=FileVersion(target);
   if (next==current) return;
   if (next<current) throw new InvalidDataException("Maintenance downgrade rejected");
   var candidate=Path.Combine(MaintenanceRegistration.Bin,"refresh-"+Guid.NewGuid().ToString("N")+".tmp");
   var backup=candidate+".previous";
   try {
    using (var f=new FileStream(candidate,FileMode.CreateNew,FileAccess.Write,FileShare.None)) { f.Write(payload,0,payload.Length); f.Flush(true); }
    WorkSecurity.SealCore(candidate,true);
    WorkSecurity.AssertFile(candidate);
    if (FileVersion(candidate)!=next) throw new InvalidDataException("Signed maintenance version differs from PE version");
    try { File.Replace(candidate,target,backup); }
    catch {
     if (!File.Exists(target) && File.Exists(backup)) File.Move(backup,target);
     throw;
    }
    if (File.Exists(backup)) try { File.Delete(backup); } catch (IOException) { } catch (UnauthorizedAccessException) { }
   } finally { if (File.Exists(candidate)) File.Delete(candidate); }
  }
 }
}
