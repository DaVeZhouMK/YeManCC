using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;

internal static class MaintenancePolicyTest {
 static bool Rejected(MethodInfo verify,params object[] input) {
  try { verify.Invoke(null,input); return false; }
  catch (TargetInvocationException e) { if (e.InnerException is InvalidDataException) return true; throw; }
 }

 static int Main(string[] args) {
  if (args.Length!=1 && args.Length!=2) return 2;
  var assembly=Assembly.LoadFile(Path.GetFullPath(args[0]));
  const BindingFlags flags=BindingFlags.Static|BindingFlags.NonPublic;
  var type=assembly.GetType("YeMan.Setup.MaintenanceUpdate",true);
  var verify=type.GetMethod("Verify",flags);
  var inventory=assembly.GetType("YeMan.Setup.MaintenanceRegistration",true).GetMethod("BundledPaths",flags);
  var files=((IEnumerable<string>)inventory.Invoke(null,null)).ToArray();
  if (files.Length<500 || !files.Contains(@"YeManCC\YeManCC.exe") || files.Contains(@"PowerControl\yeman-settings.json")) throw new Exception("Bad bundled inventory");
  string sidecar=(string)type.GetField("PackageEntry",flags).GetRawConstantValue();
  if (files.Any(path=>path.Replace((char)92,'/').Equals(sidecar,StringComparison.OrdinalIgnoreCase))) throw new Exception("Maintenance attachment is not an installed file");
  if (args.Length==2) CheckExtraction(assembly,sidecar,args[1]);
  var owns=assembly.GetType("YeMan.Setup.CoreRemoval",true).GetMethod("OwnsTaskAction",flags);
  Func<string,string,string,bool> action=(name,image,argv)=>(bool)owns.Invoke(null,new object[]{name,image,argv});
  const string app=@"C:\SOFT\YeMan\YeManCC\YeManCC.exe";
  const string desktop=@"C:\SOFT\YeMan\PowerControl\还原成非平板模式.vbs";
  if (!action("anything",app,"--minimized") || !action("桌面模式-开机设置为桌面模式",desktop,"") ||
      action("other",desktop,"") || action("桌面模式-开机设置为桌面模式",desktop,"other") ||
      action("监控-开机启动监控锁帧软件RTSS",@"C:\Program Files (x86)\RivaTuner Statistics Server\RTSS.exe","")) throw new Exception("Task ownership changed");
  var valueType=assembly.GetType("YeMan.Setup.MaintenanceValue",true);
  var value=Activator.CreateInstance(valueType);
  valueType.GetField("type").SetValue(value,"dword");
  var valid=assembly.GetType("YeMan.Setup.MaintenanceRollback",true).GetMethod("Valid",flags);
  if ((bool)valid.Invoke(null,new object[]{value,"dword",true})) throw new Exception("Missing DWORD accepted");
  valueType.GetField("number").SetValue(value,0L);
  if (!(bool)valid.Invoke(null,new object[]{value,"dword",true})) throw new Exception("Explicit zero rejected");
  using (var key=new RSACryptoServiceProvider(2048))
  using (var other=new RSACryptoServiceProvider(2048))
  using (var sha=SHA256.Create()) {
   key.PersistKeyInCsp=false;
   other.PersistKeyInCsp=false;
   var payload=new byte[] {65,66,67};
   var hash=BitConverter.ToString(sha.ComputeHash(payload)).Replace("-","");
   var json="{\"Schema\":1,\"Product\":\"YeManCC.Uninstaller\",\"Version\":\"0.0.44.0\",\"Sha256\":\""+hash+"\"}";
   var manifest=Encoding.UTF8.GetBytes(json);
   var signature=key.SignData(manifest,"SHA256");
   var anchor=key.ToXmlString(false);
   var release=verify.Invoke(null,new object[] {manifest,signature,payload,anchor});
   if ((string)release.GetType().GetField("Version").GetValue(release)!="0.0.44.0") throw new Exception("Signed fixture rejected");
   var bad=Encoding.UTF8.GetBytes(json.Replace("YeManCC.Uninstaller","Other.Product"));
   if (!Rejected(verify,manifest,signature,payload,other.ToXmlString(false)) ||
       !Rejected(verify,manifest,signature,new byte[] {65,66,68},anchor) ||
       !Rejected(verify,bad,signature,payload,anchor) ||
       !Rejected(verify,bad,key.SignData(bad,"SHA256"),payload,anchor)) throw new Exception("Invalid fixture accepted");
  }
  Console.WriteLine("PASS: {0} program paths, 5 task ownership cases, 2 DWORD cases, signed payload accepted, 4 tamper cases rejected; installation not run",files.Length);
  return 0;
 }

 static void CheckExtraction(Assembly assembly,string sidecar,string workspace) {
  if (!Directory.Exists(workspace)) throw new DirectoryNotFoundException(workspace);
  var parent=Path.Combine(workspace,"extract-"+Guid.NewGuid().ToString("N"));
  Directory.CreateDirectory(parent);
  var extract=assembly.GetType("YeMan.Setup.SafeFiles",true).GetMethod("Extract");
  Func<string,bool> allowed=n=>n.StartsWith("YeManCC/",StringComparison.OrdinalIgnoreCase);
  Func<string,bool> skip=n=>n.Equals(sidecar,StringComparison.OrdinalIgnoreCase);
  var names=new[] {sidecar,sidecar.ToUpperInvariant(),"YeManCC-Maintenance-Update.zip","YeManCC/../YeManCC-Maintenance-Update.zip"};
  for (int i=0;i<names.Length;i++) {
   string zip=Path.Combine(parent,"test-"+i+".zip"), output=Path.Combine(parent,"app-"+i);
   using (var archive=ZipFile.Open(zip,ZipArchiveMode.Create)) {
    foreach (var name in new[] {"YeManCC/version.json",names[i]}) {
     using (var stream=archive.CreateEntry(name).Open()) stream.WriteByte(42);
    }
   }
   var input=new object[]{zip,output,allowed,skip};
   if (i>1) {
    if (!Rejected(extract,input) || File.Exists(Path.Combine(output,"YeManCC","version.json"))) throw new Exception("Unsafe attachment passed ZIP validation");
   } else {
    extract.Invoke(null,input);
    var paths=Directory.GetFiles(output,"*",SearchOption.AllDirectories);
    if (paths.Length!=1 || Path.GetFileName(paths[0])!="version.json") throw new Exception("Attachment copied into program tree");
   }
  }
  Console.WriteLine("PASS: actual installer extractor skips attachment case-insensitively; rejects unsafe layout and parent traversal before writing");
 }
}
