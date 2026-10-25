// Candidate-only R6 fixture. Compiled with an unchanged copy of all current Host .cs files.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Threading;

internal static class PropertyNameFixture
{
    private static long checks,sink;
    private static readonly JsonSerializerOptions JsonOptions=new(){Encoder=JavaScriptEncoder.UnsafeRelaxedJsonEscaping};
    private sealed record Scenario(string Name,string?[] Values,bool Cold=false);
    private sealed record Timing(double Ns,double Bytes,int Events);
    private static void Check(bool value,string text){Interlocked.Increment(ref checks);if(!value)throw new InvalidOperationException(text);}
    // R5_CORPUS_INSERT
    // NATIVE_ORDER_INSERT
    private static string NativeFrame(int id=0,string unicode="",bool touch=true)
    {
        var f=Frame(id,unicode,touch);var ordered=new Dictionary<string,object?>();
        foreach(var key in NativeOrder)if(f.TryGetValue(key,out var value))ordered[key]=value;
        Check(ordered.Count==f.Count,"Actual native key order does not cover frame");return Json(ordered);
    }
    private static string Reorder(string message,int variant)
    {
        using var doc=JsonDocument.Parse(message);var fields=doc.RootElement.EnumerateObject().ToArray();
        if(variant<0)Array.Reverse(fields);
        else if(variant>0){var rnd=new Random(variant);for(int i=fields.Length-1;i>0;i--){int j=rnd.Next(i+1);(fields[i],fields[j])=(fields[j],fields[i]);}}
        return "{"+string.Join(",",fields.Select(p=>Json(p.Name)+":"+p.Value.GetRawText()))+"}";
    }
    private static string EscapedName(string name)=>"\""+string.Concat(name.Select(c=>"\\u"+((int)c).ToString("x4",CultureInfo.InvariantCulture)))+"\"";
    private static string EscapeKeys(string message)
    {
        using var doc=JsonDocument.Parse(message);
        return "{"+string.Join(",",doc.RootElement.EnumerateObject().Select(p=>EscapedName(p.Name)+":"+p.Value.GetRawText()))+"}";
    }
    private static string BaseCommand(int id,string command)
    {
        var f=Frame(id);foreach(var key in new[]{"buttons","lx","ly","rx","ry","lt","rt","ltRaw","rtRaw","sampleSequence","monotonicTick","normalizedInputHash","motionPairProofId","backButtons","raw","imu","touchpads"})f.Remove(key);
        f["command"]=command;if(command.StartsWith("HIDHIDE_",StringComparison.Ordinal)){f["path"]="HID\\fixture";f["on"]=true;f["cloak"]=false;}
        return Json(f);
    }
    private static string ManyProperties(int repeats,bool unknownTail=false)
    {
        string frame=NativeFrame();return frame[..^1]+string.Concat(Enumerable.Repeat(",\"msiReportChainSource\":{}",repeats))+(unknownTail?",\"unknown-overflow-tail\":0":"")+"}";
    }
    private static string Digest(string value)=>Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(value)));
    private static object Sequences(string dir)
    {
        int cases=0,accepted=0,rejected=0;var categories=new Dictionary<string,int>();
        using var evidence=new StreamWriter(Path.Combine(dir,"r6-cases.jsonl"),false,new UTF8Encoding(false));
        void Case(string label,string? message,string? expected=null,bool? accept=null)
        {
            if(label.StartsWith("known-key-",StringComparison.Ordinal)||label=="unknown-name")Program.FixtureForceProbe();
            string before=Program.FixtureOutcome(false,message),candidate=Program.FixtureOutcome(true,message);
            Check(before==candidate,"R6 outcome/envelope/float bits/exception mismatch: "+label);
            Check(Program.FixtureHintsOwned(),"Hints retained a noncanonical key or exceeded bound: "+label);
            if(expected is not null)Check(Error(before)==expected,"R6 rejection priority changed: "+label);
            if(accept is not null)Check(Accepted(before)==accept,"R6 accepted control failed: "+label);
            cases++;if(Accepted(before))accepted++;else rejected++;categories[label]=categories.GetValueOrDefault(label)+1;
            evidence.WriteLine(Json(new{label,accepted=Accepted(before),error=Error(before),codeUnits=message?.Length,
                messageSha256=message is null?null:Digest(message),outcomeSha256=Digest(before)}));
        }
        Program.FixtureResetHints();
        var oldCold=Program.FixtureOutcome(false,NativeFrame());
        Check(Program.FixtureHints() is null,"Before parser unexpectedly initialized R6 hints");
        Case("cold-native-wire",NativeFrame(),accept:true);
        Check(Program.FixtureHints() is {Length:64},"Hint array capacity differs");
        var own=Program.FixtureHints();
        foreach(string key in Program.FixtureKnownKeys())
        {
            Case("known-key-raw","{"+Json(key)+":0}");Check(Program.FixtureHints()![0]==key,"Known key was not hinted");
            Case("known-key-escaped","{"+EscapedName(key)+":0}");
        }
        foreach(string name in new[]{"COMMAND","Command","command\0","cómmand","ｃｏｍｍａｎｄ","command\u2003","İ","ı","\0",new string('x',8000)})
        {
            Case("unknown-name","{"+Json(name)+":0}");Check(Program.FixtureHints()![0] is null,"Unknown key retained in hint");
        }
        Case("metadata-prepare-accepted",BaseCommand(1,"PREPARE_TARGET")[..^1]+",\"msiSteamDeckReleaseFence\":true,\"msiReportChainDiagnostic\":{\"schemaVersion\":1,\"enabled\":true,\"msiClawKnownBoardAuthority\":true,\"visibilityDiagnosticBuild\":true,\"virtualDetailLogEnabled\":true,\"explicitSessionRequested\":true,\"nativePid\":12345,\"runId\":\"fixture-run\",\"epoch\":1,\"powerGeneration\":1,\"diagnosticSessionId\":\"fixture-session\"}}",accept:true);
        Case("metadata-source-accepted",NativeFrame()[..^1]+",\"msiReportChainSource\":{\"schemaVersion\":1,\"diagnosticSessionId\":\"fixture-session\",\"sourceFrameSequence\":123,\"sourceGeneration\":4}}",accept:true);
        foreach(int count in new[]{32,33,64,65,90,300})
        {
            Case("over64-metadata",ManyProperties(count),accept:true);
            Case("over64-unknown-tail",ManyProperties(count,true),"unknown-field");
            Case("over64-duplicate-tail",ManyProperties(count)[..^1]+",\"buttons\":0}","duplicate-field:buttons");
        }
        for(int i=0;i<4000;i++)
        {
            string msg=NativeFrame(i,i%7==0?"中é🚀":"",(i&1)==0);
            switch(i%8)
            {
                case 0:Case("steady-dynamic-native",msg,accept:true);break;
                case 1:Case("reversed-fields",Reorder(msg,-1),accept:true);break;
                case 2:Case("shuffled-fields",Reorder(msg,i+1),accept:true);break;
                case 3:Case("escaped-known-fields",EscapeKeys(msg),accept:true);break;
                case 4:Case("command-switch",BaseCommand(i,new[]{"HELLO","QUIESCE","HIDHIDE_QUERY","HIDHIDE_CLOAK","PREPARE_TARGET"}[i%5]),accept:true);break;
                case 5:Case("escaped-duplicate",msg[..^1]+","+EscapedName("buttons")+":0}","duplicate-field:buttons");break;
                case 6:Case("unknown-name-on-known-slot","{\"not-command-"+i+"\":0,\"command\":\"HELLO\",\"protocolVersion\":99}","unknown-field");break;
                case 7:Case("metadata-order-alternation",msg[..^1]+",\"msiReportChainDiagnostic\":{},\"msiReportChainDiagnostic\":{},\"msiReportChainSource\":{},"+EscapedName("msiReportChainSource")+":{}}",accept:true);break;
            }
        }
        var random=new Random(0x6156);
        var units=new[]{0,1,31,32,65,127,128,255,0x2003,0x2028,0xd7ff,0xd800,0xdbff,0xdc00,0xdfff,0xe000,0xfffe,0xffff};
        foreach(int code in units)
        {
            string raw="{\""+(char)code+"\":1,\"command\":\"HELLO\"}";
            Case("boundary-name-raw",raw);Case("boundary-name-escaped","{\"\\u"+code.ToString("x4")+"\":1,\"command\":\"HELLO\"}");
        }
        for(int i=0;i<3000;i++)
        {
            string prefix=i%2==0?"command":"";string name=prefix+string.Concat(Enumerable.Range(0,random.Next(1,12)).Select(_=>(char)random.Next(65536)));
            string message=i%3==0?"{\""+name+"\":0}":"{"+EscapedName(name)+":0,\"command\":false}";
            Case("random-unicode-name",message);
            if(i%20==0)Case("recover-from-malformed",NativeFrame(i),accept:true);
        }
        foreach(string malformed in new[]{"{\"\\uZZZZ\":1}","{\"\\u123\":1}","{\"\\q\":1}","{\"command\":\"HELLO\",\"lx\":NaN}","{\"command\":\"HELLO\",\"lx\":1e999}","{\"command\":\"HELLO\",\"command\":false,\"x\":1}"})Case("malformed-name-or-number",malformed);
        foreach(string culture in new[]{"en-US","tr-TR","ja-JP","zh-HK"})
        {
            var saved=CultureInfo.CurrentCulture;try{CultureInfo.CurrentCulture=CultureInfo.GetCultureInfo(culture);
                Case("culture-known",EscapeKeys(NativeFrame(6)),accept:true);Case("culture-case","{\"Command\":\"HELLO\",\"command\":false}","invalid-command");
            }finally{CultureInfo.CurrentCulture=saved;}
        }
        Check(ReferenceEquals(own,Program.FixtureHints()),"Hint array changed within same thread without reset");
        evidence.Flush();return new{Cases=cases,Accepted=accepted,Rejected=rejected,Categories=categories,HintSlots=64,
            HintCanonicalOwnership=true,UnknownInputsRetained=false,Over64Fallback=true,NoDocumentOrElementInHints=true};
    }
    private static object ParallelChecks()
    {
        const int threads=6,perThread=512;var hints=new string?[]?[threads];var failures=new Exception?[threads];
        var inputs=Enumerable.Range(0,32).Select(i=>(i%4) switch{0=>NativeFrame(i),1=>Reorder(NativeFrame(i),i+1),2=>EscapeKeys(NativeFrame(i)),_=>BaseCommand(i,"HIDHIDE_QUERY")}).ToArray();
        using var barrier=new Barrier(threads);var workers=new Thread[threads];
        for(int t=0;t<threads;t++){int id=t;workers[t]=new Thread(()=>{try{
            Check(Program.FixtureHints() is null&&Program.FixtureCooldown()==0,"Fresh thread inherited another thread hints/cooldown");
            Program.FixtureOutcome(true,NativeFrame(id));Check(Program.FixtureCooldown()==8,"Fresh thread cooldown initialization differs");hints[id]=Program.FixtureHints();Check(barrier.SignalAndWait(TimeSpan.FromSeconds(10)),"Thread barrier timed out");
            for(int i=0;i<perThread;i++){
                string msg=inputs[(i+id)%inputs.Length];Check(Program.FixtureOutcome(false,msg)==Program.FixtureOutcome(true,msg),"Parallel parser parity changed");
                Check(ReferenceEquals(hints[id],Program.FixtureHints())&&Program.FixtureHintsOwned(),"Parallel hint identity or canonical ownership changed");
                if(i%16==0)Thread.Yield();
            }
        }catch(Exception e){failures[id]=e;}});workers[t].Start();}
        foreach(var worker in workers)worker.Join();foreach(var error in failures)Check(error is null,"Parallel fixture failure: "+error);
        for(int i=0;i<threads;i++)for(int j=i+1;j<threads;j++)Check(!ReferenceEquals(hints[i],hints[j]),"ThreadStatic arrays shared");
        return new{Threads=threads,Cases=threads*perThread,PerThread=perThread,DistinctArrays=true,CanonicalOwnership=true};
    }
    private static object AdaptiveChecks(string dir)
    {
        int cases=0,probes=0,fallbacks=0;var categories=new Dictionary<string,int>();
        using var trace=new StreamWriter(Path.Combine(dir,"adaptive-transitions.jsonl"),false,new UTF8Encoding(false));
        void Case(string label,string? message,int? expectedCooldown=null,bool? expectedAccepted=null,string? expectedError=null)
        {
            int prior=Program.FixtureCooldown();var hints=Program.FixtureHints();var priorHints=hints?.ToArray();
            string before=Program.FixtureOutcome(false,message),candidate=Program.FixtureOutcome(true,message);
            Check(before==candidate,"Adaptive sequence outcome/error/exception changed: "+label);
            int after=Program.FixtureCooldown();Check(after is >=0 and <=8&&Program.FixtureHintsOwned(),"Adaptive state/ownership escaped bound");
            if(prior>0&&hints is not null){Check(ReferenceEquals(hints,Program.FixtureHints()),"Cooldown changed hint array");for(int i=0;i<64;i++)Check(ReferenceEquals(priorHints![i],hints[i]),"Cooldown modified hints");}
            if(expectedCooldown is not null)Check(after==expectedCooldown,"Wrong adaptive cooldown boundary: "+label+" before="+prior+" after="+after);
            if(expectedAccepted is not null)Check(Accepted(before)==expectedAccepted,"Adaptive accepted control failed: "+label);
            if(expectedError is not null)Check(Error(before)==expectedError,"Adaptive rejection priority changed: "+label);
            cases++;categories[label]=categories.GetValueOrDefault(label)+1;
            if(prior==0)probes++;else fallbacks++;
            trace.WriteLine(Json(new{label,cooldownBefore=prior,cooldownAfter=after,accepted=Accepted(before),error=Error(before),messageSha256=message is null?null:Digest(message),outcomeSha256=Digest(before)}));
        }
        Program.FixtureResetHints();Case("cold-first-probe",NativeFrame(),8,true);
        foreach(var bad in new string?[]{null,"", " ","{", "[]", "null", "true", "{\"x\":\"\uD800\""})Case("early-exit-does-not-consume-cooldown",bad,8);
        for(int i=8;i>0;i--)Case("eight-verbatim-fallbacks",NativeFrame(i),i-1,true);
        Case("stable-probe-reactivates",EscapeKeys(NativeFrame()),0,true);
        for(int i=0;i<16;i++)Case("steady-hits-remain-active",NativeFrame(i),0,true);
        Program.FixtureResetHints();int longProbes=0;
        for(int i=0;i<81;i++){
            int prior=Program.FixtureCooldown();if(prior==0)longProbes++;
            string msg="{\""+new string((char)('a'+i%26),8000)+"\":0,\"command\":\"HELLO\"}";
            Case("long-unknown-periodic-probe",msg,prior==0?8:prior-1,false,"unknown-field");
        }
        Check(longProbes==9,"Persistent misses did not limit probes to 1/9 object messages");
        Program.FixtureResetHints();int rotatingProbes=0;
        for(int i=0;i<81;i++){
            int prior=Program.FixtureCooldown();if(prior==0)rotatingProbes++;
            Case("rotating-periodic-probe",Reorder(NativeFrame(i),i+123),prior==0?8:prior-1,true);
        }
        Check(rotatingProbes==9,"Rotating miss probe interval changed");
        Case("arm-recovery-worst-phase",Reorder(NativeFrame(900),77777),8,true);
        for(int i=0;i<18;i++)Case("recover-from-worst-phase",NativeFrame(i),i<8?7-i:i==8?8:i<17?16-i:0,true);
        for(int i=0;i<32;i++)Case("recovered-steady",NativeFrame(i),0,true);
        Case("command-switch-arms-cooldown",BaseCommand(1,"HIDHIDE_QUERY"),8,true);
        for(int i=0;i<18;i++)Case("recover-after-command",NativeFrame(i),i<8?7-i:i==8?8:i<17?16-i:0,true);
        for(int i=0;i<96;i++){
            string msg=(i%6) switch{0=>NativeFrame(i),1=>BaseCommand(i,"HELLO"),2=>BaseCommand(i,"HIDHIDE_CLOAK"),
                3=>EscapeKeys(NativeFrame(i)),4=>NativeFrame(i)[..^1]+",\"msiReportChainSource\":{},\"msiReportChainSource\":{}}",
                _=>"{\"command\":false,\"x\":0,\"x\":1}"};
            Case("switch-metadata-duplicate-sequence",msg);
        }
        Program.FixtureResetHints();Case("duplicate-miss-arms-before-return","{\"unknown-repeated\":0,\"unknown-repeated\":1}",8,false,"duplicate-field:unknown-repeated");
        Case("fallback-duplicate-priority","{\"x\":0,\"x\":1,\"command\":false}",7,false,"duplicate-field:x");
        Case("fallback-escaped-duplicate","{\"command\":false,\"\\u0063ommand\":false}",6,false,"duplicate-field:command");
        Case("fallback-over64-metadata",ManyProperties(90),5,true);
        Case("fallback-over64-unknown",ManyProperties(90,true),4,false,"unknown-field");
        trace.Flush();return new{Cases=cases,Categories=categories,ProbeClassifications=probes,FallbackClassifications=fallbacks,
            PersistentMissProbes=longProbes,PersistentMissMessages=81,RotatingProbes=rotatingProbes,RotatingMessages=81,
            WorstRecoveryObjectMessages=18,CooldownObjectMessages=8,MissThreshold="at least half of first min(count,64)",
            Caveat="Probe/fallback classification uses entry state; early guard/non-object/document errors execute neither enumeration nor cooldown decrement. Trace records them separately."};
    }
    private static List<Scenario> Corpus()
    {
        string?[] Values(Func<int,string?> make)=>Enumerable.Range(0,32).Select(make).ToArray();
        return new(){
            new("steady-native-wire",Values(i=>NativeFrame(i))),
            new("steady-static-native-wire",Values(_=>NativeFrame())),
            new("mixed-Unicode-values-native-wire",Values(i=>NativeFrame(i,"中é🚀"))),
            new("steady-escaped-known-names",Values(i=>EscapeKeys(NativeFrame(i)))),
            new("rotating-order-native-wire",Values(i=>Reorder(NativeFrame(i),i+1))),
            new("alternating-reversed-order",Values(i=>i%2==0?NativeFrame(i):Reorder(NativeFrame(i),-1))),
            new("alternating-frame-commands",Values(i=>i%3==0?NativeFrame(i):BaseCommand(i,i%3==1?"HELLO":"HIDHIDE_QUERY"))),
            new("adversarial-unknown-name-misses",Values(i=>"{\"changing-unknown-"+i+"\":0,\"command\":\"HELLO\"}")),
            new("unknown-long-name",Values(i=>"{\""+new string((char)('a'+i%26),8000)+"\":0,\"command\":\"HELLO\"}")),
            new("over64-tolerated-metadata",Values(_=>ManyProperties(90))),
            new("duplicate-known",Values(i=>NativeFrame(i)[..^1]+",\"buttons\":0}")),
            new("malformed-before-enumeration",Values(i=>i%2==0?"{\"command\":"+i:"{\"\\uZZZZ\":0}")),
            new("cold-hints-native-wire",Values(i=>NativeFrame(i)),true),
            new("reorder32-then-recover64-native-wire",Enumerable.Range(0,96).Select(i=>(string?)(i<32?Reorder(NativeFrame(i),i+1):NativeFrame(i))).ToArray()),
            new("burst32frames-then-HIDHIDE",Enumerable.Range(0,33).Select(i=>(string?)(i<32?NativeFrame(i):BaseCommand(i,"HIDHIDE_QUERY"))).ToArray())};
    }
    [MethodImpl(MethodImplOptions.NoInlining)]private static long Loop(string?[] values,int repeats,int variant,bool cold)
    {
        long result=0;for(int r=0;r<repeats;r++)foreach(var text in values){if(cold)Program.FixtureResetHints();try{if(Program.FixtureParseMode(variant,text))result++;}catch(ArgumentException){result--;}}
        sink+=result;return result;
    }
    private static Timing Measure(Scenario scenario,int repeats,int variant,bool allocations)
    {
        long startBytes=allocations?GC.GetAllocatedBytesForCurrentThread():0;long start=Stopwatch.GetTimestamp();
        Loop(scenario.Values,repeats,variant,scenario.Cold);long end=Stopwatch.GetTimestamp();long bytes=allocations?GC.GetAllocatedBytesForCurrentThread()-startBytes:0;
        int events=scenario.Values.Length*repeats;return new((end-start)*1e9/Stopwatch.Frequency/events,(double)bytes/events,events);
    }
    private static double Median(List<double> values)=>values.OrderBy(x=>x).ElementAt(values.Count/2);
    private static object Costs(int rounds)
    {
        var rows=new List<object>();var corpus=Corpus();
        long globalStart=Stopwatch.GetTimestamp();int globalWarmEvents=0;
        do{foreach(var scenario in corpus)for(int variant=0;variant<3;variant++){Loop(scenario.Values,4,variant,scenario.Cold);globalWarmEvents+=scenario.Values.Length*4;}}
        while(Stopwatch.GetElapsedTime(globalStart).TotalMilliseconds<2000);
        foreach(var scenario in corpus)
        {
            int[] accepts=new int[3],exceptions=new int[3];
            foreach(var text in scenario.Values)for(int variant=0;variant<3;variant++){try{if(Program.FixtureParseMode(variant,text))accepts[variant]++;}catch(ArgumentException){exceptions[variant]++;}}
            Check(accepts.All(x=>x==accepts[0])&&exceptions.All(x=>x==exceptions[0]),"Benchmark corpus acceptance differs across variants");
            if(scenario.Name.Contains("wire",StringComparison.Ordinal)||scenario.Name=="steady-escaped-known-names"||scenario.Name=="over64-tolerated-metadata"||scenario.Name=="burst32frames-then-HIDHIDE")Check(accepts[0]==scenario.Values.Length,"Benchmark full acceptance missing");
            long warmStart=Stopwatch.GetTimestamp();int warmEvents=0;
            do{for(int variant=0;variant<3;variant++){Loop(scenario.Values,8,variant,scenario.Cold);warmEvents+=scenario.Values.Length*8;}}
            while(Stopwatch.GetElapsedTime(warmStart).TotalMilliseconds<100);
            var pilot=Measure(scenario,8,0,false);int repeats=Math.Clamp((int)(20_000_000/Math.Max(pilot.Ns,1)/scenario.Values.Length),2,100000);
            var timings=new[]{new List<double>(),new List<double>(),new List<double>()};
            for(int round=0;round<rounds;round++)for(int step=0;step<3;step++){int variant=(round+step)%3;timings[variant].Add(Measure(scenario,repeats,variant,false).Ns);}
            var oldAlloc=Measure(scenario,64,0,true);var plainAlloc=Measure(scenario,64,1,true);var newAlloc=Measure(scenario,64,2,true);
            var oldNs=Median(timings[0]);var plainNs=Median(timings[1]);var newNs=Median(timings[2]);Console.WriteLine($"parser {scenario.Name}: before {oldNs:F2}, plain {plainNs:F2}, adaptive {newNs:F2} ns; alloc {oldAlloc.Bytes:F2}/{plainAlloc.Bytes:F2}/{newAlloc.Bytes:F2}");
            rows.Add(new{scenario.Name,Mode="complete-TryParseEnvelope-three-variant",BeforeNs=oldNs,PlainCandidateNs=plainNs,CandidateNs=newNs,DeltaNs=newNs-oldNs,AdaptiveMinusPlainNs=newNs-plainNs,
                BeforeAllocatedBytes=oldAlloc.Bytes,PlainAllocatedBytes=plainAlloc.Bytes,CandidateAllocatedBytes=newAlloc.Bytes,AllocatedBytesSaved=oldAlloc.Bytes-newAlloc.Bytes,
                BeforeRounds=timings[0],PlainRounds=timings[1],CandidateRounds=timings[2],EventsPerRound=scenario.Values.Length*repeats,Rounds=rounds,WarmEvents=warmEvents,
                GlobalWarmupMilliseconds=2000,GlobalWarmEvents=globalWarmEvents,WarmupMilliseconds=100,AllocationEvents=oldAlloc.Events,CorpusMessages=scenario.Values.Length,AcceptedPerCorpus=accepts[0],ExceptionsPerCorpus=exceptions[0],
                MinCodeUnits=scenario.Values.Min(x=>x?.Length??0),MaxCodeUnits=scenario.Values.Max(x=>x?.Length??0),ColdHintArrayPerMessage=scenario.Cold});
        }
        return rows;
    }
    private static object NameLoopEvidence()
    {
        // Allocation negative control for the exact NameEquals primitive, isolated from parser timing.
        using var doc=JsonDocument.Parse(EscapeKeys(NativeFrame()));var property=doc.RootElement.EnumerateObject().First();string name=property.Name;
        for(int i=0;i<10000;i++){sink+=property.Name.Length;if(property.NameEquals(name))sink++;}
        long start=GC.GetAllocatedBytesForCurrentThread();for(int i=0;i<10000;i++)sink+=property.Name.Length;long nameBytes=GC.GetAllocatedBytesForCurrentThread()-start;
        start=GC.GetAllocatedBytesForCurrentThread();for(int i=0;i<10000;i++)if(property.NameEquals(name))sink++;long equalsBytes=GC.GetAllocatedBytesForCurrentThread()-start;
        Check(nameBytes>0&&equalsBytes==0,"NameEquals allocation negative control missing");
        return new{EscapedProperty=name,Events=10000,PropertyNameAllocatedBytesPerEvent=nameBytes/10000.0,NameEqualsAllocatedBytesPerEvent=equalsBytes/10000.0,Scope="Primitive allocation only, not whole-parser or process CPU"};
    }
    public static int Main(string[] args)
    {
        try{
            string dir=args[0];bool benchmark=args[1]=="benchmark";int rounds=int.Parse(args[2],CultureInfo.InvariantCulture);
            var r5=ParserCases(dir);Check(Program.FixtureHintsOwned(),"R5 corpus hint ownership changed");var r6=Sequences(dir);var parallel=ParallelChecks();var adaptive=AdaptiveChecks(dir);var primitive=NameLoopEvidence();
            long functional=checks;object costs=benchmark?Costs(rounds):Array.Empty<object>();var runtimePath=typeof(object).Assembly.Location;
            var result=new{Ok=true,ProductionModified=false,FunctionalAssertions=functional,R5ParserCases=((dynamic)r5).Cases,R6ParserCases=((dynamic)r6).Cases,
                R5Corpus=r5,R6Corpus=r6,Parallel=parallel,Adaptive=adaptive,PrimitiveAllocation=primitive,Costs=costs,BenchmarkExecuted=benchmark,
                Runtime=new{Framework=RuntimeInformation.FrameworkDescription,Environment.Version,CoreLibPath=runtimePath,CoreLibSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(runtimePath))),
                    TieredCompilation=Environment.GetEnvironmentVariable("DOTNET_TieredCompilation"),TieredPGO=Environment.GetEnvironmentVariable("DOTNET_TieredPGO")},
                ExecutionScope="Only actual-copy parser, parser helpers/hash and fixtures. Host Main/pipe/SDK/device/driver methods never invoked.",
                CostScope="Complete TryParseEnvelope; inputs prepared outside timers; separate warmup and allocation pass; seven three-variant rounds with rotating order by default. Only one new adaptive policy; immutable previous plain candidate is control. Current-thread managed allocated bytes only, not whole-process CPU or native allocations. Cold scenario resets only the new hints, not the existing field cache or runtime pools.",sink};
            File.WriteAllText(Path.Combine(dir,"result.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true,PropertyNamingPolicy=JsonNamingPolicy.CamelCase}));
            Console.WriteLine($"passed: {functional} functional assertions; {((dynamic)r5).Cases} R5 + {((dynamic)r6).Cases} R6 parser cases");return 0;
        }catch(Exception error){Console.Error.WriteLine(error);return 3;}
    }
}
