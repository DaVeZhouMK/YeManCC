using System.Management;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using LibreHardwareMonitor.Hardware.Motherboard.Lpc.EC;
using LibreHardwareMonitor.PawnIo;
namespace YeManBatteryHost;
internal sealed record Reading(int? Limit,int? Raw,bool? Paused=null);
internal interface IChargeProvider : IDisposable
{
    bool CanReadLimit { get; }
    Reading Read();
    void Write(int value,Reading before);
}
internal static class WindowsFacts
{
    [StructLayout(LayoutKind.Sequential)] internal struct PowerStatus { public byte Ac,Flags,Percent,SystemFlag; public uint Life,FullLife; }
    [DllImport("kernel32.dll")] [return:MarshalAs(UnmanagedType.Bool)] static extern bool GetSystemPowerStatus(out PowerStatus status);
    public static (bool Present,bool Ac,int? Percent) Power()
    {
        if(!GetSystemPowerStatus(out var p))return(false,false,null);
        return(p.Flags!=255&&(p.Flags&128)==0,p.Ac==1,p.Percent<=100?p.Percent:null);
    }
    public static Identity Identity()
    {
        using var board=Wmi.First("root\\CIMV2","Win32_BaseBoard");
        using var system=Wmi.First("root\\CIMV2","Win32_ComputerSystem");
        using var cpu=Wmi.First("root\\CIMV2","Win32_Processor");
        using var bios=Wmi.First("root\\CIMV2","Win32_BIOS");
        static string S(ManagementBaseObject o,string k)=>Convert.ToString(o[k])?.Trim()??"";
        static int N(ManagementBaseObject o,string k)=>o[k] is null?255:Convert.ToInt32(o[k]);
        return new(S(board,"Manufacturer"),S(board,"Product"),S(system,"Model"),S(cpu,"Name"),S(bios,"SMBIOSBIOSVersion"),N(bios,"EmbeddedControllerMajorVersion"),N(bios,"EmbeddedControllerMinorVersion"));
    }
}
internal static class Wmi
{
    public static ManagementObject First(string scope,string name)
    {
        using var search=new ManagementObjectSearcher(scope,"SELECT * FROM "+name);
        search.Options.Timeout=TimeSpan.FromSeconds(3);
        using var all=search.Get();
        var matches=all.Cast<ManagementObject>().ToArray();
        if(matches.Length==0)throw new IOException("未找到厂商接口："+name);
        var result=matches[0];foreach(var item in matches.Skip(1))item.Dispose();return result;
    }
    public static ManagementBaseObject Call(ManagementObject instance,string name,Action<ManagementBaseObject>? fill=null)
    {
        using var input=instance.GetMethodParameters(name);
        if(fill is not null){if(input is null)throw new IOException("厂商接口缺少参数");fill(input);}
        return instance.InvokeMethod(name,input,new InvokeMethodOptions{Timeout=TimeSpan.FromSeconds(3)})??throw new IOException("厂商接口没有返回结果");
    }
    public static void CheckReturn(ManagementBaseObject output)
    {
        var value=output.Properties.Cast<PropertyData>().FirstOrDefault(p=>p.Name.Equals("ReturnValue",StringComparison.OrdinalIgnoreCase))?.Value;
        if(value is not null&&Convert.ToUInt32(value)!=0)throw new IOException("厂商接口拒绝设置");
    }
}
internal sealed class AsusProvider : IChargeProvider
{
    readonly SafeFileHandle? handle;
    readonly Func<uint,uint,uint>? transport;
    public bool CanReadLimit=>false; // ASUS RSOC exposes presence and a write ACK, not the current threshold.
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] static extern bool DeviceIoControl(SafeFileHandle handle,uint code,uint[] input,int inSize,byte[] output,int outSize,out int returned,IntPtr overlapped);
    public AsusProvider(Func<uint,uint,uint>? transport=null){this.transport=transport;if(transport is not null)return;handle=CreateFile(@"\\.\ATKACPI",0xc0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);if(handle.IsInvalid){handle.Dispose();throw new IOException("ASUS 系统控制接口不可用");}}
    uint Call(uint method,uint value)
    {
        if(transport is not null)return transport(method,value);
        uint[] data=[method,8,0x00120057,value];byte[] output=new byte[16];
        if(!DeviceIoControl(handle!,0x0022240c,data,16,output,16,out var n,IntPtr.Zero)||n<4)throw new IOException("ASUS 充电接口通信失败");
        return BitConverter.ToUInt32(output);
    }
    public Reading Read(){if((Call(0x53545344,0)&0x10000)==0)throw new IOException("固件未提供 ASUS 充电上限接口");return new(null,null);}
    public void Write(int value,Reading before){if(Call(0x53564544,(uint)value)!=1)throw new IOException("ASUS 固件未接受设置");}
    public void Dispose()=>handle?.Dispose();
}
internal sealed class LenovoProvider : IChargeProvider
{
    readonly ManagementObject instance=Wmi.First("root\\WMI","LENOVO_OTHER_METHOD");
    public bool CanReadLimit=>true;
    public Reading Read()
    {
        using var output=Wmi.Call(instance,"GetFeatureValue",p=>p["IDs"]=0x03010001u);Wmi.CheckReturn(output);
        var raw=Convert.ToInt32(output["Value"]??throw new IOException("联想接口缺少回读值"));
        if(raw!=0&&raw!=1)throw new IOException("联想养护模式值未知");return new(raw==1?80:100,raw);
    }
    public void Write(int value,Reading before){if(Read().Raw!=before.Raw)throw new IOException("养护模式已变化，请重新读取");using var output=Wmi.Call(instance,"SetFeatureValue",p=>{p["IDs"]=0x03010001u;p["value"]=value==80?1u:0u;});Wmi.CheckReturn(output);}
    public void Dispose()=>instance?.Dispose();
}
internal sealed class MsiProvider : IChargeProvider
{
    readonly IMsiWmi? wmi;
    readonly Func<string,byte[],byte[]>? transport;
    public MsiProvider(Func<string,byte[],byte[]>? transport=null){this.transport=transport;if(transport is null)wmi=new MsiWmi();}
    internal MsiProvider(IMsiWmi wmi){this.wmi=wmi;}
    public bool CanReadLimit=>true;
    byte[] Packet(string method,byte[] bytes)=>transport is not null?transport(method,bytes):MsiPacket.Call(wmi!,method,bytes);
    public Reading Read()
    {
        var data=new byte[32];data[0]=215;var result=Packet("Get_Data",data);
        if(result.Length<2||result[0]!=1)throw new IOException("MSI 充电状态读取失败");
        int raw=result[1],percent=raw&127;
        if(percent>100||((raw&128)!=0&&percent<20))throw new IOException("MSI 充电阈值未知");
        return new((raw&128)==0?100:percent,raw);
    }
    public void Write(int value,Reading before)
    {
        if(before.Raw is null)throw new IOException("缺少 MSI 原始状态");
        if(Read().Raw!=before.Raw)throw new IOException("MSI 充电状态已变化，请重新读取");
        var data=new byte[32];data[0]=215;data[1]=(byte)Policy.MsiEncode(before.Raw.Value,value);
        _ = Packet("Set_Data",data); // Set_Data has no documented status flag; Controller requires successful readback.
    }
    public void Dispose()=>wmi?.Dispose();
}
// Keep the signed PawnIO module supplied by the existing HC/LHM runtime. Never instantiate an HC device.
internal interface IEcAccess : IDisposable
{
    byte Read(ushort register);
    void Write(ushort register,byte value);
}
internal sealed class EcAccess : IEcAccess
{
    readonly bool direct;
    readonly WindowsEmbeddedControllerIO? ec;
    readonly LpcIo? io;
    readonly Mutex? bus;
    bool entered;
    public EcAccess(bool direct)
    {
        this.direct=direct;
        {
            // LHM Mutexes.Open is normally called by Computer.Open; this host never scans hardware.
            // Own the shared mutex explicitly, including ACPI EC access.
            bus=new Mutex(false,direct?@"Global\Access_ISABUS.HTP.Method":@"Global\Access_EC");
            bool owned;try{owned=bus.WaitOne(250);}catch(AbandonedMutexException){owned=true;}
            if(!owned){bus.Dispose();throw new IOException("硬件通道忙，请稍后重试");}
            try{if(direct){io=new LpcIo();io.SelectSlot(1);foreach(byte b in new byte[]{0x87,0x01,0x55,0xaa})io.WritePort(0x4e,b);}else ec=new WindowsEmbeddedControllerIO();entered=true;}
            catch{io?.Close();bus.ReleaseMutex();bus.Dispose();throw;}
        }
    }
    void Wait(bool output=false)
    {
        for(int n=0;n<50;n++)
        {
            var status=ec!.ReadIOPort(WindowsEmbeddedControllerIO.Port.Command);
            if(status!=255&&(output?(status&1)!=0:(status&2)==0))return;
            Thread.Sleep(1);
        }
        throw new IOException("EC 未在时限内回应");
    }
    void Address(ushort register)
    {
        io!.WritePort(0x4e,0x2e);io.WritePort(0x4f,0x11);io.WritePort(0x4e,0x2f);io.WritePort(0x4f,(byte)(register>>8));
        io.WritePort(0x4e,0x2e);io.WritePort(0x4f,0x10);io.WritePort(0x4e,0x2f);io.WritePort(0x4f,(byte)register);
        io.WritePort(0x4e,0x2e);io.WritePort(0x4f,0x12);io.WritePort(0x4e,0x2f);
    }
    public byte Read(ushort register)
    {
        if(direct){Address(register);return io!.ReadPort(0x4f);}
        if(register>255)throw new IOException("EC 地址空间不匹配");
        Wait();if((ec!.ReadIOPort(WindowsEmbeddedControllerIO.Port.Command)&1)!=0)throw new IOException("EC 有未完成的数据，稍后重试");
        ec.WriteIOPort(WindowsEmbeddedControllerIO.Port.Command,0x80);Wait();ec.WriteIOPort(WindowsEmbeddedControllerIO.Port.Data,(byte)register);Wait(true);
        return ec.ReadIOPort(WindowsEmbeddedControllerIO.Port.Data);
    }
    public void Write(ushort register,byte value)
    {
        if(direct){Address(register);io!.WritePort(0x4f,value);return;}
        if(register>255)throw new IOException("EC 地址空间不匹配");
        Wait();ec!.WriteIOPort(WindowsEmbeddedControllerIO.Port.Command,0x81);Wait();ec.WriteIOPort(WindowsEmbeddedControllerIO.Port.Data,(byte)register);Wait();ec.WriteIOPort(WindowsEmbeddedControllerIO.Port.Data,value);Wait();
    }
    public void Dispose()
    {
        // IT87 secondary configuration exit is a no-op, as in HC's LpcPort.
        if(entered){entered=false;try{ec?.Dispose();io?.Close();}finally{bus!.ReleaseMutex();bus.Dispose();}}
    }
}
internal sealed class EcProvider(Route route,Func<bool,IEcAccess>? accessFactory=null) : IChargeProvider
{
    IEcAccess Open()=>accessFactory?.Invoke(Direct)??new EcAccess(Direct);
    bool Direct=>route.RouteId is "oxp-x1-amd-ec" or "oxp-x1-intel-ec" or "ayaneo-ceii-bypass";
    // CEii helper defaults its high address byte to 0xD1: ECRamDirectRead(0xD1) means 0xD1D1.
    ushort Address=>route.RouteId switch{"ayaneo-ceii-bypass"=>0xd1d1,"ayaneo-cec-bypass" or "suiplay-cec-inheritance"=>0x1e,"gpd-win-mini-2025"=>0x1d,"oxp-acpi-ec"=>0xa3,_=>0x4a3};
    int Pause=>Direct?1:0x55;
    int Resume=>Direct?0x65:0xaa;
    public bool CanReadLimit=>!route.Software;
    public Reading Read()
    {
        using var access=Open();var raw=access.Read(Address);
        if(route.Software){if(raw!=Pause&&raw!=Resume)throw new IOException("充电旁路状态未知");return new(null,raw,raw==Pause);}
        if(raw<route.Min||raw>100)throw new IOException("EC 充电阈值未知");
        return new(raw,raw);
    }
    public void Write(int value,Reading before)
    {
        using var access=Open();
        if(before.Raw is null||access.Read(Address)!=before.Raw.Value)throw new IOException("充电状态已被其他软件改变，请重新读取");
        access.Write(Address,(byte)(route.Software?(value==1?Pause:Resume):value));
    }
    public void Dispose(){}
}
internal sealed class DeckProvider : IChargeProvider
{
    readonly IntPtr module;
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate IntPtr Map(IntPtr address,uint size,out IntPtr memory);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] [return:MarshalAs(UnmanagedType.Bool)] delegate bool Unmap(IntPtr memory,IntPtr address);
    readonly Map map;readonly Unmap unmap;
    public bool CanReadLimit=>true;
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
    public DeckProvider(string powerRoot)
    {
        // Refuse to load an auto-installing legacy library when its driver is absent.
        using var driver=CreateFile(@"\\.\inpoutx64",0xc0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
        if(driver.IsInvalid)throw new IOException("Steam Deck 电池驱动不可用");
        var library=Path.Combine(powerRoot,"handheldcompanion-runtime","YMCC-DEVICECORE-20261018-v1","Resources","inpoutx64.dll");
        module=NativeLibrary.Load(library);
        try{map=Marshal.GetDelegateForFunctionPointer<Map>(NativeLibrary.GetExport(module,"MapPhysToLin"));unmap=Marshal.GetDelegateForFunctionPointer<Unmap>(NativeLibrary.GetExport(module,"UnmapPhysicalMemory"));}
        catch{NativeLibrary.Free(module);throw;}
    }
    byte ReadByte(long address)
    {
        var ptr=map(new IntPtr(address),1,out var handle);if(ptr==IntPtr.Zero)throw new IOException("电池固件读取失败");
        try{return Marshal.ReadByte(ptr);}finally{unmap(handle,ptr);}
    }
    void CheckFirmware()
    {
        int board=ReadByte(0xfe7003bd),fw=ReadByte(0xfe700c4c)|(ReadByte(0xfe700c4d)<<8);
        if(!(board==10&&fw==0xb030||board==5&&new[]{0x1010,0x1050,0x1090,0x1100}.Contains(fw)))throw new IOException("此 Steam Deck 板号或固件尚未适配");
    }
    public Reading Read(){CheckFirmware();int raw=ReadByte(0xfe700b9f);if(raw<40||raw>100)throw new IOException("电池固件返回未知上限");return new(raw,raw);}
    public void Write(int value,Reading before)
    {
        var now=Read();if(now.Raw!=before.Raw)throw new IOException("充电设置已改变，请重新读取");
        var ptr=map(new IntPtr(0xfe700b9f),1,out var handle);if(ptr==IntPtr.Zero)throw new IOException("电池固件写入失败");
        try{Marshal.WriteByte(ptr,(byte)value);}finally{unmap(handle,ptr);}
    }
    public void Dispose()=>NativeLibrary.Free(module);
}
internal static class Providers
{
    public static IChargeProvider Open(Route route,string powerRoot)=>route.RouteId switch
    {
        "asus-ally-acpi"=>new AsusProvider(),"lenovo-go-conservation"=>new LenovoProvider(),"msi-claw-wmi"=>new MsiProvider(),"msi-claw-cg3em-ap"=>new MsiApProvider(),
        "valve-deck-firmware"=>new DeckProvider(powerRoot),_=>new EcProvider(route)
    };
}
