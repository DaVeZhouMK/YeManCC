using System.Reflection;
using System.Text;
using System.Text.Json;
using R114Offline;
internal static class Fixture
{
    private const string Prefix="YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ";
    private static readonly Type Helper=typeof(MsiNativePortCycle);
    private static readonly Type? FactsType=Helper.GetNestedType("GameAuthorityFacts",BindingFlags.NonPublic);
    private static int assertions,MaxBytes,MaxParents,MaxInterfaceAttempts,MaxGuardBytes;
    private static readonly List<string> Failures=[];
    private sealed record Test(Scenario Config,int Expected,Action<JsonElement>? Inspect=null);
    private sealed record Outcome(string Id,int Result,string[] Calls,int DiagnosticCount,int Utf8Bytes,int ConsoleCalls,int SerializeCalls,JsonElement? Facts);
    private sealed class Output:TextWriter
    {
        internal readonly StringBuilder Text=new();
        internal int Writes;
        public override Encoding Encoding=>Encoding.UTF8;
        public override void WriteLine(string? value){
            Writes++;
            if(Boundary.Config.Writer=="throw")throw new IOException("inert closed stdout");
            if(Boundary.Config.Writer=="partial-throw"){Text.Append(value![..40]);throw new IOException("inert partial pipe write");}
            Text.Append(value).Append(Environment.NewLine);
        }
    }
    private static void Check(bool value,string label){assertions++;if(!value)Failures.Add(label);}
    private static object? Call(string name,params object?[] args)
    {
        var matches=Helper.GetMethods(BindingFlags.Static|BindingFlags.NonPublic).Where(m=>m.Name==name && m.GetParameters().Length>=args.Length).ToArray();
        var method=matches.SingleOrDefault(m=>m.GetParameters().Length==args.Length) ?? matches.Single();
        var parameters=method.GetParameters();
        var actual=new object?[parameters.Length];Array.Copy(args,actual,args.Length);
        for(var i=args.Length;i<actual.Length;i++)actual[i]=parameters[i].DefaultValue;
        return method.Invoke(null,actual);
    }
    private static object? NewFacts()=>FactsType?.GetMethod("TryCreate",BindingFlags.NonPublic|BindingFlags.Static)!.Invoke(null,null);
    private static int ExecuteDirect(Scenario c)
    {
        using var parent=ParentProcessLifetime.TryOpen(999999);
        if(parent is null)return 3;
        var facts=c.Disabled?null:NewFacts();
        var checks=0;
        Func<bool> authority=()=>{
            checks++;var fail=c.Stage=="first"?checks==1:checks==2;
            var target=fail && c.DirectTarget.Length>0?c.DirectTarget:c.Target;
            var root=fail && c.DirectRoot.Length>0?c.DirectRoot:c.Root;
            return (bool)(FactsType is null?Call("CurrentGameHid",target,root,c.Xusb):Call("CurrentGameHid",target,root,c.Xusb,facts))!;
        };
        object?[] args=[c.Root,Scenario.Location,(Func<bool>)(()=>parent.IsAlive),(Func<bool>)(()=>(bool)Call("CurrentBoardAllowed",c.Root)!),(Func<string,string[]>)(root=>(string[])Call("CurrentLocations",root)!),(Func<string,Action?>)(_ => (Action?)Call("PrepareGameRefresh",c.Target)),authority];
        return (int)(FactsType is null?Call("ExecuteVerified",args):Call("ExecuteVerified",args.Concat(new[]{facts}).ToArray()))!;
    }
    private static Outcome Run(Test test)
    {
        var c=test.Config;Boundary.Reset(c);
        var writer=new Output();var saved=Console.Out;int result;
        Console.SetOut(writer);
        try{
            var args=new List<string>{MsiNativePortCycle.Command,c.Root,"--location",Scenario.Location,"--parent-pid","999999","--once"};
            if(!c.RootOnly)args.AddRange([c.Xusb?"--game-xusb":"--game-hid",c.Target]);
            result=c.Direct || c.Disabled?ExecuteDirect(c):MsiNativePortCycle.Run(args.ToArray());
        }finally{Console.SetOut(saved);}
        var text=writer.Text.ToString();var bytes=Encoding.UTF8.GetByteCount(text);
        var lines=text.Split(['\r','\n'],StringSplitOptions.RemoveEmptyEntries);
        Check(result==test.Expected,c.Id+": expected result "+test.Expected+" got "+result);
        Check(lines.Length<=1,c.Id+": one record cap");
        Check(bytes<=2048,c.Id+": whole stdout UTF8 cap");
        Check(writer.Writes<=1 && Boundary.SerializeCalls<=1,c.Id+": one serialization/write attempt");
        if(c.Disabled)Check(writer.Writes==0 && Boundary.SerializeCalls==0,c.Id+": disabled diagnostic has zero serialization/output");
        JsonElement? facts=null;
        var shouldLog=FactsType is not null && result==8 && !c.Disabled && c.Writer!="throw" && c.Serializer.Length==0;
        Check(lines.Length==(shouldLog?1:0),c.Id+": failure-only diagnostic");
        if(shouldLog && lines.Length==1){
            Check(lines[0].StartsWith(Prefix,StringComparison.Ordinal),c.Id+": recognizable prefix");
            using var doc=JsonDocument.Parse(lines[0][Prefix.Length..]);facts=doc.RootElement.Clone();
            var f=facts.Value;
            Check(f.GetProperty("Schema").GetInt32()==1 && f.GetProperty("Kind").GetString()=="ymcc.gameAuthority.failure" && f.GetProperty("Result").GetInt32()==8,c.Id+": schema");
            Check(f.GetProperty("Stage").GetString()==c.Stage,c.Id+": guard stage");
            Check(f.GetProperty("Xusb").GetBoolean()==c.Xusb,c.Id+": transport kind");
            var parents=f.GetProperty("ParentChain").GetArrayLength();var attempts=f.GetProperty("InterfaceAttempts").GetArrayLength();
            Check(parents<=16 && attempts<=2,c.Id+": facts count caps");
            MaxParents=Math.Max(MaxParents,parents);MaxInterfaceAttempts=Math.Max(MaxInterfaceAttempts,attempts);
            test.Inspect?.Invoke(f);
        }
        if(result==8)Check(!Boundary.Calls.Any(x=>x.StartsWith("disable:") || x.StartsWith("enable:") || x=="sdk-cycle"),c.Id+": denial precedes mutation");
        if(result==0)Check(writer.Writes==0 && Boundary.SerializeCalls==0,c.Id+": success never serializes or logs");
        if(c.RootOnly)Check(!Boundary.Calls.Any(x=>x.StartsWith("device-id:") || x.StartsWith("interface-") || x.StartsWith("property:10:")),c.Id+": independent root path");
        MaxBytes=Math.Max(MaxBytes,bytes);
        return new(c.Id,result,Boundary.Calls.ToArray(),lines.Length,bytes,writer.Writes,Boundary.SerializeCalls,facts);
    }
    private static void Null(JsonElement f,string key)=>Check(f.GetProperty(key).ValueKind==JsonValueKind.Null,"unevaluated "+key);
    private static void Value(JsonElement f,string key,bool value)=>Check(f.GetProperty(key).GetBoolean()==value,"observed "+key);
    private static List<Test> Tests()
    {
        var tests=new List<Test>();
        foreach(var stage in new[]{"first","after-prepare"}){
            Scenario S(string id,string fault="",bool xusb=false)=>new(){Id=stage+"/"+id,Stage=stage,Fault=fault,Xusb=xusb};
            void Add(Scenario c,int result=8,Action<JsonElement>? inspect=null)=>tests.Add(new(c,result,inspect));
            var locate=S("locate","locate");locate.Error=uint.MaxValue;
            Add(locate,8,f=>{Check(f.GetProperty("LocateResult").GetUInt32()==uint.MaxValue,"exact Locate error");Null(f,"CompatibleSize");Check(f.GetProperty("ParentChain").GetArrayLength()==0,"Locate short circuit");});
            var size=S("compatible-size-code","compatible-size-code");size.Error=5;
            Add(size,8,f=>{Check(f.GetProperty("CompatibleSize").GetProperty("Result").GetUInt32()==5,"size result");Null(f,"CompatibleData");});
            foreach(var bytes in new[]{0,1,2,3,65537}){var c=S("compatible-size-"+bytes);c.SizeBytes=bytes;Add(c,8,f=>{Check(f.GetProperty("CompatibleSize").GetProperty("Bytes").GetUInt32()==bytes,"size bytes");Null(f,"CompatibleData");});}
            var data=S("compatible-data-code","compatible-data-code");data.Error=uint.MaxValue;
            Add(data,8,f=>{Check(f.GetProperty("CompatibleData").GetProperty("Result").GetUInt32()==uint.MaxValue,"data error");Null(f,"CompatibleCount");});
            foreach(var type in new uint[]{0,0x12,0x2010,uint.MaxValue}){var c=S("compatible-type-"+type);c.DataType=type;Add(c,8,f=>Check(f.GetProperty("CompatibleData").GetProperty("Type").GetUInt32()==type,"data type"));}
            foreach(var depth in new[]{0,1,2,15}){var c=S("device-id-"+depth,"device-id");c.RootDepth=15;c.IdFailureDepth=depth;Add(c,8,f=>{Check(f.GetProperty("ParentChain").GetArrayLength()==depth+1,"ID failure depth");Check(f.GetProperty("ChainEnd").GetString()=="device-id-error","ID stop");Null(f,"RootInChain");});}
            Add(S("parent-error","parent-error"),8,f=>{Check(f.GetProperty("ParentChain")[0].GetProperty("ParentResult").GetUInt32()==13,"parent error observed");Value(f,"RootInChain",false);Null(f,"NoVirtualParent");Null(f,"GameUsage");});
            Add(S("foreign-root","foreign-root"),8,f=>{Value(f,"RootInChain",false);Null(f,"NoVirtualParent");});
            Add(S("depth-limit","depth-limit"),8,f=>Check(f.GetProperty("ParentChain").GetArrayLength()==16 && f.GetProperty("ChainEnd").GetString()=="depth-limit","16 reads only"));
            Add(S("self-loop","self-loop"),8,f=>Check(f.GetProperty("ParentChain").GetArrayLength()==16,"loop still bounded"));
            foreach(var marker in new[]{@"ROOT\VIGEMBUS\VIRTUAL",@"ROOT\HIDMAESTRO\VIRTUAL"}){var c=S("virtual-"+marker);c.Virtual=marker;Add(c,8,f=>{Value(f,"RootInChain",true);Value(f,"NoVirtualParent",false);Null(f,"GameUsage");});}
            foreach(var usage in new[]{"","HID_DEVICE_UP:0001_U:0006","HID_DEVICE_UP:0001_U:0002","HID_DEVICE_UP:000C_U:0001","HID_DEVICE_UP:0020_U:0001","HID_DEVICE_UP:FF00_U:0001","HID_DEVICE_UP:0001_U:00050"," HID_DEVICE_UP:0001_U:0005","HID_DEVICE_UP:0001_U:0005 "}){var c=S("usage-"+usage);c.Usage=usage;Add(c,8,f=>{Value(f,"RootInChain",true);Value(f,"NoVirtualParent",true);Value(f,"GameUsage",false);});}
            foreach(var bytes in new[]{0,1,3,4}){var c=S("compatible-data-bytes-"+bytes);c.DataBytes=bytes;if(bytes==4)c.SizeBytes=4;Add(c,8,f=>Value(f,"GameUsage",false));}
            var count=S("compatible-max-count","compatible-max-count");count.SizeBytes=65536;Add(count,8,f=>Check(f.GetProperty("CompatibleCount").GetInt32()==16384,"all existing IDs counted without logging IDs"));
            var oversized=S("compatible-decode-throws");oversized.DataBytes=65536;Add(oversized,6);
            Add(S("cm-throws","locate-throw"),6);
            foreach(var fault in new[]{"x-size-code","x-size-short","x-size-long","x-list-code","x-retry-exhausted","x-empty"}){
                var c=S(fault,fault,true);Add(c,8,f=>{Value(f,"ActiveXusb",false);Null(f,"RootInChain");Null(f,"GameUsage");var a=f.GetProperty("InterfaceAttempts");Check(a.GetArrayLength()==(fault=="x-retry-exhausted"?2:1),"XUSB attempt count");if(fault=="x-retry-exhausted")Check(a[1].GetProperty("ListResult").GetUInt32()==26,"retry exhaustion retained");});
            }
            var xf=S("x-foreign","foreign-root",true);Add(xf,8,f=>{Value(f,"ActiveXusb",true);Value(f,"RootInChain",false);});
            foreach(var marker in new[]{@"ROOT\VIGEMBUS\VIRTUAL",@"ROOT\HIDMAESTRO\VIRTUAL"}){var c=S("x-virtual-"+marker,"",true);c.Virtual=marker;Add(c,8,f=>{Value(f,"ActiveXusb",true);Value(f,"RootInChain",true);Value(f,"NoVirtualParent",false);});}
            var xp=S("x-max-parent-and-retries","x-retry-exhausted",true);xp.RootDepth=15;Add(xp,8,f=>Check(f.GetProperty("ParentChain").GetArrayLength()==16 && f.GetProperty("InterfaceAttempts").GetArrayLength()==2,"maximum facts counts"));
            foreach(var direct in new[]{"invalid-root","invalid-hid","invalid-xusb","foreign-vendor"}){
                var c=S(direct);c.Direct=true;
                if(direct=="invalid-root")c.DirectRoot=@"ROOT\WRONG\ROOT";
                if(direct=="invalid-hid")c.DirectTarget=@"HID\INVALID";
                if(direct=="invalid-xusb"){c.Xusb=true;c.DirectTarget=@"USB\INVALID";}
                if(direct=="foreign-vendor")c.DirectTarget=@"HID\VID_0B05&PID_1B4C&IG_03\ROG";
                Add(c,8,f=>{Value(f,"InputsValid",direct=="foreign-vendor");if(direct!="foreign-vendor")Null(f,"LocateResult");Null(f,"RootInChain");});
            }
            foreach(var mode in new[]{"disabled","writer-throw","serializer-throw","oversize"}){
                var c=S(mode,"locate");c.Disabled=mode=="disabled";c.Writer=mode=="writer-throw"?"throw":"";c.Serializer=mode=="serializer-throw"?"throw":mode=="oversize"?"oversize":"";Add(c);
            }
        }
        foreach(var family in new[]{"msi","rog","gpd"})foreach(var xusb in new[]{false,true})tests.Add(new(new(){Id="success/"+family+"/"+xusb,Family=family,Xusb=xusb},0));
        foreach(var usage in new[]{"HID_DEVICE_UP:0001_U:0004","hid_device_up:0001_u:0005"})tests.Add(new(new(){Id="success/"+usage,Usage=usage},0));
        foreach(var stage in new[]{"first","after-prepare"}){
            tests.Add(new(new(){Id=stage+"/success-x-retry",Stage=stage,Xusb=true,Fault="x-retry-success"},0));
            tests.Add(new(new(){Id=stage+"/size-type-unchecked",Stage=stage,SizeType=uint.MaxValue},0));
            tests.Add(new(new(){Id=stage+"/compatible-max-size",Stage=stage,SizeBytes=65536},0));
        }
        tests.Add(new(new(){Id="root/success",RootOnly=true},0));
        foreach(var (fault,result) in new[]{("sdk-null",7),("sdk-throw",6),("sdk-cycle-throw",6),("location-first",5),("location-after",5)})tests.Add(new(new(){Id="root/"+fault,Fault=fault,RootOnly=true},result));
        foreach(var alive in new[]{1,2,3,4})foreach(var rootOnly in new[]{false,true})tests.Add(new(new(){Id="alive/"+alive+"/"+rootOnly,AliveFalseAt=alive,RootOnly=rootOnly},3));
        foreach(var rootOnly in new[]{false,true}){
            tests.Add(new(new(){Id="parent-open/"+rootOnly,NoParent=true,RootOnly=rootOnly},3));
            tests.Add(new(new(){Id="board/"+rootOnly,WrongBoard=true,RootOnly=rootOnly},4));
        }
        foreach(var (fault,result) in new[]{("location-first",5),("location-after",5),("prepare-null",7),("action-locate",6),("action-node",6),("disable",6),("enable",6)})tests.Add(new(new(){Id="other/"+fault,Fault=fault},result));
        return tests;
    }
    private sealed record PolicyOutcome(string Id,bool Admitted,string[] Calls);
    private sealed class ObservedList(string name,string[] values):IReadOnlyList<string>
    {
        public int Count{get{Boundary.Calls.Add(name+":count");return values.Length;}}
        public string this[int index]{get{Boundary.Calls.Add(name+":index:"+index);return values[index];}}
        public IEnumerator<string> GetEnumerator(){Boundary.Calls.Add(name+":begin");try{foreach(var v in values){Boundary.Calls.Add(name+":value:"+v);yield return v;}}finally{Boundary.Calls.Add(name+":dispose");}}
        System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator()=>GetEnumerator();
    }
    private static List<PolicyOutcome> PolicyScans()
    {
        var results=new List<PolicyOutcome>();
        foreach(var xusb in new[]{false,true})foreach(var branch in new[]{"instance","root","vendor","missing-root","vigem","hidmaestro","usage","accepted"}){
            var c=new Scenario{Xusb=xusb};Boundary.Reset(c);var root=branch=="root"?"INVALID":c.Root;
            var target=branch=="instance"?"INVALID":branch=="vendor"?(xusb?@"USB\VID_0B05&PID_1B4C&MI_00\FOREIGN":@"HID\VID_0B05&PID_1B4C&IG_03\FOREIGN"):c.Target;
            var parents=new ObservedList("parents",branch=="missing-root"?[c.Target,c.Root+"-OTHER"]:branch=="vigem"?[c.Target,@"ROOT\VIGEMBUS\VIRTUAL",c.Root]:branch=="hidmaestro"?[c.Target,@"ROOT\HIDMAESTRO\VIRTUAL",c.Root]:[c.Target,c.Root]);
            var compatible=new ObservedList("compatible",branch=="usage"?["HID_DEVICE_UP:0001_U:0006"]:["HID_DEVICE_UP:0001_U:0005"]);
            var facts=NewFacts();
            object?[] args=xusb?[target,root,parents,true]:[target,root,parents,compatible];
            var admitted=(bool)(FactsType is null?Call(xusb?"GameXusbAdmitted":"GameHidAdmitted",args):Call(xusb?"GameXusbAdmitted":"GameHidAdmitted",args.Concat(new[]{facts}).ToArray()))!;
            Check(admitted==(branch=="accepted" || xusb && branch=="usage"),"pure exact policy branch "+xusb+"/"+branch);
            results.Add(new(xusb+"/"+branch,admitted,Boundary.Calls.ToArray()));
        }
        foreach(var active in new[]{false,true}){
            var c=new Scenario{Xusb=true};Boundary.Reset(c);var parents=new ObservedList("parents",[c.Root]);
            var admitted=MsiNativePortCycle.GameXusbAdmitted(c.Target,c.Root,parents,active);
            Check(admitted==active,"active short circuit");results.Add(new("xusb-active/"+active,admitted,Boundary.Calls.ToArray()));
        }
        return results;
    }
    private static void OneShot()
    {
        if(FactsType is null)return;
        foreach(var writerMode in new[]{"","throw","partial-throw"})foreach(var serializer in new[]{"","throw","oversize","at-cap","over-cap","utf8-over-cap"}){
            Boundary.Reset(new(){Writer=writerMode,Serializer=serializer});
            var facts=NewFacts();var output=new Output();var saved=Console.Out;Console.SetOut(output);
            try{var write=FactsType.GetMethod("WriteFailure",BindingFlags.Instance|BindingFlags.NonPublic)!;write.Invoke(facts,["first"]);write.Invoke(facts,["after-prepare"]);}
            finally{Console.SetOut(saved);}
            Check(Boundary.SerializeCalls==1,"one-shot even when serialization/output fails");
            Check(output.Writes==(serializer.Length==0 || serializer=="at-cap"?1:0),"one write attempt cap");
            var bytes=Encoding.UTF8.GetByteCount(output.Text.ToString());MaxGuardBytes=Math.Max(MaxGuardBytes,bytes);
            Check(bytes<=2048,"byte guard includes prefix/newline, partial writes and multibyte UTF8");
            if(serializer=="at-cap" && writerMode.Length==0)Check(bytes==2048,"exact 2048-byte record accepted");
            Check(Boundary.Calls.Count==0,"diagnostic emission adds no OS calls");
        }
    }
    internal static int Main(string[] args)
    {
        var saved=Console.Out;var selftest=new StringWriter();bool okay;
        Console.SetOut(selftest);try{okay=MsiNativePortCycle.SelfTest();}finally{Console.SetOut(saved);}
        using var self=JsonDocument.Parse(selftest.ToString());Check(okay && self.RootElement.GetProperty("failures").GetInt32()==0,"unchanged production SelfTest");
        var results=Tests().Select(Run).ToList();var policyScans=PolicyScans();OneShot();
        Boundary.Reset(new());Check(MsiNativePortCycle.Run(["invalid"])==2 && Boundary.Calls.Count==0,"invalid CLI no I/O");
        var receipt=new{instrumented=FactsType is not null,scenarios=results.Count,assertions,failures=Failures,existingSelfTest=self.RootElement.Clone(),maxDiagnosticUtf8Bytes=MaxBytes,maxParentSteps=MaxParents,maxInterfaceAttempts=MaxInterfaceAttempts,maxGuardUtf8Bytes=MaxGuardBytes,realHardwareAccess=false,realSdkLinked=false,policyScans,results};
        File.WriteAllText(args[0],JsonSerializer.Serialize(receipt,new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine(JsonSerializer.Serialize(new{receipt.scenarios,receipt.assertions,failures=Failures.Count,receipt.existingSelfTest,receipt.maxDiagnosticUtf8Bytes,receipt.maxParentSteps,receipt.maxInterfaceAttempts}));
        foreach(var f in Failures)Console.Error.WriteLine(f);
        return Failures.Count==0?0:1;
    }
}
