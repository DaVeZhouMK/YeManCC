using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Envelope = GeneratedParsers.CommandEnvelope;

// SYNTHETIC data only; only actual extracted pure parsing and fingerprint methods execute.
internal static class FrameEnvelopeFixture
{
    private delegate bool Parser(string? text,out Envelope? envelope,out string error);
    private static readonly Parser Before=GeneratedParsers.Baseline.Parse, After=GeneratedParsers.Candidate.Parse;
    private static long checks,cases,sink,accepted,rejected,exceptions;
    private static readonly ConcurrentDictionary<Type,PropertyInfo[]> Properties=new();
    private static readonly SortedDictionary<string,int> Coverage=new(StringComparer.Ordinal);
    private sealed record Raw(string Text);
    private sealed record Outcome(bool? Ok,Envelope? Envelope,string Error,Exception? Exception);
    private sealed record Scenario(string Name,bool Affected,string?[] Values);
    private sealed record Sample(double Ns,double Bytes);
    private static readonly Envelope Sentinel=new("sentinel","sentinel",9,"sentinel",9,9,"sentinel","sentinel","sentinel","sentinel","sentinel",9,"sentinel");
    private static void Check(bool ok,string message)
    {Interlocked.Increment(ref checks);if(!ok)throw new InvalidOperationException(message);}
    private static PropertyInfo[] Props(Type t)=>Properties.GetOrAdd(t,k=>k.GetProperties(BindingFlags.Public|BindingFlags.Instance).Where(p=>p.GetIndexParameters().Length==0).OrderBy(p=>p.Name,StringComparer.Ordinal).ToArray());
    private static Outcome Capture(Parser parser,string? text)
    {
        Envelope? e=Sentinel;string error="out-sentinel";bool? result=null;Exception? fault=null;
        try{result=parser(text,out e,out error);}catch(Exception ex){fault=ex;}
        return new(result,e,error,fault);
    }
    private static bool GraphEqual(object? a,object? b,Dictionary<object,object> ab,Dictionary<object,object> ba)
    {
        Interlocked.Increment(ref checks);
        if(a is null||b is null)return a is null&&b is null;
        Type t=a.GetType();if(t!=b.GetType())return false;
        if(a is float af)return BitConverter.SingleToInt32Bits(af)==BitConverter.SingleToInt32Bits((float)b);
        if(a is double ad)return BitConverter.DoubleToInt64Bits(ad)==BitConverter.DoubleToInt64Bits((double)b);
        if(a is decimal dm)return decimal.GetBits(dm).SequenceEqual(decimal.GetBits((decimal)b));
        if(a is string sa)return string.Equals(sa,(string)b,StringComparison.Ordinal);
        if(t.IsPrimitive||t.IsEnum)return a.Equals(b);
        if(!t.IsValueType)
        {
            if(ab.TryGetValue(a,out var prior))return ReferenceEquals(prior,b);
            if(ba.ContainsKey(b))return false;
            ab.Add(a,b);ba.Add(b,a);
            if(ReferenceEquals(a,b)&&!(a is Array zero&&zero.Length==0))return false;
        }
        if(a is Array aa)
        {
            var bb=(Array)b;if(aa.Rank!=bb.Rank||aa.Length!=bb.Length)return false;
            for(int n=0;n<aa.Rank;n++)if(aa.GetLength(n)!=bb.GetLength(n)||aa.GetLowerBound(n)!=bb.GetLowerBound(n))return false;
            var x=aa.GetEnumerator();var y=bb.GetEnumerator();
            while(x.MoveNext()){if(!y.MoveNext()||!GraphEqual(x.Current,y.Current,ab,ba))return false;}
            return !y.MoveNext();
        }
        foreach(var p in Props(t))if(!GraphEqual(p.GetValue(a),p.GetValue(b),ab,ba))return false;
        foreach(var f in t.GetFields(BindingFlags.Public|BindingFlags.Instance))if(!GraphEqual(f.GetValue(a),f.GetValue(b),ab,ba))return false;
        return true;
    }
    private static bool GraphEqual(object? a,object? b)=>GraphEqual(a,b,new(ReferenceEqualityComparer.Instance),new(ReferenceEqualityComparer.Instance));
    private static string ExceptionShape(Exception? e)=>e is null?"none":e.GetType().FullName+"|"+e.HResult+"|"+(e is ArgumentException a?a.ParamName:"")+"|"+e.Message+"|"+ExceptionShape(e.InnerException);
    private static Outcome Pair(string? text,string label,string? expectedError=null,bool? expectedOk=null,bool track=true)
    {
        var a=Capture(Before,text);var b=Capture(After,text);Interlocked.Increment(ref cases);
        Check(a.Ok==b.Ok,label+": return");Check(a.Error==b.Error,label+": error/out");
        Check(ExceptionShape(a.Exception)==ExceptionShape(b.Exception),label+": exception type/message/param/HResult");
        Check(GraphEqual(a.Envelope,b.Envelope),label+": all properties, ignored metadata, bits or alias graph");
        if(a.Ok!=true)Check(a.Envelope is null&&b.Envelope is null,label+": failed/throwing out envelope is null");
        else Check(!ReferenceEquals(a.Envelope,b.Envelope),label+": independent A/B envelopes");
        if(expectedError is not null)Check(a.Error==expectedError,label+": expected "+expectedError+", got "+a.Error);
        if(expectedOk is not null)Check(a.Ok==expectedOk&&a.Exception is null,label+": explicit acceptance");
        if(track){Coverage.TryGetValue(label.Split(':')[0],out int count);Coverage[label.Split(':')[0]]=count+1;}
        if(a.Exception is not null)Interlocked.Increment(ref exceptions);else if(a.Ok==true)Interlocked.Increment(ref accepted);else Interlocked.Increment(ref rejected);
        return a;
    }
    private static string JsonValue(object? v)=>v is Raw r?r.Text:JsonSerializer.Serialize(v);
    private static string Json(Dictionary<string,object?> d)=>"{"+string.Join(',',d.Select(kv=>JsonSerializer.Serialize(kv.Key)+":"+JsonValue(kv.Value)))+"}";
    private static string Add(string text,string key,string raw)=>text[..^1]+","+JsonSerializer.Serialize(key)+":"+raw+"}";
    private static Dictionary<string,object?> Base(string command,int n=0,string persona="xbox360")=>new(StringComparer.Ordinal)
    {
        ["protocolVersion"]=1,["command"]=command,["requestId"]="r-"+n,["sequence"]=(ulong)n+1,
        ["runId"]="run",["epoch"]=1UL,["powerGeneration"]=1UL,["targetId"]="target",["ownerIdentity"]="owner",
        ["persona"]=persona,["profileIdentity"]=persona=="steamdeck"?"steam-deck-composite":"profile",
        ["startupNonce"]="nonce",["configRevision"]=1UL,["configHash"]=new string('a',64)
    };
    private static Dictionary<string,object?> Frame(int n=0,string persona="xbox360",bool moving=false)
    {
        var d=Base("SUBMIT_FRAME",n,persona);int buttons=moving?(n*997)&0xffff:0;
        float lx=moving?(n%9-4)/4f:0,ly=moving?(n%7-3)/3f:0,rx=moving?(n%5-2)/2f:0,ry=moving?(n%3-1):0,lt=moving?n%11/10f:0,rt=moving?n%7/6f:0;
        d["buttons"]=buttons;d["lx"]=lx;d["ly"]=ly;d["rx"]=rx;d["ry"]=ry;d["lt"]=lt;d["rt"]=rt;
        d["ltRaw"]=lt;d["rtRaw"]=rt;d["backButtons"]=0;
        d["sampleSequence"]=(ulong)n+1;d["monotonicTick"]=10000UL+(ulong)n;
        d["normalizedInputHash"]=GeneratedParsers.Baseline.Hash(buttons,lx,ly,rx,ry,lt,rt);
        d["motionPairProofId"]="motion-"+n;return d;
    }
    private static object Source(int n=0)=>new{schemaVersion=1,diagnosticSessionId="diagnostic-"+n,sourceFrameSequence=(ulong)n+1,sourceGeneration=7UL};
    private static object Prepare()=>new{schemaVersion=1,enabled=true,msiClawKnownBoardAuthority=true,visibilityDiagnosticBuild=true,virtualDetailLogEnabled=true,explicitSessionRequested=true,nativePid=1234,runId="run",epoch=1UL,powerGeneration=1UL,diagnosticSessionId="prepare-session",durationMs=1000,maxMeaningfulEdges=64};
    private static object RawDomain(int n)=>new{buttons=n%65536,sThumbLX=-32768,sThumbLY=32767,sThumbRX=-12345,sThumbRY=23456,bLeftTrigger=0,bRightTrigger=255};
    private static object Imu(int n)=>new{admitted=true,gyroDps=new{x=1.25+n,y=-2.5,z=3.75},accelG=new{x=-0.0,y=0.125,z=0.999},quaternion=new{x=0.5,y=-0.25,z=0.75,w=1.0}};
    private static object Pads(int n)=>new{left=new{active=true,click=(n&1)==0,x=-32768,y=32767,pressure=12345},right=new{active=false,click=false,x=0,y=0,pressure=0}};
    private static string[] Commands=>GeneratedParsers.Commands;

    private static void Deterministic()
    {
        Check(Props(typeof(Envelope)).Length==59,"all 59 stored properties");
        Check(Props(typeof(Envelope)).Count(p=>p.GetCustomAttribute<JsonIgnoreAttribute>() is not null)==3,"ignored metadata properties included");
        foreach(string command in Commands)foreach(int flags in Enumerable.Range(0,8))
        {
            var d=command=="SUBMIT_FRAME"?Frame(flags):Base(command,flags);
            if((flags&1)!=0)d["physicalBaseContainerInstanceId"]="HID\\VID_TEST\\BASE";
            if((flags&2)!=0)d["physicalDeviceInstanceId"]="HID\\VID_TEST\\DEVICE";
            if((flags&4)!=0&&command.StartsWith("HIDHIDE_",StringComparison.Ordinal)){d["path"]="HID\\PATH";d["on"]=true;d["cloak"]=false;}
            Pair(Json(d),"allcommands:"+command+flags,expectedOk:true);
        }
        foreach(string persona in new[]{"xbox360","elite","steamdeck","dualsense","dualsense-edge","dualshock4","other"})
        {
            var d=Frame(3,persona,true);d["raw"]=RawDomain(1234);d["imu"]=Imu(3);d["ltRaw"]=new Raw("-0.0");d["rtRaw"]=.75f;d["backButtons"]=int.MaxValue;
            Pair(Json(d),"personas:"+persona,expectedOk:true);
            foreach(int bits in new[]{-1,0,1,2,3,31,63,64,int.MaxValue}){d["screenButtons"]=bits;Pair(Json(d),"screen-buttons:"+persona+bits);}
            d.Remove("screenButtons");d["touchpads"]=Pads(3);Pair(Json(d),"pads-personas:"+persona);
        }
        foreach(string field in new[]{"physicalBaseContainerInstanceId","physicalDeviceInstanceId"})
        foreach(object? v in new object?[]{null,""," ","x\0","x\u0085",new string('x',160),new string('x',161),1,true,new Raw("{}"),new Raw("[]")})
        foreach(string command in new[]{"SUBMIT_FRAME","HELLO","PREPARE_TARGET","HIDHIDE_QUERY"})
        {var d=command=="SUBMIT_FRAME"?Frame():Base(command);d[field]=v;Pair(Json(d),"physical:"+field+command);}
        foreach(string command in Commands)foreach(object? v in new object?[]{null,true,false,0,"x",new Raw("{}")})
        {
            var d=command=="SUBMIT_FRAME"?Frame():Base(command,persona:"steamdeck");d["msiSteamDeckReleaseFence"]=v;
            Pair(Json(d),"release-fence:"+command);
        }
        foreach(bool physical in new[]{false,true})foreach(bool prepare in new[]{false,true})
        {
            var d=prepare?Base("PREPARE_TARGET",persona:"steamdeck"):Frame();
            if(physical){d["physicalBaseContainerInstanceId"]="base";d["physicalDeviceInstanceId"]="device";}
            d[prepare?"msiReportChainDiagnostic":"msiReportChainSource"]=prepare?Prepare():Source();
            var result=Pair(Json(d),"metadata:valid-"+prepare+physical,expectedOk:true);
            Check(prepare?result.Envelope!.ReportChainPrepare1410 is not null:result.Envelope!.ReportChainSource1410 is not null,"actual metadata parser active");
            if(prepare)Check(result.Envelope!.ReportChainPrepareSpecified1410,"prepare specified flag");
        }
        foreach(string command in Commands)
        foreach(string key in new[]{"msiReportChainDiagnostic","msiReportChainSource"})
        foreach(string value in new[]{"null","false","{}","[]","{\"schemaVersion\":1}",JsonValue(Source()),JsonValue(Prepare()),"{\"diagnosticSessionId\":\"\\ud800\"}"})
        {
            var d=command=="SUBMIT_FRAME"?Frame():Base(command);d[key]=new Raw(value);string text=Json(d);
            Pair(text,"metadata:commands-shapes");Pair(Add(text,key,value),"metadata:duplicate");
        }
        string frame=Json(Frame());
        Pair(Add(frame,"command","false"),"precedence:duplicate-command","duplicate-field:command",false);
        Pair("{\"command\":false,\"extra\":1,\"extra\":2}","precedence:duplicate-before-command","duplicate-field:extra",false);
        Pair("{\"command\":false,\"extra\":1}","precedence:invalid-before-unknown","invalid-command",false);
        Pair("{\"command\":\"UNKNOWN\",\"extra\":1}","precedence:unknown-command-before-fields","unknown-command",false);
        Pair(Add(frame,"extra","0"),"precedence:unknown-field","unknown-field",false);
        var bad=Frame();bad["msiSteamDeckReleaseFence"]=true;bad["physicalBaseContainerInstanceId"]=false;bad["buttons"]=-1;
        Pair(Json(bad),"precedence:fence-before-physical-frame","invalid-msi-steamdeck-release-fence",false);
        bad.Remove("msiSteamDeckReleaseFence");bad["physicalDeviceInstanceId"]=false;
        Pair(Json(bad),"precedence:base-physical-first","invalid-physical-base-instance-id",false);
        bad.Remove("physicalBaseContainerInstanceId");Pair(Json(bad),"precedence:device-before-frame","invalid-physical-device-instance-id",false);
        Pair(frame.Replace("\"command\":","\"comm\\u0061nd\":",StringComparison.Ordinal),"escaped-key:valid",expectedOk:true);
        string escapedDuplicate=frame[..^1]+",\"comm\\u0061nd\":\"SUBMIT_FRAME\"}";
        Pair(escapedDuplicate,"escaped-key:duplicate","duplicate-field:command",false);
        foreach(string? text in new string?[]{null,""," ","\t\r\n","null","[]","1","true","{","{}","{\"command\":\"HELLO\",}","/*x*/{}","{\"command\":\"HELLO\"}//x",new string('x',16385),"\ud800",new string(' ',16385)})Pair(text,"invalid:syntax-root-size");
        foreach(string field in Frame().Keys)
        {
            var d=Frame();d.Remove(field);Pair(Json(d),"missing:"+field);
            foreach(object? value in new object?[]{null,false,"",new Raw("[]"),new Raw("{}")}){d=Frame();d[field]=value;Pair(Json(d),"wrong-kind:"+field);}
        }
        foreach(string field in new[]{"requestId","runId","targetId","ownerIdentity","persona","profileIdentity","startupNonce","configHash","motionPairProofId"})
        foreach(int n in new[]{0,1,31,32,63,64,95,96,159,160,161,5461,5462,16385})
        {var d=Frame();d[field]=new string('x',n);Pair(Json(d),"lengths:"+field+n);}
        foreach(string raw in new[]{"null","false","[]","{}","{\"buttons\":1e9999}","{\"buttons\":2147483648}","{\"sThumbLX\":-999999}","{\"bLeftTrigger\":999999}"}){var d=Frame();d["raw"]=new Raw(raw);Pair(Json(d),"raw:shape-exception-clamp");}
        foreach(string imu in new[]{"null","{}","{\"admitted\":true,\"gyroDps\":{\"x\":1e9999}}","{\"quaternion\":{\"x\":-0.0,\"y\":1,\"z\":2,\"w\":3}}","{\"gyroDps\":{\"x\":0,\"x\":1}}"}){var d=Frame();d["imu"]=new Raw(imu);Pair(Json(d),"imu:bits-exception-duplicates");}
        foreach(string pad in new[]{"null","[]","{}",JsonValue(Pads(1)),"{\"left\":null,\"right\":null}","{\"left\":{\"active\":false,\"click\":true,\"x\":0,\"y\":0,\"pressure\":0},\"right\":{}}"}){var d=Frame(persona:"steamdeck");d["touchpads"]=new Raw(pad);Pair(Json(d),"pads:shape-defaults");}
        foreach(string culture in new[]{"en-US","fr-FR","ar-SA","zh-CN"})
        {var old=CultureInfo.CurrentCulture;try{CultureInfo.CurrentCulture=CultureInfo.GetCultureInfo(culture);for(int n=0;n<16;n++)Pair(Json(Frame(n,moving:true)),"cultures:"+culture,expectedOk:true);}finally{CultureInfo.CurrentCulture=old;}}
        var e=Capture(Before,frame).Envelope!;
        Check(JsonSerializer.Serialize(e)==JsonSerializer.Serialize(e with{ReportChainSource1410=new("different",1,2)}),"serializer hides source metadata negative control");
        Check(!GraphEqual(e,e with{ReportChainSource1410=new("different",1,2)}),"recursive oracle notices ignored source metadata");
        Check(!GraphEqual(e,e with{ReportChainPrepareSpecified1410=true}),"recursive oracle notices ignored prepare flag");
        Check(!GraphEqual(e,e with{Lx=BitConverter.Int32BitsToSingle(unchecked((int)0x80000000))}),"bit oracle notices float -zero");
        Check(!GraphEqual(e with{ImuQuatW=0.0},e with{ImuQuatW=BitConverter.Int64BitsToDouble(unchecked((long)0x8000000000000000))}),"bit oracle notices double -zero");
        object[] shared={new[]{1,2},new[]{1,2}};object[] sameShape={new[]{1,2},new[]{1,2}};
        Check(GraphEqual(shared,sameShape),"array recursion");sameShape[1]=sameShape[0];Check(!GraphEqual(shared,sameShape),"alias graph mismatch detected");
    }
    private static Dictionary<string,object?> ObjectFields(object value)
    {
        using var d=JsonDocument.Parse(JsonValue(value));
        return d.RootElement.EnumerateObject().ToDictionary(p=>p.Name,p=>(object?)new Raw(p.Value.GetRawText()),StringComparer.Ordinal);
    }
    private static void MetadataAndOptionalEdges()
    {
        foreach(bool prepare in new[]{false,true})
        {
            string key=prepare?"msiReportChainDiagnostic":"msiReportChainSource";
            object template=prepare?Prepare():Source();
            foreach(string member in ObjectFields(template).Keys)
            foreach(string raw in new[]{"null","false","true","0","1","-1","18446744073709551615","18446744073709551616","\"\"","\" \"","\"\\ud800\"","[]","{}"})
            {
                var metadata=ObjectFields(template);metadata[member]=new Raw(raw);
                var d=prepare?Base("PREPARE_TARGET",persona:"steamdeck"):Frame();d[key]=new Raw(Json(metadata));
                Pair(Json(d),"metadata-members:"+member);
                metadata.Remove(member);d[key]=new Raw(Json(metadata));Pair(Json(d),"metadata-missing:"+member);
            }
            var valid=prepare?Base("PREPARE_TARGET",persona:"steamdeck"):Frame();valid[key]=template;
            var duplicate=Pair(Add(Json(valid),key,JsonValue(template)),"metadata-double-valid:"+key,expectedOk:true);
            Check(prepare?duplicate.Envelope!.ReportChainPrepareSpecified1410&&duplicate.Envelope.ReportChainPrepare1410 is null:duplicate.Envelope!.ReportChainSource1410 is null,"duplicate metadata suppresses payload and retains specified flag");
            valid[key]=new Raw(Add(JsonValue(template),"extra","true"));Pair(Json(valid),"metadata-unknown-member:"+key,expectedOk:true);
        }
        foreach(string field in new[]{"durationMs","maxMeaningfulEdges"})
        foreach(int number in new[]{-1,0,1,7,8,127,128,129,9999,10000,30000,30001})
        {
            var metadata=ObjectFields(Prepare());metadata[field]=number;var d=Base("PREPARE_TARGET",persona:"steamdeck");d["msiReportChainDiagnostic"]=new Raw(Json(metadata));Pair(Json(d),"metadata-bounds:"+field+number);
        }
        foreach(string persona in new[]{"steamdeck","xbox360","dualsense",""})foreach(string profile in new[]{"steam-deck-composite","profile",""})
        {
            var d=Base("PREPARE_TARGET",persona:persona);d["profileIdentity"]=profile;d["msiSteamDeckReleaseFence"]=true;d["physicalBaseContainerInstanceId"]=false;
            Pair(Json(d),"release-order-persona:"+persona+profile);
        }
        foreach(string command in Commands)foreach(string field in new[]{"path","on","cloak"})
        foreach(object? value in new object?[]{null,false,true,1,"",new Raw("{}"),new Raw("[]")})
        {var d=command=="SUBMIT_FRAME"?Frame():Base(command);d[field]=value;Pair(Json(d),"hidhide-loose:"+command+field);}
        foreach(string field in new[]{"buttons","sThumbLX","sThumbLY","sThumbRX","sThumbRY","bLeftTrigger","bRightTrigger"})
        foreach(string number in new[]{"-2147483649","-32769","-32768","-1","0","1","255","256","32767","32768","2147483647","2147483648","-0.0","1e9999"})
        {var raw=ObjectFields(RawDomain(3));raw[field]=new Raw(number);var d=Frame();d["raw"]=new Raw(Json(raw));Pair(Json(d),"raw-bounds:"+field);}
        foreach(string persona in new[]{"steamdeck","dualsense","dualsense-edge","dualshock4","xbox360"})
        foreach(int x in new[]{-32769,-32768,-1,0,32767,32768})foreach(int pressure in new[]{-1,0,1,32767,32768})
        {
            var d=Frame(persona:persona);d["touchpads"]=new{left=new{active=true,click=true,x,y=0,pressure},right=new{active=false,click=false,x=0,y=0,pressure=0}};Pair(Json(d),"pad-bounds:"+persona);
        }
    }
    private static void NumbersAndUnicode()
    {
        string[] numbers={"0","0.0","0e0","-0","-0.0","-0e99","1e-1000","-1e-1000","1e9999","-1e9999","1","-1","1.00000001","0.5","-0.5","5e-324","1.401298464324817e-45","3.4028234663852886e38","NaN","Infinity","01","1.","+0","18446744073709551615","18446744073709551616","-1","2147483648"};
        foreach(string field in new[]{"lx","ly","rx","ry","lt","rt","ltRaw","rtRaw","buttons","sequence","sampleSequence","monotonicTick","epoch","powerGeneration","configRevision","protocolVersion","backButtons","screenButtons"})
        foreach(string number in numbers){var d=Frame();d[field]=new Raw(number);Pair(Json(d),"numbers:"+field);}
        foreach(int bits in new[]{0,1,0x007fffff,0x00800000,0x3f000000,0x3f7fffff,0x3f800000,unchecked((int)0x80000000),unchecked((int)0x80000001),unchecked((int)0xbf800000)})
        {
            float axis=BitConverter.Int32BitsToSingle(bits);var d=Frame();d["lx"]=axis;
            d["normalizedInputHash"]=GeneratedParsers.Baseline.Hash(0,axis,0,0,0,0,0);
            Pair(Json(d),"valid-float-bits:"+bits);
        }
        foreach(string axis in new[]{"gyroDps","accelG","quaternion"})
        {
            var d=Frame();d["imu"]=new Raw("{\"admitted\":true,\""+axis+"\":{\"x\":5e-324,\"y\":-5e-324,\"z\":-0.0}}");
            Pair(Json(d),"imu-double-bits:"+axis,expectedOk:true);
        }
        for(int ch=0;ch<=char.MaxValue;ch++)
        {
            var d=Base("HELLO");d["requestId"]=new Raw("\"A\\u"+ch.ToString("x4",CultureInfo.InvariantCulture)+"Z\"");
            Pair(Json(d),"unicode-all-units:"+ch.ToString("x4",CultureInfo.InvariantCulture));
        }
        foreach(string literal in new[]{"\\ud800","\\udfff","\\ud83d\\ude80","\\ud800\\ud800","\\udc00\\ud800","\\u0000","\\u0085","\\u009f","\\u2028","\\ufffe","中é🚀","a\\\\b\\\"c"})
        foreach(string field in new[]{"requestId","runId","physicalBaseContainerInstanceId","motionPairProofId"})
        {var d=Frame();d[field]=new Raw("\"A"+literal+"Z\"");Pair(Json(d),"unicode-frame:"+field);}
        foreach(int n in new[]{159,160,161,5461,5462,16384}){var d=Frame();d["requestId"]=new string('中',n);Pair(Json(d),"unicode-size:"+n);}
        for(int depth=60;depth<=66;depth++){var d=Frame();d["msiReportChainSource"]=new Raw(new string('[',depth)+"0"+new string(']',depth));Pair(Json(d),"metadata-depth:"+depth);}
    }
    private static void RandomCases()
    {
        var random=new Random(2026101533);
        for(int n=0;n<4000;n++)
        {
            bool frame=(n%4)!=0;var d=frame?Frame(n,(n%3)==0?"steamdeck":"xbox360",true):Base(Commands[random.Next(Commands.Length)],n);
            if(n%5==0)d["physicalBaseContainerInstanceId"]="base-"+n;
            if(n%7==0)d["physicalDeviceInstanceId"]="device-"+n;
            if(frame&&n%3==0){d["raw"]=RawDomain(n);d["imu"]=Imu(n);d["backButtons"]=random.Next(-99,99);}
            if(n%11==0)d["msiReportChainSource"]=Source(n);
            if(n%13==0)d["requestId"]=new string(Enumerable.Range(0,random.Next(0,180)).Select(_=>(char)random.Next(65536)).ToArray());
            if(n%4==0){string key=d.Keys.ElementAt(random.Next(d.Count));d[key]=new Raw(new[]{"null","false","-0.0","1e9999","[]","{}","\"\\ud800\""}[random.Next(7)]);}
            d=d.OrderBy(_=>random.Next()).ToDictionary(k=>k.Key,v=>v.Value,StringComparer.Ordinal);
            string text=Json(d);if(n%17==0)text=Add(text,"extra",n.ToString(CultureInfo.InvariantCulture));if(n%19==0)text=Add(text,"command","\"SUBMIT_FRAME\"");
            Pair(text,"random:"+n);
        }
    }
    private static void Snapshot(StringBuilder b,object? o)
    {
        if(o is null){b.Append("null;");return;}Type t=o.GetType();b.Append(t.Name).Append(':');
        if(o is string s){foreach(char c in s)b.Append(((int)c).ToString("X4",CultureInfo.InvariantCulture));b.Append(';');return;}
        if(o is float f){b.Append(BitConverter.SingleToInt32Bits(f)).Append(';');return;}
        if(o is double d){b.Append(BitConverter.DoubleToInt64Bits(d)).Append(';');return;}
        if(t.IsPrimitive||t.IsEnum){b.Append(Convert.ToString(o,CultureInfo.InvariantCulture)).Append(';');return;}
        if(o is Array a){foreach(object? v in a)Snapshot(b,v);return;}
        foreach(var p in Props(t)){b.Append(p.Name).Append('=');Snapshot(b,p.GetValue(o));}
    }
    private static string Snapshot(object? o){var b=new StringBuilder();Snapshot(b,o);return b.ToString();}
    private static object? Memo(Type variant)
    {
        var memo=variant.GetNestedType("CanonicalFrameFingerprintMemo",BindingFlags.NonPublic)!;
        return memo.GetField("s_lastSuccessful",BindingFlags.NonPublic|BindingFlags.Static)!.GetValue(null);
    }
    private static void OwnershipAndThreads()
    {
        var d=Frame(7,"steamdeck",true);d["msiReportChainSource"]=Source(7);d["raw"]=RawDomain(7);d["imu"]=Imu(7);d["touchpads"]=Pads(7);string text=Json(d);
        foreach(Parser parser in new[]{Before,After})
        {
            var first=Capture(parser,text);Check(first.Ok==true,"ownership seed accepted");string frozen=Snapshot(first.Envelope);
            var second=Capture(parser,text);Check(!ReferenceEquals(first.Envelope,second.Envelope),"new envelope on each successful parse");
            Check(!ReferenceEquals(first.Envelope!.ReportChainSource1410,second.Envelope!.ReportChainSource1410),"fresh source metadata record");
            for(int n=0;n<128;n++){Capture(parser,Json(Frame(n,moving:true)));Capture(parser,"{}");}
            Check(Snapshot(first.Envelope)==frozen,"old output retains fields/metadata after dictionary reuse and document disposal");
        }
        var mainA=GeneratedParsers.Baseline.Cache;var mainB=GeneratedParsers.Candidate.Cache;
        Check(mainA is not null&&mainB is not null&&!ReferenceEquals(mainA,mainB),"per-variant ThreadStatic dictionaries isolated");
        Pair(text,"thread-cache:main",expectedOk:true);
        Check(ReferenceEquals(mainA,GeneratedParsers.Baseline.Cache)&&ReferenceEquals(mainB,GeneratedParsers.Candidate.Cache),"actual dictionary reuse retained");
        var caches=new object?[8];var memoCaches=new object?[8];var faults=new ConcurrentQueue<Exception>();var threads=new Thread[4];
        for(int id=0;id<4;id++)
        {
            int index=id;threads[id]=new Thread(()=>{try
            {
                Check(GeneratedParsers.Baseline.Cache is null&&GeneratedParsers.Candidate.Cache is null,"new thread cache starts null");
                Check(Memo(typeof(GeneratedParsers.Baseline)) is null&&Memo(typeof(GeneratedParsers.Candidate)) is null,"actual fingerprint caches start empty on new thread");
                for(int n=0;n<96;n++)Pair(n%9==0?"{}":Json(Frame(index*1000+n,moving:true)),"thread-parity",track:false);
                caches[2*index]=GeneratedParsers.Baseline.Cache;caches[2*index+1]=GeneratedParsers.Candidate.Cache;
                memoCaches[2*index]=Memo(typeof(GeneratedParsers.Baseline));memoCaches[2*index+1]=Memo(typeof(GeneratedParsers.Candidate));
            }catch(Exception e){faults.Enqueue(e);}});threads[id].Start();
        }
        foreach(var t in threads)t.Join();if(faults.TryDequeue(out var fault))throw fault;
        Check(caches.All(x=>x is not null),"worker caches created");
        Check(caches.Distinct(ReferenceEqualityComparer.Instance).Count()==8,"eight actual ThreadStatic caches distinct");
        Check(caches.All(x=>!ReferenceEquals(x,mainA)&&!ReferenceEquals(x,mainB)),"worker caches distinct from main thread");
        Check(memoCaches.All(x=>x is not null)&&memoCaches.Distinct(ReferenceEqualityComparer.Instance).Count()==8,"eight actual fingerprint caches distinct across variants/threads");
    }
    private static Scenario[] Scenarios()
    {
        string?[] Values(Func<int,string?> f)=>Enumerable.Range(0,64).Select(f).ToArray();
        Scenario Frames(string name,Func<int,Dictionary<string,object?>> f)=>new(name,true,Values(i=>Json(f(i))));
        var list=new List<Scenario>
        {
            Frames("frame-ascii-stable",i=>Frame(i)),Frames("frame-ascii-moving",i=>Frame(i,moving:true)),
            Frames("frame-unicode",i=>{var d=Frame(i,moving:true);d["requestId"]="请求-é-🚀-"+i;d["runId"]="运行-中";d["ownerIdentity"]="持有者";return d;}),
            Frames("frame-physical-ids",i=>{var d=Frame(i,moving:true);d["physicalBaseContainerInstanceId"]="HID\\BASE-"+i;d["physicalDeviceInstanceId"]="HID\\DEVICE-"+i;return d;}),
            Frames("frame-raw-domain",i=>{var d=Frame(i,moving:true);d["raw"]=RawDomain(i);d["ltRaw"]=.25f;d["rtRaw"]=.75f;return d;}),
            Frames("frame-imu",i=>{var d=Frame(i,"steamdeck",true);d["imu"]=Imu(i);d["backButtons"]=i%16;return d;})
        };
        foreach(var (suffix,persona) in new[]{("steamdeck","steamdeck"),("dualsense","dualsense"),("edge","dualsense-edge"),("ds4","dualshock4")})
            list.Add(Frames("frame-pads-"+suffix,i=>{var d=Frame(i,persona,true);d["touchpads"]=Pads(i);d["screenButtons"]=persona=="steamdeck"||persona=="dualsense-edge"?63:1;return d;}));
        list.Add(Frames("frame-source-metadata",i=>{var d=Frame(i,moving:true);d["msiReportChainSource"]=Source(i);return d;}));
        list.Add(Frames("frame-invalid-metadata",i=>{var d=Frame(i,moving:true);d["msiReportChainSource"]=new{schemaVersion=999,diagnosticSessionId="not-admitted"};return d;}));
        list.Add(Frames("frame-legacy-minimal",i=>{var d=Frame(i);foreach(string k in new[]{"ltRaw","rtRaw","backButtons"})d.Remove(k);return d;}));
        list.Add(new("frame-reordered-escaped",true,Values(i=>Json(Frame(i,moving:true).Reverse().ToDictionary(k=>k.Key,v=>v.Value)).Replace("\"command\":","\"comm\\u0061nd\":",StringComparison.Ordinal))));
        string[] nonframes=Commands.Where(x=>x!="SUBMIT_FRAME").ToArray();
        list.Add(new("nonframe-all-commands",false,Values(i=>Json(Base(nonframes[i%nonframes.Length],i)))));
        list.Add(new("nonframe-hidhide-physical",false,Values(i=>{var d=Base("HIDHIDE_CLOAK",i);d["physicalBaseContainerInstanceId"]="base";d["physicalDeviceInstanceId"]="device";d["path"]="HID\\PATH";d["on"]=true;d["cloak"]=false;return Json(d);}))); 
        list.Add(new("nonframe-prepare-metadata-fence",false,Values(i=>{var d=Base("PREPARE_TARGET",i,"steamdeck");d["msiSteamDeckReleaseFence"]=true;d["msiReportChainDiagnostic"]=Prepare();return Json(d);}))); 
        list.Add(new("invalid-early",false,Values(i=>new[]{"{","[]","{}","{\"command\":false}","{\"command\":\"UNKNOWN\"}"}[i%5])));
        list.Add(new("invalid-late-frame",false,Values(i=>{var d=Frame(i);if(i%3==0)d["normalizedInputHash"]=new string('0',64);else if(i%3==1)d["motionPairProofId"]="";else d["screenButtons"]=-1;return Json(d);}))); 
        return list.ToArray();
    }
    private static void ScenarioProof(string run)
    {
        var scenarios=Scenarios();using var plan=JsonDocument.Parse(File.ReadAllText(Path.Combine(run,"acceptance-plan.json")));var root=plan.RootElement;
        Check(scenarios.Where(x=>x.Affected).Select(x=>x.Name).SequenceEqual(root.GetProperty("affectedFullParserWorkloads").EnumerateArray().Select(x=>x.GetString())),"frozen affected workload names");
        Check(scenarios.Where(x=>!x.Affected).Select(x=>x.Name).SequenceEqual(root.GetProperty("controlFullParserWorkloads").EnumerateArray().Select(x=>x.GetString())),"frozen control workload names");
        foreach(var s in scenarios)foreach(string? text in s.Values)Pair(text,"frozen-workloads:"+s.Name,expectedOk:s.Affected?true:null);
        File.WriteAllText(Path.Combine(run,"synthetic-perf-inputs.json"),JsonSerializer.Serialize(scenarios));
    }
    private static void Verify(string run,string profile)
    {
        Deterministic();MetadataAndOptionalEdges();NumbersAndUnicode();RandomCases();OwnershipAndThreads();ScenarioProof(run);
        var result=new{pass=true,profile,checks,cases,accepted,rejected,exceptions,envelopeStoredProperties=Props(typeof(Envelope)).Length,ignoredMetadataProperties=3,coverage=Coverage,
            runtime=RuntimeInformation.FrameworkDescription,framework=AppContext.TargetFrameworkName,inputKind="SYNTHETIC fixtures only; no archive or field-performance claim",
            baseline="complete current actual parser",metadata="full actual MsiVirtualReportChain1410.cs; source and prepare outputs asserted",threadCache="separate actual ThreadStatic dictionary and memo per variant; four dedicated threads verified",
            compare="59 properties including JsonIgnore metadata; records/arrays/alias graph; float/double bits; false and throwing out values; exception type/message/param/HResult",
            scope="No SDK/session/Host Main/pipe/device/service/registry/driver/network calls",performance="HELD; not measured",oomParityClaim=false};
        File.WriteAllText(Path.Combine(run,profile+"-correctness.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine(JsonSerializer.Serialize(result));
    }
    private static long Loop(Parser parser,Scenario s,int count)
    {
        long sum=0;
        for(int n=0;n<count;n++)
        {
            bool ok=parser(s.Values[n%s.Values.Length],out var e,out var error);
            if(ok){var parsed=e!;sum+=(long)(parsed.Sequence^parsed.SampleSequence)+parsed.Buttons;}
            else sum+=error.Length;
            GC.KeepAlive(e);
        }
        return sum;
    }
    private static Sample Measure(Parser parser,Scenario s,int count,bool allocations)
    {
        long bytes=allocations?GC.GetAllocatedBytesForCurrentThread():0;
        long start=Stopwatch.GetTimestamp();long value=Loop(parser,s,count);long stop=Stopwatch.GetTimestamp();
        double allocated=allocations?(double)(GC.GetAllocatedBytesForCurrentThread()-bytes)/count:0;
        Interlocked.Add(ref sink,value);return new((stop-start)*1e9/Stopwatch.Frequency/count,allocated);
    }
    private static double Median(IEnumerable<double> v){var a=v.OrderBy(x=>x).ToArray();return a[a.Length/2];}
    private static void Benchmark(string run,string profile,int campaign)
    {
        using var doc=JsonDocument.Parse(File.ReadAllText(Path.Combine(run,"acceptance-plan.json")));var p=doc.RootElement;
        int count=p.GetProperty("callsPerMeasurement").GetInt32(),allocCount=p.GetProperty("allocationCalls").GetInt32(),rounds=p.GetProperty("pairedRounds").GetInt32();
        Scenario[] scenarios=Scenarios();long start=Stopwatch.GetTimestamp();
        do{foreach(var s in scenarios){Loop(Before,s,64);Loop(After,s,64);}}while(Stopwatch.GetElapsedTime(start).TotalMilliseconds<p.GetProperty("globalWarmupMs").GetInt32());
        var rows=new List<object>();bool all=true;
        foreach(var s in scenarios)
        {
            start=Stopwatch.GetTimestamp();do{Loop(Before,s,64);Loop(After,s,64);}while(Stopwatch.GetElapsedTime(start).TotalMilliseconds<p.GetProperty("scenarioWarmupMs").GetInt32());
            var a=new List<double>();var b=new List<double>();var pair=new List<double>();int wins=0;
            for(int n=0;n<rounds;n++)
            {
                Sample x,y;
                if(((n+campaign)&1)==0){x=Measure(Before,s,count,false);y=Measure(After,s,count,false);}
                else{y=Measure(After,s,count,false);x=Measure(Before,s,count,false);}
                a.Add(x.Ns);b.Add(y.Ns);pair.Add((x.Ns-y.Ns)/x.Ns);if(y.Ns<x.Ns)wins++;
            }
            var ba=Measure(Before,s,allocCount,true);var ca=Measure(After,s,allocCount,true);
            double paired=Median(pair),ratio=1-Median(b)/Median(a);bool alloc=ca.Bytes<=ba.Bytes;
            bool pass=alloc&&(s.Affected?
                wins>=p.GetProperty("minimumFasterRounds").GetInt32()&&paired>=p.GetProperty("minimumPairedReduction").GetDouble()&&ratio>=p.GetProperty("minimumRatioReduction").GetDouble():
                Median(b)/Median(a)<=1+p.GetProperty("maximumControlRegression").GetDouble());all&=pass;
            rows.Add(new{name=s.Name,affected=s.Affected,rounds,callsPerMeasurement=count,fasterRounds=wins,pairedMedianReduction=paired,ratioOfMediansReduction=ratio,
                beforeMedianNs=Median(a),afterMedianNs=Median(b),beforeNs=a,afterNs=b,beforeAllocatedBytesPerOp=ba.Bytes,afterAllocatedBytesPerOp=ca.Bytes,allocationsNotIncreased=alloc,pass});
        }
        var result=new{pass=all,profile,campaign,rows,inputKind="SYNTHETIC",scope="Complete parser only; no constructor-only, SDK, pipe, field or whole-process claim",sink};
        File.WriteAllText(Path.Combine(run,$"{profile}-campaign-{campaign}.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine(JsonSerializer.Serialize(result));if(!all)Environment.ExitCode=2;
    }
    private static int Main(string[] args)
    {
        try
        {
            if(args.Length<3)throw new ArgumentException("verify|benchmark worker-run profile [campaign]");
            string run=Path.GetFullPath(args[1]);
            if(!run.Replace('\\','/').Contains("/VirtualInputPerf-R33-20261015/frame-envelope-worker/",StringComparison.Ordinal))throw new ArgumentException("Evidence must stay in worker directory");
            if(args[0]=="verify")Verify(run,args[2]);
            else if(args[0]=="benchmark")
            {
                if(!File.Exists(Path.Combine(run,"formal-attempt.json")))throw new InvalidOperationException("Performance HELD; use reviewed driver opt-in");
                Benchmark(run,args[2],int.Parse(args[3],CultureInfo.InvariantCulture));
            }
            else throw new ArgumentException("Unknown mode");return Environment.ExitCode;
        }
        catch(Exception ex){Console.Error.WriteLine(ex);return 3;}
    }
}
