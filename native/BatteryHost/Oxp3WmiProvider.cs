using System.Globalization;
using System.Management;
namespace YeManBatteryHost;
// Pure OXP3 charging transport for the existing BatteryHost. The route is the reviewed
// OneXPlayerX2 WMI EC interface used by the pinned OneXPlayer3 source; X1 direct-I/O,
// family guesses and class-name fallbacks are never used here.
internal interface IOxp3WmiEc : IDisposable
{
    byte ReadByte(ushort register);
    void WriteByte(ushort register,byte value);
}
// External process/memory method seam: production uses the WMI ManagementObject;
// tests inject a controlled memory method and still execute the exact argument
// packing and response parsing below.
internal interface IOxp3WmiMethod : IDisposable
{
    string? Invoke(string method,object[] arguments);
}
internal sealed class Oxp3WmiManagementMethod : IOxp3WmiMethod
{
    readonly ManagementObject instance;
    public Oxp3WmiManagementMethod()
    {
        ManagementObject? selected=null;
        try
        {
            using(var searcher=new ManagementObjectSearcher("root\\WMI","SELECT * FROM SuRwECRegInterface"))
            {
                searcher.Options.Timeout=TimeSpan.FromSeconds(3);
                using var instances=searcher.Get();
                selected=SelectUniqueInstance(instances.Cast<ManagementObject>(),static (ManagementObject item)=>item.Dispose());
            }
            // Transfer ownership only after enumeration and query-resource cleanup succeed.
            instance=selected??throw new IOException("OXP3 WMI EC 接口不可用");
            selected=null;
        }
        finally{selected?.Dispose();}
    }
    // A reviewed model does not identify which of several EC endpoints is its owner.
    // This exact selector is shared with inert tests; it never invokes an EC method.
    internal static T SelectUniqueInstance<T>(IEnumerable<T> candidates,Action<T>? cleanup=null) where T:class,IDisposable
    {
        ArgumentNullException.ThrowIfNull(candidates);
        // ManagementObject hides Component.Dispose; production must bind its concrete cleanup.
        cleanup??=static item=>item.Dispose();
        T? selected=null;
        try
        {
            foreach(var candidate in candidates)
            {
                if(candidate is null)throw new IOException("OXP3 WMI EC 实例无效");
                if(selected is not null)
                {
                    // Even a duplicate object reference is ambiguous enumeration; dispose once.
                    try{throw new IOException("OXP3 WMI EC 存在多个实例，无法确认充电接口");}
                    finally{if(!ReferenceEquals(candidate,selected))cleanup(candidate);}
                }
                selected=candidate;
            }
            if(selected is null)throw new IOException("OXP3 WMI EC 接口不可用");
            var result=selected;
            selected=null;
            return result;
        }
        finally{if(selected is not null)cleanup(selected);}
    }
    public string? Invoke(string method,object[] arguments)
    {
        instance.InvokeMethod(method,arguments);
        return arguments.Length>1?arguments[1] as string:null;
    }
    public void Dispose()=>instance.Dispose();
}
internal sealed class Oxp3WmiEc : IOxp3WmiEc
{
    readonly IOxp3WmiMethod method;
    public Oxp3WmiEc(IOxp3WmiMethod? method=null){this.method=method??new Oxp3WmiManagementMethod();}
    public byte ReadByte(ushort register)
    {
        object[] arguments=[Oxp3WmiProvider.PackAddress(register),null!];
        var response=method.Invoke("ReadECReg",arguments)??throw new IOException("OXP3 ReadECReg 没有返回结果");
        var fields=response.Split(',',StringSplitOptions.TrimEntries);
        if(fields.Length<2||ParseHexByte(fields[0])!=0)throw new IOException("OXP3 ReadECReg 失败："+response);
        return ParseHexByte(fields[1]);
    }
    public void WriteByte(ushort register,byte value)
    {
        object[] arguments=[Oxp3WmiProvider.PackWrite(register,value),null!];
        var response=method.Invoke("WriteECReg",arguments)??throw new IOException("OXP3 WriteECReg 没有返回结果");
        var fields=response.Split(',',StringSplitOptions.TrimEntries);
        if(fields.Length==0||ParseHexByte(fields[0])!=0)throw new IOException("OXP3 WriteECReg 失败："+response);
    }
    public void Dispose()=>method.Dispose();
    static byte ParseHexByte(string value)
    {
        var normalized=value.StartsWith("0x",StringComparison.OrdinalIgnoreCase)?value[2..]:value;
        if(byte.TryParse(normalized,NumberStyles.HexNumber,CultureInfo.InvariantCulture,out var result))return result;
        throw new IOException("OXP3 EC 响应字节未知："+value);
    }
}
internal sealed class Oxp3WmiProvider : IChargeProvider,IChargeWriteAdmission
{
    internal const ushort LimitRegister=0x04A3;
    internal const ushort BypassRegister=0x04A4;
    readonly IOxp3WmiEc ec;
    public Oxp3WmiProvider(IOxp3WmiEc? ec=null){this.ec=ec??new Oxp3WmiEc();}
    public bool CanReadLimit=>true;
    public Reading Read()
    {
        var raw=ec.ReadByte(LimitRegister);
        if(raw>100)throw new IOException("OXP3 充电阈值未知");
        return new(raw,raw);
    }
    public void Write(int value,Reading before)=>Write(value,before,static ()=>{});
    public void Write(int value,Reading before,Action requireAvailable)
    {
        if(value<0||value>100)throw new IOException("OXP3 充电档位超出源范围");
        if(before.Raw is null)throw new IOException("缺少 OXP3 充电原始状态");
        if(ec.ReadByte(LimitRegister)!=before.Raw)throw new IOException("充电状态已被其他软件改变，请重新读取");
        requireAvailable(); // Recheck the owner after the final potentially blocking WMI pre-read.
        ec.WriteByte(LimitRegister,(byte)value);
    }
    internal static byte BypassValue(int mode)=>mode switch{1=>0x01,2=>0x0B,_=>0x00};
    internal void WriteBypassMode(int mode)
    {
        if(mode<0||mode>2)throw new IOException("OXP3 旁路模式未知");
        ec.WriteByte(BypassRegister,BypassValue(mode));
    }
    internal static uint PackAddress(ushort register)=>((uint)(register>>8))|((uint)(byte)register<<8);
    internal static uint PackWrite(ushort register,byte value)=>PackAddress(register)|((uint)value<<16);
    public void Dispose()=>ec.Dispose();
}
