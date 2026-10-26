// Inert boundaries for the complete, mechanically bound production helper.
// No product assembly, SDK, P/Invoke, registry or device implementation is linked.
using System.Text;
using System.Text.Json;
namespace R114Offline
{
    internal sealed class Scenario
    {
        internal string Id="",Fault="",Stage="first",Family="msi",Writer="",Serializer="";
        internal string Usage="HID_DEVICE_UP:0001_U:0005",Virtual="",DirectTarget="",DirectRoot="";
        internal bool Xusb,RootOnly,Direct,Disabled,NoParent,WrongBoard;
        internal int AliveFalseAt,RootDepth=2,IdFailureDepth,SizeBytes=-1,DataBytes=-1;
        internal uint Error=13,SizeType=0x2012,DataType=0x2012;
#if CURRENT_STATUS
        internal string ProbeFault="";
        internal uint StatusResult,StatusBits=0x0000200A,ProblemCode=22;
#endif
        internal string Root=>Family=="rog"?@"USB\VID_0B05&PID_1B4C\ROG":Family=="gpd"?@"USB\VID_045E&PID_028E\GPD":@"USB\VID_0DB0&PID_1901\00006F64096B22E7";
        internal string Target=>Xusb?Root[..21]+@"&MI_00\XUSB":Family=="rog"?@"HID\VID_0B05&PID_1B4C&IG_03\ROG":Family=="gpd"?@"HID\VID_045E&PID_028E&IG_03\GPD":@"HID\VID_0DB0&PID_1901&IG_03\8&3121E4E&0&0000";
        internal const string Location="PCIROOT(0)#USBROOT(0)#USB(4)";
    }
    internal static class Boundary
    {
        internal static Scenario Config=new();
        internal static readonly List<string> Calls=[];
        internal static int GameLocates,RootLocates,AliveCalls,InterfaceSizes,SerializeCalls;
#if CURRENT_STATUS
        internal static int ProbeLocates,StatusCalls;
        internal static bool FailedDisableReturned;
#endif
        internal static void Reset(Scenario c){Config=c;Calls.Clear();GameLocates=RootLocates=AliveCalls=InterfaceSizes=SerializeCalls=0;
#if CURRENT_STATUS
            ProbeLocates=StatusCalls=0;FailedDisableReturned=false;
#endif
        }
        internal static bool Failing=>Config.Stage=="first"?GameLocates==1:GameLocates==3;
        internal static bool Fault(string f)=>Config.Fault==f && Failing;
        internal static string SerializeDiagnostic<T>(T facts)
        {
            SerializeCalls++;
            if(Config.Serializer=="throw")throw new InvalidOperationException("inert serializer");
            if(Config.Serializer=="oversize")return new string('X',4095);
            if(Config.Serializer=="at-cap" || Config.Serializer=="over-cap"){
                var length=2048-Encoding.UTF8.GetByteCount("YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ")-2+(Config.Serializer=="over-cap"?1:0);
                return "\""+new string('a',length-2)+"\"";
            }
            if(Config.Serializer=="utf8-over-cap")return "\""+new string('中',800)+"\"";
            return JsonSerializer.Serialize(facts);
        }
        internal static uint Locate(ref uint node,string instance,uint flags)
        {
            if(flags!=0)throw new Exception("Locate flags changed");
#if CURRENT_STATUS
            if(FailedDisableReturned){
                ProbeLocates++;var post=ProbeLocates==2;var fault=Config.ProbeFault;
                node=fault==(post?"post-node-mismatch":"node-mismatch")?11u:10u;
                var located=fault==(post?"post-locate-error":"locate-error")?13u:0;
                var throws=fault==(post?"post-locate-throw":"locate-throw");
                Calls.Add($"diagnostic-locate:{instance}:{flags}:{(throws?"throw":located.ToString())}:{node}");
                if(throws)throw new InvalidOperationException("inert diagnostic locate");
                return located;
            }
#endif
            uint result=0;
            if(instance.Equals(Config.Root,StringComparison.OrdinalIgnoreCase)){
                RootLocates++;node=1;
                if(Config.Fault=="location-first" && RootLocates==1 || Config.Fault=="location-after" && RootLocates==2)result=13;
            }else{
                GameLocates++;node=10;InterfaceSizes=0;
                if(Fault("locate") || Config.Fault=="prepare-null" && GameLocates==2 || Config.Fault=="action-locate" && GameLocates==4)result=Config.Error;
                if(Config.Fault=="action-node" && GameLocates==4)node=11;
                if(Fault("locate-throw")){Calls.Add($"locate:{instance}:{flags}:throw");throw new InvalidOperationException("inert CM");}
            }
            Calls.Add($"locate:{instance}:{flags}:{result}:{node}");return result;
        }
        internal static uint Property(uint node,Guid format,uint id,out uint type,byte[]? buffer,ref uint bytes,uint flags)
        {
            if(format!=new Guid("a45c254e-df1c-4efd-8020-67d146a850e0") || flags!=0)throw new Exception("Property key/flags changed");
            var data=Encoding.Unicode.GetBytes((id==37?Scenario.Location:Failing?Config.Usage:"HID_DEVICE_UP:0001_U:0005")+"\0\0");
            var size=buffer is null;
            type=id==4 && Failing?(size?Config.SizeType:Config.DataType):0x2012;
            uint result=size?0x1Au:0;
            if(id==4 && Failing){
                if(size){
                    if(Config.SizeBytes>=0)bytes=(uint)Config.SizeBytes;else bytes=(uint)data.Length;
                    if(Config.Fault=="compatible-size-code")result=Config.Error;
                }else{
                    if(Config.Fault=="compatible-data-code")result=Config.Error;
                    bytes=Config.DataBytes>=0?(uint)Config.DataBytes:(uint)Math.Min(data.Length,buffer!.Length);
                    if(Config.Fault=="compatible-max-count"){
                        Array.Fill(buffer!,(byte)0);for(var i=0;i<buffer!.Length;i+=4)buffer[i]=(byte)'a';bytes=(uint)buffer.Length;
                    }else Array.Copy(data,buffer!,Math.Min(data.Length,buffer!.Length));
                }
            }else{
                bytes=(uint)data.Length;
                if(!size)Array.Copy(data,buffer!,Math.Min(data.Length,buffer!.Length));
            }
            Calls.Add($"property:{node}:{id}:{(size?"size":"data")}:{result}:{type}:{bytes}:{flags}");return result;
        }
        internal static uint DeviceId(uint node,StringBuilder identifier,int length,uint flags)
        {
            var depth=(int)node-10;
            uint result=Fault("device-id") && depth==Config.IdFailureDepth?Config.Error:0;
            var text=depth==Config.RootDepth && !Fault("foreign-root") && !Fault("depth-limit")?Config.Root:depth==1 && Failing && Config.Virtual.Length>0?Config.Virtual:depth==0?Config.Target:@"USB\FOREIGN\PARENT_"+depth;
            if(result==0)identifier.Append(text);
            Calls.Add($"device-id:{node}:{length}:{flags}:{result}:{(result==0?text:"")}");return result;
        }
        internal static uint Parent(out uint parent,uint node,uint flags)
        {
            var depth=(int)node-10;parent=node+1;
            var result=Fault("parent-error") || Fault("foreign-root") && depth==Config.RootDepth?Config.Error:0;
            if(Fault("self-loop"))parent=node;
            Calls.Add($"parent:{node}:{flags}:{result}:{parent}");return result;
        }
        internal static uint InterfaceSize(out uint characters,ref Guid type,string instance,uint flags)
        {
            if(type!=new Guid("EC87F1E3-C13B-4100-B5F7-8B84D54260CB") || flags!=0)throw new Exception("Interface class/flags changed");
            InterfaceSizes++;characters=Fault("x-size-short")?1u:Fault("x-size-long")?65537u:65536u;
            var result=Fault("x-size-code")?Config.Error:0;
            Calls.Add($"interface-size:{instance}:{flags}:{result}:{characters}");return result;
        }
        internal static uint Interfaces(ref Guid type,string instance,char[] buffer,uint characters,uint flags)
        {
            uint result=Fault("x-list-code")?Config.Error:Fault("x-retry-exhausted") || Fault("x-retry-success") && InterfaceSizes==1?0x1Au:0;
            if(!Fault("x-empty"))"inert-xusb\0\0".CopyTo(0,buffer,0,12);
            Calls.Add($"interface-list:{instance}:{characters}:{flags}:{result}");return result;
        }
        internal static uint Disable(uint node,uint flags)
        {
            if(flags!=4)throw new Exception("Disable flags changed");
#if CURRENT_STATUS
            if(Config.Fault=="disable-throw"){Calls.Add($"disable:{node}:{flags}:throw");throw new InvalidOperationException("inert Disable");}
#endif
            var result=Config.Fault=="disable"?Config.Error:0;Calls.Add($"disable:{node}:{flags}:{result}");
#if CURRENT_STATUS
            FailedDisableReturned=result!=0;
#endif
            return result;
        }
#if CURRENT_STATUS
        internal static uint DevNodeStatus(out uint status,out uint problem,uint node,uint flags)
        {
            if(node!=10 || flags!=0)throw new Exception("Status node/flags changed");
            StatusCalls++;status=Config.StatusBits;problem=Config.ProblemCode;
            Calls.Add($"devnode-status:{node}:{flags}:{(Config.ProbeFault=="status-throw"?"throw":Config.StatusResult.ToString())}:{status}:{problem}");
            if(Config.ProbeFault=="status-throw")throw new InvalidOperationException("inert diagnostic status");
            return Config.StatusResult;
        }
#endif
        internal static uint Enable(uint node,uint flags)
        {
            if(flags!=0)throw new Exception("Enable flags changed");
            var result=Config.Fault=="enable"?Config.Error:0;Calls.Add($"enable:{node}:{flags}:{result}");return result;
        }
    }
    internal static class Registry
    {
        internal static readonly Key LocalMachine=new();
        internal sealed class Key:IDisposable
        {
            public Key OpenSubKey(string name,bool writable){Boundary.Calls.Add($"bios-open:{name}:{writable}");return new();}
            public object GetValue(string name){Boundary.Calls.Add("bios-value:"+name);return Boundary.Config.WrongBoard?"UNKNOWN":name=="BaseBoardManufacturer"?Boundary.Config.Family=="rog"?"ASUSTEK COMPUTER INC.":Boundary.Config.Family=="gpd"?"GPD":"MICRO-STAR INTERNATIONAL CO., LTD.":Boundary.Config.Family=="rog"?"RC73XA":Boundary.Config.Family=="gpd"?"G1618-05":"MS-1T91";}
            public void Dispose()=>Boundary.Calls.Add("bios-dispose");
        }
    }
}
internal sealed class ParentProcessLifetime:IDisposable
{
    internal static ParentProcessLifetime? TryOpen(int pid){R114Offline.Boundary.Calls.Add("parent-open:"+pid);return R114Offline.Boundary.Config.NoParent?null:new();}
    internal bool IsAlive{get{R114Offline.Boundary.AliveCalls++;var result=R114Offline.Boundary.AliveCalls!=R114Offline.Boundary.Config.AliveFalseAt;R114Offline.Boundary.Calls.Add("alive:"+result);return result;}}
    public void Dispose()=>R114Offline.Boundary.Calls.Add("parent-dispose");
}
namespace Nefarius.Utilities.DeviceManagement.PnP
{
    internal enum DeviceLocationFlags{Normal}
    internal sealed class PnPDevice
    {
        internal static PnPDevice? GetDeviceByInstanceId(string id,DeviceLocationFlags flags){R114Offline.Boundary.Calls.Add($"sdk-get:{id}:{flags}");if(R114Offline.Boundary.Config.Fault=="sdk-throw")throw new InvalidOperationException("inert SDK");return R114Offline.Boundary.Config.Fault=="sdk-null"?null:new();}
    }
    internal sealed class UsbPnPDevice
    {
        internal void CyclePort(){R114Offline.Boundary.Calls.Add("sdk-cycle");if(R114Offline.Boundary.Config.Fault=="sdk-cycle-throw")throw new InvalidOperationException("inert SDK");}
    }
}
namespace Nefarius.Utilities.DeviceManagement.Extensions
{
    internal static class DeviceExtensions
    {
        internal static Nefarius.Utilities.DeviceManagement.PnP.UsbPnPDevice ToUsbPnPDevice(this Nefarius.Utilities.DeviceManagement.PnP.PnPDevice device){R114Offline.Boundary.Calls.Add("sdk-usb");return new();}
    }
}
