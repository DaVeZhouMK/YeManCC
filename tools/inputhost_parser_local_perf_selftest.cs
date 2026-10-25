using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Threading;

internal static class ParserLocalFixture
{
    private static long checks,sink;
    private static readonly JsonSerializerOptions JsonOptions=new(){Encoder=JavaScriptEncoder.UnsafeRelaxedJsonEscaping};
    private sealed record Scenario(string Name,string?[] Values);
    private sealed record Timing(double Ns,double Bytes,int Events);
    private static void Check(bool value,string text){checks++;if(!value)throw new InvalidOperationException(text);}
    // R5_FULL_PARSER_CORPUS_INSERTED

    private static string StringOutcome(bool proposed,IReadOnlyDictionary<string,JsonElement> properties,int max)
    {
        try{var ok=Program.FixtureString(proposed,properties,max,out var value);return Json(new{ok,value});}
        catch(Exception e){return Json(new{exception=e.GetType().FullName,message=e.Message});}
    }
    private static object StringCases()
    {
        int unicode=0,negativeControl=0,boundary=0;
        for(int code=0;code<65536;code++)
        {
            var text="{\"value\":\"A\\u"+code.ToString("x4",CultureInfo.InvariantCulture)+"Z\"}";
            using var doc=JsonDocument.Parse(text);var properties=new Dictionary<string,JsonElement>{{"value",doc.RootElement.GetProperty("value")}};
            Check(StringOutcome(false,properties,160)==StringOutcome(true,properties,160),"all UTF16/control/helper output parity");unicode++;
            if(!char.IsSurrogate((char)code)&&char.IsControl((char)code)&&code>=32)
            {Check(!Program.FixtureString(true,properties,160,out _),"nonASCII runtime control rejected");negativeControl++;}
        }
        foreach(var length in new[]{0,1,31,32,63,64,159,160,161,512})
        foreach(var limit in new[]{-1,0,1,32,64,96,160,161,512})
        foreach(var tail in new[]{"","\0","\u0085","\u009f","\u200b","\ud83d\ude80"})
        {
            using var doc=JsonDocument.Parse(Json(new{value=new string('x',length)+tail}));var properties=new Dictionary<string,JsonElement>{{"value",doc.RootElement.GetProperty("value")}};
            Check(StringOutcome(false,properties,limit)==StringOutcome(true,properties,limit),"helper boundary/shortcircuit/out value parity");boundary++;
        }
        foreach(var value in new[]{"null","true","false","0","[]","{}"})
        {using var doc=JsonDocument.Parse(value);var properties=new Dictionary<string,JsonElement>{{"value",doc.RootElement}};Check(StringOutcome(false,properties,160)==StringOutcome(true,properties,160),"helper wrong kind parity");}
        Check(StringOutcome(false,new Dictionary<string,JsonElement>(),160)==StringOutcome(true,new Dictionary<string,JsonElement>(),160),"helper missing parity");
        return new{UnicodeCases=unicode,BoundaryCases=boundary,AsciiOnlyNegativeControlWouldFail=negativeControl};
    }
    private static Dictionary<string,object?> Base(string command,int id=0)
    {
        var f=Frame(id);foreach(var key in new[]{"buttons","lx","ly","rx","ry","lt","rt","ltRaw","rtRaw","sampleSequence","monotonicTick","normalizedInputHash","motionPairProofId","backButtons","raw","imu","touchpads","screenButtons"})f.Remove(key);f["command"]=command;return f;
    }
    private static void CompareCase(string text,string label)
    {var expected=Program.FixtureOutcome(0,text);for(int variant=1;variant<4;variant++)Check(Program.FixtureOutcome(variant,text)==expected,"complete parser variant "+label);}
    private static object OptionalFieldsAndRecordCases()
    {
        int cases=0;
        foreach(var command in Program.FixtureCommands)
        foreach(var flags in Enumerable.Range(0,32))
        {
            var f=command=="SUBMIT_FRAME"?Frame(flags):Base(command,flags);
            if((flags&1)!=0)f["physicalBaseContainerInstanceId"]="HID\\VID_0DB0&PID_1902\\BASE";
            if((flags&2)!=0)f["physicalDeviceInstanceId"]="HID\\VID_0DB0&PID_1902\\DEVICE";
            if((flags&4)!=0)f["path"]=flags%2==0?"": "HID\\VID_TEST\\x";
            if((flags&8)!=0)f["on"]=(flags&1)!=0;
            if((flags&16)!=0)f["cloak"]=(flags&2)!=0;
            CompareCase(Json(f),command+" optional flags "+flags);cases++;
        }
        foreach(var name in new[]{"physicalBaseContainerInstanceId","physicalDeviceInstanceId"})
        foreach(var value in new object?[]{null,""," ","\0","x\u0085",new string('x',160),new string('x',161),123,true,new{nested=true}})
        foreach(var command in new[]{"HELLO","PREPARE_TARGET","SUBMIT_FRAME","HIDHIDE_QUERY"})
        {var f=command=="SUBMIT_FRAME"?Frame():Base(command);f[name]=value;CompareCase(Json(f),"physical validation "+name);cases++;}
        foreach(var name in new[]{"path","on","cloak"})foreach(var value in new object?[]{null,""," \u0085",true,false,0,1,new string('x',200),new{nested=true}})
        {var f=Base("HIDHIDE_CLOAK");f[name]=value;CompareCase(Json(f),"permissive HidHide field "+name);cases++;}
        foreach(var command in new[]{"PREPARE_TARGET","SUBMIT_FRAME"})foreach(var physical in new[]{false,true})
        {
            var f=command=="SUBMIT_FRAME"?Frame():Base(command);if(physical)f["physicalDeviceInstanceId"]="HID\\TEST";
            var raw=Json(f);foreach(var suffix in new[]{",\"msiReportChainSource\":{}",",\"msiReportChainSource\":{},\"msiReportChainSource\":{}",",\"msiReportChainDiagnostic\":{},\"msiReportChainDiagnostic\":{}",",\"msiSteamDeckReleaseFence\":true",",\"msiSteamDeckReleaseFence\":false"}){CompareCase(raw[..^1]+suffix+"}","metadata optional fields");cases++;}
        }
        foreach(var culture in new[]{"en-US","tr-TR","zh-CN","ar-SA"})
        {
            var prior=CultureInfo.CurrentCulture;try{CultureInfo.CurrentCulture=CultureInfo.GetCultureInfo(culture);CompareCase(Json(Frame(2)),"culture "+culture);CompareCase(Json(Base("HIDHIDE_CLOAK")),"culture command "+culture);cases+=2;}finally{CultureInfo.CurrentCulture=prior;}
        }
        var stableText=Json(Frame(77));for(int variant=0;variant<4;variant++)
        {
            var first=Program.FixtureEnvelope(variant,stableText);var prior=Program.FixtureOutcome(variant,stableText);var second=Program.FixtureEnvelope(variant,stableText);Check(first is not null&&second is not null&&!ReferenceEquals(first,second),"successful parse remains fresh envelope");
            CompareCase(Json(Frame(78)),"subsequent frame ownership");Check(Program.FixtureOutcome(variant,stableText)==prior,"subsequent frame changed old parse fields");
        }
        return new{Cases=cases,PhysicalAllowedForFrame=true,HidHideFalseAndAbsentDefaultsPreserved=true,AllVariantsCompared=4};
    }
    private static object ParallelCases()
    {
        var failures=new Exception?[4];var threads=new Thread[4];for(int i=0;i<4;i++){var id=i;threads[i]=new Thread(()=>{try{for(int j=0;j<256;j++){var f=Frame(j);if(j%3==0)f["physicalDeviceInstanceId"]="device-"+id;if(j%7==0)f["runId"]="\u0085";var text=Json(f);var expected=Program.FixtureOutcome(0,text);for(int variant=1;variant<4;variant++)if(Program.FixtureOutcome(variant,text)!=expected)throw new Exception("parallel exact parser differs");}}catch(Exception e){failures[id]=e;}});threads[i].Start();}
        foreach(var t in threads)Check(t.Join(10000),"parallel parser finished");foreach(var failure in failures)Check(failure is null,"parallel parser parity");return new{Threads=4,InputCases=4*256,VariantComparisons=4*256*3};
    }
    [MethodImpl(MethodImplOptions.NoInlining)]private static long Loop(Scenario scenario,int repeats,int variant)
    {long count=0;for(int r=0;r<repeats;r++)foreach(var text in scenario.Values){try{if(Program.FixtureParse(variant,text))count++;}catch(ArgumentException){count--;}}sink+=count;return count;}
    private static Timing Measure(Scenario scenario,int repeats,int variant,bool allocations)
    {var alloc=allocations?GC.GetAllocatedBytesForCurrentThread():0;var start=Stopwatch.GetTimestamp();Loop(scenario,repeats,variant);var stop=Stopwatch.GetTimestamp();var bytes=allocations?GC.GetAllocatedBytesForCurrentThread()-alloc:0;var events=scenario.Values.Length*repeats;return new((stop-start)*1e9/Stopwatch.Frequency/events,(double)bytes/events,events);}
    private static double Median(List<double> values){values.Sort();return values[values.Count/2];}
    private static string NativeFrame(int id)
    {
        var frame=Frame(id);var ordered=new Dictionary<string,object?>();foreach(var key in Program.FixtureNativeOrder)if(frame.TryGetValue(key,out var value))ordered[key]=value;Check(ordered.Count==frame.Count,"Actual Native order covers prepared frame");return Json(ordered);
    }
    private static object Costs()
    {
        string?[] Values(Func<int,string?>make)=>Enumerable.Range(0,32).Select(make).ToArray();
        var scenarios=new List<Scenario>{new("actual-native-key-order-frame",Values(NativeFrame)),new("legacy-corpus-order-ASCII-frame",Values(i=>Json(Frame(i)))),new("unicode-frame",Values(i=>Json(Frame(i,"\u4e2d\u00e9\ud83d\ude80")))),new("physical-identity-frame",Values(i=>{var f=Frame(i);f["physicalBaseContainerInstanceId"]="base-"+i;f["physicalDeviceInstanceId"]="device-"+i;return Json(f);})),new("HIDHIDE-with-fields",Values(i=>{var f=Base("HIDHIDE_CLOAK",i);f["path"]="HID\\PATH-"+i;f["on"]=true;f["cloak"]=false;return Json(f);})),new("nonframe-no-optional",Values(i=>Json(Base("QUIESCE",i)))),new("invalid-control-runId",Values(i=>{var f=Frame(i);f["runId"]="run\u0085";return Json(f);})),new("invalid-length-request",Values(i=>{var f=Frame(i);f["requestId"]=new string('x',161);return Json(f);})),new("malformed-and-early-rejection",Values(i=>i%2==0?"{":"{\"command\":false}"))};
        // Initial all-variant warmup precedes every recorded scenario. It lets
        // queued tiered compilation finish after the large functional suite.
        var warmStart=Stopwatch.GetTimestamp();do{foreach(var scenario in scenarios.Take(2))for(int variant=0;variant<4;variant++)Loop(scenario,8,variant);}while(Stopwatch.GetElapsedTime(warmStart).TotalMilliseconds<1200);
        var rows=new List<object>();foreach(var scenario in scenarios)
        {
            var start=Stopwatch.GetTimestamp();do{for(int variant=0;variant<4;variant++)Loop(scenario,8,variant);}while(Stopwatch.GetElapsedTime(start).TotalMilliseconds<400);
            var pilot=Measure(scenario,8,0,false);var repeats=Math.Clamp((int)(12_000_000/Math.Max(pilot.Ns,1)/scenario.Values.Length),4,100000);var timings=Enumerable.Range(0,4).Select(_=>new List<double>()).ToArray();
            for(int round=0;round<9;round++)for(int offset=0;offset<4;offset++){var variant=(round&1)==0?(offset+round)%4:(3-offset+round)%4;timings[variant].Add(Measure(scenario,repeats,variant,false).Ns);}
            var variants=new List<object>();for(int variant=0;variant<4;variant++){var allocations=Measure(scenario,16,variant,true);variants.Add(new{Variant=variant,MedianNs=Median(timings[variant]),RoundNs=timings[variant],AllocatedBytes=allocations.Bytes});}
            rows.Add(new{scenario.Name,Rounds=9,EventsPerRound=scenario.Values.Length*repeats,Variants=variants});
        }return rows;
    }
    private static int Main(string[] args)
    {
        try{var dir=args[0];var prior=ParserCases(dir);var strings=StringCases();var optional=OptionalFieldsAndRecordCases();var parallel=ParallelCases();var functional=checks;var benchmark=args.Length>1&&args[1]=="benchmark";var costs=benchmark?Costs():null;
            var result=new{pass=true,checks,functionalChecks=functional,r5CorpusCases=((dynamic)prior).Cases,PriorCorpus=prior,StringCases=strings,OptionalFields=optional,Parallel=parallel,Costs=costs,Runtime=RuntimeInformation.FrameworkDescription,ExecutionScope="Copied actual Host parser/string helper and fingerprint only. No original Host Main, device, SDK, driver, pipe or lifecycle methods.",CostScope="Nine rotating variant orders;400ms active warmup per scenario. Inputs prepared outside timing, allocation sampled separately. Full parser cost, not whole-process CPU.",sink};
            File.WriteAllText(Path.Combine(dir,"result.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));return 0;
        }catch(Exception e){Console.Error.WriteLine(e);return 3;}
    }
}
