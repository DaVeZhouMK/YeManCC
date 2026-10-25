using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading.Tasks;
internal sealed class ReplySeed {
 public string? Command="SUBMIT_FRAME",RequestId="receipt-fixture",RunId=new('a',32),TargetId="target",Owner="owner",Persona="steamdeck",Profile="steam-deck-composite",Nonce="nonce",Hash=new('f',64),HostId=new('b',32),Status="frame-submitted",Detail="submitted",EndpointDetail="unique-started",Release;
 public ulong Sequence=42,Epoch=12,Power=7,Revision=3;
 public long SubmitUs=27;
 public int Primary=1,Companion=0,Child=2,SdRoots=1,SdChild=2;
 public bool NullEnvelope,Ok=true,Asset=true,Reconciled=true,Admitted=true,Started=true,ReportSpecified;
 public bool? First=false;
}
internal static class ReplyLocalFixture {
 static long Checks,Sink;
 static void Check(bool p,string m){Checks++;if(!p)throw new Exception(m);}
 sealed record Prepared(ReplySeed Seed,object Session,object? Envelope);
 static Prepared Prepare(ReplySeed s)=>new(s,Program.FixtureReplySession(s),Program.FixtureReplyEnvelope(s));
 static string Call(Prepared p,int v)=>Program.FixtureReply(p.Session,p.Envelope,v,p.Seed.Ok,p.Seed.Status!,p.Seed.Detail!,p.Seed.First,p.Seed.Release);
 static string Outcome(Prepared p,int v){try{return "ok:"+Call(p,v);}catch(Exception e){return "error:"+e.GetType().FullName+":"+e.Message;}}
 static void Compare(ReplySeed seed,string label){
  var cases=new[]{Prepare(seed),Prepare(seed),Prepare(seed)};var before=Outcome(cases[0],0);
  for(int v=1;v<3;v++){Check(before==Outcome(cases[v],v),"response bytes/error changed "+label+" variant"+v);Check(Program.FixtureReplyFlag(cases[0].Session)==Program.FixtureReplyFlag(cases[v].Session),"response capability state changed "+label);}
  if(before.StartsWith("ok:",StringComparison.Ordinal)){
   using var d=JsonDocument.Parse(before[3..]);var fields=d.RootElement.EnumerateObject().Select(x=>x.Name).ToArray();
   Check(fields.Length==fields.Distinct(StringComparer.Ordinal).Count(),"duplicate reply property "+label);
   Check(d.RootElement.GetProperty("sequence").GetUInt64()==(seed.NullEnvelope?0:seed.Sequence),"sequence precision "+label);
   Check(d.RootElement.GetProperty("submitElapsedUs").GetInt64()==seed.SubmitUs,"submit precision "+label);
  }
 }
 static void Functional(){
  foreach(var command in new string?[]{null,"","SUBMIT_FRAME","HELLO","PREPARE_TARGET","RELEASE_TARGET","SHUTDOWN","HIDHIDE_QUERY_STATE","RECREATE_TARGET","STATUS"})
   foreach(var first in new bool?[]{null,false,true})foreach(var release in new string?[]{null,"","released","return-debt-retained"})foreach(bool ok in new[]{false,true})
    Compare(new ReplySeed{Command=command,First=first,Release=release,Ok=ok,Status=command=="HELLO"&&ok?"host-hello":"status",ReportSpecified=command=="PREPARE_TARGET"},"command/nullability");
  foreach(bool n in new[]{false,true})foreach(ulong u in new[]{0UL,1UL,9007199254740993UL,ulong.MaxValue})foreach(long l in new[]{long.MinValue,-1L,0L,1L,long.MaxValue})
   Compare(new ReplySeed{NullEnvelope=n,Sequence=u,Epoch=u,Power=u,Revision=u,SubmitUs=l,Primary=int.MaxValue,Companion=1,Child=int.MinValue},"integer");
  foreach(var text in new string?[]{null,"","plain","中文微星 G3E / é Ω 😀","\"\\\r\n\t\0<>+&'","\ud800","\udfff",new string('中',160),new string('x',16385)})
   Compare(new ReplySeed{RequestId=text,RunId=text,TargetId=text,Owner=text,Persona=text,Profile=text,Nonce=text,Hash=text,HostId=text,Status=text,Detail=text,EndpointDetail=text,Release=text},"string-boundary");
  for(int c=0;c<=ushort.MaxValue;c++)Compare(new ReplySeed{Detail=new string((char)c,1)},"UTF16:"+c);
  var rand=new Random(731104);for(int k=0;k<2048;k++){
   var chars=new char[rand.Next(0,180)];for(int j=0;j<chars.Length;j++)chars[j]=(char)rand.Next(65536);
   Compare(new ReplySeed{Detail=new string(chars),Sequence=((ulong)(uint)rand.Next()<<32)|(uint)rand.Next(),First=k%3==0?null:k%3==1,Ok=(k&1)==0},"random:"+k);
  }
  var prior=CultureInfo.CurrentCulture;
  foreach(var name in new[]{"en-US","tr-TR","zh-CN","ar-SA"}){CultureInfo.CurrentCulture=CultureInfo.GetCultureInfo(name);Compare(new ReplySeed{Sequence=ulong.MaxValue,SubmitUs=long.MinValue,Detail="culture I ı İ 中文"},name);}
  CultureInfo.CurrentCulture=prior;
  var concurrency=new string[4];Parallel.For(0,4,t=>{try{for(int k=0;k<256;k++){var p=Prepare(new ReplySeed{Sequence=(ulong)(t*1000+k),Detail="thread"+t});var a=Call(p,0);for(int v=1;v<3;v++)if(a!=Call(p,v))throw new Exception("concurrent response changed");}concurrency[t]="ok";}catch(Exception e){concurrency[t]=e.ToString();}});foreach(var s in concurrency)Check(s=="ok","thread "+s);
  var retained=Prepare(new ReplySeed{Detail="retained-original",Sequence=55});var old=Call(retained,2);for(int k=0;k<64;k++)Call(Prepare(new ReplySeed{Detail="later"+k}),2);Check(Call(retained,2)==old,"subsequent replies mutated prior data");
 }
 static double Median(IEnumerable<double> a){var v=a.OrderBy(x=>x).ToArray();return v[v.Length/2];}
 static object Benchmark(){
  var scenarios=new List<(string,ReplySeed[])>{
   ("frame-ascii",Enumerable.Range(0,8).Select(i=>new ReplySeed{Sequence=(ulong)i,SubmitUs=i+4}).ToArray()),
   ("frame-unicode",Enumerable.Range(0,8).Select(i=>new ReplySeed{Detail="微星 手柄提交 中文 é 😀"+i,Owner="用户",EndpointDetail="唯一设备"}).ToArray()),
   ("escaped-error",new[]{new ReplySeed{Ok=false,Status="frame-rejected",Detail="\"\\\r\n\0<&>' +"}}),
   ("null-envelope",new[]{new ReplySeed{NullEnvelope=true,First=null,Release=null}}),
   ("hello-suffix",new[]{new ReplySeed{Command="HELLO",Status="host-hello"}}),
   ("prepare-suffix",new[]{new ReplySeed{Command="PREPARE_TARGET",Status="target-prepared",ReportSpecified=true}}),
   ("release-reply",new[]{new ReplySeed{Command="RELEASE_TARGET",Status="target-released",Release="release-unproven",First=null}}),
  };
  var all=scenarios.SelectMany(x=>x.Item2).Select(Prepare).ToArray();var warm=Stopwatch.StartNew();int w=0;while(warm.ElapsedMilliseconds<1200){foreach(var p in all)Sink+=Call(p,w%3).Length;w++;}
  var results=new List<object>();foreach(var (name,raw) in scenarios){var items=raw.Select(Prepare).ToArray();warm.Restart();w=0;while(warm.ElapsedMilliseconds<400){foreach(var p in items)Sink+=Call(p,w%3).Length;w++;}
   var rounds=new double[3][] {new double[9],new double[9],new double[9]};const int n=2500;
   for(int r=0;r<9;r++)for(int position=0;position<3;position++){int v=(r+position)%3;var start=Stopwatch.GetTimestamp();for(int j=0;j<n;j++)Sink+=Call(items[j%items.Length],v).Length;rounds[v][r]=Stopwatch.GetElapsedTime(start).TotalNanoseconds/n;}
   var allocations=new double[3];for(int v=0;v<3;v++){var start=GC.GetAllocatedBytesForCurrentThread();for(int j=0;j<n;j++)Sink+=Call(items[j%items.Length],v).Length;allocations[v]=(GC.GetAllocatedBytesForCurrentThread()-start)/(double)n;}
   results.Add(new{name,variants=Enumerable.Range(0,3).Select(v=>new{variant=v,medianNs=Median(rounds[v]),roundNs=rounds[v],allocatedBytes=allocations[v]}).ToArray(),writerPairedFaster=Enumerable.Range(0,9).Count(r=>rounds[2][r]<rounds[0][r])});
  }
  return new{scenarios=results,globalWarmupMs=1200,perScenarioWarmupMs=400,rounds=9,iterationsPerRound=2500,variantNames=new[]{"actual-before-anonymous","record-reflection","record-explicit-writer"},sink=Sink};
 }
 public static int Main(string[] args){try{Functional();object? performance=args.Length>1&&args[1]=="benchmark"?Benchmark():null;var result=new{pass=true,checks=Checks,allUtf16Cases=65536,threadCases=1024,performance,framework=RuntimeInformation.FrameworkDescription,processCpuFieldProof=false,productOrDeviceExecuted=false};System.IO.Directory.CreateDirectory(args[0]);System.IO.File.WriteAllText(System.IO.Path.Combine(args[0],"result.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine("PASS "+Checks+" checks");return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
