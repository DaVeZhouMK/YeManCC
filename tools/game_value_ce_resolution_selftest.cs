using System;using System.IO;using System.Collections.Generic;using System.Web.Script.Serialization;
internal class CeResolutionTests {
 static int Main(string[] args){string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);var rows=new List<object>();var host=new MemoryScanHost();string settings=Path.Combine(root,"settings.json"),native=Path.Combine(root,"cheatengine-x86_64.exe"),fallback=Path.Combine(root,"Default CE");
 Action<string,string,string> check=(name,input,expected)=>{if(input==null){if(File.Exists(settings))File.Delete(settings);}else File.WriteAllText(settings,new JavaScriptSerializer().Serialize(new{cePath=input}));string actual=host.ResolveCE(settings,fallback);if(actual!=expected)throw new Exception(name+": "+actual);rows.Add(new{name=name,ok=true});};
 try {
 check("Missing GCM settings",null,"");check("Empty CE path","","");check("Relative executable rejected","cheatengine-x86_64.exe","");check("Missing directory",Path.Combine(root,"missing"),"");
 File.WriteAllBytes(native,new byte[0]);check("Configured directory resolves native x64 CE",root,native);check("Quoted native executable","\""+native+"\"",native);
 string launcher=Path.Combine(root,"Cheat Engine.exe");File.WriteAllBytes(launcher,new byte[0]);check("Launcher resolves native sibling",launcher,native);
 check("Explicit missing exe",Path.Combine(root,"absent.exe"),"");check("Non exe rejected",Path.Combine(root,"table.ct"),"");
 string vector=Path.Combine(root,"cheatengine-x86_64-SSE4-AVX2.exe");File.WriteAllBytes(vector,new byte[0]);File.Delete(native);check("SSE native directory fallback",root,vector);check("Native executable explicitly selected",vector,vector);
 Directory.CreateDirectory(fallback);string defaultNative=Path.Combine(fallback,"cheatengine-x86_64.exe");File.WriteAllBytes(defaultNative,new byte[0]);
 check("Default CE works without any GCM settings",null,defaultNative);check("Default CE works with an empty GCM path","",defaultNative);check("Stale absolute GCM path repairs through default",Path.Combine(root,"old","gone.exe"),defaultNative);check("A valid custom CE selection retains priority",vector,vector);check("Explicit relative CE remains invalid","cheatengine-x86_64.exe","");
 File.WriteAllText(settings,"{broken");bool rejected=false;try{host.ResolveCE(settings,fallback);}catch{rejected=true;}if(!rejected||File.ReadAllText(settings)!="{broken")throw new Exception("Malformed GCM config was hidden or overwritten");rows.Add(new{name="Malformed settings preserved and reported",ok=true});
 string keyName=@"Software\YMCC-CeStartupFixture-"+Guid.NewGuid().ToString("N");
 try{using(var key=Microsoft.Win32.Registry.CurrentUser.CreateSubKey(keyName)){
  key.SetValue("Pause when scanning on by default",1);key.SetValue("ScanThreadpriority",2);key.SetValue("Language","keep");
  if(!MemoryScanHost.EnsureCeFirstRun(key)||!Object.Equals(key.GetValue("First Time User"),0))throw new Exception("First-run setup not initialized");
  if(MemoryScanHost.EnsureCeFirstRun(key))throw new Exception("Initialized flag rewritten");
  key.SetValue("First Time User",1);if(!MemoryScanHost.EnsureCeFirstRun(key))throw new Exception("Pending setup not initialized");
  if(!Object.Equals(key.GetValue("Pause when scanning on by default"),1)||!Object.Equals(key.GetValue("ScanThreadpriority"),2)||!Object.Equals(key.GetValue("Language"),"keep"))throw new Exception("CE preferences changed");
  key.SetValue("First Time User","unexpected");rejected=false;try{MemoryScanHost.EnsureCeFirstRun(key);}catch(InvalidDataException){rejected=true;}if(!rejected||!Object.Equals(key.GetValue("First Time User"),"unexpected"))throw new Exception("Unknown setup flag overwritten");
  rows.Add(new{name="First-run missing and pending flag initializes once and preserves preferences; malformed flag refused",ok=true});
 }}finally{Microsoft.Win32.Registry.CurrentUser.DeleteSubKeyTree(keyName);}
 Console.WriteLine(new JavaScriptSerializer().Serialize(new{ok=true,checks=rows.Count,cases=rows,ceOrGameStarted=false,realCeRegistryWritten=false}));return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
