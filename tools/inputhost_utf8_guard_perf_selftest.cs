// Isolated copied-Host guard/parser fixture. Original Program.Main is never used.
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

internal static class Utf8GuardFixture
{
    private static long checks,sink;
    private static readonly JsonSerializerOptions JsonOptions=new(){Encoder=JavaScriptEncoder.UnsafeRelaxedJsonEscaping};
    private sealed record Scenario(string Name,string?[] Values);
    private sealed record Timing(double Ns,double Bytes,int Events);
    private static void Check(bool value,string text){checks++;if(!value)throw new InvalidOperationException(text);}
    private static void EqualGuard(string? value)
    {
        bool before=ExtractedGuards.Before(value),candidate=ExtractedGuards.Candidate(value);
        Check(before==candidate,"initial guard mismatch");
        if(!string.IsNullOrWhiteSpace(value)&&value.Length<=Program.FixtureMax/3)
            Check(Encoding.UTF8.GetByteCount(value)<=Program.FixtureMax,"short-string byte bound violated");
    }
    private sealed class WideUtf8Provider:EncodingProvider
    {
        public override Encoding? GetEncoding(int codepage)=>codepage==65001?Wide():null;
        public override Encoding? GetEncoding(string name)=>name.Equals("utf-8",StringComparison.OrdinalIgnoreCase)?Wide():null;
        private static Encoding Wide(){var e=(Encoding)Encoding.UTF8.Clone();e.EncoderFallback=new EncoderReplacementFallback(new string('\u4e2d',8));return e;}
    }
    private static object BoundProof()
    {
        var utf8=Encoding.UTF8;
        Check(utf8.IsReadOnly,"Shared UTF8 is mutable; reject bound");
        Check(ReferenceEquals(utf8,Encoding.UTF8),"Shared UTF8 identity not stable");
        Check(utf8.EncoderFallback is EncoderReplacementFallback,"Unsupported shared fallback; reject");
        var fallback=(EncoderReplacementFallback)utf8.EncoderFallback;
        Check(fallback.DefaultString=="\ufffd"&&fallback.MaxCharCount==1,"Shared replacement exceeds bound");
        string? setterError=null;
        try{utf8.EncoderFallback=new EncoderReplacementFallback(new string('\u4e2d',8));}catch(Exception e){setterError=e.GetType().FullName;}
        Check(setterError==typeof(InvalidOperationException).FullName,"Shared fallback setter was not refused");
        var clone=(Encoding)utf8.Clone();clone.EncoderFallback=new EncoderReplacementFallback(new string('\u4e2d',8));
        Check(clone.GetByteCount("\ud800")==24,"Wide fallback negative control missing");
        Check(Encoding.UTF8.GetByteCount("\ud800")==3,"Clone changed shared UTF8");
        Encoding.RegisterProvider(new WideUtf8Provider());
        Check(Encoding.GetEncoding(65001).GetByteCount("\ud800")==24,"Provider negative control missing");
        Check(ReferenceEquals(utf8,Encoding.UTF8)&&Encoding.UTF8.GetByteCount("\ud800")==3,"Provider changed direct UTF8 property");
        int max=0,whitespace=0;long pairs=0;
        for(int unit=0;unit<=char.MaxValue;unit++)
        {
            var text=new string((char)unit,1);var bytes=utf8.GetByteCount(text);max=Math.Max(max,bytes);
            Check(bytes<=3,"Single UTF16 code unit exceeded 3 bytes");
            if(char.IsWhiteSpace((char)unit))whitespace++;
            EqualGuard(text);
        }
        var pair=new char[2];
        for(int high=0xd800;high<=0xdbff;high++)for(int low=0xdc00;low<=0xdfff;low++)
        {
            pair[0]=(char)high;pair[1]=(char)low;
            Check(utf8.GetByteCount(pair.AsSpan())==4,"Surrogate pair was not four bytes");pairs++;
        }
        var rnd=new Random(0x6152026);
        for(int i=0;i<10000;i++)
        {
            int length=i<100?rnd.Next(16000,17000):rnd.Next(1,6000);var chars=new char[length];
            for(int j=0;j<length;j++)chars[j]=(char)rnd.Next(65536);
            var text=new string(chars);Check(utf8.GetByteCount(text)<=3L*text.Length,"Random UTF16 bound mismatch");EqualGuard(text);
        }
        var boundaries=new List<object>();
        foreach(int length in new[]{0,1,5460,5461,5462,16383,16384,16385})
        foreach(char unit in new[]{'A','\0','\u007f','\u0080','\u07ff','\u0800','\uffff','\ud800','\udc00',' ','\u2003'})
        {
            var text=new string(unit,length);EqualGuard(text);
            boundaries.Add(new{length,unit=(int)unit,bytes=utf8.GetByteCount(text),reject=ExtractedGuards.Before(text)});
        }
        foreach(int length in new[]{5461,5462,16384})
        {
            var text=string.Concat(Enumerable.Repeat("\ud83d\ude80",length/2))+(length%2==1?"\ud800":"");EqualGuard(text);
            boundaries.Add(new{length,unit=-1,bytes=utf8.GetByteCount(text),reject=ExtractedGuards.Before(text)});
        }
        EqualGuard(null);EqualGuard("");EqualGuard("\ud800\ud800\udc00\udc00");EqualGuard(" \t\n\r\u00a0\u2003\u3000");
        Check(utf8.GetByteCount(new string('\u0800',5461))==16383,"5461 sharp bound not established");
        Check(utf8.GetByteCount(new string('\u0800',5462))==16386,"5462 cutoff negative control missing");
        Check(ExtractedGuards.Candidate(new string('\u0800',5462)),"Long branch did not bytecount");
        Check(!ExtractedGuards.Candidate(new string('A',16384))&&ExtractedGuards.Candidate(new string('A',16385)),"Exact byte threshold changed");
        return new{Utf16SingleUnits=65536,ValidSurrogatePairs=pairs,RandomUtf16Strings=10000,
            MaxBytesPerSingleUnit=max,WhitespaceUnits=whitespace,SafeLength=5461,MaxMessageBytes=Program.FixtureMax,
            SharedUtf8Type=utf8.GetType().FullName,utf8.IsReadOnly,EncoderFallbackType=fallback.GetType().FullName,
            fallback.DefaultString,fallback.MaxCharCount,SharedSetterException=setterError,
            CloneProviderNegativeControlBytes=24,DirectSharedUnpairedBytes=3,Boundaries=boundaries,
            Proof="Each non-surrogate BMP unit uses 1/2/3 bytes; each matched high+low uses 4 bytes for 2 units; each unpaired unit uses fixed U+FFFD (3 bytes). Summing gives bytes <= 3*UTF16 length. Shared property is unaffected by supported clone/provider operations."};
    }
    private static Dictionary<string,object?> Frame(int id=0,string unicode="",bool touch=true)
    {
        int buttons=id&65535;float lx=(id%17)/16f-0.5f,ly=0f,rx=0.125f,ry=-0.25f,lt=0.25f,rt=0.75f;
        var d=new Dictionary<string,object?>{
            ["protocolVersion"]=1,["command"]="SUBMIT_FRAME",["requestId"]="fixture-request-"+id+unicode,
            ["sequence"]=(ulong)id+1,["runId"]="fixture-run",["epoch"]=1,["powerGeneration"]=1,
            ["targetId"]="ymcc-hidmaestro-gamepad",["ownerIdentity"]="ymcc.inputhost.virtual-target",
            ["persona"]="steamdeck",["profileIdentity"]="steam-deck-composite",["startupNonce"]="fixture-nonce",
            ["configRevision"]=1,["configHash"]=new string('a',64),["buttons"]=buttons,
            ["lx"]=lx,["ly"]=ly,["rx"]=rx,["ry"]=ry,["lt"]=lt,["rt"]=rt,["ltRaw"]=0.2f,["rtRaw"]=0.8f,
            ["sampleSequence"]=(ulong)id+1,["monotonicTick"]=(ulong)id+3000,
            ["normalizedInputHash"]=Program.FixtureHash(buttons,lx,ly,rx,ry,lt,rt),["motionPairProofId"]="hc-same-provider-selector-v2",
            ["backButtons"]=id&3,["raw"]=new Dictionary<string,object?>{
                ["buttons"]=buttons,["sThumbLX"]=-16384,["sThumbLY"]=0,["sThumbRX"]=4096,["sThumbRY"]=-8192,
                ["bLeftTrigger"]=51,["bRightTrigger"]=204},
            ["imu"]=new{admitted=true,gyroDps=new{x=0.29897409677505493,y=0.019747555255889893,z=0.1081552505493164},
                accelG=new{x=0.015371999703347683,y=0.9323239922523499,z=-0.40796801447868347},
                quaternion=new{w=-0.020204337313771248,x=0.03335440903902054,y=-0.852171778678894,z=0.521807074546814}}};
        if(touch)d["touchpads"]=new{left=new{active=true,click=false,x=-12345,y=23456,pressure=32767},right=new{active=false,click=false,x=0,y=0,pressure=0}};
        return d;
    }
    private static string Json(object value)=>JsonSerializer.Serialize(value,JsonOptions);
    private static string PaddedFrame(int length,char pad)
    {
        var f=Frame();var raw=(Dictionary<string,object?>)f["raw"]!;raw["guardFixturePadding"]="";
        int empty=Json(f).Length;if(length<empty)throw new InvalidOperationException("Padding target too short");
        raw["guardFixturePadding"]=new string(pad,length-empty);var text=Json(f);Check(text.Length==length,"Padded length changed");return text;
    }
    private static string Error(string outcome){using var doc=JsonDocument.Parse(outcome);return doc.RootElement.TryGetProperty("error",out var e)?e.GetString()!:"exception";}
    private static bool Accepted(string outcome){using var doc=JsonDocument.Parse(outcome);return doc.RootElement.TryGetProperty("accepted",out var e)&&e.GetBoolean();}
    private static object ParserCases(string dir)
    {
        int count=0,accepted=0,rejected=0;var errors=new Dictionary<string,int>(StringComparer.Ordinal);
        using var evidence=new StreamWriter(Path.Combine(dir,"parser-cases.jsonl"),false,new UTF8Encoding(false));
        void Case(string label,string? message,string? expectedError=null,bool? expectAccepted=null)
        {
            var left=Program.FixtureOutcome(false,message);var right=Program.FixtureOutcome(true,message);
            Check(left==right,"Full parser acceptance/error/envelope changed: "+label);
            if(expectedError is not null)Check(Error(left)==expectedError,"Unexpected negative-control order: "+label);
            if(expectAccepted is not null)Check(Accepted(left)==expectAccepted.Value,"Expected acceptance missing: "+label);
            if(Accepted(left))accepted++;else rejected++;count++;var err=Error(left);errors[err]=errors.GetValueOrDefault(err)+1;
            evidence.WriteLine(Json(new{label,codeUnits=message?.Length,bytes=message is null?(int?)null:Encoding.UTF8.GetByteCount(message),outcome=left}));
        }
        foreach(var text in new string?[]{null,""," ","\u2003\u3000","\0","{","[]","true","null","{}","{\"command\":NaN}","{\"command\":Infinity}"})Case("early/root/invalid",text);
        Case("duplicate-before-invalid-command","{\"x\":1,\"x\":2,\"command\":false}","duplicate-field:x");
        Case("invalid-command-before-unknown-field","{\"unknown\":1,\"command\":false}","invalid-command");
        Case("unknown-command-before-unknown-field","{\"unknown\":1,\"command\":\"KEEPALIVE\"}","unknown-command");
        Case("unknown-field-before-version","{\"unknown\":1,\"command\":\"HELLO\",\"protocolVersion\":99}","unknown-field");
        Case("duplicate-command","{\"command\":\"HELLO\",\"command\":\"SUBMIT_FRAME\"}","duplicate-field:command");
        Case("ordinary-fullframe-acceptance",Json(Frame()),expectAccepted:true);
        foreach(var cmd in new[]{"HELLO","PREPARE_TARGET","RECREATE_TARGET","SUBMIT_NEUTRAL","TUPLE_SUBMIT_STATS","QUIESCE","RELEASE_TARGET","SHUTDOWN","HIDHIDE_QUERY","HIDHIDE_CLOAK"})
        {var f=Frame();foreach(var key in new[]{"buttons","lx","ly","rx","ry","lt","rt","ltRaw","rtRaw","sampleSequence","monotonicTick","normalizedInputHash","motionPairProofId","backButtons","raw","imu","touchpads"})f.Remove(key);f["command"]=cmd;Case(cmd,Json(f),expectAccepted:true);}
        foreach(int length in new[]{5461,5462,16384,16385})foreach(char unit in new[]{'A','\u4e2d'})Case("padded-boundary-"+length+"-"+(int)unit,PaddedFrame(length,unit));
        foreach(string text in new[]{"\ud83d\ude80","\ud800","\udc00","\ud800\ud800","\udc00\ud800","\0","\u2003","\u4e2d","\u00e9"})
        {
            Case("raw-unicode-json","{\"command\":\"HELLO\",\"probe\":\""+text+"\"}");
            Case("escaped-unicode-json","{\"command\":\"HELLO\",\"probe\":"+Json(text)+"}");
        }
        foreach(char unit in new[]{'A','\u4e2d','\ud800',' '})foreach(int length in new[]{5461,5462,16384,16385})Case("raw-length-boundary",new string(unit,length));
        for(int i=0;i<5000;i++)
        {
            var f=Frame(i,i%3==0?"\u4e2d\u00e9\ud83d\ude80":"",(i&1)==0);string label="random-ordinary-frame";
            switch(i%10)
            {
                case 0:f["unknownField"]=i;label="unknown";break;
                case 1:f["lx"]=2.0;label="invalid-axis";break;
                case 2:f["normalizedInputHash"]=new string('0',64);label="hash-mismatch";break;
                case 3:f.Remove("runId");label="missing-base";break;
                case 4:f["requestId"]="bad\0control";label="embedded-control";break;
                case 5:f["msiReportChainSource"]=new{bad="metadata"};label="safe-invalid-metadata";break;
                case 6:f["imu"]=new{admitted=true,gyroDps=new{x="NaN",y=1.0,z=2.0}};label="optional-imu-fallback";break;
            }
            var msg=Json(f);if(i%50==7){msg=msg[..^1]+",\"buttons\":1}";label="duplicate-frame-buttons";}Case(label,msg);
        }
        var basic=Json(Frame());
        Case("duplicate-metadata-tolerated",basic[..^1]+",\"msiReportChainSource\":{},\"msiReportChainSource\":{}}");
        Case("nonfinite-axis-overflow",basic.Replace("\"lx\":-0.5","\"lx\":1e999"));
        Case("signed-zero-axis",basic.Replace("\"ly\":0","\"ly\":-0"),"invalid-frame");
        Case("nan-axis-token",basic.Replace("\"lx\":-0.5","\"lx\":NaN"));
        Case("trailing-comma",basic[..^1]+",}");Case("comment","/*x*/"+basic);
        evidence.Flush();return new{Cases=count,Accepted=accepted,Rejected=rejected,Errors=errors,
            Fingerprint="All CommandEnvelope public properties included even JsonIgnore metadata; float/double bits recorded. Exceptions type/message compared.",
            DistinctCaseNote="Case count is separate from repeated assertions, exhaustive unit/pair tests and benchmark iterations."};
    }
    private static List<Scenario> Corpus()
    {
        string?[] Values(Func<int,string?>make)=>Enumerable.Range(0,32).Select(make).ToArray();
        var ordinary=Values(i=>Json(Frame(i,"",true)));var mixed=Values(i=>Json(Frame(i,"\u4e2d\u00e9\ud83d\ude80",true)));
        var dense=Values(i=>{var f=Frame(i);((Dictionary<string,object?>)f["raw"]!)["guardFixturePadding"]=new string('\u4e2d',400);return Json(f);});
        return new(){new("ordinary-ASCII-fullwire",ordinary),new("mixed-Unicode-fullwire",mixed),new("dense-Unicode-fullwire",dense),
            new("malformed-short",Values(i=>i%2==0?"{\"command\":"+i:"{\"x\":\"\ud800\"")),
            new("null-whitespace",Values(i=>i%3==0?null:i%3==1?"":new string('\u2003',32))),
            new("boundary-5461-ASCII",Values(i=>PaddedFrame(5461,'A'))),new("boundary-5462-ASCII",Values(i=>PaddedFrame(5462,'A'))),
            new("limit-16384-ASCII",Values(i=>PaddedFrame(16384,'A'))),new("oversized-ASCII",Values(i=>PaddedFrame(16385,'A'))),
            new("oversized-threebyte-5462",Values(i=>new string('\u4e2d',5462))),
            new("unpaired-short",Values(i=>new string('\ud800',512))),new("surrogate-pair-short",Values(i=>string.Concat(Enumerable.Repeat("\ud83d\ude80",256))))};
    }
    [MethodImpl(MethodImplOptions.NoInlining)]private static long Loop(string?[] values,int repeats,bool candidate,bool parser)
    {
        long result=0;
        if(parser){for(int r=0;r<repeats;r++)foreach(var text in values){try{if(Program.FixtureParse(candidate,text))result++;}catch(ArgumentException){result--;}}}
        else if(candidate){for(int r=0;r<repeats;r++)foreach(var text in values)if(ExtractedGuards.Candidate(text))result++;}
        else{for(int r=0;r<repeats;r++)foreach(var text in values)if(ExtractedGuards.Before(text))result++;}
        sink+=result;return result;
    }
    private static Timing Measure(Scenario scenario,int repeats,bool candidate,bool parser,bool allocations)
    {
        long startBytes=allocations?GC.GetAllocatedBytesForCurrentThread():0;long start=Stopwatch.GetTimestamp();
        Loop(scenario.Values,repeats,candidate,parser);long end=Stopwatch.GetTimestamp();
        long bytes=allocations?GC.GetAllocatedBytesForCurrentThread()-startBytes:0;int events=scenario.Values.Length*repeats;
        return new((end-start)*1e9/Stopwatch.Frequency/events,(double)bytes/events,events);
    }
    private static double Median(List<double>v){v.Sort();return v[v.Count/2];}
    private static object Costs(List<Scenario>corpus,int rounds)
    {
        var rows=new List<object>();
        foreach(bool parser in new[]{false,true})foreach(var scenario in corpus)
        {
            // Time-bound warmup is separate from measurements; allow tiered JIT/PGO promotion.
            long warmStart=Stopwatch.GetTimestamp();int warmEvents=0;
            do{Loop(scenario.Values,8,false,parser);Loop(scenario.Values,8,true,parser);warmEvents+=scenario.Values.Length*16;}
            while(Stopwatch.GetElapsedTime(warmStart).TotalMilliseconds<35);
            var pilot=Measure(scenario,8,false,parser,false);
            int repeats=Math.Clamp((int)(15_000_000/Math.Max(pilot.Ns,1)/scenario.Values.Length),2,100000);
            var before=new List<double>();var candidate=new List<double>();
            for(int round=0;round<rounds;round++)
            {if((round&1)==0){before.Add(Measure(scenario,repeats,false,parser,false).Ns);candidate.Add(Measure(scenario,repeats,true,parser,false).Ns);}
             else{candidate.Add(Measure(scenario,repeats,true,parser,false).Ns);before.Add(Measure(scenario,repeats,false,parser,false).Ns);}}
            var oldAlloc=Measure(scenario,16,false,parser,true);var newAlloc=Measure(scenario,16,true,parser,true);
            if(!parser&&scenario.Name=="ordinary-ASCII-fullwire")Check(oldAlloc.Bytes==0&&newAlloc.Bytes==0,"ordinary guard unexpectedly allocated");
            Console.WriteLine($"{(parser?"parser":"guard")} {scenario.Name}: {Median(before):F2} -> {Median(candidate):F2} ns; alloc {oldAlloc.Bytes:F2} -> {newAlloc.Bytes:F2}");
            rows.Add(new{Mode=parser?"complete-TryParseEnvelope":"initial-guard",scenario.Name,
                MinCodeUnits=scenario.Values.Min(x=>x?.Length??0),MaxCodeUnits=scenario.Values.Max(x=>x?.Length??0),
                MinUtf8Bytes=scenario.Values.Min(x=>x is null?0:Encoding.UTF8.GetByteCount(x)),MaxUtf8Bytes=scenario.Values.Max(x=>x is null?0:Encoding.UTF8.GetByteCount(x)),
                SkipBytecountFraction=scenario.Values.Count(x=>!string.IsNullOrWhiteSpace(x)&&x.Length<=Program.FixtureMax/3)/(double)scenario.Values.Length,
                WarmEvents=warmEvents,EventsPerRound=scenario.Values.Length*repeats,Rounds=rounds,
                BeforeNs=Median(before),CandidateNs=Median(candidate),BeforeRounds=before,CandidateRounds=candidate,
                BeforeAllocatedBytes=oldAlloc.Bytes,CandidateAllocatedBytes=newAlloc.Bytes});
        }
        return rows;
    }
    private static int Main(string[] args)
    {
        try
        {
            var dir=args[0];Directory.CreateDirectory(dir);bool benchmark=args[1]=="benchmark";int rounds=int.Parse(args[2],CultureInfo.InvariantCulture);
            var bound=BoundProof();var parser=ParserCases(dir);long functional=checks;
            File.WriteAllText(Path.Combine(dir,"functional-result.json"),JsonSerializer.Serialize(new{ok=true,functionalAssertions=functional,bound,fullParser=parser},new JsonSerializerOptions{WriteIndented=true,PropertyNamingPolicy=JsonNamingPolicy.CamelCase}));
            object? costs=null;
            if(benchmark){
                var corpus=Corpus();
                // Initial mixed warmup precedes per-scenario promotion; no benchmark timers include it.
                var warm=Stopwatch.StartNew();do{foreach(var s in corpus){Loop(s.Values,2,false,true);Loop(s.Values,2,true,true);}}while(warm.ElapsedMilliseconds<250);
                Thread.Sleep(30);costs=Costs(corpus,rounds);
            }
            var runtimePath=typeof(Encoding).Assembly.Location;
            var result=new{Ok=true,ProductionModified=false,FunctionalAssertions=functional,AllAssertions=checks,
                ParserCaseCount=((dynamic)parser).Cases,Bound=bound,FullParser=parser,Costs=costs,BenchmarkExecuted=benchmark,
                Runtime=new{Framework=RuntimeInformation.FrameworkDescription,Environment.Version,CoreLibPath=runtimePath,
                    CoreLibSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(runtimePath))),
                    TieredCompilation=Environment.GetEnvironmentVariable("DOTNET_TieredCompilation"),TieredPGO=Environment.GetEnvironmentVariable("DOTNET_TieredPGO")},
                ExecutionScope="Only copied Program parser, parser helpers/canonical hash and test fixtures; original Host Main and device/driver/SDK methods never called.",
                CostScope="Inputs prepared outside timers. Guard reject predicate and full parser measured separately; accepted parser records destroyed by GC as usual; allocated bytes counted separately per current thread. ArgumentException from malformed UTF16 is caught by the outer benchmark harness, equally for both variants. Not whole-process CPU.",sink};
            File.WriteAllText(Path.Combine(dir,"result.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true,PropertyNamingPolicy=JsonNamingPolicy.CamelCase}));
            Console.WriteLine($"passed: {functional} functional assertions; {((dynamic)parser).Cases} parser cases; benchmark={benchmark}");return 0;
        }catch(Exception e){Console.Error.WriteLine(e);return 3;}
    }
}
