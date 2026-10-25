using System;using System.IO;using System.IO.MemoryMappedFiles;using System.Diagnostics;using System.Threading;using System.Web.Script.Serialization;
class Fixture {
 static void Json(string p,object v){string tmp=p+".tmp";File.WriteAllText(tmp,new JavaScriptSerializer().Serialize(v));if(File.Exists(p))File.Replace(tmp,p,null);else File.Move(tmp,p);}
 static void Main(string[] args){bool ui=args.Length==1;string root=args[0],file=ui?Path.Combine(root,"fixture-memory.bin"):args[0],ready=ui?Path.Combine(root,"fixture-ready.json"):args[1],stop=ui?Path.Combine(root,"fixture-stop"):args[2];
 if(ui){using(var created=File.Create(file))created.SetLength(1048576);}
 using(var data=new FileStream(file,FileMode.Open,FileAccess.ReadWrite,FileShare.ReadWrite))using(var map=MemoryMappedFile.CreateFromFile(data,null,1048576,MemoryMappedFileAccess.ReadWrite,null,HandleInheritability.None,true))using(var view=map.CreateViewAccessor()){
 if(ui){view.Write(0,5000);view.Write(4,5000);Json(ready,new{pid=Process.GetCurrentProcess().Id});}else File.WriteAllText(ready,Process.GetCurrentProcess().Id.ToString());
 long last=0;while(!File.Exists(stop)){
 if(ui){string control=Path.Combine(root,"fixture-control.json");if(File.Exists(control)){long stamp=File.GetLastWriteTimeUtc(control).Ticks;if(stamp!=last){try{var c=new JavaScriptSerializer().Deserialize<System.Collections.Generic.Dictionary<string,object>>(File.ReadAllText(control));if(c.ContainsKey("value"))view.Write(0,Convert.ToInt32(c["value"]));if(c.ContainsKey("other"))view.Write(4,Convert.ToInt32(c["other"]));last=stamp;}catch(IOException){}}}try{Json(Path.Combine(root,"fixture-values.json"),new{value=view.ReadInt32(0),other=view.ReadInt32(4)});}catch(IOException){}}
 view.Write(100,DateTime.UtcNow.Ticks);Thread.Sleep(20);
 }}}
}
