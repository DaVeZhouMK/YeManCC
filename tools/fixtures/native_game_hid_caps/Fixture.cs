using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;

internal sealed record Scenario(string Name,string Family="msi",string Fault="",int FailProof=1,string Compatible="",ushort UsagePage=1,ushort Usage=5,string Board="",bool Xusb=false,bool GpdRoot=false,bool GpdSibling=false);

internal static class Boundary
{
    internal static Scenario Case=new("initial");
    internal static int Proofs,Disabled,Enabled,Opened,Preparsed,Freed,AliveReads,InterfaceQueries;
    internal static readonly List<string> Calls=[];
    internal static int Factories,Conversions,Cycles;
    internal static readonly List<Guid> InterfaceTypes=[];
    internal static readonly List<string> InterfaceInstances=[];
    internal static bool Prepared=>Case.GpdRoot && Conversions>0;
    internal static bool RootFault(string name)=>Case.GpdRoot && Case.Fault==name;
    internal static bool Timing;
    internal static bool ColdLifetime;
    internal static long ClockMs;
    internal static long DisableDelayMs=17;
    internal static int ObservationLocates;
    internal static string ClockFault="";
    internal static string Root=>Case.Family=="rog"?@"USB\VID_0B05&PID_1B4C\ROG":Case.Family=="rog-original"?@"USB\VID_0B05&PID_1ABE\ROG":Case.Family=="gpd"?@"USB\VID_045E&PID_028E\GPD":@"USB\VID_0DB0&PID_1901\00006F64096B22E7";
    internal static string Target=>Case.GpdRoot?(Case.GpdSibling?Root+"-SIBLING":Root):Case.Xusb?((Case.Name.Contains("field-hid-xusb",StringComparison.Ordinal)?"HID":"USB")+Root[3..21]+@"&MI_00\XUSB"):Case.Family=="msi"?@"HID\VID_0DB0&PID_1901&IG_02\8&3E27176&0&0000":"HID"+Root[3..21]+@"&IG_02\GAME";
    internal const string Position="PCIROOT(0)#USBROOT(0)#USB(4)";
    internal static bool Fault(string name)=>Case.Fault==name && Proofs>=Case.FailProof;
    internal static void Reset(Scenario scenario){Case=scenario;Proofs=Disabled=Enabled=Opened=Preparsed=Freed=AliveReads=InterfaceQueries=0;Calls.Clear();Timing=false;ClockMs=0;DisableDelayMs=17;ObservationLocates=0;ClockFault="";Factories=Conversions=Cycles=0;InterfaceTypes.Clear();InterfaceInstances.Clear();}
}

internal static class Fixture
{
    private static readonly List<object> Results=[];
    private static readonly List<string> Failures=[];
    private static int assertions;
    private static void Check(bool value,string label){assertions++;if(!value)Failures.Add(label);}
    private static void Run(Scenario scenario,int expected,bool baseline)
    {
        Boundary.Reset(scenario);
        var previous=Console.Out;using var capture=new StringWriter();Console.SetOut(capture);
        int result;
        try{result=MsiNativePortCycle.Run([MsiNativePortCycle.Command,Boundary.Root,"--location",Boundary.Position,"--parent-pid","999999","--once",scenario.Xusb?"--game-xusb":"--game-hid",Boundary.Target]);}
        finally{Console.SetOut(previous);}
        var quarantined=!baseline && scenario.Family=="rog" && Boundary.Target.StartsWith(@"HID",StringComparison.Ordinal);
        var expectedResult=quarantined?8:baseline && expected==0 && scenario.Compatible.Length==0?8:expected;
        Check(result==expectedResult,scenario.Name+" result="+result+" expected="+expectedResult);
        if(expectedResult!=0 && expectedResult!=6)Check(Boundary.Disabled==0,scenario.Name+" refused before disable");
        else if(expectedResult==0){Check(Boundary.Disabled==1 && Boundary.Enabled==1,scenario.Name+" one node refresh");Check(scenario.Xusb?Boundary.Proofs==0 && Boundary.Opened==0 && Boundary.InterfaceQueries==2:Boundary.Proofs==3,scenario.Name+" current proof boundary");}
        Check(Boundary.Freed==Boundary.Preparsed,scenario.Name+" preparsed data released");
        var text=capture.ToString();Check(Encoding.UTF8.GetByteCount(text)<=2048,scenario.Name+" diagnostic cap");
        JsonElement? diagnostic=null;
        if(result==8 && text.Length>0){var prefix="YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ";Check(text.StartsWith(prefix,StringComparison.Ordinal),scenario.Name+" existing diagnostic prefix");using var parsed=JsonDocument.Parse(text[prefix.Length..]);diagnostic=parsed.RootElement.Clone();}
        if(quarantined){
            Check(Boundary.Disabled==0 && Boundary.Enabled==0 && Boundary.Opened==0 && Boundary.Proofs==0 && Boundary.InterfaceQueries==0 && Boundary.Calls.Count==0,scenario.Name+" quarantined ROG HID node has no device operations");
            Check(diagnostic is not null && diagnostic.Value.TryGetProperty("MutationPolicy",out var policy) && policy.GetString()=="rog-hid-game-node-refresh-unvalidated",scenario.Name+" mutation policy differs from identity recognition");
        }
        if(expectedResult==6){
            const string prefix="YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ";
            Check(text.StartsWith(prefix,StringComparison.Ordinal),scenario.Name+" refresh diagnostic present");
            if(text.StartsWith(prefix,StringComparison.Ordinal)){
                using var parsed=JsonDocument.Parse(text[prefix.Length..]);diagnostic=parsed.RootElement.Clone();
                var d=diagnostic.Value;
                bool FactBool(string key,bool expected)=>d.TryGetProperty(key,out var value) && value.ValueKind==(expected?JsonValueKind.True:JsonValueKind.False);
                bool FactUint(string key,uint expected)=>d.TryGetProperty(key,out var value) && value.ValueKind==JsonValueKind.Number && value.TryGetUInt32(out var number) && number==expected;
                Check(d.GetProperty("Schema").GetInt32()==1 && d.GetProperty("Kind").GetString()=="ymcc.gameRefresh.failure" && d.GetProperty("Result").GetInt32()==6,scenario.Name+" refresh diagnostic identity");
                var disableFault=scenario.Fault.StartsWith("disable",StringComparison.Ordinal);
                var throws=scenario.Fault is "disable-throw" or "enable-throw" or "enable-once-throw" or "enable-compensation-result";
                var denied=scenario.Fault is "enable-identity-retired" or "enable-node-replaced" or "enable-location-changed";
                var guardThrows=scenario.Fault is "enable-authority-throws" or "enable-location-throws";
                var compensationScope=scenario.Family=="rog";
                var compensation=compensationScope && !disableFault && !denied && !guardThrows;
                var compensationThrows=scenario.Fault is "enable-throw" or "enable-compensation-throws";
                Check(d.GetProperty("Stage").GetString()==(disableFault?"disable":"enable"),scenario.Name+" exact failed stage");
                Check(d.GetProperty("Xusb").GetBoolean()==scenario.Xusb,scenario.Name+" exact input route");
                Check(d.GetProperty("PrepareLocateResult").GetUInt32()==0 && d.GetProperty("CurrentLocateResult").GetUInt32()==0 && d.GetProperty("SameNode").GetBoolean(),scenario.Name+" exact current-node facts");
                Check(d.GetProperty("DisableCallEntered").GetBoolean() && d.GetProperty("EnableCallEntered").GetBoolean()==!disableFault,scenario.Name+" exact entered calls");
                Check(d.GetProperty("TimingClock").GetString()=="monotonic" && d.GetProperty("DisableCallElapsedMs").GetInt64()>=0,scenario.Name+" entered disable monotonic milliseconds retained including throw");
                if(scenario.Fault=="disable-result"){
                    var observation=d.GetProperty("DisableFailureStatus");
                    Check(d.GetProperty("DisableFailureObservationElapsedMs").GetInt64()>=0 && observation.GetProperty("State").GetString()=="observed",scenario.Name+" failed disable observation timed");
                    foreach(var key in new[]{"LocateElapsedMs","StatusElapsedMs","PostLocateElapsedMs"})Check(observation.GetProperty(key).GetInt64()>=0,scenario.Name+" "+key+" entered");
                    var call=Boundary.Calls.IndexOf("disable:10:4");
                    Check(Boundary.Calls.Skip(call).SequenceEqual(new[]{"disable:10:4","locate:"+Boundary.Target,"status:10:0","locate:"+Boundary.Target}),scenario.Name+" exactly original failure-only three reads in order");
                }else{
                    Check(d.GetProperty("DisableFailureObservationElapsedMs").ValueKind==JsonValueKind.Null && !d.TryGetProperty("DisableFailureStatus",out _),scenario.Name+" unentered observation remains null and adds no read");
                    Check(!Boundary.Calls.Any(call=>call.StartsWith("status:",StringComparison.Ordinal)),scenario.Name+" no failure-only status outside nonzero disable return");
                }
                Check(Boundary.Disabled==1 && Boundary.Enabled==(disableFault?0:compensation?2:1),scenario.Name+" bounded exact enable compensation");
                Check(d.GetProperty("CompensationChecked").GetBoolean()==(compensationScope && !disableFault),scenario.Name+" compensation only after acknowledged disable");
                Check(d.GetProperty("CompensationAttempted").GetBoolean()==compensation,scenario.Name+" no retry on changed or unknown identity");
                if(compensationScope && !disableFault && !guardThrows)Check(FactBool("CompensationIdentityMatched",!denied),scenario.Name+" exact return identity decision");
                if(compensation){
                    var returned=d.GetProperty("CompensationEnableResult");
                    var returnsError=scenario.Fault is "enable-result" or "enable-compensation-result";
                    Check(compensationThrows?returned.ValueKind==JsonValueKind.Null:FactUint("CompensationEnableResult",returnsError?uint.MaxValue:0),scenario.Name+" original and compensation outcomes separated");
                    Check(FactBool("CompensationSameNode",true) && FactUint("CompensationLocateResult",0),scenario.Name+" same captured node only");
                    Check(scenario.Xusb?Boundary.InterfaceQueries==2:Boundary.Proofs==3,scenario.Name+" disabled return never requires active game interface");
                }
                if(compensationScope && (compensationThrows || guardThrows))Check(d.GetProperty("CompensationExceptionType").GetString()=="System.ApplicationException" && d.GetProperty("CompensationExceptionHResult").ValueKind==JsonValueKind.Number,scenario.Name+" compensation exception retained separately");
                var failed=d.GetProperty(disableFault?"DisableResult":"EnableResult");
                Check(throws?failed.ValueKind==JsonValueKind.Null:failed.GetUInt32()==uint.MaxValue,scenario.Name+" returned code versus thrown call");
                if(!disableFault)Check(d.GetProperty("DisableResult").GetUInt32()==0,scenario.Name+" successful disable retained");
                Check(d.GetProperty("ExceptionType").GetString()=="System.InvalidOperationException" && d.GetProperty("ExceptionHResult").ValueKind==JsonValueKind.Number,scenario.Name+" exception type and hresult");
                Check(text.Split(prefix,StringSplitOptions.None).Length==2,scenario.Name+" unique bounded record");
            }
        }
        Results.Add(new{scenario,result,expected=expectedResult,Boundary.Proofs,Boundary.Disabled,Boundary.Enabled,Boundary.Opened,Boundary.Preparsed,Boundary.Freed,Boundary.InterfaceQueries,diagnostic,calls=Boundary.Calls.ToArray()});
    }
    private static void RunTiming(string fault,string clockFault="",long disableElapsedMs=17)
    {
        var label="timing-"+fault+"-"+clockFault+(disableElapsedMs==17?"":"-disable-"+disableElapsedMs);
        Boundary.Reset(new(label,Fault:fault,FailProof:0));Boundary.Timing=true;Boundary.ClockMs=1000;Boundary.ClockFault=clockFault;Boundary.DisableDelayMs=disableElapsedMs;
        var facts=new MsiNativePortCycle.GameRefreshFacts();
        var previous=Console.Out;using var capture=new StringWriter();Console.SetOut(capture);
        Exception? error=null;
        try{MsiNativePortCycle.PrepareTimingRefresh(facts,()=>fault=="admission-throw"?throw new ApplicationException("inert admission exception"):fault!="admission-false")!();}
        catch(Exception caught){error=caught;facts.WriteFailure(caught);facts.WriteFailure(caught);}
        finally{Console.SetOut(previous);}
        var unentered=fault.StartsWith("admission-",StringComparison.Ordinal);
        var success=fault=="success";
        var observed=!unentered && !success && fault!="disable-throw";
        var preOnly=fault is "locate-error" or "locate-mismatch" or "locate-throw";
        var statusEntered=observed && !preOnly;
        var postEntered=statusEntered && fault!="status-throw";
        var expectedCalls=new List<string>{"locate:"+Boundary.Target,"locate:"+Boundary.Target};
        if(!unentered)expectedCalls.Add("disable:10:4");
        if(observed)expectedCalls.Add("locate:"+Boundary.Target);
        if(statusEntered)expectedCalls.Add("status:10:0");
        if(postEntered)expectedCalls.Add("locate:"+Boundary.Target);
        if(success)expectedCalls.Add("enable:10:0");
        Check(Boundary.Calls.SequenceEqual(expectedCalls),label+" exact original calls and order");
        Check(Boundary.Disabled==(unentered?0:1) && Boundary.Enabled==(success?1:0),label+" original mutation counts");
        Check(success?error is null:error?.GetType().Name==(fault=="admission-throw"?"ApplicationException":fault=="admission-false"?"HidAuthorityChangedException":"InvalidOperationException"),label+" original success or exception preserved");
        Check(facts.DisableCallEntered==!unentered && facts.EnableCallEntered==success,label+" original entered flags");
        void Duration(long? actual,long? expected,string name)=>Check(actual==(clockFault.Length==0?expected:null),label+" "+name+" exact monotonic ms or null");
        Duration(facts.DisableCallElapsedMs,unentered?null:disableElapsedMs,"disable excludes observation");
        Duration(facts.DisableFailureObservationElapsedMs,observed?7+(statusEntered?11:0)+(postEntered?13:0):null,"whole observation");
        if(observed){
            var observation=facts.DisableFailureStatus!;
            Duration(observation.LocateElapsedMs,7,"first locate including throw");
            Duration(observation.StatusElapsedMs,statusEntered?11:null,"status including throw");
            Duration(observation.PostLocateElapsedMs,postEntered?13:null,"post locate including throw");
            var state=fault.EndsWith("throw",StringComparison.Ordinal)?"unknown-diagnostic-throw":fault switch{
                "locate-error"=>"unknown-locate-error","locate-mismatch"=>"unknown-node-mismatch",
                "status-error"=>"unknown-status-error","post-locate-error"=>"unknown-post-locate-error",
                "post-locate-mismatch"=>"unknown-post-node-mismatch",_=>"observed"};
            Check(observation.State==state,label+" observation facts preserve UNKNOWN");
            Check(facts.DisableResult==23 && !facts.CompensationChecked,label+" nonzero result never authorizes enable");
        }else Check(facts.DisableFailureStatus is null,label+" unentered observation omitted");
        var output=capture.ToString();JsonElement? diagnostic=null;
        if(success)Check(output.Length==0,label+" successful path emits no record");
        else{
            const string prefix="YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ";
            Check(output.StartsWith(prefix,StringComparison.Ordinal) && output.Split(prefix,StringSplitOptions.None).Length==2,label+" one original failure record");
            Check(Encoding.UTF8.GetByteCount(output)<=2048,label+" unchanged failure byte cap");
            if(output.StartsWith(prefix,StringComparison.Ordinal)){
                using var parsed=JsonDocument.Parse(output[prefix.Length..]);diagnostic=parsed.RootElement.Clone();
                Check(diagnostic.Value.GetProperty("TimingClock").GetString()=="monotonic" && diagnostic.Value.GetProperty("DisableCallElapsedMs").ValueKind==(unentered || clockFault.Length>0?JsonValueKind.Null:JsonValueKind.Number),label+" serialized timing presence");
            }
        }
        Results.Add(new{scenario=label,clockFault,disableElapsedMs,refreshSucceeded=error is null,exception=error?.GetType().FullName,Boundary.Disabled,Boundary.Enabled,diagnostic,calls=Boundary.Calls.ToArray()});
        Boundary.Timing=false;Boundary.ClockFault="";
    }
    // Only the original OS/SDK boundaries are inert: every case enters actual Run9.
    private static void RunGpdRoot(Scenario scenario,int expected,int factories,int conversions,int cycles)
    {
        Boundary.Reset(scenario);
        if(scenario.Fault=="old-disable-result23"){
            Boundary.Timing=true;Boundary.ClockMs=1000;Boundary.DisableDelayMs=65370;
        }
        string[] arguments=[MsiNativePortCycle.Command,Boundary.Root,"--location",Boundary.Position,
            "--parent-pid","999999","--once","--game-xusb",Boundary.Target];
        var previous=Console.Out;using var capture=new StringWriter();Console.SetOut(capture);
        int result;
        try{result=MsiNativePortCycle.Run(arguments);}
        finally{Console.SetOut(previous);}
        var label=scenario.Name;
        Check(arguments.Length==9 && arguments[7]=="--game-xusb" && Boundary.Target==Boundary.Root,
            label+" complete actual Run9 with exact selected root");
        Check(result==expected,label+" result="+result+" expected="+expected);
        Check(Boundary.Disabled==0 && Boundary.Enabled==0,label+" no CM Disable/Enable on success or failure");
        Check(Boundary.Factories==factories && Boundary.Conversions==conversions && Boundary.Cycles==cycles,
            label+" exact SDK factory/conversion/CyclePort counts");
        Check(Boundary.Calls.Count(call=>call=="factory:"+Boundary.Root)==factories &&
            Boundary.Calls.Count(call=>call=="usb:"+Boundary.Root)==conversions &&
            Boundary.Calls.Count(call=>call=="cycle:"+Boundary.Root)==cycles,
            label+" SDK consumes only selected root and retains entered calls");
        if(conversions>0)Check(Boundary.Calls.IndexOf("factory:"+Boundary.Root)<Boundary.Calls.IndexOf("usb:"+Boundary.Root),
            label+" factory precedes conversion");
        if(cycles>0)Check(Boundary.Calls.IndexOf("usb:"+Boundary.Root)<Boundary.Calls.IndexOf("cycle:"+Boundary.Root),
            label+" conversion precedes one cycle");
        Check(Boundary.Proofs==0 && Boundary.Opened==0 && Boundary.Preparsed==0 && Boundary.Freed==0,
            label+" XUSB never consumes HID compatible/caps/open proof");
        var xusbClass=new Guid("EC87F1E3-C13B-4100-B5F7-8B84D54260CB");
        Check(Boundary.InterfaceTypes.All(type=>type==xusbClass) &&
            Boundary.InterfaceInstances.All(instance=>instance==Boundary.Target),
            label+" discovery keeps actual XUSB class and target");
        if(expected==0){
            Check(Boundary.InterfaceQueries==2,label+" active authority checked before and after SDK preparation");
            Check(Boundary.Calls.Count(call=>call=="status:1:0")==1,label+" selected root known started before factory");
            var lastLocate=Boundary.Calls.LastIndexOf("locate:"+Boundary.Root);
            Check(lastLocate>Boundary.Calls.IndexOf("usb:"+Boundary.Root) && lastLocate<Boundary.Calls.IndexOf("cycle:"+Boundary.Root),
                label+" selected DEVINST reread after preparation before mutation");
        }
        if(scenario.Fault.StartsWith("status-",StringComparison.Ordinal))
            Check(Boundary.Calls.Count(call=>call=="status:1:0")==1 && Boundary.Factories==0,
                label+" unknown/notstarted/problem status refuses before SDK");
        if(scenario.Fault is "wrong-board" or "parent-dead" or "location-mismatch")
            Check(Boundary.InterfaceQueries==0 && !Boundary.Calls.Any(call=>call.StartsWith("status:",StringComparison.Ordinal)),
                label+" original early refusal has no authority or status operation");
        if(scenario.Fault=="after-prepare-authority-lost")
            Check(Boundary.InterfaceQueries==2,label+" authority actually fails on second read");
        if(scenario.Fault=="authority-lost")
            Check(Boundary.InterfaceQueries==1,label+" initial authority loss precedes factory");
        if(scenario.Fault=="after-prepare-node-changed")
            Check(Boundary.Calls.Contains("root-node-after-prepare:99") && Boundary.InterfaceQueries==1,
                label+" changed DEVINST blocks stale authority");
        if(scenario.Fault=="after-prepare-parent-dead")
            Check(Boundary.Calls.Contains("parent-after-prepare:dead"),label+" captured parent fails after preparation");
        if(scenario.Fault=="after-prepare-location-changed")
            Check(Boundary.Calls.Contains("location-after-prepare:changed"),label+" location changed after preparation");
        var text=capture.ToString();JsonElement? diagnostic=null;
        Check(Encoding.UTF8.GetByteCount(text)<=2048,label+" unchanged diagnostic cap");
        if(text.Length>0){
            const string prefix="YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ";
            Check(text.StartsWith(prefix,StringComparison.Ordinal) && text.Split(prefix,StringSplitOptions.None).Length==2,
                label+" one original bounded diagnostic");
            if(text.StartsWith(prefix,StringComparison.Ordinal)){
                using var parsed=JsonDocument.Parse(text[prefix.Length..]);diagnostic=parsed.RootElement.Clone();
                Check(diagnostic.Value.TryGetProperty("Xusb",out var xusb) && xusb.ValueKind==JsonValueKind.True,
                    label+" diagnostic preserves actual XUSB source");
            }
        }
        if(scenario.Fault=="cycle-throw"){
            Check(result==6 && diagnostic is not null,label+" thrown CyclePort preserves 6 and real diagnostic");
            if(diagnostic is JsonElement cycleFailure){
                bool False(string key)=>cycleFailure.TryGetProperty(key,out var value) && value.ValueKind==JsonValueKind.False;
                Check(cycleFailure.TryGetProperty("Kind",out var kind) && kind.GetString()=="ymcc.gameRefresh.failure" &&
                    cycleFailure.TryGetProperty("Result",out var code) && code.GetInt32()==6,
                    label+" original refresh failure identity");
                Check(cycleFailure.TryGetProperty("Stage",out var stage) && stage.GetString()=="root-cycle" &&
                    cycleFailure.TryGetProperty("RootPortCycle",out var rootCycle) && rootCycle.ValueKind==JsonValueKind.True,
                    label+" actual root-cycle stage and explicit RootPortCycle");
                Check(False("DisableCallEntered") && False("EnableCallEntered"),
                    label+" never fabricates old CM entered flags");
                Check(cycleFailure.TryGetProperty("ExceptionType",out var error) && error.GetString()=="System.ApplicationException",
                    label+" actual SDK exception retained");
            }
        }
        if(scenario.Fault=="old-disable-result23"){
            // Old Run9 enters Disable, advances 65370 virtual ms, returns 23,
            // and reports result6/factory0/cycle0: candidate assertions must fail.
            Check(Boundary.ClockMs>=1000 && Boundary.ClockMs<1000+Boundary.DisableDelayMs,
                label+" candidate bypasses simulated 65-second CM Disable");
            if(Boundary.Disabled>0 && diagnostic is JsonElement oldFailure)
                Check(oldFailure.TryGetProperty("DisableResult",out var code) && code.TryGetUInt32(out var value) && value==23 &&
                    oldFailure.TryGetProperty("DisableCallElapsedMs",out var elapsed) && elapsed.GetInt64()==65370,
                    label+" necessary old-source failure retains 23 and exact virtual duration");
        }
        Results.Add(new{scenario,arguments,result,expected,inputRoute="xusb",Boundary.Disabled,Boundary.Enabled,
            Boundary.Factories,Boundary.Conversions,Boundary.Cycles,Boundary.InterfaceQueries,Boundary.Proofs,
            Boundary.Opened,Boundary.Preparsed,Boundary.Freed,Boundary.ClockMs,Boundary.DisableDelayMs,
            diagnostic,calls=Boundary.Calls.ToArray()});
        Boundary.Timing=false;
    }
    private static void RunGpdRootControl(Scenario scenario)
    {
        Run(scenario,0,false);
        Check(Boundary.Target!=Boundary.Root,scenario.Name+" nonexact target retained");
        Check(Boundary.Factories==0 && Boundary.Conversions==0 && Boundary.Cycles==0,
            scenario.Name+" sibling/HID/other OEM never promoted to root");
        Check(Boundary.Disabled==1 && Boundary.Enabled==1 &&
            Boundary.Calls.Contains("locate:"+Boundary.Target) && Boundary.Calls.Contains("disable:10:4") &&
            Boundary.Calls.Contains("enable:10:0"),scenario.Name+" original target CM path retained");
        Check(!Boundary.Calls.Any(call=>call.StartsWith("status:",StringComparison.Ordinal)),
            scenario.Name+" no new root-cycle status read");
        Check(scenario.Xusb?Boundary.Proofs==0 && Boundary.Opened==0:Boundary.Proofs==3 && Boundary.Opened>0,
            scenario.Name+" original input-specific proof retained");
    }
    private static void RunGpdRootCases()
    {
        RunGpdRoot(new("gpd-root-cycle-old-disable-result23","gpd","old-disable-result23",0,Xusb:true,GpdRoot:true),0,1,1,1);
        RunGpdRoot(new("gpd-root-cycle-normal","gpd",Xusb:true,GpdRoot:true),0,1,1,1);
        foreach(var item in new (string Fault,int Result,int Factory,int Conversion,int Cycle)[]{
            ("factory-null",7,1,0,0),("factory-throw",6,1,0,0),("cycle-throw",6,1,1,1),
            ("after-prepare-parent-dead",3,1,1,0),("after-prepare-location-changed",5,1,1,0),
            ("after-prepare-node-changed",8,1,1,0),("after-prepare-authority-lost",8,1,1,0),
            ("status-notstarted",7,0,0,0),("status-problem-bit",7,0,0,0),
            ("status-problem-code",7,0,0,0),("status-unknown",7,0,0,0),("status-throw",6,0,0,0),
            ("authority-lost",8,0,0,0),("wrong-board",4,0,0,0),("parent-dead",3,0,0,0),
            ("location-mismatch",5,0,0,0)})
            RunGpdRoot(new("gpd-root-cycle-"+item.Fault,"gpd",item.Fault,0,Xusb:true,GpdRoot:true),
                item.Result,item.Factory,item.Conversion,item.Cycle);
        RunGpdRootControl(new("gpd-root-cycle-control-msi-mi00","msi",Xusb:true));
        RunGpdRootControl(new("gpd-root-cycle-control-gpd-hid","gpd"));
        RunGpdRootControl(new("gpd-root-cycle-control-rog-mi00","rog",Xusb:true));
        RunGpdRootControl(new("gpd-root-cycle-control-gpd-mi00","gpd",Xusb:true));
        RunGpdRootControl(new("gpd-root-cycle-control-gpd-sibling","gpd",Xusb:true,GpdRoot:true,GpdSibling:true));
    }
    private static void RunColdLifetimeCases()
    {
        foreach(var fault in new[]{"success","parent-open-failed","parent-dead","birth-mismatch","expired","oversized-budget","cancel-signaled","cancel-wait-failed","after-prepare-parent-dead","after-prepare-cancel","after-prepare-authority-lost","cycle-throw","status-unknown","location-mismatch"}){
            Boundary.Reset(new("cold-lifetime-"+fault,"gpd",fault,0,Xusb:true,GpdRoot:true));Boundary.ColdLifetime=true;
            var parentId=int.Parse(Environment.GetEnvironmentVariable("YMCC_COLD_FIXTURE_PARENT_PID")!);
            using var parent=System.Diagnostics.Process.GetProcessById(parentId);
            var birth=(ulong)parent.StartTime.ToUniversalTime().ToFileTimeUtc();if(fault=="birth-mismatch")++birth;
            var now=checked((ulong)Environment.TickCount64);var deadline=fault=="expired"?now:now+(fault=="oversized-budget"?31000UL:30000UL);
            using var cancellation=new EventWaitHandle(false,EventResetMode.ManualReset);
            var value=cancellation.SafeWaitHandle.DangerousGetHandle();
            // Transfer only this fixture-created event to actual Run15. Its
            // SafeWaitHandle owns and closes it; the outer object cannot double-close.
            var args=new[]{MsiNativePortCycle.Command,Boundary.Root,"--location",Boundary.Position,"--parent-pid",parentId.ToString(),"--once",MsiNativePortCycle.GameXusbCycle,Boundary.Root,"--parent-birth",birth.ToString(),"--deadline-tick",deadline.ToString(),"--cancel-handle",((nuint)value).ToString()};
            if(!MsiNativePortCycle.TryArguments(args,out _,out _,out _))throw new InvalidOperationException("Fixture Run15 arguments rejected before owned handle transfer");
            cancellation.SafeWaitHandle.SetHandleAsInvalid();
            var previous=Console.Out;using var text=new StringWriter();Console.SetOut(text);
            int result;try{result=MsiNativePortCycle.Run(args);}finally{Console.SetOut(previous);}
            var expected=fault switch{"success"=>0,"after-prepare-authority-lost"=>8,"cycle-throw"=>6,"status-unknown"=>7,"location-mismatch"=>5,_=>13};
            Check(result==expected,"cold-lifetime-"+fault+" typed result="+result+" expected="+expected);
            Check(Boundary.Disabled==0&&Boundary.Enabled==0,"cold-lifetime-"+fault+" no CM Disable/Enable");
            Check(Boundary.Cycles==(fault is "success" or "cycle-throw"?1:0),"cold-lifetime-"+fault+" exactly expected SDK mutation entries");
            Check(result!=13||Boundary.Cycles==0,"cold-lifetime-"+fault+" typed13 proves no CyclePort entry");
            Results.Add(new{fault,result,expected,Boundary.Disabled,Boundary.Enabled,Boundary.Cycles,calls=Boundary.Calls.ToArray(),scope="actual Run15; inert device/registry/parent/wait ports; fixture-owned event handle"});
            Boundary.ColdLifetime=false;
        }
    }
    public static int Main(string[] args)
    {
        var baseline=args.Length==2&&args[1]=="--baseline";
        var timingOnly=args.Length==3&&args[1]=="--case"&&args[2]=="timing";
        var gpdRootOnly=args.Length==3&&args[1]=="--case"&&args[2]=="gpd-root-cycle";
        var coldLifetimeOnly=args.Length==3&&args[1]=="--case"&&args[2]=="gpd-cold-lifetime";
        if(args.Length<1 || args.Length!=1&&!baseline&&!timingOnly&&!gpdRootOnly&&!coldLifetimeOnly){Console.Error.WriteLine("Unknown helper fixture selection; executed=0");return 2;}
        if(coldLifetimeOnly){RunColdLifetimeCases();}
        else if(gpdRootOnly){RunGpdRootCases();}
        else if(baseline){Run(new("g3e-real-empty-compatible"),0,true);}
        else{
            if(!timingOnly){
            foreach(var family in new[]{"msi","rog","rog-original","gpd"}){
                foreach(var usage in new ushort[]{4,5})foreach(var compatible in new[]{"","HID_DEVICE_UP:0001_U:0005","HID_DEVICE_UP:0001_U:0006"})
                    Run(new(family+"-game-"+usage+"-compat-"+compatible,family,Compatible:compatible,Usage:usage),0,false);
                foreach(var fault in new[]{"property-missing","property-data-error","property-size-race"})Run(new(family+"-"+fault,family,fault),0,false);
                foreach(var pair in new (ushort Page,ushort Usage)[]{(1,6),(1,7),(1,1),(1,2),(12,1),(32,1),(0xff00,1),(0xffa0,1),(0xfff0,0x40)})
                    Run(new(family+"-non-game-"+pair.Page+"-"+pair.Usage,family,Compatible:"HID_DEVICE_UP:0001_U:0005",UsagePage:pair.Page,Usage:pair.Usage),8,false);
                foreach(var fault in new[]{"foreign-root","virtual-parent","interface-size-error","interface-list-error","interface-empty","interface-too-many","wrong-binding","wrong-binding-type","wrong-binding-terminator","wrong-attributes","open-failed","attributes-failed","preparsed-failed","preparsed-null","caps-failed","zero-report","node-retired","binding-retired"})
                    Run(new(family+"-"+fault,family,fault),8,false);
                foreach(var fault in new[]{"wrong-binding","node-retired","caps-failed"})Run(new(family+"-after-prepare-"+fault,family,fault,2),8,false);
                foreach(var fault in new[]{"wrong-binding","node-retired","caps-failed","path-replaced","interface-disappeared"})Run(new(family+"-immediate-before-disable-"+fault,family,fault,3),8,false);
                Run(new(family+"-path-replaced-after-prepare",family,"path-replaced",2),8,false);
                Run(new(family+"-wrong-board",family,"wrong-board"),4,false);
                Run(new(family+"-parent-dead",family,"parent-dead"),3,false);
                Run(new(family+"-parent-dies-after-prepare",family,"parent-late"),3,false);
                Run(new(family+"-location-changed",family,"location-changed"),5,false);
                Run(new(family+"-xusb-active",family,Xusb:true),0,false);
                if(family is "rog" or "rog-original"){
                    Run(new(family+"-field-hid-xusb",family,Xusb:true),0,false);
                    foreach(var fault in new[]{"interface-empty","interface-size-error","interface-list-error","foreign-root","virtual-parent"})
                        Run(new(family+"-field-hid-xusb-"+fault,family,fault,0,Xusb:true),8,false);
                }
                foreach(var fault in new[]{"interface-empty","interface-size-error","interface-list-error","foreign-root","virtual-parent"})
                    Run(new(family+"-xusb-"+fault,family,fault,0,Xusb:true),8,false);
            }
            foreach(var family in new[]{"msi","rog","rog-original","gpd"})foreach(var xusb in new[]{false,true})
                foreach(var fault in new[]{"disable-result","disable-throw","enable-result","enable-throw","enable-once-result","enable-once-throw","enable-compensation-throws","enable-compensation-result","enable-identity-retired","enable-node-replaced","enable-location-changed","enable-authority-throws","enable-location-throws","enable-parent-gone-after-disable"})
                    Run(new(family+"-refresh-"+fault+(xusb?"-xusb":"-hid"),family,fault,0,Xusb:xusb),6,false);
            Check(Marshal.SizeOf(typeof(MsiNativePortCycle).GetNestedType("HidCaps",BindingFlags.NonPublic)!)==64,"HIDP_CAPS SDK size");
            Check(Marshal.SizeOf(typeof(MsiNativePortCycle).GetNestedType("HidAttributes",BindingFlags.NonPublic)!)==12,"HIDD_ATTRIBUTES SDK size");
            Check(MsiNativePortCycle.SelfTest(),"existing helper lifecycle and family selftests");
            var disableCalls=0;var enableCalls=0;var returnChecks=0;
            var clean=MsiNativePortCycle.RefreshGameNode(()=>true,_=>{disableCalls++;return 0;},()=>{enableCalls++;return enableCalls==1?1u:0;},null,()=>{returnChecks++;return true;});
            Check(!clean && disableCalls==1 && enableCalls==2 && returnChecks==1,"missing diagnostic object cannot suppress cleanup or wash original failure");
            disableCalls=enableCalls=returnChecks=0;
            clean=MsiNativePortCycle.RefreshGameNode(()=>true,_=>{disableCalls++;return 1;},()=>{enableCalls++;return 0;},null,()=>{returnChecks++;return true;});
            Check(!clean && disableCalls==1 && enableCalls==0 && returnChecks==0,"unknown or failed disable never authorizes enable compensation");
            disableCalls=enableCalls=returnChecks=0;
            clean=MsiNativePortCycle.RefreshGameNode(()=>true,_=>{disableCalls++;return 0;},()=>{enableCalls++;return 0;},null,()=>{returnChecks++;return true;});
            Check(clean && disableCalls==1 && enableCalls==1 && returnChecks==0,"healthy refresh adds no return discovery or extra enable");
            }
            foreach(var fault in new[]{"admission-false","admission-throw","success","disable-throw","observe-ok","locate-error","locate-mismatch","locate-throw","status-error","status-throw","post-locate-error","post-locate-mismatch","post-locate-throw"})RunTiming(fault);
            foreach(var clockFault in new[]{"timestamp","elapsed"})foreach(var fault in new[]{"success","disable-throw","observe-ok"})RunTiming(fault,clockFault);
            foreach(var duration in new long[]{65370,65378})RunTiming("observe-ok",disableElapsedMs:duration);
        }
        var report=new{productionCompensationScope="rog-pid1b4c-exact-owned-node",passed=Failures.Count==0,assertions,cases=Results.Count,failures=Failures,results=Results,baseline,selection=coldLifetimeOnly?"gpd-cold-lifetime":gpdRootOnly?"gpd-root-cycle":timingOnly?"timing":"default",realHardwareAccess=false,realSdkLoaded=false,productMainExecuted=false};
        File.WriteAllText(args[0],JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine(JsonSerializer.Serialize(new{report.passed,assertions,report.cases,report.failures,baseline}));return report.passed?0:1;
    }
}

internal sealed class ParentProcessLifetime:IDisposable
{
    internal static ParentProcessLifetime? TryOpen(int parent)=>Boundary.ColdLifetime&&Boundary.Case.Fault=="parent-open-failed"?null:new();
    internal bool IsAlive{get{Boundary.AliveReads++;if(Boundary.Prepared && Boundary.RootFault("after-prepare-parent-dead")){Boundary.Calls.Add("parent-after-prepare:dead");return false;}return Boundary.Case.Fault!="parent-dead" && !(Boundary.Case.Fault=="parent-late" && Boundary.AliveReads>=3) && !(Boundary.Case.Fault=="enable-parent-gone-after-disable" && Boundary.Disabled>0);}}
    public void Dispose(){}
}

namespace InertRegistry
{
    internal static class Registry
    {
        internal static readonly Key LocalMachine=new();
        internal sealed class Key:IDisposable
        {
            internal Key OpenSubKey(string name,bool writable)=>new();
            internal object GetValue(string name)=>Boundary.Case.Fault=="wrong-board"?"UNKNOWN":name=="BaseBoardManufacturer"?Boundary.Case.Family.StartsWith("rog")?"ASUSTEK COMPUTER INC.":Boundary.Case.Family=="gpd"?"GPD":"MICRO-STAR INTERNATIONAL CO., LTD.":Boundary.Case.Board.Length>0?Boundary.Case.Board:Boundary.Case.Family.StartsWith("rog")?"RC73XA":Boundary.Case.Family=="gpd"?"G1618-05":"MS-1T52";
            public void Dispose(){}
        }
    }
}

namespace Nefarius.Utilities.DeviceManagement.PnP
{
    internal enum DeviceLocationFlags{Normal}
    internal sealed class PnPDevice
    {
        internal string Instance {get;}
        private PnPDevice(string instance){Instance=instance;}
        internal static PnPDevice? GetDeviceByInstanceId(string instance,DeviceLocationFlags flags)
        {
            if(!Boundary.Case.GpdRoot)throw new InvalidOperationException("root cycling is outside this fixture");
            Boundary.Factories++;Boundary.Calls.Add("factory:"+instance);
            if(instance!=Boundary.Root || flags!=DeviceLocationFlags.Normal)
                throw new InvalidOperationException("inert factory received a nonselected root");
            if(Boundary.RootFault("factory-throw"))throw new ApplicationException("inert SDK factory failure");
            return Boundary.RootFault("factory-null")?null:new(instance);
        }
    }
    internal sealed class UsbPnPDevice
    {
        private readonly string instance;
        internal UsbPnPDevice(string value){instance=value;}
        internal void CyclePort()
        {
            if(!Boundary.Case.GpdRoot)throw new InvalidOperationException("root cycling is outside this fixture");
            Boundary.Cycles++;Boundary.Calls.Add("cycle:"+instance);
            if(Boundary.RootFault("cycle-throw"))throw new ApplicationException("inert SDK CyclePort failure");
        }
    }
}
namespace Nefarius.Utilities.DeviceManagement.Extensions
{
    internal static class DeviceExtensions
    {
        internal static Nefarius.Utilities.DeviceManagement.PnP.UsbPnPDevice ToUsbPnPDevice(this Nefarius.Utilities.DeviceManagement.PnP.PnPDevice device)
        {
            if(!Boundary.Case.GpdRoot)throw new InvalidOperationException("root cycling is outside this fixture");
            Boundary.Conversions++;Boundary.Calls.Add("usb:"+device.Instance);
            return new(device.Instance);
        }
    }
}

internal static partial class MsiNativePortCycle
{
    // Optional lifecycle import: no OS wait; an unexpected call refuses execution.
    private static uint WaitForCycleCancellation(SafeWaitHandle handle,uint milliseconds)
    { Boundary.Calls.Add("cancel-wait-inert");return Boundary.ColdLifetime?(Boundary.Case.Fault=="cancel-signaled"||Boundary.Prepared&&Boundary.Case.Fault=="after-prepare-cancel"?0u:Boundary.Case.Fault=="cancel-wait-failed"?uint.MaxValue:0x102u):uint.MaxValue; }
    // Fixture-only lexical replacement: normal cases use the real monotonic clock;
    // explicit timing cases advance only at inert device boundaries, without waits.
    private static class Stopwatch
    {
        internal static long GetTimestamp()
        {
            if(Boundary.ClockFault=="timestamp")throw new ArithmeticException("inert timestamp failure");
            return Boundary.Timing?Boundary.ClockMs:System.Diagnostics.Stopwatch.GetTimestamp();
        }
        internal static TimeSpan GetElapsedTime(long started)
        {
            if(Boundary.ClockFault=="elapsed")throw new ArithmeticException("inert elapsed calculation failure");
            return Boundary.Timing?TimeSpan.FromMilliseconds(Boundary.ClockMs-started):System.Diagnostics.Stopwatch.GetElapsedTime(started);
        }
    }
    internal static Action? PrepareTimingRefresh(GameRefreshFacts facts,Func<bool> current)=>PrepareGameRefresh(Boundary.Target,current,facts);
    private static uint Locate(ref uint node,string instance,uint flags){Boundary.Calls.Add("locate:"+instance);
        if(Boundary.Timing && Boundary.Disabled>0){
            var first=++Boundary.ObservationLocates==1;Boundary.ClockMs+=first?7:13;
            var part=first?"locate":"post-locate";node=Boundary.Case.Fault==part+"-mismatch"?99u:10u;
            if(Boundary.Case.Fault==part+"-throw")throw new ApplicationException("inert observation locate exception");
            return Boundary.Case.Fault==part+"-error"?13u:0;
        }
        if(Boundary.Prepared && Boundary.RootFault("after-prepare-node-changed") && instance==Boundary.Root){Boundary.Calls.Add("root-node-after-prepare:99");node=99;return 0;}
        node=instance==Boundary.Root?1u:Boundary.Fault("node-retired") || Boundary.Enabled>0 && Boundary.Fault("enable-node-replaced")?99u:10u;if(instance!=Boundary.Root && Boundary.Enabled>0){if(Boundary.Fault("enable-authority-throws"))throw new ApplicationException("inert return identity exception");if(Boundary.Fault("enable-identity-retired"))return 13;}return 0;}
    private static uint Property(uint node,ref PropertyKey key,out uint type,byte[]? buffer,ref uint bytes,uint flags)
    {
        type=0x2012;
        if(key.Id==37 && Boundary.Enabled>0 && Boundary.Fault("enable-location-throws"))throw new ApplicationException("inert return location exception");
        if(key.Id==4 && buffer is null)Boundary.Proofs++;
        if(key.Id==4 && Boundary.Fault("property-missing")){bytes=0;return 13;}
        if(key.Id==4 && buffer is not null && Boundary.Fault("property-data-error"))return 13;
        var rootLocationChanged=key.Id==37 && (Boundary.RootFault("location-mismatch") || Boundary.Prepared && Boundary.RootFault("after-prepare-location-changed"));
        if(rootLocationChanged && Boundary.Prepared && buffer is null)Boundary.Calls.Add("location-after-prepare:changed");
        var text=rootLocationChanged?"USB#OTHER":key.Id==37?(Boundary.Case.Fault=="location-changed" && Boundary.Proofs>=1 || Boundary.Enabled>0 && Boundary.Fault("enable-location-changed"))?"USB#OTHER":Boundary.Position:Boundary.Case.Compatible;
        var data=Encoding.Unicode.GetBytes(text+"\0");
        if(buffer is null){bytes=key.Id==4 && text.Length==0?4u:(uint)data.Length;return 26;}
        if(key.Id==4 && Boundary.Fault("property-size-race")){bytes=(uint)buffer.Length+100;return 26;}
        data.CopyTo(buffer,0);bytes=(uint)data.Length;return 0;
    }
    private static uint Parent(out uint parent,uint node,uint flags){parent=node==10?11u:1u;return node==1?13u:0;}
    private static uint DeviceId(uint node,StringBuilder identifier,int length,uint flags){identifier.Append(node==1?Boundary.Fault("foreign-root")?Boundary.Root+"OTHER":Boundary.Root:node==11?Boundary.Fault("virtual-parent")?@"ROOT\HIDMAESTRO\VIRTUAL":@"USB\MI_00\PARENT":Boundary.Target);return 0;}
    private static uint DevNodeStatus(out uint status,out uint problem,uint node,uint flags)
    {
        Boundary.Calls.Add("status:"+node+":"+flags);status=8;problem=0;
        if(Boundary.Case.GpdRoot){
            if(Boundary.RootFault("status-notstarted"))status=0;
            if(Boundary.RootFault("status-problem-bit"))status|=0x400;
            if(Boundary.RootFault("status-problem-code"))problem=22;
            if(Boundary.RootFault("status-unknown"))return 13;
            if(Boundary.RootFault("status-throw"))throw new ApplicationException("inert root status failure");
        }
        if(Boundary.Timing){Boundary.ClockMs+=11;if(Boundary.Case.Fault=="status-throw")throw new ApplicationException("inert observation status exception");}
        return Boundary.Case.Fault=="status-error"?13u:0;
    }
    private static uint Disable(uint node,uint flags){Boundary.Disabled++;Boundary.Calls.Add("disable:"+node+":"+flags);if(Boundary.Timing)Boundary.ClockMs+=Boundary.DisableDelayMs;if(Boundary.Fault("disable-throw"))throw new InvalidOperationException("inert disable exception");return Boundary.Timing && Boundary.Case.Fault!="success"?23u:Boundary.Fault("disable-result")?uint.MaxValue:0;}
    private static uint Enable(uint node,uint flags){
        Boundary.Enabled++;Boundary.Calls.Add("enable:"+node+":"+flags);
        if(Boundary.Enabled==1 && Boundary.Case.Fault.StartsWith("enable-",StringComparison.Ordinal)){
            if(Boundary.Case.Fault is "enable-throw" or "enable-once-throw" or "enable-compensation-result")throw new InvalidOperationException("inert original enable exception");
            return uint.MaxValue;
        }
        if(Boundary.Fault("enable-throw") || Boundary.Fault("enable-compensation-throws"))throw new ApplicationException("inert compensation enable exception");
        return Boundary.Fault("enable-result") || Boundary.Fault("enable-compensation-result")?uint.MaxValue:0;
    }
    private static uint InterfaceSize(out uint characters,ref Guid type,string instance,uint flags){Boundary.InterfaceQueries++;Boundary.InterfaceTypes.Add(type);Boundary.InterfaceInstances.Add(instance);characters=Boundary.Fault("interface-too-many")?400u:64u;return Boundary.Fault("interface-size-error")?13u:0;}
    private static uint Interfaces(ref Guid type,string instance,char[] buffer,uint characters,uint flags){if(Boundary.Fault("interface-list-error"))return 26;var rootAuthorityLost=Boundary.RootFault("authority-lost") || Boundary.Prepared && Boundary.RootFault("after-prepare-authority-lost");var value=rootAuthorityLost || Boundary.Fault("interface-empty") || Boundary.Fault("interface-disappeared")?"\0\0":Boundary.Fault("interface-too-many")?string.Concat(Enumerable.Repeat("inert-hid\0",17))+"\0":Boundary.Fault("path-replaced")?"inert-hid-replaced\0\0":"inert-hid\0\0";value.CopyTo(0,buffer,0,value.Length);return 0;}
    private static uint InterfaceProperty(string path,ref PropertyKey key,out uint type,byte[] buffer,ref uint bytes,uint flags){type=Boundary.Fault("wrong-binding-type")?0x2012u:0x12u;var text=Boundary.Fault("wrong-binding") || Boundary.Fault("binding-retired") && Boundary.Opened>0?Boundary.Target+"OTHER":Boundary.Target;var value=Encoding.Unicode.GetBytes(text+(Boundary.Fault("wrong-binding-terminator")?"":"\0"));value.CopyTo(buffer,0);bytes=(uint)value.Length;return 0;}
    private static SafeFileHandle OpenHid(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template){Boundary.Opened++;return new(Boundary.Fault("open-failed")?new IntPtr(-1):new IntPtr(42),false);}
    private static bool HidAttributesRead(SafeFileHandle handle,ref HidAttributes attributes){attributes.VendorId=Convert.ToUInt16(Boundary.Root.Substring(8,4),16);attributes.ProductId=Boundary.Fault("wrong-attributes")?(ushort)1:Convert.ToUInt16(Boundary.Root.Substring(17,4),16);return !Boundary.Fault("attributes-failed");}
    private static bool HidPreparsedRead(SafeFileHandle handle,out IntPtr preparsed){preparsed=Boundary.Fault("preparsed-null") || Boundary.Fault("preparsed-failed")?IntPtr.Zero:new IntPtr(123);if(preparsed!=IntPtr.Zero)Boundary.Preparsed++;return !Boundary.Fault("preparsed-failed");}
    private static int HidCapabilitiesRead(IntPtr preparsed,out HidCaps caps){caps=new(){UsagePage=Boundary.Case.UsagePage,Usage=Boundary.Case.Usage,InputReportByteLength=Boundary.Fault("zero-report")?(ushort)0:(ushort)64,Reserved=new ushort[17]};return Boundary.Fault("caps-failed")?0:0x00110000;}
    private static bool HidPreparsedFree(IntPtr preparsed){Boundary.Freed++;return true;}
}
