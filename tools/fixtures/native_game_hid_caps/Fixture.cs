using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;

internal sealed record Scenario(string Name,string Family="msi",string Fault="",int FailProof=1,string Compatible="",ushort UsagePage=1,ushort Usage=5,string Board="",bool Xusb=false);

internal static class Boundary
{
    internal static Scenario Case=new("initial");
    internal static int Proofs,Disabled,Enabled,Opened,Preparsed,Freed,AliveReads,InterfaceQueries;
    internal static readonly List<string> Calls=[];
    internal static string Root=>Case.Family=="rog"?@"USB\VID_0B05&PID_1B4C\ROG":Case.Family=="rog-original"?@"USB\VID_0B05&PID_1ABE\ROG":Case.Family=="gpd"?@"USB\VID_045E&PID_028E\GPD":@"USB\VID_0DB0&PID_1901\00006F64096B22E7";
    internal static string Target=>Case.Xusb?((Case.Name.Contains("field-hid-xusb",StringComparison.Ordinal)?"HID":"USB")+Root[3..21]+@"&MI_00\XUSB"):Case.Family=="msi"?@"HID\VID_0DB0&PID_1901&IG_02\8&3E27176&0&0000":"HID"+Root[3..21]+@"&IG_02\GAME";
    internal const string Position="PCIROOT(0)#USBROOT(0)#USB(4)";
    internal static bool Fault(string name)=>Case.Fault==name && Proofs>=Case.FailProof;
    internal static void Reset(Scenario scenario){Case=scenario;Proofs=Disabled=Enabled=Opened=Preparsed=Freed=AliveReads=InterfaceQueries=0;Calls.Clear();}
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
    public static int Main(string[] args)
    {
        var baseline=args.Contains("--baseline");
        if(baseline){Run(new("g3e-real-empty-compatible"),0,true);}
        else{
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
        var report=new{productionCompensationScope="rog-pid1b4c-exact-owned-node",passed=Failures.Count==0,assertions,cases=Results.Count,failures=Failures,results=Results,baseline,realHardwareAccess=false,realSdkLoaded=false,productMainExecuted=false};
        File.WriteAllText(args[0],JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine(JsonSerializer.Serialize(new{report.passed,assertions,report.cases,report.failures,baseline}));return report.passed?0:1;
    }
}

internal sealed class ParentProcessLifetime:IDisposable
{
    internal static ParentProcessLifetime? TryOpen(int parent)=>new();
    internal bool IsAlive{get{Boundary.AliveReads++;return Boundary.Case.Fault!="parent-dead" && !(Boundary.Case.Fault=="parent-late" && Boundary.AliveReads>=3) && !(Boundary.Case.Fault=="enable-parent-gone-after-disable" && Boundary.Disabled>0);}}
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
    internal sealed class PnPDevice{internal static PnPDevice GetDeviceByInstanceId(string instance,DeviceLocationFlags flags)=>new();}
    internal sealed class UsbPnPDevice{internal void CyclePort()=>throw new InvalidOperationException("root cycling is outside this fixture");}
}
namespace Nefarius.Utilities.DeviceManagement.Extensions
{
    internal static class DeviceExtensions{internal static Nefarius.Utilities.DeviceManagement.PnP.UsbPnPDevice ToUsbPnPDevice(this Nefarius.Utilities.DeviceManagement.PnP.PnPDevice device)=>new();}
}

internal static partial class MsiNativePortCycle
{
    private static uint Locate(ref uint node,string instance,uint flags){Boundary.Calls.Add("locate:"+instance);node=instance==Boundary.Root?1u:Boundary.Fault("node-retired") || Boundary.Enabled>0 && Boundary.Fault("enable-node-replaced")?99u:10u;if(instance!=Boundary.Root && Boundary.Enabled>0){if(Boundary.Fault("enable-authority-throws"))throw new ApplicationException("inert return identity exception");if(Boundary.Fault("enable-identity-retired"))return 13;}return 0;}
    private static uint Property(uint node,ref PropertyKey key,out uint type,byte[]? buffer,ref uint bytes,uint flags)
    {
        type=0x2012;
        if(key.Id==37 && Boundary.Enabled>0 && Boundary.Fault("enable-location-throws"))throw new ApplicationException("inert return location exception");
        if(key.Id==4 && buffer is null)Boundary.Proofs++;
        if(key.Id==4 && Boundary.Fault("property-missing")){bytes=0;return 13;}
        if(key.Id==4 && buffer is not null && Boundary.Fault("property-data-error"))return 13;
        var text=key.Id==37?(Boundary.Case.Fault=="location-changed" && Boundary.Proofs>=1 || Boundary.Enabled>0 && Boundary.Fault("enable-location-changed"))?"USB#OTHER":Boundary.Position:Boundary.Case.Compatible;
        var data=Encoding.Unicode.GetBytes(text+"\0");
        if(buffer is null){bytes=key.Id==4 && text.Length==0?4u:(uint)data.Length;return 26;}
        if(key.Id==4 && Boundary.Fault("property-size-race")){bytes=(uint)buffer.Length+100;return 26;}
        data.CopyTo(buffer,0);bytes=(uint)data.Length;return 0;
    }
    private static uint Parent(out uint parent,uint node,uint flags){parent=node==10?11u:1u;return node==1?13u:0;}
    private static uint DeviceId(uint node,StringBuilder identifier,int length,uint flags){identifier.Append(node==1?Boundary.Fault("foreign-root")?Boundary.Root+"OTHER":Boundary.Root:node==11?Boundary.Fault("virtual-parent")?@"ROOT\HIDMAESTRO\VIRTUAL":@"USB\MI_00\PARENT":Boundary.Target);return 0;}
    private static uint Disable(uint node,uint flags){Boundary.Disabled++;Boundary.Calls.Add("disable:"+node+":"+flags);if(Boundary.Fault("disable-throw"))throw new InvalidOperationException("inert disable exception");return Boundary.Fault("disable-result")?uint.MaxValue:0;}
    private static uint Enable(uint node,uint flags){
        Boundary.Enabled++;Boundary.Calls.Add("enable:"+node+":"+flags);
        if(Boundary.Enabled==1 && Boundary.Case.Fault.StartsWith("enable-",StringComparison.Ordinal)){
            if(Boundary.Case.Fault is "enable-throw" or "enable-once-throw" or "enable-compensation-result")throw new InvalidOperationException("inert original enable exception");
            return uint.MaxValue;
        }
        if(Boundary.Fault("enable-throw") || Boundary.Fault("enable-compensation-throws"))throw new ApplicationException("inert compensation enable exception");
        return Boundary.Fault("enable-result") || Boundary.Fault("enable-compensation-result")?uint.MaxValue:0;
    }
    private static uint InterfaceSize(out uint characters,ref Guid type,string instance,uint flags){Boundary.InterfaceQueries++;characters=Boundary.Fault("interface-too-many")?400u:64u;return Boundary.Fault("interface-size-error")?13u:0;}
    private static uint Interfaces(ref Guid type,string instance,char[] buffer,uint characters,uint flags){if(Boundary.Fault("interface-list-error"))return 26;var value=Boundary.Fault("interface-empty") || Boundary.Fault("interface-disappeared")?"\0\0":Boundary.Fault("interface-too-many")?string.Concat(Enumerable.Repeat("inert-hid\0",17))+"\0":Boundary.Fault("path-replaced")?"inert-hid-replaced\0\0":"inert-hid\0\0";value.CopyTo(0,buffer,0,value.Length);return 0;}
    private static uint InterfaceProperty(string path,ref PropertyKey key,out uint type,byte[] buffer,ref uint bytes,uint flags){type=Boundary.Fault("wrong-binding-type")?0x2012u:0x12u;var text=Boundary.Fault("wrong-binding") || Boundary.Fault("binding-retired") && Boundary.Opened>0?Boundary.Target+"OTHER":Boundary.Target;var value=Encoding.Unicode.GetBytes(text+(Boundary.Fault("wrong-binding-terminator")?"":"\0"));value.CopyTo(buffer,0);bytes=(uint)value.Length;return 0;}
    private static SafeFileHandle OpenHid(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template){Boundary.Opened++;return new(Boundary.Fault("open-failed")?new IntPtr(-1):new IntPtr(42),false);}
    private static bool HidAttributesRead(SafeFileHandle handle,ref HidAttributes attributes){attributes.VendorId=Convert.ToUInt16(Boundary.Root.Substring(8,4),16);attributes.ProductId=Boundary.Fault("wrong-attributes")?(ushort)1:Convert.ToUInt16(Boundary.Root.Substring(17,4),16);return !Boundary.Fault("attributes-failed");}
    private static bool HidPreparsedRead(SafeFileHandle handle,out IntPtr preparsed){preparsed=Boundary.Fault("preparsed-null") || Boundary.Fault("preparsed-failed")?IntPtr.Zero:new IntPtr(123);if(preparsed!=IntPtr.Zero)Boundary.Preparsed++;return !Boundary.Fault("preparsed-failed");}
    private static int HidCapabilitiesRead(IntPtr preparsed,out HidCaps caps){caps=new(){UsagePage=Boundary.Case.UsagePage,Usage=Boundary.Case.Usage,InputReportByteLength=Boundary.Fault("zero-report")?(ushort)0:(ushort)64,Reserved=new ushort[17]};return Boundary.Fault("caps-failed")?0:0x00110000;}
    private static bool HidPreparsedFree(IntPtr preparsed){Boundary.Freed++;return true;}
}
