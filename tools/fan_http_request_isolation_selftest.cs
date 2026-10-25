using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Diagnostics;
using System.Security.Cryptography;

static class Probe
{
 static Assembly assembly=null!;
 static FieldInfo? copyProbe, completedProbe;
 static long? CopyCount()=>copyProbe is null?null:(long)copyProbe.GetValue(null)!;
 static async Task HandlerCompleted(string? requestId)
 {
  if(completedProbe is null||requestId is null)return;
  var completed=completedProbe.GetValue(null)!;var timer=Stopwatch.StartNew();
  while(!(bool)Call(completed,"ContainsKey",requestId)!&&timer.ElapsedMilliseconds<3000)await Task.Delay(1);
  if(!(bool)Call(completed,"ContainsKey",requestId)!)throw new InvalidOperationException("instrumented handler did not finish");
 }
 static readonly BindingFlags Any=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
 static object? Call(object target,string name,params object?[] args)
 {
  var type=target as Type??target.GetType();
  var method=type.GetMethods(Any).First(m=>m.Name==name&&m.GetParameters().Length==args.Length);
  try{return method.Invoke(target is Type?null:target,args);}catch(TargetInvocationException ex)when(ex.InnerException is not null){System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(ex.InnerException).Throw();throw;}
 }
 static int checks;static readonly List<string> failures=[];static readonly List<object> timings=[];
 static void Check(bool condition,string name){checks++;if(!condition)failures.Add(name);Console.WriteLine((condition?"PASS ":"FAIL ")+name);}
 static long PathCount(string path)
 {
  var raw=Call(assembly.GetType("YeManFanHost.FanPerfCounters",true)!,"Snapshot");
  var j=JsonSerializer.SerializeToElement(raw);
  return j.GetProperty("paths").EnumerateArray().First(r=>r.GetProperty("path").GetString()==path).GetProperty("total").GetInt64();
 }
 static async Task Started(string path,long count)
 {
  var until=Stopwatch.StartNew();while(PathCount(path)<=count&&until.ElapsedMilliseconds<3000)await Task.Delay(10);
  Check(PathCount(path)>count,"partial body reached actual authenticated handler "+path);
 }
 static async Task<(TcpClient Client,NetworkStream Stream)> Partial(int port,string token,string route,int length,string prefix)
 {
  var client=new TcpClient();await client.ConnectAsync(IPAddress.Loopback,port);
  var stream=client.GetStream();var header=$"POST {route} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nX-YeMan-Fan-Session: {token}\r\nContent-Type: application/json\r\nContent-Length: {length}\r\nConnection: close\r\n\r\n{prefix}";
  await stream.WriteAsync(Encoding.ASCII.GetBytes(header));return(client,stream);
 }
 static async Task<string> ReadHeaders(NetworkStream stream)
 {
  var bytes=new List<byte>();var one=new byte[1];using var cts=new CancellationTokenSource(3000);
  while(bytes.Count<16384){int n=await stream.ReadAsync(one,cts.Token);if(n==0)break;bytes.Add(one[0]);int c=bytes.Count;if(c>=4&&bytes[c-4]==13&&bytes[c-3]==10&&bytes[c-2]==13&&bytes[c-1]==10)break;}
  return Encoding.ASCII.GetString(bytes.ToArray()).Split("\r\n")[0];
 }
 static async Task<JsonElement> Request(HttpClient http,string route,object? body=null,bool get=false)
 {
  long? beforeCopies=CopyCount();var timer=Stopwatch.StartNew();using var response=get?await http.GetAsync(route):await http.PostAsync(route,new StringContent(JsonSerializer.Serialize(body??new{}),Encoding.UTF8,"application/json"));
  string raw=await response.Content.ReadAsStringAsync();string? requestId=response.Headers.TryGetValues("X-YeMan-Fan-Request-Id",out var ids)?ids.FirstOrDefault():null;
  await HandlerCompleted(requestId);timings.Add(new{route,status=(int)response.StatusCode,elapsedMs=timer.Elapsed.TotalMilliseconds,jsonStateCopies=CopyCount()-beforeCopies});
  if(!response.IsSuccessStatusCode){using var error=JsonDocument.Parse(raw);throw new InvalidOperationException(route+" status="+(int)response.StatusCode+" code="+error.RootElement.GetProperty("error").GetProperty("code").GetString());}
  using var document=JsonDocument.Parse(raw);return document.RootElement.Clone();
 }
 public static async Task<int> Main(string[] args)
 {
  string path=Path.GetFullPath(args[0]),logroot=Path.GetFullPath(args[1]);Directory.CreateDirectory(logroot);
  AssemblyLoadContext.Default.Resolving+=(_,name)=>{string p=Path.Combine(Path.GetDirectoryName(path)!,name.Name+".dll");return File.Exists(p)?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;};
  assembly=AssemblyLoadContext.Default.LoadFromAssemblyPath(path);
  copyProbe=assembly.GetType("YeManFanHost.HostStateJsonCopy",true)!.GetField("CopyOperationsForProbe",Any);
  completedProbe=assembly.GetType("YeManFanHost.LocalFanHostServer",true)!.GetField("CompletedRequestsForProbe",Any);
  var optionsType=assembly.GetType("YeManFanHost.HostOptions",true)!;var engineType=assembly.GetType("YeManFanHost.FanHostEngine",true)!;var serverType=assembly.GetType("YeManFanHost.LocalFanHostServer",true)!;var logType=assembly.GetType("YeManFanHost.HostDiagnosticLog",true)!;
  var portProbe=new TcpListener(IPAddress.Loopback,0);portProbe.Start();int port=((IPEndPoint)portProbe.LocalEndpoint).Port;portProbe.Stop();string token=Convert.ToHexString(RandomNumberGenerator.GetBytes(32)).ToLowerInvariant();
  var options=Call(optionsType,"Parse",new object?[]{new[]{"--port",port.ToString(),"--parent-pid",Environment.ProcessId.ToString(),"--mock-handshake","--session-token",token}})!;
  var log=Activator.CreateInstance(logType,Any,null,[logroot,0],null)!;var engine=Activator.CreateInstance(engineType,Any,null,[options,log],null)!;var server=Activator.CreateInstance(serverType,Any,null,[engine,options,log],null)!;
  Task? run=null;TcpClient? first=null;TcpClient? pendingAtStop=null;var unobserved=new List<string>();
  EventHandler<UnobservedTaskExceptionEventArgs> observe=(_,e)=>{lock(unobserved)unobserved.Add("task="+_?.GetType().FullName+" "+e.Exception.ToString());e.SetObserved();};TaskScheduler.UnobservedTaskException+=observe;
  try
  {
   Check(engineType.GetField("realBackend",Any)!.GetValue(engine) is null,"actual engine has no real backend");
   run=(Task)Call(server,"RunAsync")!;
   using var http=new HttpClient(new HttpClientHandler{UseProxy=false}){BaseAddress=new Uri($"http://127.0.0.1:{port}"),Timeout=TimeSpan.FromSeconds(4)};http.DefaultRequestHeaders.Add("X-YeMan-Fan-Session",token);
   var health=await Request(http,"/health",get:true);Check(health.GetProperty("state").GetProperty("hostMode").GetString()=="safe-no-hardware","actual listener is safe mock");
   var portProbe2=new TcpListener(IPAddress.Loopback,0);portProbe2.Start();int closedPort=((IPEndPoint)portProbe2.LocalEndpoint).Port;portProbe2.Stop();
   using(var closedListener=new HttpListener())
   {
    closedListener.Prefixes.Add($"http://127.0.0.1:{closedPort}/");closedListener.Start();var accepting=closedListener.GetContextAsync();
    var closedPeer=await Partial(closedPort,token,"/api/cancel-control-intent",2,"{}");using(closedPeer.Client)
    {
     var closedContext=await accepting.WaitAsync(TimeSpan.FromSeconds(3));closedContext.Response.Abort();bool handled=true;
     try{await ((Task)Call(server,"HandleAsync",closedContext)!).WaitAsync(TimeSpan.FromSeconds(3));}catch(Exception ex){handled=false;Console.WriteLine("DETAIL closed-response "+ex.GetType().Name+":"+ex.Message);}
     Check(handled,"response already closed before dispatch settles without escaping request task");
    }
   }
   long before=PathCount("/api/enable");var partial=await Partial(port,token,"/api/enable",65536,"{");first=partial.Client;await Started("/api/enable",before);
   var state=await Request(http,"/api/state",get:true);Check(state.GetProperty("ok").GetBoolean(),"GET state reachable while authenticated body stays incomplete");
   await Request(http,"/api/open");await Request(http,"/api/open-events");var acquired=await Request(http,"/api/acquire-control");string lease=acquired.GetProperty("lease").GetProperty("leaseId").GetString()!;
   var nodes=new[]{new{tempC=0,dutyPercent=65},new{tempC=40,dutyPercent=65},new{tempC=70,dutyPercent=65},new{tempC=100,dutyPercent=65}};
   var enabled=await Request(http,"/api/enable",new{leaseId=lease,nodes});Check(enabled.GetProperty("ok").GetBoolean()&&enabled.GetProperty("state").GetProperty("state").GetString()=="Ready","manual Open Events Acquire Enable reachable while other body hangs");
   var preset=await Request(http,"/api/preset",new{name="game",leaseId=lease,nodes});Check(preset.GetProperty("state").GetProperty("activeCurve").GetArrayLength()==4,"preset keeps the explicit curve response");
   var named=await Request(http,"/api/preset",new{name="balanced",leaseId=lease});Check(named.GetProperty("state").GetProperty("activeCurve").EnumerateArray().Select(n=>n.GetProperty("dutyPercent").GetDouble()).SequenceEqual(new[]{0d,20d,45d,90d}),"named preset retains its original fallback nodes and current lease");
   var customNodes=new[]{new{tempC=0d,dutyPercent=37.5},new{tempC=40d,dutyPercent=45.25},new{tempC=70d,dutyPercent=68.75},new{tempC=100d,dutyPercent=93.5}};
   var custom=await Request(http,"/api/preset",new{name="caller-custom",leaseId=lease,nodes=customNodes});Check(custom.GetProperty("state").GetProperty("activeCurve").EnumerateArray().Select(n=>n.GetProperty("dutyPercent").GetDouble()).SequenceEqual(customNodes.Select(n=>n.dutyPercent)),"custom preset forwards exact fractional nodes even with no named fallback");
   foreach(var invalidLease in new string?[]{null,lease+"-foreign"})
   {
    using var rejected=await http.PostAsync("/api/preset",new StringContent(JsonSerializer.Serialize(new{name="balanced",leaseId=invalidLease}),Encoding.UTF8,"application/json"));using var rejection=JsonDocument.Parse(await rejected.Content.ReadAsStringAsync());
    if(rejected.Headers.TryGetValues("X-YeMan-Fan-Request-Id",out var requestIds))await HandlerCompleted(requestIds.FirstOrDefault());
    var current=await Request(http,"/api/state",get:true);Check((int)rejected.StatusCode==409&&rejection.RootElement.GetProperty("error").GetProperty("code").GetString()==(invalidLease is null?"LEASE_REQUIRED":"LEASE_INVALID")&&current.GetProperty("state").GetProperty("activeCurve").EnumerateArray().Select(n=>n.GetProperty("dutyPercent").GetDouble()).SequenceEqual(customNodes.Select(n=>n.dutyPercent)),"preset rejects missing/foreign lease without disturbing current curve: "+(invalidLease is null?"missing":"foreign"));
   }
   var cancel=await Request(http,"/api/cancel-control-intent");Check(cancel.GetProperty("controlIntentCancelled").GetBoolean()&&cancel.GetProperty("state").ValueKind==JsonValueKind.Object,"cancel-intent keeps its extra response field and state");
   await Request(http,"/api/enable",new{leaseId=lease,nodes});
   var restored=await Request(http,"/api/restore",new{leaseId=lease});Check(restored.GetProperty("state").ValueKind==JsonValueKind.Object,"restore preserves state response");
   // Restore keeps this caller's lease; a second Acquire is correctly refused.
   await Request(http,"/api/enable",new{leaseId=lease,nodes});
   await Request(http,"/api/heartbeat",new{leaseId=lease});Check(true,"current lease heartbeat reachable while other body hangs");
   var unauth=await Partial(port,"wrong","/api/enable",65536,"{");using(unauth.Client){string status=await ReadHeaders(unauth.Stream);Check(status.Contains("401"),"bad credentials rejected before incomplete body is read");}
   await partial.Stream.WriteAsync(Encoding.ASCII.GetBytes(new string(' ',65535)));string invalidStatus=await ReadHeaders(partial.Stream);Check(invalidStatus.Contains("400"),"eventually completed malformed body stays INVALID_JSON");first.Dispose();first=null;
   state=await Request(http,"/api/state",get:true);Check(state.GetProperty("state").GetProperty("activeCurve").EnumerateArray().All(n=>n.GetProperty("dutyPercent").GetDouble()==65),"failed concurrent body does not replace accepted manual curve");
   var off=await Request(http,"/api/disable",new{leaseId=lease});Check(off.GetProperty("ok").GetBoolean(),"explicit Disable remains reachable");
   await Request(http,"/api/release-control",new{leaseId=lease});
   await Request(http,"/api/open");await Request(http,"/api/open-events");acquired=await Request(http,"/api/acquire-control");lease=acquired.GetProperty("lease").GetProperty("leaseId").GetString()!;
   enabled=await Request(http,"/api/enable",new{leaseId=lease,nodes});Check(enabled.GetProperty("state").GetProperty("state").GetString()=="Ready","next explicit manual attempt works after nonterminal OFF/release");
   before=PathCount("/api/enable");partial=await Partial(port,token,"/api/enable",65536,"{");pendingAtStop=partial.Client;await Started("/api/enable",before);
   var close=await Request(http,"/api/close");Check(close.GetProperty("ok").GetBoolean(),"terminal Close reachable while second body is incomplete");
   using(var closedResponse=await http.PostAsync("/api/open",new StringContent("{}",Encoding.UTF8,"application/json")))
   { using var closedBody=JsonDocument.Parse(await closedResponse.Content.ReadAsStringAsync());Check((int)closedResponse.StatusCode==409&&closedBody.RootElement.GetProperty("error").GetProperty("code").GetString()=="HOST_CLOSING","closed Host rejects reuse until original bridge replaces its terminal session"); }
   var stopped=Stopwatch.StartNew();var shutdown=await Request(http,"/api/shutdown");Check(shutdown.GetProperty("shutdownRequested").GetBoolean(),"normal accepted shutdown retains its stop responsibility");await run.WaitAsync(TimeSpan.FromSeconds(4));Check(run.IsCompletedSuccessfully,"server RunAsync terminates with incomplete body still connected");timings.Add(new{route="Shutdown/RunAsync",elapsedMs=stopped.Elapsed.TotalMilliseconds});
   ((IDisposable)server).Dispose();server=null!;((IDisposable)engine).Dispose();engine=null!;pendingAtStop.Dispose();pendingAtStop=null;
   for(int i=0;i<3;i++){GC.Collect();GC.WaitForPendingFinalizers();await Task.Delay(60);}
   Check(unobserved.Count==0,"request shutdown leaves no unobserved task faults");
   // Cancel the waiter fully before closing the socket accept, so a missing
   // observer cannot pass merely because the two completions happened together.
   var acceptPortProbe=new TcpListener(IPAddress.Loopback,0);acceptPortProbe.Start();int acceptPort=((IPEndPoint)acceptPortProbe.LocalEndpoint).Port;acceptPortProbe.Stop();
   var acceptOptions=Call(optionsType,"Parse",new object?[]{new[]{"--port",acceptPort.ToString(),"--mock-handshake","--session-token",token}})!;
   var acceptEngine=Activator.CreateInstance(engineType,Any,null,[acceptOptions,log],null)!;
   var acceptServer=Activator.CreateInstance(serverType,Any,null,[acceptEngine,acceptOptions,log],null)!;
   try
   {
    var acceptCts=(CancellationTokenSource)serverType.GetField("stopCts",Any)!.GetValue(acceptServer)!;
    using var cancellationGap=acceptCts.Token.Register(()=>Thread.Sleep(60));
    var accepting=(Task)Call(acceptServer,"RunAsync")!;
    var stopOwner=Task.Run(()=>Call(acceptServer,"Stop"));
    await accepting.WaitAsync(TimeSpan.FromSeconds(4));await stopOwner.WaitAsync(TimeSpan.FromSeconds(4));
   }
   finally{((IDisposable)acceptServer).Dispose();((IDisposable)acceptEngine).Dispose();}
   for(int i=0;i<3;i++){GC.Collect();GC.WaitForPendingFinalizers();await Task.Delay(60);}
   Check(unobserved.Count==0,"fault of accept completed after waiter cancellation is observed");
   // Force the observed Cancel -> owner Dispose -> Stop continuation ordering
   // with a real idle listener; no parent watchdog or device session is started.
   var portProbe3=new TcpListener(IPAddress.Loopback,0);portProbe3.Start();int inlinePort=((IPEndPoint)portProbe3.LocalEndpoint).Port;portProbe3.Stop();
   var inlineOptions=Call(optionsType,"Parse",new object?[]{new[]{"--port",inlinePort.ToString(),"--mock-handshake","--session-token",token}})!;
   var inlineEngine=Activator.CreateInstance(engineType,Any,null,[inlineOptions,log],null)!;
   var inlineServer=Activator.CreateInstance(serverType,Any,null,[inlineEngine,inlineOptions,log],null)!;
   try
   {
    Call(inlineEngine,"Close");var inlineListener=(HttpListener)serverType.GetField("listener",Any)!.GetValue(inlineServer)!;inlineListener.Start();
    var inlineCts=(CancellationTokenSource)serverType.GetField("stopCts",Any)!.GetValue(inlineServer)!;
    using var inlineDispose=inlineCts.Token.Register(inlineListener.Close);bool settled=true;
    var finalizer=serverType.GetMethod("StopListenerAfterConfirmedEngineClose",Any)!;var disposition=Enum.Parse(finalizer.GetParameters()[0].ParameterType,"ConfirmedRelease");
    try{Call(inlineServer,"StopListenerAfterConfirmedEngineClose",disposition);}catch(ObjectDisposedException){settled=false;}
    Check(settled&&inlineCts.IsCancellationRequested,"confirmed stop tolerates owner disposal inline within cancellation");
   }
   finally{((IDisposable)inlineServer).Dispose();((IDisposable)inlineEngine).Dispose();}
   Check(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),"test never loads HC assembly");
  }
  catch(Exception ex){failures.Add(ex.GetType().Name+":"+ex.Message);Console.WriteLine("FAIL "+ex.GetType().Name+":"+ex.Message);}
  finally
  {
   first?.Dispose();pendingAtStop?.Dispose();if(server is not null){try{Call(server,"Stop");if(run is not null)await run.WaitAsync(TimeSpan.FromSeconds(4));}catch{}try{((IDisposable)server).Dispose();}catch{}}if(engine is not null)((IDisposable)engine).Dispose();if(log is IDisposable d)d.Dispose();TaskScheduler.UnobservedTaskException-=observe;
  }
  var result=new{ok=failures.Count==0,checks,failures,unobservedTasks=unobserved,timings,hostDllSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant(),hardwareCalls=0,scope="actual LocalFanHostServer+mock engine and loopback TCP; parent=self; no production Host process/global mutex/HC/device/native IPC/CPU benchmark"};Console.WriteLine(JsonSerializer.Serialize(result));return failures.Count==0?0:1;
 }
}
