extern alias Baseline;
extern alias Candidate;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using SonyReportBufferContracts;
using B = Baseline::SonyReportBufferTestApi;
using C = Candidate::SonyReportBufferTestApi;

internal static class SonyReportBufferFixture
{
    private static int checks;
    private static void Check(bool ok, string detail) { ++checks; if (!ok) throw new InvalidOperationException(detail); }
    private static void Equal(byte[] a, byte[] b, string detail) => Check(a.AsSpan().SequenceEqual(b), detail);
    private static readonly string[] Personas = { "dualshock4", "dualsense", "dualsense-edge" };
    private static readonly long[] Counters = { -1, 0, 1, 63, 64, 255, 256, 65535, 65536, 0xffffff, 0x1000000, long.MaxValue };
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    private static Exception Failure(Action action)
    {
        try { action(); } catch (Exception ex) { return ex; }
        throw new InvalidOperationException("Expected exception did not occur");
    }
    private static string Hash(int buttons, params float[] values)
    {
        string text = buttons.ToString(CultureInfo.InvariantCulture) + string.Concat(values.Select(v => "|" + unchecked((uint)BitConverter.SingleToInt32Bits(v)).ToString("X8", CultureInfo.InvariantCulture)));
        return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(text)));
    }
    internal static string Message(string persona, int sequence, string shape, int? seed = null)
    {
        int i = seed ?? sequence;
        bool stable = shape == "stable-zero";
        float lx = stable ? 0 : (i % 201 - 100) / 100f;
        float ly = stable ? 0 : ((i * 3) % 201 - 100) / 100f;
        float rx = stable ? 0 : ((i * 7) % 201 - 100) / 100f;
        float ry = stable ? 0 : ((i * 11) % 201 - 100) / 100f;
        float lt = stable ? 0 : i % 256 / 255f, rt = stable ? 0 : (255 - i % 256) / 255f;
        int buttons = stable ? 0 : (int)((uint)i * 7919U & 0xffffU);
        var d = new Dictionary<string, object?> {
            ["protocolVersion"]=1,["command"]="SUBMIT_FRAME",["requestId"]="sony-"+sequence,
            ["sequence"]=sequence,["runId"]="sony-buffer-run",["epoch"]=1,["powerGeneration"]=1,
            ["targetId"]="sony-buffer-target",["ownerIdentity"]="coordinator",["persona"]=persona,
            ["profileIdentity"]=persona=="dualshock4"?"dualshock-4-v2":persona,["startupNonce"]="sony-buffer-nonce",
            ["configRevision"]=1,["configHash"]=new string('a',64),["buttons"]=buttons,
            ["lx"]=lx,["ly"]=ly,["rx"]=rx,["ry"]=ry,["lt"]=lt,["rt"]=rt,
            ["sampleSequence"]=sequence,["monotonicTick"]=sequence,
            ["normalizedInputHash"]=Hash(buttons,lx,ly,rx,ry,lt,rt),["motionPairProofId"]="sony-proof"
        };
        if (!stable)
        {
            if ((i & 3) != 0) { d["ltRaw"] = i % 31 / 255f; d["rtRaw"] = (i % 31 + 1) / 255f; }
            d["backButtons"] = i % 16;
            d["screenButtons"] = i % (persona == "dualshock4" ? 2 : persona == "dualsense" ? 4 : 64);
            bool active = (i & 1) == 0;
            var left = new {active, click=active && (i&2)!=0, x=active?(i*139%32768):0, y=active?-(i*97%32768):0, pressure=active?i%32768:0};
            var right = new {active, click=active && (i&4)!=0, x=active?-(i*71%32768):0, y=active?(i*59%32768):0, pressure=active?(32767-i%32768):0};
            d["touchpads"] = new {left,right};
            d["raw"] = new {buttons, sThumbLX=(short)(i*17),sThumbLY=(short)(i*19),sThumbRX=(short)(i*23),sThumbRY=(short)(i*29),bLeftTrigger=i%256,bRightTrigger=(255-i%256)};
            d["imu"] = new { admitted=(i&4)==0, gyroDps=new {x=(double)(i%5000-2500),y=(double)(2500-i%5000),z=i*0.125}, accelG=new {x=(i%17-8)*0.5,y=i*0.125,z=1.0}, quaternion=new {w=0.5,x=-0.5,y=0.25,z=-0.25} };
        }
        return JsonSerializer.Serialize(d);
    }
    private static string ResponseWithoutClock(string response)
    {
        var node=JsonNode.Parse(response)!.AsObject();
        Check(node["ok"]!.GetValue<bool>(),"Expected actual Host Handle to accept");
        Check(node["status"]!.GetValue<string>()=="frame-accepted","Unexpected actual status");
        Check(node.Remove("submitElapsedUs"),"Expected timing field");
        return node.ToJsonString();
    }
    private static void Verify()
    {
        foreach (string persona in Personas)
        {
            using var b=B.Create(persona); using var c=C.Create(persona);
            Check(!b.IsCandidate && c.IsCandidate,"Variant identities");
            Check(c.SessionBuffer!.Length==63,"Session private buffer size");
            for (int i=0;i<256;i++) foreach(long counter in Counters)
            {
                string message=Message(persona,1,"motion-touch",i);
                var before=b.Fresh(message,counter); var fresh=c.Fresh(message,counter);
                Equal(before,fresh,persona+" wrapper oracle");
                var held=fresh.ToArray(); var next=c.Fresh(message,counter+1);
                Check(!ReferenceEquals(fresh,next),"Fresh wrapper reference contract");
                Equal(fresh,held,"Fresh wrapper prior storage retained");
                Array.Fill(c.SessionBuffer!, (byte)(i^0xa5)); c.Fill(message,counter,c.SessionBuffer);
                Equal(before,c.SessionBuffer!,"All 63 bytes / poisoned buffer");
                b.SubmitRaw(before); c.SubmitRaw(c.SessionBuffer!);
                Equal(b.ReadPayload(),c.ReadPayload(),"Real SDK post-default full payload");
                Equal(b.ReadSdkBuffer(),c.ReadSdkBuffer(),"Real SDK private buffer");
                var copied=c.ReadPayload(); var sdkCopy=c.ReadSdkBuffer();
                Check(!c.SdkDirectlyRetains(c.SessionBuffer!),"Actual SDK fields must not retain caller array");
                Array.Fill(c.SessionBuffer!, (byte)0x5a);
                Equal(copied,c.ReadPayload(),"Mutating borrowed payload after return must not affect output");
                Equal(sdkCopy,c.ReadSdkBuffer(),"Mutating payload must not affect SDK-owned copy");
                string zero=Message(persona,1,"stable-zero"); c.Fill(zero,0,c.SessionBuffer);
                Equal(b.Fresh(zero,0),c.SessionBuffer!,"Active to inactive / IMU and touch clearing");
            }
            string valid=Message(persona,1,"motion-touch",8);
            foreach (int n in new[]{0,1,62,64,128})
            {
                var buffer=Enumerable.Repeat((byte)0xa5,n).ToArray(); var old=buffer.ToArray();
                Check(Failure(()=>c.Fill(valid,0,buffer)) is ArgumentException,"Wrong borrowed length rejected");
                Equal(old,buffer,"Wrong-length buffer must be untouched");
            }
            Check(Failure(()=>c.Fill(valid,0,null)) is ArgumentNullException,"Null borrowed destination");
            foreach(int size in new[]{0,1,63,65,128})
            {
                var be=Failure(()=>b.Fresh(valid,0,size));var ce=Failure(()=>c.Fresh(valid,0,size));
                Check(be.GetType()==ce.GetType()&&be.Message==ce.Message,"Profile size exception parity");
                Check(Failure(()=>c.Fill(valid,0,new byte[62],size)) is InvalidOperationException,"Profile error priority before destination size");
            }
            foreach(int n in new[]{0,65,257})
            {
                uint seq=c.SdkSequence;var payload=c.ReadPayload();
                Check(Failure(()=>c.SubmitRaw(new byte[n])) is ArgumentException,"Real SDK wrong-length exception");
                Check(seq==c.SdkSequence,"SDK failed argument must not publish a new sequence");
                Equal(payload,c.ReadPayload(),"SDK failed argument must not publish payload");
            }
            c.MarkSdkDisposed(true);uint prior=c.SdkSequence;
            Check(Failure(()=>c.SubmitRaw(new byte[63])) is ObjectDisposedException,"Real SDK disposed guard");
            Check(c.SdkSequence==prior,"Disposed SDK no publish");c.MarkSdkDisposed(false);
            foreach(string invalid in new[]{"{}",valid.Replace("\"lx\":", "\"unknown\":1,\"lx\":"),Message(persona,1,"stable-zero").Replace("\"lx\":0","\"lx\":-0"),valid[..^1]})
            {
                bool ba=b.Parse(invalid,out var be),ca=c.Parse(invalid,out var ce);
                Check(ba==ca&&be==ce,"Actual parser error parity");
            }
            b.ResetForSample();c.ResetForSample();
            for(int i=1;i<=1024;i++)
            {
                string m=Message(persona,i,(i%3)==0?"stable-zero":"motion-touch");
                string br=b.Handle(m),cr=c.Handle(m);
                Check(ResponseWithoutClock(br)==ResponseWithoutClock(cr),"Full actual parse-build-SDK-response parity");
                Equal(b.ReadPayload(),c.ReadPayload(),"Full pipeline 63B sequence parity");
                Check(b.SdkSequence==c.SdkSequence&&b.SuccessfulSubmits==c.SuccessfulSubmits,"Production submit counters");
            }
            Array.Fill(c.SessionBuffer!,(byte)0xcc);b.SubmitNeutral();c.SubmitNeutral();
            Equal(b.ReadPayload(),c.ReadPayload(),"Actual neutral output parity and no stale bytes");
            using var c2=C.Create(persona);
            Check(!ReferenceEquals(c.SessionBuffer,c2.SessionBuffer),"Concurrent HostSession buffers must be distinct");
            var expected1=new byte[128][];var expected2=new byte[128][];
            var msgs1=new string[128];var msgs2=new string[128];
            for(int i=0;i<128;i++)
            {
                msgs1[i]=Message(persona,1,"motion-touch",i);msgs2[i]=Message(persona,1,"motion-touch",i+1024);
                b.SubmitRaw(b.Fresh(msgs1[i],i));expected1[i]=b.ReadPayload();
                b.SubmitRaw(b.Fresh(msgs2[i],i+256));expected2[i]=b.ReadPayload();
            }
            int parallelChecks=0;
            Task.WaitAll(Task.Run(()=>{for(int i=0;i<128;i++){c.ConcurrentFillAndSubmit(msgs1[i],i);if(!expected1[i].AsSpan().SequenceEqual(c.ReadPayload()))throw new InvalidOperationException("Concurrent session A all63 mismatch");Interlocked.Increment(ref parallelChecks);}}),
                Task.Run(()=>{for(int i=0;i<128;i++){c2.ConcurrentFillAndSubmit(msgs2[i],i+256);if(!expected2[i].AsSpan().SequenceEqual(c2.ReadPayload()))throw new InvalidOperationException("Concurrent session B all63 mismatch");Interlocked.Increment(ref parallelChecks);}}));
            Check(parallelChecks==256,"Concurrent per-session submission checks completed");
        }
    }
    [DllImport("kernel32.dll",SetLastError=true)] private static extern bool GetProcessTimes(IntPtr p,out long creation,out long exit,out long kernel,out long user);
    [DllImport("kernel32.dll",SetLastError=true)] private static extern bool QueryThreadCycleTime(IntPtr t,out ulong cycles);
    private static double CpuMs(){if(!GetProcessTimes(new IntPtr(-1),out _,out _,out long k,out long u))throw new System.ComponentModel.Win32Exception();return (k+u)/10000.0;}
    private static ulong Cycles(){if(!QueryThreadCycleTime(new IntPtr(-2),out var c))throw new System.ComponentModel.Win32Exception();return c;}
    private sealed record Sample(double ProcessCpuMs,ulong ThreadCycles,double ElapsedMs,long AllocatedBytes,int Checksum);
    private static Sample Measure(ISonyFixture fixture,string[] messages)
    {
        fixture.ResetForSample();
        // Seed the exact tuple outside measurement, then reset sequence/counters.
        fixture.Handle(messages[0]);fixture.ResetForSample();
        long allocation=GC.GetAllocatedBytesForCurrentThread();double cpu=CpuMs();ulong cycles=Cycles();long time=Stopwatch.GetTimestamp();int digest=0;
        string last="";foreach(string message in messages){last=fixture.Handle(message);digest=unchecked(digest+last.Length);}
        double elapsed=Stopwatch.GetElapsedTime(time).TotalMilliseconds;ulong cycleDelta=Cycles()-cycles;double cpuDelta=CpuMs()-cpu;long allocated=GC.GetAllocatedBytesForCurrentThread()-allocation;
        using var response=JsonDocument.Parse(last);
        Check(response.RootElement.GetProperty("ok").GetBoolean(),"Formal sample actual response");
        Check(fixture.SuccessfulSubmits==messages.Length&&fixture.SdkSequence==2U*(uint)messages.Length,"Formal sample executed every actual SDK submit");
        return new(cpuDelta,cycleDelta,elapsed,allocated,digest);
    }
    private static double Median(IEnumerable<double> values){var a=values.Order().ToArray();return a[a.Length/2];}
    private static object Metric((Sample B,Sample C)[] pairs,Func<Sample,double> value,double reduction,int faster)
    {
        bool valid=pairs.All(x=>value(x.B)>0&&value(x.C)>0);
        double paired=valid?Median(pairs.Select(x=>(value(x.B)-value(x.C))/value(x.B))):double.NaN;
        double ratio=valid?1-Median(pairs.Select(x=>value(x.C)))/Median(pairs.Select(x=>value(x.B))):double.NaN;
        int wins=pairs.Count(x=>value(x.C)<value(x.B));
        return new {valid,pairedMedianReduction=valid?(double?)paired:null,ratioOfMediansReduction=valid?(double?)ratio:null,fasterPairs=wins,pass=valid&&paired>=reduction&&ratio>=reduction&&wins>=faster};
    }
    private static bool Passed(object metric)=>(bool)metric.GetType().GetProperty("pass")!.GetValue(metric)!;
    private static object Benchmark(JsonElement plan,string output)
    {
        int calls=plan.GetProperty("callsPerSample").GetInt32(), rounds=plan.GetProperty("rounds").GetInt32(),warmups=plan.GetProperty("warmupPassesPerVariant").GetInt32();
        double reduction=plan.GetProperty("minimumCpuReduction").GetDouble(),maxElapsedRegression=plan.GetProperty("maximumElapsedRegression").GetDouble();int faster=plan.GetProperty("minimumFasterPairs").GetInt32();
        var rows=new List<object>();bool all=true;
        foreach(string persona in Personas)foreach(string shape in new[]{"stable-zero","motion-touch"})
        {
            var messages=Enumerable.Range(1,calls).Select(i=>Message(persona,i,shape)).ToArray();
            using var b=B.Create(persona);using var c=C.Create(persona);
            var warm=new List<object>();for(int i=0;i<warmups;i++){if((i&1)==0){warm.Add(new{side="B",value=Measure(b,messages)});warm.Add(new{side="C",value=Measure(c,messages)});}else{warm.Add(new{side="C",value=Measure(c,messages)});warm.Add(new{side="B",value=Measure(b,messages)});}}
            var pairs=new List<(Sample B,Sample C)>();var raw=new List<object>();
            for(int i=0;i<rounds;i++)
            {
                Sample bs,cs;if((i&1)==0){bs=Measure(b,messages);cs=Measure(c,messages);}else{cs=Measure(c,messages);bs=Measure(b,messages);}
                pairs.Add((bs,cs));raw.Add(new{round=i,order=(i&1)==0?"BC":"CB",baseline=bs,candidate=cs});
                File.WriteAllText(Path.Combine(output,persona+"-"+shape+"-raw.json"),JsonSerializer.Serialize(new{warmups=warm,pairs=raw},Json));
            }
            var cpu=Metric(pairs.ToArray(),s=>s.ProcessCpuMs,reduction,faster);var cycles=Metric(pairs.ToArray(),s=>s.ThreadCycles,reduction,faster);
            double elapsedPair=Median(pairs.Select(x=>(x.B.ElapsedMs-x.C.ElapsedMs)/x.B.ElapsedMs)),elapsedRatio=1-Median(pairs.Select(x=>x.C.ElapsedMs))/Median(pairs.Select(x=>x.B.ElapsedMs));
            bool allocation=pairs.All(x=>x.C.AllocatedBytes<=x.B.AllocatedBytes)&&Median(pairs.Select(x=>(double)x.C.AllocatedBytes))<Median(pairs.Select(x=>(double)x.B.AllocatedBytes));
            bool pass=Passed(cpu)&&Passed(cycles)&&allocation&&elapsedPair>=-maxElapsedRegression&&elapsedRatio>=-maxElapsedRegression;all&=pass;
            rows.Add(new{persona,shape,pass,cpu,cycles,allocationPass=allocation,elapsedPairReduction=elapsedPair,elapsedRatioReduction=elapsedRatio,warmups=warm,pairs=raw});
        }
        return new{performancePass=all,rows};
    }
    public static int Main(string[] args)
    {
        if(args.Length!=3 || args[0] is not("verify" or "benchmark"))throw new ArgumentException("verify|benchmark plan.json output-dir");
        string output=Path.GetFullPath(args[2]);Directory.CreateDirectory(output);
        try
        {
            Verify();object? benchmark=null;
            if(args[0]=="benchmark"){using var plan=JsonDocument.Parse(File.ReadAllText(args[1]));benchmark=Benchmark(plan.RootElement,output);}
            File.WriteAllText(Path.Combine(output,"result.json"),JsonSerializer.Serialize(new{pass=true,checks,benchmark,actualSdk=true,productConstructorsRun=false,deviceOrNamedMappingCalls=false},Json));return 0;
        }
        catch(Exception ex){File.WriteAllText(Path.Combine(output,"result.json"),JsonSerializer.Serialize(new{pass=false,checks,error=ex.ToString()},Json));return 1;}
    }
}
