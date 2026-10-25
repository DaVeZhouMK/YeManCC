using System;using System.IO;using System.Collections.Generic;using System.Web.Script.Serialization;
internal class CeResolutionTests {
 static int Main(string[] args){string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);var rows=new List<object>();var host=new MemoryScanHost();string settings=Path.Combine(root,"settings.json"),native=Path.Combine(root,"cheatengine-x86_64.exe");
 Action<string,string,string> check=(name,input,expected)=>{if(input==null){if(File.Exists(settings))File.Delete(settings);}else File.WriteAllText(settings,new JavaScriptSerializer().Serialize(new{cePath=input}));string actual=host.ResolveCE(settings);if(actual!=expected)throw new Exception(name+": "+actual);rows.Add(new{name=name,ok=true});};
 try {
 check("Missing GCM settings",null,"");check("Empty CE path","","");check("Relative executable rejected","cheatengine-x86_64.exe","");check("Missing directory",Path.Combine(root,"missing"),"");
 File.WriteAllBytes(native,new byte[0]);check("Configured directory resolves native x64 CE",root,native);check("Quoted native executable","\""+native+"\"",native);
 string launcher=Path.Combine(root,"Cheat Engine.exe");File.WriteAllBytes(launcher,new byte[0]);check("Launcher resolves native sibling",launcher,native);
 check("Explicit missing exe",Path.Combine(root,"absent.exe"),"");check("Non exe rejected",Path.Combine(root,"table.ct"),"");
 string vector=Path.Combine(root,"cheatengine-x86_64-SSE4-AVX2.exe");File.WriteAllBytes(vector,new byte[0]);File.Delete(native);check("SSE native directory fallback",root,vector);check("Native executable explicitly selected",vector,vector);
 Console.WriteLine(new JavaScriptSerializer().Serialize(new{ok=true,checks=rows.Count,cases=rows,ceOrGameStarted=false}));return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
