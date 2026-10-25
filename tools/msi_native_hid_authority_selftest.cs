using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using R114Offline;
internal static class MsiNativeHidAuthorityFixture
{
    static int assertions;
    static readonly List<string> failures=[];
    static readonly List<object> cases=[];
    static void Check(bool value,string why){assertions++;if(!value)failures.Add(why);}
    static MethodInfo Method(string name)=>typeof(MsiNativePortCycle).GetMethods(BindingFlags.Static|BindingFlags.NonPublic).Single(m=>m.Name==name);
    static object? Call(string name,params object?[] args)
    {
        var method=Method(name);var ps=method.GetParameters();var actual=new object?[ps.Length];Array.Copy(args,actual,args.Length);
        for(int i=args.Length;i<ps.Length;i++)actual[i]=ps[i].DefaultValue;
        try{return method.Invoke(null,actual);}catch(TargetInvocationException e){throw e.InnerException!;}
    }
    static Type FactsType=>typeof(MsiNativePortCycle).GetNestedType("GameAuthorityFacts",BindingFlags.NonPublic)!;
    static object NewFacts()=>Activator.CreateInstance(FactsType,true)!;
    static object? NewBinding(){var t=typeof(MsiNativePortCycle).GetNestedType("HidAuthorityBinding",BindingFlags.NonPublic);return t is null?null:Activator.CreateInstance(t,true);}
    static bool Current(Scenario c,object facts,object? binding=null)=> (bool)(Method("CurrentGameHid").GetParameters().Length>4?Call("CurrentGameHid",c.Target,c.Root,c.Xusb,facts,binding):Call("CurrentGameHid",c.Target,c.Root,c.Xusb,facts))!;
    static string Diagnostic(object facts)
    {
        using var output=new StringWriter();var saved=Console.Out;Console.SetOut(output);
        try{FactsType.GetMethod("WriteFailure",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(facts,["first"]);}finally{Console.SetOut(saved);}
        return output.ToString();
    }
    static Scenario ScenarioFor(string family="msi")=>new(){Family=family,WrongBoard=false,NoParent=false,Disabled=false,RootOnly=false,Xusb=false,Direct=false,AliveFalseAt=0,IdFailureDepth=0};
    static object[] OtherFamilies()
    {
        var results=new List<object>();
        foreach(var family in new[]{"rog","gpd"})foreach(var fault in new[]{"","empty","keyboard","compatible-size-code","compatible-data-code","wrong-size-type","wrong-data-type","too-small","foreign-root","parent","device-id","virtual","depth-limit","odd-data","oversize-data","zero-data","huge-size"}){
            var c=ScenarioFor(family);c.Fault=fault;
            if(fault=="empty"){c.Usage="";c.SizeBytes=4;c.DataBytes=2;}
            if(fault=="keyboard")c.Usage="HID_DEVICE_UP:0001_U:0006";
            if(fault=="wrong-size-type")c.SizeType=0x12;
            if(fault=="wrong-data-type")c.DataType=0x12;
            if(fault=="too-small")c.SizeBytes=2;
            if(fault=="virtual")c.Virtual=@"ROOT\HIDMAESTRO\VIRTUAL";
            if(fault=="odd-data")c.DataBytes=3;
            if(fault=="oversize-data")c.DataBytes=65000;
            if(fault=="zero-data")c.DataBytes=0;
            if(fault=="huge-size")c.SizeBytes=65537;
            AuthorityIo.Reset(c);var facts=NewFacts();string result;
            try{result=Current(c,facts).ToString();}catch(Exception e){result="throw:"+e.GetType().Name;}
#if LEGACY
            Check(AuthorityIo.Opens==0 && AuthorityIo.InterfaceQueries==0,"legacy nonMSI has no HID capability I/O: "+family+"/"+fault);
#endif
            results.Add(new{family,fault,result,calls=Boundary.Calls.ToArray(),diagnostic=Diagnostic(facts)});
        }
        return results.ToArray();
    }
    static object[] FullRunDifferential()
    {
        var results=new List<object>();
        void RunOne(Scenario c,string label)
        {
            AuthorityIo.Reset(c);var root=c.Root;
            var args=new List<string>{MsiNativePortCycle.Command,root,"--location",Scenario.Location,"--parent-pid","2147483000","--once"};
            if(!c.RootOnly){args.Add(c.Xusb?"--game-xusb":"--game-hid");args.Add(c.Target);}
            using var output=new StringWriter();var saved=Console.Out;Console.SetOut(output);int result;
            // This is the complete helper Run in an assembly with no unmanaged
            // entry point or real SDK/registry/lifetime implementation.
            try{result=MsiNativePortCycle.Run(args.ToArray());}finally{Console.SetOut(saved);}
#if !PREEXISTING
#if LEGACY
            Check(AuthorityIo.Opens==0 && AuthorityIo.InterfaceQueries==0,"legacy full Run no HID I/O: "+label);
#else
            if(c.Xusb || c.RootOnly)Check(AuthorityIo.Opens==0 && AuthorityIo.InterfaceQueries==0,"XUSB/root-only has no new HID I/O: "+label);
#endif
#endif
            results.Add(new{label,result,calls=Boundary.Calls.ToArray(),diagnostic=output.ToString(),hidOpens=AuthorityIo.Opens});
        }
        foreach(var family in new[]{"rog","gpd"})foreach(var xusb in new[]{false,true}){
            foreach(var fault in new[]{"","wrong-board","location-first","location-after","prepare-null","action-node","action-locate","disable","enable","cancel-first","cancel-after-prepare","cancel-final"}){
                var c=ScenarioFor(family);c.Xusb=xusb;c.Fault=fault;
                c.WrongBoard=fault=="wrong-board";
                c.AliveFalseAt=fault=="cancel-first"?1:fault=="cancel-after-prepare"?3:fault=="cancel-final"?4:0;
                RunOne(c,family+"/"+(xusb?"xusb":"hid")+"/"+fault);
            }
            foreach(var fault in xusb?new[]{"x-empty","x-size-code","x-list-code","x-retry-success","x-retry-exhausted"}:new[]{"empty","compatible-size-code","compatible-data-code","keyboard"}){
                var c=ScenarioFor(family);c.Xusb=xusb;c.Fault=fault;
                if(fault=="empty"){c.Usage="";c.SizeBytes=4;c.DataBytes=2;}
                if(fault=="keyboard")c.Usage="HID_DEVICE_UP:0001_U:0006";
                RunOne(c,family+"/"+(xusb?"xusb":"hid")+"/"+fault);
            }
        }
        foreach(var fault in new[]{"","wrong-board","location-first","location-after","sdk-null","sdk-throw","sdk-cycle-throw"}){
            var c=ScenarioFor();c.RootOnly=true;c.Fault=fault;c.WrongBoard=fault=="wrong-board";RunOne(c,"msi/root-only/"+fault);
        }
        return results.ToArray();
    }
    static int Pipeline(Scenario c,object facts,Action? prepareEdge=null,Action? finalAliveEdge=null,int cancelAt=0,bool knownBoard=true,bool moved=false)
    {
        var binding=NewBinding();int alive=0;
        bool Alive(){alive++;if(alive==4)finalAliveEdge?.Invoke();return alive!=cancelAt;}
        bool Authority()=>Current(c,facts,binding);
        Action? Prepare(string _){prepareEdge?.Invoke();return (Action?)(Method("PrepareGameRefresh").GetParameters().Length>1?Call("PrepareGameRefresh",c.Target,binding is null?null:(Func<bool>)(()=>Alive()&&Authority()&&Alive())):Call("PrepareGameRefresh",c.Target));}
        return (int)Call("ExecuteVerified",c.Root,Scenario.Location,(Func<bool>)Alive,(Func<bool>)(()=>knownBoard),(Func<string,string[]>)(_=>moved?[]:[Scenario.Location]),(Func<string,Action?>)Prepare,(Func<bool>)Authority,facts)!;
    }
    static void LegacyReproduction()
    {
        var c=ScenarioFor();c.Usage="";c.SizeBytes=4;c.DataBytes=2;AuthorityIo.Reset(c);var facts=NewFacts();
        Check(!Current(c,facts),"legacy empty CompatibleIds has no usage authority");
        var data=JsonSerializer.SerializeToElement(facts);
        Check(data.GetProperty("CompatibleCount").GetInt32()==0 && !data.GetProperty("GameUsage").GetBoolean(),"exact现场 compatible-count/game-usage");
        AuthorityIo.Reset(c);var result=Pipeline(c,facts);
        Check(result==8 && !Boundary.Calls.Any(x=>x.StartsWith("disable:")),"legacy empty => first exit8 without mutation");
        cases.Add(new{id="legacy-empty-reproduction",result,facts=JsonSerializer.SerializeToElement(facts),diagnostic=Diagnostic(facts)});
    }
#if PREEXISTING
    static void PreexistingProblems()
    {
        foreach(var family in new[]{"rog","gpd"}){
            var c=ScenarioFor(family);AuthorityIo.Reset(c);var facts=NewFacts();
            Check(Current(c,facts)&&AuthorityIo.Opens>0,"pre-existing candidate wrongly requires HID I/O for "+family);
            cases.Add(new{id="preexisting-extra-io-"+family,opens=AuthorityIo.Opens,calls=Boundary.Calls.ToArray()});
            c.Fault="compatible-data-code";AuthorityIo.Reset(c);
            Check(Current(c,NewFacts())&&AuthorityIo.Opens>0,"pre-existing nonMSI bypasses property-error shortcircuit: "+family);
        }
        var m=ScenarioFor();AuthorityIo.Reset(m,"active-lost");AuthorityIo.ForceEmpty=true;
        Check(Current(m,NewFacts()),"pre-existing accepts interface removed from active list during caps read");
        cases.Add(new{id="preexisting-active-retirement-admitted",reads=AuthorityIo.Reads});
        AuthorityIo.Reset(m);AuthorityIo.ForceEmpty=true;
        var swapped=Pipeline(m,NewFacts(),()=>{AuthorityIo.Replaced=true;AuthorityIo.PathNow=AuthorityIo.PathB;});
        Check(swapped==0,"pre-existing admits replacement node/interface between first authority and preparation");
        cases.Add(new{id="preexisting-replacement-admitted",result=swapped});
        AuthorityIo.Reset(m);AuthorityIo.ForceEmpty=true;
        var late=Pipeline(m,NewFacts(),finalAliveEdge:()=>AuthorityIo.PathNow=AuthorityIo.PathB);
        Check(late==0,"pre-existing permits interface replacement after post-prepare authority");
        cases.Add(new{id="preexisting-late-interface-replacement-admitted",result=late});
    }
#elif !LEGACY
    static void CandidateCases()
    {
        var c=ScenarioFor();
        void One(string id,bool expected,Action? configure=null,string fault="")
        {
            AuthorityIo.Reset(c,fault);AuthorityIo.ForceEmpty=true;configure?.Invoke();var facts=NewFacts();var result=Current(c,facts,NewBinding());
            Check(result==expected,id+" authority");Check(AuthorityIo.ResourcesReleased,id+" releases handles/preparsed");
            if(expected)Check(AuthorityIo.Reads>0&&AuthorityIo.MapQueries>=2,id+" fresh caps with before/after binding");
            if(fault is "mapping-changed" or "node-changed" or "active-lost")Check(JsonSerializer.SerializeToElement(facts).GetProperty("HidCapabilityState").GetString()=="interface-retired",id+" explicit binding-failure reason");
            if(fault=="caps-failed")Check(JsonSerializer.SerializeToElement(facts).GetProperty("HidCapabilityState").GetString()=="hid-capabilities-unavailable",id+" explicit caps-read-failure reason");
            cases.Add(new{id,result,opens=AuthorityIo.Opens,reads=AuthorityIo.Reads,freed=AuthorityIo.Freed,facts=JsonSerializer.SerializeToElement(facts)});
        }
        One("gamepad-empty-compatible",true);
        One("joystick-empty-compatible",true,()=>AuthorityIo.Usage=4);
        One("keyboard",false,()=>AuthorityIo.Usage=6);
        One("mouse",false,()=>AuthorityIo.Usage=2);
        One("vendor-collection",false,()=>AuthorityIo.Page=0xff00);
        One("command-collection",false,()=>{AuthorityIo.Page=0xff01;AuthorityIo.Usage=1;});
        One("wrong-vid",false,()=>AuthorityIo.Vid=0x1234);
        One("wrong-pid",false,()=>AuthorityIo.Pid=0x1902);
        One("zero-input-report",false,()=>AuthorityIo.InputLength=0);
        foreach(var fault in new[]{"no-interfaces","interface-size-failed","interface-list-failed","map-failed","map-wrong-type","map-no-terminator","open-failed","attributes-failed","preparsed-null","preparsed-failed","preparsed-failed-with-pointer","caps-failed","mapping-changed","node-changed","active-lost"})One(fault,false,fault:fault);
        foreach(var fault in new[]{"foreign-root","virtual","depth-limit","device-id","locate"}){
            c.Fault=fault;c.Virtual=fault=="virtual"?@"ROOT\HIDMAESTRO\VIRTUAL":"";
            AuthorityIo.Reset(c);AuthorityIo.ForceEmpty=true;
            Check(!Current(c,NewFacts(),NewBinding())&&AuthorityIo.Opens==0,"parent/input shortcircuit "+fault);
        }
        c.Fault="";c.Virtual="";
        foreach(var ig in new[]{"IG_00","IG_01","IG_02","IG_07","MI_03"}){
            // Use an explicit instance argument so no IG name is authority.
            AuthorityIo.Reset(c);AuthorityIo.ForceEmpty=true;
            var target=c.Target.Replace("IG_03",ig);AuthorityIo.Mapping=AuthorityIo.ExpectedInstance=target;
            Check((bool)Call("CurrentHidGameUsage",target,c.Root,10u,NewFacts())!,"game collection independent of name "+ig);
            Check(AuthorityIo.ResourcesReleased,"name variation releases resources "+ig);
        }
        void Flow(string id,int expected,Action? prep=null,Action? final=null,int cancel=0,string fault="",bool board=true,bool moved=false)
        {
            AuthorityIo.Reset(c,fault);AuthorityIo.ForceEmpty=true;var facts=NewFacts();
            var result=Pipeline(c,facts,prep,final,cancel,board,moved);
            Check(result==expected,id+" result "+result+" expected "+expected);
            Check(AuthorityIo.ResourcesReleased,id+" all resources released");
            Check(expected==0?Boundary.Calls.Count(x=>x.StartsWith("disable:"))==1&&!Boundary.Calls.Any(x=>x.StartsWith("sdk-")):!Boundary.Calls.Any(x=>x.StartsWith("disable:")),id+" mutation boundary");
            if(expected==0)Check(AuthorityIo.Reads==3,id+" caps freshly read first/after-prepare/immediately-before-mutation");
            if(id is "replaced-between-first-and-prepare" or "changed-path-after-prepare")Check(JsonSerializer.SerializeToElement(facts).GetProperty("HidCapabilityState").GetString()=="interface-replaced",id+" explicit pinned-binding-failure reason");
            cases.Add(new{id,result,reads=AuthorityIo.Reads,calls=Boundary.Calls.ToArray(),facts=JsonSerializer.SerializeToElement(facts),diagnostic=result==8?Diagnostic(facts):""});
        }
        Flow("verified-game-flow",0);
        Flow("replaced-between-first-and-prepare",8,()=>{AuthorityIo.Replaced=true;AuthorityIo.PathNow=AuthorityIo.PathB;});
        Flow("changed-path-after-prepare",8,final:()=>AuthorityIo.PathNow=AuthorityIo.PathB);
        Flow("non-game-after-prepare",8,final:()=>AuthorityIo.Usage=6);
        Flow("cancel-before-reads",3,cancel:1);
        Flow("cancel-after-prepare",3,cancel:3);
        Flow("cancel-final",3,cancel:4);
        Flow("cancel-final-guard-before-caps",8,cancel:5);
        Flow("cancel-final-guard-after-caps",8,cancel:6);
        Flow("unknown-machine",4,board:false);
        Flow("moved-location",5,moved:true);
        Flow("caps-read-throws",6,fault:"caps-throw");
        // Maximum existing parent depth plus optional diagnostics stays bounded.
        c.RootDepth=15;AuthorityIo.Reset(c);AuthorityIo.ForceEmpty=true;AuthorityIo.Usage=6;var deep=NewFacts();
        Check(!Current(c,deep,NewBinding()),"non-game with deepest chain");
        var line=Diagnostic(deep);Check(line.Length>0&&Encoding.UTF8.GetByteCount(line)<=2048,"schema1 optional diagnostics UTF8 byte cap");
        Check(line.Contains("\"Schema\":1")&&line.Contains("\"HidUsagePage\":1")&&line.Contains("\"HidUsage\":6")&&line.Contains("non-game-or-wrong-device"),"diagnostic includes actual usage and reason");
        cases.Add(new{id="deep-schema1-diagnostic",utf8Bytes=Encoding.UTF8.GetByteCount(line),diagnostic=line});
        Check(Diagnostic(deep)=="","one diagnostic only");
        var oversized=NewFacts();FactsType.GetProperty("HidCapabilityState")!.SetValue(oversized,new string('x',4096));Check(Diagnostic(oversized)=="","oversized diagnostics suppressed");
        var attr=typeof(MsiNativePortCycle).GetNestedType("HidAttributes",BindingFlags.NonPublic)!;var caps=typeof(MsiNativePortCycle).GetNestedType("HidCaps",BindingFlags.NonPublic)!;
        Check(Marshal.SizeOf(attr)==12&&Marshal.SizeOf(caps)==64,"Win32 HID attribute/caps ABI layout");
    }
#endif
    static int Main(string[] args)
    {
        var original=Console.Out;using var silence=new StringWriter();Console.SetOut(silence);
        try{
            Check(MsiNativePortCycle.SelfTest(),"all 101 upstream SelfTest cases");
            var upstreamSelfTest=JsonSerializer.Deserialize<JsonElement>(silence.ToString().Split(new[]{'\r','\n'},StringSplitOptions.RemoveEmptyEntries)[0]);
            Check(upstreamSelfTest.GetProperty("cases").GetInt32()==101&&upstreamSelfTest.GetProperty("failures").GetInt32()==0,"upstream exact 101-case receipt");
#if LEGACY
            LegacyReproduction();
#elif PREEXISTING
            PreexistingProblems();
#else
            CandidateCases();
#endif
            var otherFamilies=OtherFamilies();var fullRunDifferential=FullRunDifferential();
            var receipt=new{assertions,failures,upstreamSelfTest,cases,otherFamilies,fullRunDifferential,realHardwareAccess=false,productEntryInvoked=false,helperRunInvokedWithInertBoundaries=true};
            File.WriteAllText(args[0],JsonSerializer.Serialize(receipt,new JsonSerializerOptions{WriteIndented=true}));
            Console.SetOut(original);Console.WriteLine(JsonSerializer.Serialize(new{assertions,failures,cases=cases.Count,otherFamilies=otherFamilies.Length,fullRunDifferential=fullRunDifferential.Length,realHardwareAccess=false}));
            return failures.Count==0?0:1;
        }catch(Exception e){Console.SetOut(original);Console.WriteLine(e);return 2;}finally{Console.SetOut(original);}
    }
}
