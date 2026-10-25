// Inert HID/CM adapter. All unmanaged entry points in the copied helper are
// replaced by these methods before compilation; SafeFileHandles own no OS handle.
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
using R114Offline;
internal static class AuthorityIo
{
    internal static string Fault="",Mapping="",PathNow="",ExpectedInstance="";
    internal const string PathA=@"\\?\hid#fixture-a#{4d1e55b2-f16f-11cf-88cb-001111000030}";
    internal const string PathB=@"\\?\hid#fixture-b#{4d1e55b2-f16f-11cf-88cb-001111000030}";
    internal static bool ForceEmpty,Retired,Replaced;
    internal static int Opens,Reads,Freed,InterfaceQueries,MapQueries;
    internal static ushort Vid=0x0db0,Pid=0x1901,Page=1,Usage=5,InputLength=64;
    internal static readonly List<SafeFileHandle> Handles=[];
    internal static readonly HashSet<IntPtr> Preparsed=[];
    internal static void Reset(Scenario c,string fault="")
    {
        Boundary.Reset(c);Fault=fault;Mapping=ExpectedInstance=c.Target;PathNow=PathA;
        ForceEmpty=Retired=Replaced=false;Opens=Reads=Freed=InterfaceQueries=MapQueries=0;
        Vid=c.Family=="rog"?(ushort)0x0b05:c.Family=="gpd"?(ushort)0x045e:(ushort)0x0db0;
        Pid=c.Family=="rog"?(ushort)0x1b4c:c.Family=="gpd"?(ushort)0x028e:(ushort)0x1901;
        Page=1;Usage=5;InputLength=64;Handles.Clear();Preparsed.Clear();
    }
    internal static uint Locate(ref uint node,string instance,uint flags)
    {
        var result=Boundary.Locate(ref node,instance,flags);
        if(Replaced && instance.Equals(Boundary.Config.Target,StringComparison.OrdinalIgnoreCase))node++;
        return result;
    }
    internal static uint Property(uint node,Guid format,uint id,out uint type,byte[]? buffer,ref uint bytes,uint flags)
    {
        if(!ForceEmpty || id!=4)return Boundary.Property(node,format,id,out type,buffer,ref bytes,flags);
        type=0x2012;
        var result=buffer is null?0x1Au:0;
        if(buffer is null)bytes=4;else{Array.Clear(buffer);bytes=2;}
        Boundary.Calls.Add($"property:{node}:{id}:{(buffer is null?"size":"data")}:{result}:{type}:{bytes}:{flags}");return result;
    }
    internal static uint InterfaceSize(out uint characters,ref Guid type,string instance,uint flags)
    {
        if(type!=new Guid("4d1e55b2-f16f-11cf-88cb-001111000030"))return Boundary.InterfaceSize(out characters,ref type,instance,flags);
        if(flags!=0 || instance!=ExpectedInstance)throw new Exception("HID list must be PRESENT and instance-filtered");
        InterfaceQueries++;Boundary.Calls.Add("hid-interface-size:"+instance);
        characters=Fault=="no-interfaces"||Retired?2u:(uint)(PathNow.Length+2);
        return Fault=="interface-size-failed"?13u:0;
    }
    internal static uint Interfaces(ref Guid type,string instance,char[] buffer,uint characters,uint flags)
    {
        if(type!=new Guid("4d1e55b2-f16f-11cf-88cb-001111000030"))return Boundary.Interfaces(ref type,instance,buffer,characters,flags);
        Boundary.Calls.Add("hid-interface-list:"+instance);
        if(Fault=="interface-list-failed")return 13;
        if(Fault!="no-interfaces"&&!Retired)(PathNow+"\0\0").CopyTo(0,buffer,0,PathNow.Length+2);
        return 0;
    }
    internal static uint InterfaceProperty(string path,Guid format,uint id,out uint type,byte[] buffer,ref uint bytes,uint flags)
    {
        if(format!=new Guid("78c34fc8-104a-4aca-9ea4-524d52996e57")||id!=256||flags!=0)throw new Exception("wrong interface instance property");
        MapQueries++;Boundary.Calls.Add("hid-map:"+path+":"+Mapping);
        type=Fault=="map-wrong-type"?0x2012u:0x12u;
        var data=Encoding.Unicode.GetBytes(Mapping+(Fault=="map-no-terminator"?"":"\0"));bytes=(uint)data.Length;
        Array.Copy(data,buffer,Math.Min(data.Length,buffer.Length));
        return Fault=="map-failed"?13u:0;
    }
    internal static SafeFileHandle OpenHid(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template)
    {
        if(access!=0 || share!=3 || security!=IntPtr.Zero || creation!=3 || flags!=0x80 || template!=IntPtr.Zero)throw new Exception("HID must be query-only, shared, existing file");
        Opens++;Boundary.Calls.Add("hid-open:"+path);
        var handle=new SafeFileHandle(new IntPtr(Fault=="open-failed"?-1:100+Opens),false);Handles.Add(handle);return handle;
    }
    static T Set<T>(T value,string field,object data) where T:struct
    {
        object boxed=value;typeof(T).GetField(field,BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!.SetValue(boxed,data);return (T)boxed;
    }
    internal static bool Attributes<T>(SafeFileHandle handle,ref T attributes) where T:struct
    {
        if(handle.IsClosed||handle.IsInvalid)throw new Exception("use after close");
        if((uint)typeof(T).GetField("Size")!.GetValue(attributes)! != Marshal.SizeOf<T>())throw new Exception("HIDD_ATTRIBUTES.Size contract");
        attributes=Set(Set(attributes,"VendorId",Vid),"ProductId",Pid);
        return Fault!="attributes-failed";
    }
    internal static bool PreparsedRead(SafeFileHandle handle,out IntPtr data)
    {
        if(handle.IsClosed||handle.IsInvalid)throw new Exception("preparsed after close");
        data=Fault=="preparsed-null"?IntPtr.Zero:new IntPtr(1000+Opens);
        if(data!=IntPtr.Zero&&!Preparsed.Add(data))throw new Exception("duplicate allocation");
        return Fault!="preparsed-failed" && Fault!="preparsed-failed-with-pointer";
    }
    internal static int CapabilitiesRead<T>(IntPtr data,out T caps) where T:struct
    {
        if(!Preparsed.Contains(data))throw new Exception("unowned preparsed pointer");
        Reads++;caps=Set(Set(Set(default(T),"UsagePage",Page),"Usage",Usage),"InputReportByteLength",InputLength);
        if(Fault=="mapping-changed")Mapping=Boundary.Config.Target+"-REPLACED";
        if(Fault=="node-changed")Replaced=true;
        if(Fault=="active-lost")Retired=true;
        if(Fault=="caps-throw")throw new IOException("fake caps failure");
        return Fault=="caps-failed"?unchecked((int)0xc0110001):0x00110000;
    }
    internal static bool PreparsedFree(IntPtr data)
    {
        if(!Preparsed.Remove(data))throw new Exception("double free");Freed++;return true;
    }
    internal static bool ResourcesReleased=>Handles.All(h=>h.IsClosed)&&Preparsed.Count==0;
}
