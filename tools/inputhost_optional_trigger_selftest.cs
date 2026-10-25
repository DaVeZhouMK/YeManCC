using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Security.Cryptography;
using System.Text;

internal sealed record ParseOutcome(bool Accepted, string Error, string? Record, float Lt, float Rt, float LtRaw, float RtRaw, byte LeftWire, byte RightWire);
internal static class OptionalTriggerFixture
{
    private static int _checks, _groups; private static volatile float _sink;
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool QueryThreadCycleTime(IntPtr thread, out ulong cycles);
    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentThread();
    private static void Check(bool value, string name) { _checks++; if (!value) throw new Exception(name); }
    private static void Group(string name, Action test) { test(); _groups++; Console.WriteLine("PASS " + name); }
    private static JsonObject Frame(string persona="steamdeck", float lt=.25f, float rt=.75f)
    {
        var obj = new JsonObject { ["protocolVersion"]=1, ["command"]="SUBMIT_FRAME", ["requestId"]="fixture-request",
            ["sequence"]=2, ["runId"]="fixture-run", ["epoch"]=1, ["powerGeneration"]=1,
            ["targetId"]="fixture-target", ["ownerIdentity"]="fixture-owner", ["persona"]=persona,
            ["profileIdentity"]=persona=="steamdeck"?"steam-deck-composite":persona, ["startupNonce"]="fixture-startup",
            ["configRevision"]=1, ["configHash"]=new string('a',64), ["buttons"]=4096,
            ["lx"]=0f,["ly"]=.5f,["rx"]=-.25f,["ry"]=0f,["lt"]=lt,["rt"]=rt,
            ["sampleSequence"]=1, ["monotonicTick"]=1234, ["motionPairProofId"]="fixture-pair" };
        var canonical="4096|00000000|3F000000|BE800000|00000000|"+BitConverter.SingleToInt32Bits(lt).ToString("X8")+"|"+BitConverter.SingleToInt32Bits(rt).ToString("X8");
        obj["normalizedInputHash"]=Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(canonical)));
        return obj;
    }
    private static (ParseOutcome, ParseOutcome) Both(string message) => (BaselineParser.Parse(message), CandidateParser.Parse(message));
    private static void MissingProof()
    {
        var (a,b)=Both(Frame().ToJsonString());
        Check(a.Accepted&&b.Accepted,"both parsers accept valid legacy frame");
        Check(a.Lt==.25f&&a.Rt==.75f&&a.LtRaw==0f&&a.RtRaw==0f,"unmodified parser directly reproduces missing raw fallback bug");
        Check(b.LtRaw==.25f&&b.RtRaw==.75f,"candidate restores both canonical triggers when raw keys absent");
        Check(a.LeftWire==0&&a.RightWire==0&&b.LeftWire==64&&b.RightWire==191,"actual TriggerToByte consumes different legacy values");
    }
    private static void MissingMatrix()
    {
        foreach(var persona in new[]{"steamdeck","xbox360","elite","dualsense","dualsense-edge","dualshock4"})
        foreach(var left in new[]{0f,.1f,.25f,1f}) foreach(var right in new[]{0f,.5f,1f})
        foreach(var mask in new[]{0,1,2,3})
        {
            var frame=Frame(persona,left,right);
            if((mask&1)!=0)frame["ltRaw"]=.625f;
            if((mask&2)!=0)frame["rtRaw"]=.875f;
            var (a,b)=Both(frame.ToJsonString());Check(a.Accepted&&b.Accepted,"matrix accepted");
            Check(b.LtRaw==((mask&1)!=0?.625f:left)&&b.RtRaw==((mask&2)!=0?.875f:right),"independent missing-key fallback");
            Check(a.Lt==b.Lt&&a.Rt==b.Rt,"canonical values unchanged");
            if(mask==3)Check(a.Record==b.Record,"full modern parsed record unchanged");
        }
    }
    private static void PresentParity()
    {
        var scalars=new[]{"0","0.125","1","-1","1.25","-0","-0.0","true","false","null","\"0.5\"","[]","{}","1e999"};
        foreach(var l in scalars)foreach(var r in scalars)
        {
            var frame=Frame();frame["ltRaw"]=JsonNode.Parse(l);frame["rtRaw"]=JsonNode.Parse(r);
            var (a,b)=Both(frame.ToJsonString());Check(a==b,"all explicit present malformed/valid optional behavior preserved");
        }
        var zero=Frame();zero["ltRaw"]=0f;zero["rtRaw"]=0f;var(_,outcome)=Both(zero.ToJsonString());
        Check(outcome.Accepted&&outcome.LtRaw==0&&outcome.RtRaw==0,"explicit zero stays release despite canonical press");
    }
    private static void RejectionsAndExtensions()
    {
        var frame=Frame();frame["ltRaw"]=.5f;frame["rtRaw"]=.5f;var text=frame.ToJsonString();
        var cases=new List<string>{"", "null", "[]", "{", text.Replace("\"normalizedInputHash\":\"", "\"normalizedInputHash\":\"a"), text[..^1]+",\"buttons\":2}", text[..^1]+",\"notAllowed\":1}"};
        foreach(var message in cases){var(a,b)=Both(message);Check(a==b&&!a.Accepted,"rejection behavior unchanged");}
        foreach(var key in new[]{"buttons","lt","rt","sampleSequence","motionPairProofId","normalizedInputHash"})
        {var f=JsonNode.Parse(text)!.AsObject();f.Remove(key);var(a,b)=Both(f.ToJsonString());Check(a==b&&!a.Accepted,"required-field rejection unchanged");}
        var raw=JsonNode.Parse(text)!.AsObject();raw["raw"]=new JsonObject{["buttons"]=4096,["sThumbLX"]=123,["sThumbLY"]=-234,["sThumbRX"]=345,["sThumbRY"]=-456,["bLeftTrigger"]=12,["bRightTrigger"]=34};
        raw["imu"]=new JsonObject{["admitted"]=true,["gyroDps"]=new JsonObject{["x"]=1.0,["y"]=2.0,["z"]=3.0},["accelG"]=new JsonObject{["x"]=0.0,["y"]=0.0,["z"]=1.0},["quaternion"]=new JsonObject{["w"]=1.0,["x"]=0.0,["y"]=0.0,["z"]=0.0}};
        var(x,y)=Both(raw.ToJsonString());Check(x.Accepted&&x==y,"modern raw and IMU parsed record identical");
    }
    private sealed record Sample(double ElapsedNs,double Cycles,long AllocatedBytes);
    private static Sample Measure(Func<string,float> parse,string message,int n)
    {
        var alloc=GC.GetAllocatedBytesForCurrentThread();
        if(!QueryThreadCycleTime(GetCurrentThread(),out var c0))throw new Exception("cycles start unavailable");
        var start=Stopwatch.GetTimestamp();float sum=0;for(var i=0;i<n;i++)sum+=parse(message);
        var end=Stopwatch.GetTimestamp();if(!QueryThreadCycleTime(GetCurrentThread(),out var c1))throw new Exception("cycles end unavailable");
        var bytes=GC.GetAllocatedBytesForCurrentThread()-alloc;_sink=sum;
        return new((end-start)*1e9/Stopwatch.Frequency/n,(double)(c1-c0)/n,bytes);
    }
    private static double Median(IEnumerable<double> values){var a=values.Order().ToArray();return a[a.Length/2];}
    private static object Metric(List<Sample> a,List<Sample>b,Func<Sample,double> pick)
    {var x=a.Select(pick).ToArray();var y=b.Select(pick).ToArray();var paired=Median(x.Zip(y,(u,v)=>1-v/u));var ratio=1-Median(y)/Median(x);return new{baseline=x,candidate=y,pairedMedianReduction=paired,ratioOfMediansReduction=ratio,pass=paired>=-.05&&ratio>=-.05};}
    private static void Benchmark(string output)
    {
        var scenarios=new List<(string Name,string Text)>();
        foreach(var present in new[]{true,false})foreach(var extended in new[]{false,true})
        {var f=Frame();if(present){f["ltRaw"]=.125f;f["rtRaw"]=.875f;}if(extended){f["raw"]=new JsonObject{["buttons"]=4096,["bLeftTrigger"]=32,["bRightTrigger"]=224};f["imu"]=new JsonObject{["admitted"]=false};}scenarios.Add(((present?"present":"absent")+(extended?"-raw-imu":"-canonical"),f.ToJsonString()));}
        var rows=new List<object>();for(var campaign=0;campaign<2;campaign++)for(var s=0;s<scenarios.Count;s++)
        {
            var scenario=scenarios[s];for(var i=0;i<1000;i++){_sink=BaselineParser.ForCost(scenario.Text);_sink=CandidateParser.ForCost(scenario.Text);}
            var a=new List<Sample>();var b=new List<Sample>();for(var round=0;round<11;round++)
            {Sample x,y;if((round+campaign+s)%2==0){x=Measure(BaselineParser.ForCost,scenario.Text,4000);y=Measure(CandidateParser.ForCost,scenario.Text,4000);}else{y=Measure(CandidateParser.ForCost,scenario.Text,4000);x=Measure(BaselineParser.ForCost,scenario.Text,4000);}a.Add(x);b.Add(y);}
            rows.Add(new{campaign,scenario=scenario.Name,iterations=4000,elapsedNs=Metric(a,b,q=>q.ElapsedNs),threadCycles=Metric(a,b,q=>q.Cycles),baselineAllocatedBytes=a.Select(q=>q.AllocatedBytes),candidateAllocatedBytes=b.Select(q=>q.AllocatedBytes),allocationPass=a.Zip(b,(x,y)=>y.AllocatedBytes<=x.AllocatedBytes).All(x=>x)});
        }
        File.WriteAllText(output,JsonSerializer.Serialize(new{rows,executedFullParser=true,actualSdkOrProductsExecuted=false},new JsonSerializerOptions{WriteIndented=true}));
    }
    private static int Main(string[] args)
    {
        try
        {
            if(args.Length==2&&args[0]=="--benchmark"){Benchmark(args[1]);return 0;}
            Group("unmodified actual parser reproduces missing fallback",MissingProof);
            Group("six persona independent missing optional keys",MissingMatrix);
            Group("all explicitly present values preserve behavior",PresentParity);
            Group("required validation and modern extensions unchanged",RejectionsAndExtensions);
            var result=new{pass=true,groups=_groups,checks=_checks,actualProductionParserBodies=true,metadataAndScreenReadersActualSource=true,onlyReleaseFenceConstantStubbed=true,actualTriggerToByte=true,productExecuted=false,liveDevices=false,sdkAssembliesLoaded=AppDomain.CurrentDomain.GetAssemblies().Count(a=>a.GetName().Name?.StartsWith("HIDMaestro",StringComparison.Ordinal)==true)};
            if(args.Length==1)File.WriteAllText(args[0],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(result));return 0;
        }
        catch(Exception e){Console.Error.WriteLine(e);return 1;}
    }
}
