using System.Collections;
using System.Reflection;
using System.Runtime.CompilerServices;
public static partial class MsiLifecycleIoPorts
{
    const BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
    public sealed record Io(string Stage,string Method,int Index,byte[] Data,int Thread);
    public static readonly List<Io> Rows=new();
    public static object Device=null!;public static int Firmware=0x167;public static byte Full=0x85,Software=0x91;public static string ReadFailure="",WriteFailure="";public static bool HideHid,MetadataAvailable=true,IgnoreWrite;public static string ReadOutcome="TransportFailed";public static int ReadFailureAfter=0;
    public static readonly Dictionary<object,bool> Connections=new(ReferenceEqualityComparer.Instance);
    public static Action<string>? OnIo;
    public static void Reset(){Rows.Clear();Connections.Clear();Firmware=0x167;Full=0x85;Software=0x91;ReadFailure=WriteFailure="";HideHid=false;MetadataAvailable=true;IgnoreWrite=false;ReadOutcome="TransportFailed";ReadFailureAfter=0;OnIo=null;}
    static void Mark(string stage,string method,int index,byte[] data){Rows.Add(new(stage,method,index,data.ToArray(),Environment.CurrentManagedThreadId));OriginalConstructorPorts.Mark("msi."+stage+":"+method+":"+index);OnIo?.Invoke(stage);}
    public static object? Get(object value,string name){for(var t=value.GetType();t is not null;t=t.BaseType){var f=t.GetField(name,All|BindingFlags.DeclaredOnly);if(f is not null)return f.GetValue(value);var p=t.GetProperty(name,All|BindingFlags.DeclaredOnly);if(p is not null)return p.GetValue(value);}return null;}
    static void Back(object value,string name,object data)=>value.GetType().GetField("<"+name+">k__BackingField",All)!.SetValue(value,data);
    public static object Enumerate(int vendor,int[] ids,int minimum)
    {
        Mark("hid-enumerate","metadata",vendor,[]);var hid=Device.GetType().Assembly.GetReferencedAssemblies().Single(a=>a.Name=="HidLibrary");var assembly=LoadHidAssembly(hid);var type=assembly.GetType("HidLibrary.HidDevice",true)!;var array=Array.CreateInstance(type,HideHid?0:1);if(HideHid)return array;
        var filters=(IDictionary)Get(Device,"hidFilters")!;DictionaryEntry selected=default;foreach(DictionaryEntry entry in filters)if(ids.Contains((int)entry.Key)){selected=entry;break;}var filter=selected.Value!;
        var device=RuntimeHelpers.GetUninitializedObject(type);GC.SuppressFinalize(device);var attr=RuntimeHelpers.GetUninitializedObject(assembly.GetType("HidLibrary.HidDeviceAttributes",true)!);var caps=RuntimeHelpers.GetUninitializedObject(assembly.GetType("HidLibrary.HidDeviceCapabilities",true)!);
        Back(attr,"VendorId",vendor);Back(attr,"ProductId",(int)selected.Key);Back(attr,"Version",Firmware);Back(caps,"UsagePage",(short)Get(filter,"UsagePage")!);Back(caps,"Usage",(short)Get(filter,"Usage")!);Back(caps,"FeatureReportByteLength",(short)Math.Max(minimum,64));
        type.GetField("_deviceAttributes",All)!.SetValue(device,attr);type.GetField("_deviceCapabilities",All)!.SetValue(device,caps);Connections.Add(device,true);array.SetValue(device,0);return array;
    }
    public static string WindowsFolder(int folder)
    {
        if(folder!=(int)Environment.SpecialFolder.Windows)throw new InvalidOperationException("UNDECLARED_SPECIAL_FOLDER");var path=Path.Combine(JoinedFactoryPorts.TempRoot(),"owned-windows");Directory.CreateDirectory(Path.Combine(path,"SysWOW64"));Mark("resource-target","owned-build-folder",folder,[]);return path;
    }
    public static bool MetadataReady(string path){Mark("wmi-metadata",path,0,[]);return MetadataAvailable;}
    public static bool Connected(object device){if(device is null)throw new NullReferenceException();Mark("hid-connected","metadata",0,[]);return Connections.TryGetValue(device,out var connected)?connected:throw new InvalidOperationException("UNDECLARED_HID_CONNECTION");}
    public static object Result(string method,byte index,byte[] data,string outcome="Ok")
    {
        var type=Device.GetType().Assembly.GetType("HandheldCompanion.WMI+WmiIoResult",true)!;var value=Activator.CreateInstance(type)!;type.GetProperty("MethodName")!.SetValue(value,method);type.GetProperty("DataBlockIndex")!.SetValue(value,index);type.GetProperty("Data")!.SetValue(value,data);var status=type.GetProperty("Outcome")!;status.SetValue(value,Enum.Parse(status.PropertyType,outcome));return value;
    }
    public static object Read(string scope,string path,string method,byte index,int length,int minimum)
    {
        if(MsiManualIoPorts.Enabled)return MsiManualIoPorts.Read(scope,path,method,index,length,minimum);
        byte[] data=method switch {"Get_WMI"=>new byte[]{0,2,3},"Get_Data" when index==152=>[Full],"Get_AP" when index==1=>[Software,0,0],"Get_AP" when index==0=>[0,0,2,0,0,0],"Get_Fan" when index is 1 or 2=>new byte[]{0,0,0,20,30,40,50,60,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},_=>throw new InvalidOperationException("UNDECLARED_MSI_READ:"+method+":"+index)};
        Mark("wmi-read",method,index,data);return Result(method,index,data,ReadFailure==method&&ReadFailureAfter--<=0?ReadOutcome:"Ok");
    }
    public static object ReadLegacy(string scope,string path,string method,byte index,int length,out bool success)
    {
        if(method!="Get_AP"||index!=0)throw new InvalidOperationException("UNDECLARED_LEGACY_MSI_READ");var data=new byte[]{0,0,2,0,0,0};Mark("legacy-read",method,index,data);success=true;return data;
    }
    public static object Write(string scope,string path,string method,byte[] bytes)
    {
        if(MsiManualIoPorts.Enabled)return MsiManualIoPorts.Write(scope,path,method,bytes);
        Mark("wmi-write",method,bytes[0],bytes);if(method!="Set_Data"||bytes.Length!=32||bytes[0]is not(152 or 212))throw new InvalidOperationException("UNDECLARED_MSI_WRITE");
        if(WriteFailure==method)return Result(method,bytes[0],[],"TransportFailed");if(!IgnoreWrite){if(bytes[0]==152)Full=bytes[1];else Software=bytes[1];}return Result(method,bytes[0],[]);
    }
}
