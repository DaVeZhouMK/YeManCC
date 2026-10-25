using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
internal static class Utf8Calls
{
 internal static int Counts,Encodes;
 internal static void Reset(){Counts=Encodes=0;}
 internal static int GetByteCount(string s){++Counts;return Encoding.UTF8.GetByteCount(s);}
 internal static int GetBytes(string s,int start,int n,byte[] output,int offset){++Encodes;return Encoding.UTF8.GetBytes(s,start,n,output,offset);}
 internal static string GetString(byte[] data,int start,int n)=>Encoding.UTF8.GetString(data,start,n);
}
internal sealed class MemoryPipe:PipeStream
{
 internal int Calls,WriteLength;internal byte[]? Bytes;internal CancellationToken Token;
 internal bool Timed,Fail,Hold;internal long Sink;
 internal TaskCompletionSource<bool> Release=new(TaskCreationOptions.RunContinuationsAsynchronously);
 internal TaskCompletionSource<bool> Entered=new(TaskCreationOptions.RunContinuationsAsynchronously);
 internal MemoryPipe():base(PipeDirection.Out,0){}
 public override ValueTask WriteAsync(ReadOnlyMemory<byte> buffer,CancellationToken cancellationToken=default)
 {
  ++Calls;WriteLength=buffer.Length;Token=cancellationToken;
  if(Fail)throw new IOException("fixture-write-failed");
  if(cancellationToken.IsCancellationRequested)return ValueTask.FromCanceled(cancellationToken);
  if(Hold){Entered.TrySetResult(true);return new ValueTask(Wait(buffer,cancellationToken));}
  if(Timed){Sink+=buffer.Length+(buffer.IsEmpty?0:buffer.Span[0]+buffer.Span[^1]);return ValueTask.CompletedTask;}
  Bytes=buffer.ToArray();return ValueTask.CompletedTask;
 }
 private async Task Wait(ReadOnlyMemory<byte> data,CancellationToken token){await Release.Task.WaitAsync(token).ConfigureAwait(false);Bytes=data.ToArray();}
}
internal static class EncodeFixture
{
 static int checks;static long sink;
 static void Check(bool ok,string label){++checks;if(!ok)throw new InvalidOperationException(label);}
 static string ExceptionOf(Action action){try{action();return "ok";}catch(Exception e){return e.GetType().FullName+"|"+(e is ArgumentException a?a.ParamName:"")+"|"+e.Message;}}
 static void Pair(BaselineCodec a,CandidateCodec b,string? message,CancellationToken token=default,bool fail=false)
 {
  using var pa=new MemoryPipe{Fail=fail};using var pb=new MemoryPipe{Fail=fail};
  var x=ExceptionOf(()=>a.WriteAsync(pa,message!,token).GetAwaiter().GetResult());
  var y=ExceptionOf(()=>b.WriteAsync(pb,message!,token).GetAwaiter().GetResult());
  Check(x==y,"exception parity");Check(pa.Calls==pb.Calls&&pa.WriteLength==pb.WriteLength,"write count/length changed");
  Check(pa.Token==pb.Token,"token changed");Check((pa.Bytes??[]).SequenceEqual(pb.Bytes??[]),"wire bytes changed");
  Check(a.FixtureBuffer.SequenceEqual(b.FixtureBuffer),"buffer contents/capacity changed");
 }
 static string Ack(bool unicode)
 {
  var d=new Dictionary<string,object?>{{"protocolVersion",7},{"ok",true},{"command","SUBMIT_FRAME"},{"status","frame-accepted"},{"sequence",uint.MaxValue},{"epoch",23UL},{"runId","fixture-run-43c7a9a1"},{"requestId","fixture-frame-00023"},{"hostInstanceId","fixture-host-abcdef"},{"targetId","virtual-target"},{"persona","steamdeck"},{"ownerIdentity","fixture-parent"},{"nonce","fixture-nonce"},{"configHash",new string('a',64)},{"canonicalHash",new string('b',64)},{"firstFrame",false},{"virtualEndpointReconciled",true},{"targetPnpAdmitted",true},{"releaseDisposition",null},{"detail",unicode?"微星手柄-😀-已提交-fixture":"canonical-frame-submitted"}};
  return JsonSerializer.Serialize(d,new JsonSerializerOptions{Encoder=System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping});
 }
 static void Correctness()
 {
  var a=new BaselineCodec();var b=new CandidateCodec();
  foreach(var n in new[]{0,1,15,31,1364,1365,1366,4095,4096,4097,5460,5461,5462,16383,16384,16385}){
   Pair(a,b,new string('A',n));Pair(a,b,new string('中',n));Pair(a,b,new string('\ud800',n));
  }
  Pair(a,b,null);Pair(a,b,Ack(false));Pair(a,b,Ack(true));
  // All individual UTF-16 units, including each unpaired surrogate replacement.
  for(int ch=0;ch<=char.MaxValue;++ch){var s=new string((char)ch,1);Check(Encoding.UTF8.GetByteCount(s)<=3*s.Length,"UTF8 unit bound");Pair(a,b,s);}
  var random=new Random(20261015);
  for(int n=0;n<2000;++n){var chars=new char[random.Next(0,1800)];for(int j=0;j<chars.Length;++j)chars[j]=(char)random.Next(65536);var s=new string(chars);Check(Encoding.UTF8.GetByteCount(s)<=3L*s.Length,"UTF8 sequence bound");Pair(a,b,s);}
  foreach(var text in new[]{"\ud83d\ude00","\ud800\ud800","\udc00\ud800","\ud800A\udc00","x\0y\r\n\t\\\"",new string('中',5461)+"A",new string('中',5461)+"AA"})Pair(a,b,text);
  Pair(a,b,Ack(false),fail:true);using var canceled=new CancellationTokenSource();canceled.Cancel();Pair(a,b,Ack(true),canceled.Token);
  // A response remains buffered until the single awaited write finishes.
  var ah=new BaselineCodec();var bh=new CandidateCodec();using var ap=new MemoryPipe{Hold=true};using var bp=new MemoryPipe{Hold=true};
  var at=ah.WriteAsync(ap,Ack(false),default);var bt=bh.WriteAsync(bp,Ack(false),default);
  ap.Entered.Task.GetAwaiter().GetResult();bp.Entered.Task.GetAwaiter().GetResult();Check(!at.IsCompleted&&!bt.IsCompleted,"write no longer awaits");
  Check(ah.FixtureBuffer.SequenceEqual(bh.FixtureBuffer),"pending buffer mismatch");
  ap.Release.TrySetResult(true);bp.Release.TrySetResult(true);at.GetAwaiter().GetResult();bt.GetAwaiter().GetResult();Check(ap.Bytes!.SequenceEqual(bp.Bytes!),"pending data lifetime mismatch");
  // The counted clones prove the exact eligible and fallback branches; actual API classes time below.
  var ca=new CountedBaselineCodec();var cb=new CountedCandidateCodec();
  foreach(var text in new[]{"",Ack(false),new string('x',1365),new string('中',1365),new string('x',1366),new string('中',5462)}){
   var cap=cb.FixtureBuffer.Length;bool eligible=text.Length<=Math.Min(cap,GeneratedCodec.MaxMessageBytes)/3;
   using var pp=new MemoryPipe();Utf8Calls.Reset();var error=ExceptionOf(()=>ca.WriteAsync(pp,text,default).GetAwaiter().GetResult());int oldCounts=Utf8Calls.Counts;
   using var pq=new MemoryPipe();Utf8Calls.Reset();var error2=ExceptionOf(()=>cb.WriteAsync(pq,text,default).GetAwaiter().GetResult());
   Check(error==error2,"counted exceptions differ");Check(oldCounts==1,"baseline count not observed");Check(Utf8Calls.Counts==(eligible?0:1),"candidate count not tied to capacity proof");
   Check(ca.FixtureBuffer.Length==cb.FixtureBuffer.Length,"counted growth differs");
  }
 }
 record Scenario(string Name,string Payload,string Seed);
 record Measurement(double Ns,long Bytes);
 static readonly Scenario[] Scenarios={new("ASCII-frame-ACK",Ack(false),""),new("Unicode-frame-ACK",Ack(true),""),new("ASCII-initial-fast-bound",new string('x',1365),""),new("Unicode-grown-buffer",new string('中',2000),new string('x',6000))};
 static Measurement Measure(Scenario s,bool candidate,int loops,bool allocation)
 {
  using var pipe=new MemoryPipe{Timed=true};Func<PipeStream,string,CancellationToken,Task> writer;
  if(candidate){var codec=new CandidateCodec();writer=codec.WriteAsync;}else{var codec=new BaselineCodec();writer=codec.WriteAsync;}
  writer(pipe,s.Seed,default).GetAwaiter().GetResult();
  for(int n=0;n<32;++n)writer(pipe,s.Payload,default).GetAwaiter().GetResult();
  long startBytes=GC.GetAllocatedBytesForCurrentThread();long begin=Stopwatch.GetTimestamp();
  for(int n=0;n<loops;++n)writer(pipe,s.Payload,default).GetAwaiter().GetResult();
  double ns=Stopwatch.GetElapsedTime(begin).TotalNanoseconds/loops;long bytes=GC.GetAllocatedBytesForCurrentThread()-startBytes;sink+=pipe.Sink;
  return new(ns,bytes/loops);
 }
 static double Median(IEnumerable<double> x){var a=x.OrderBy(v=>v).ToArray();return a[a.Length/2];}
 static object Formal(Scenario s,int campaign,out bool pass)
 {
  var before=new List<double>();var after=new List<double>();var paired=new List<double>();int wins=0;
  for(int n=0;n<11;++n){double a,b;if(((n+campaign)&1)==0){a=Measure(s,false,24000,false).Ns;b=Measure(s,true,24000,false).Ns;}else{b=Measure(s,true,24000,false).Ns;a=Measure(s,false,24000,false).Ns;}before.Add(a);after.Add(b);paired.Add((a-b)/a);if(b<a)++wins;}
  var ba=Measure(s,false,24000,true);var ca=Measure(s,true,24000,true);var pm=Median(paired);var ratio=(Median(before)-Median(after))/Median(before);pass=wins>=10&&pm>=.05&&ratio>=.05&&ca.Bytes<=ba.Bytes;
  return new{name=s.Name,utf16Units=s.Payload.Length,utf8Bytes=Encoding.UTF8.GetByteCount(s.Payload),rounds=11,fasterRounds=wins,pairedMedianReduction=pm,ratioOfMediansReduction=ratio,beforeMedianNs=Median(before),afterMedianNs=Median(after),beforeAllocatedBytesPerWrite=ba.Bytes,afterAllocatedBytesPerWrite=ca.Bytes,allocationsNotHigher=ca.Bytes<=ba.Bytes,beforeSamplesNs=before,afterSamplesNs=after,pass};
 }
 static int Main(string[] args)
 {
  try{Correctness();if(args.Length==1&&args[0]=="verify"){Console.WriteLine(JsonSerializer.Serialize(new{pass=true,checks,utf16Units=65536,randomSequences=2000,actualEncoding=true,privatePipeStreamStandIn=true,actualOSPipe=false,productOrDeviceExecuted=false}));return 0;}
   Check(args.Length==2&&args[0]=="benchmark","invalid mode");int campaign=int.Parse(args[1]);
   // Fixed warmup, both orders, no adaptive repetition after formal timing.
   var warm=Stopwatch.StartNew();while(warm.ElapsedMilliseconds<1500)foreach(var s in Scenarios){Measure(s,false,128,false);Measure(s,true,128,false);}
   var rows=new List<object>();bool pass=true;foreach(var s in Scenarios){rows.Add(Formal(s,campaign,out var ok));pass&=ok;}
   Console.WriteLine(JsonSerializer.Serialize(new{pass,campaign,checks,rows,scope="Actual WriteAsync with synchronous memory-only PipeStream stand-in; no OS pipe/Host/whole-process claim",tieredCompilation=Environment.GetEnvironmentVariable("DOTNET_TieredCompilation")}));return pass?0:2;
  }catch(Exception e){Console.Error.WriteLine(e);return 1;}
 }
}
