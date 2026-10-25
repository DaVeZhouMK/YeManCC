using System.Diagnostics;
using System.Text.Json;
if(args.Length!=2)return 2;
string executable=Path.GetFullPath(args[0]),root=Path.GetFullPath(args[1]);Directory.CreateDirectory(root);
int checks=0;void Need(bool ok,string detail){if(!ok)throw new Exception(detail);checks++;}
Process Launch(params string[] arguments){var info=new ProcessStartInfo(executable){UseShellExecute=false,CreateNoWindow=true,RedirectStandardError=true};foreach(var argument in arguments)info.ArgumentList.Add(argument);return Process.Start(info)??throw new IOException("fixture did not start");}
using(var burst=Launch("burst")){
 using var cancel=new CancellationTokenSource();var read=ModulePayloadBootstrap.ReadDiagnostics(burst.StandardError,cancel.Token);
 Need(burst.WaitForExit(3000),"diagnostic burst did not complete");
 string captured=ModulePayloadBootstrap.FinishDiagnosticRead(read);
 Need(captured.Length==16_384&&captured.All(value=>value=='X'),"bounded capture did not retain only 16 KiB of 1 MiB error output");
}
foreach(bool cancellation in new[]{false,true}){
 string ready=Path.Combine(root,"child-"+cancellation+".ready"),release=ready+".release";
 using var process=Launch("retain",ready,release);using var cancel=new CancellationTokenSource();
 var read=ModulePayloadBootstrap.ReadDiagnostics(process.StandardError,cancel.Token);Process? child=null;
 try{
  Need(process.WaitForExit(2000),"parent process did not exit");
  var budget=Stopwatch.StartNew();while(!File.Exists(ready)&&budget.ElapsedMilliseconds<2000)Thread.Sleep(20);
  Need(File.Exists(ready),"pipe inheritor did not start");
  child=Process.GetProcessById(int.Parse(File.ReadAllText(ready)));Need(!child.HasExited&&!read.IsCompleted,"test did not reproduce inherited open diagnostic pipe");
  if(cancellation){cancel.Cancel();Need(read.Wait(1500),"reader cancellation failed to release open pipe");}
  else{
   budget.Restart();string detail=ModulePayloadBootstrap.FinishDiagnosticRead(read,150);
   Need(detail=="diagnostic pipe remained open"&&budget.ElapsedMilliseconds<1200,"helper exit was trapped by its descendant diagnostic pipe");
  }
 }finally{
  File.WriteAllText(release,"release");if(child is not null){if(!child.WaitForExit(2000)){child.Kill();child.WaitForExit(1000);}child.Dispose();}
 }
 Need(read.Wait(2000),"diagnostic task did not finish after controlled child release");
}
string json=JsonSerializer.Serialize(new{status="PASS",checks,hardwareIo=false,consumer="production ModulePayloadBootstrap diagnostics reader/drain",realProcessesAndInheritedPipes=true});
File.WriteAllText(Path.Combine(root,"result.json"),json);Console.WriteLine(json);return 0;
