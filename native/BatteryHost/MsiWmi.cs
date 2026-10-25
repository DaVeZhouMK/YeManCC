using System.Management;
namespace YeManBatteryHost;
// Keep the WMI parameter container as well as its embedded Data instance. Some
// MSI providers expose the complete input shape only through Get_WMI output.
internal interface IMsiWmiObject : IDisposable
{
    IMsiWmiObject? Data { get; set; }
    byte[]? Bytes { get; set; }
    void CheckReturn();
}
internal interface IMsiWmi : IDisposable
{
    IMsiWmiObject? Parameters(string method);
    IMsiWmiObject? Invoke(string method,IMsiWmiObject? input);
}
internal sealed class MsiWmiObject(ManagementBaseObject native) : IMsiWmiObject
{
    internal ManagementBaseObject Value=>native;
    public IMsiWmiObject? Data
    {
        get=>native.Properties.Cast<PropertyData>().FirstOrDefault(p=>p.Name=="Data")?.Value is ManagementBaseObject data?new MsiWmiObject(data):null;
        set=>native["Data"]=(value as MsiWmiObject)?.Value;
    }
    public byte[]? Bytes {get=>native["Bytes"] as byte[];set=>native["Bytes"]=value;}
    public void CheckReturn()=>MsiPacket.CheckReturn(native.Properties.Cast<PropertyData>().FirstOrDefault(p=>p.Name.Equals("ReturnValue",StringComparison.OrdinalIgnoreCase))?.Value);
    public void Dispose()=>native.Dispose();
}
internal sealed class MsiWmi : IMsiWmi
{
    readonly ManagementObject instance=Wmi.First("root\\WMI","MSI_ACPI");
    public IMsiWmiObject? Parameters(string method)=>instance.GetMethodParameters(method) is {} p?new MsiWmiObject(p):null;
    public IMsiWmiObject? Invoke(string method,IMsiWmiObject? input)=>instance.InvokeMethod(method,(input as MsiWmiObject)?.Value,new InvokeMethodOptions{Timeout=TimeSpan.FromSeconds(3)}) is {} output?new MsiWmiObject(output):null;
    public void Dispose()=>instance.Dispose();
}
internal static class MsiPacket
{
    internal static void CheckReturn(object? value)
    {
        // MSI methods can return Boolean (true means success), while numeric
        // WMI status values use zero. Do not convert true into numeric failure 1.
        if(value is bool accepted){if(!accepted)throw new IOException("MSI 接口拒绝请求");}
        else if(value is not null&&Convert.ToUInt32(value)!=0)throw new IOException("MSI 接口拒绝请求");
    }
    public static byte[] Call(IMsiWmi wmi,string method,byte[] bytes)
    {
        IMsiWmiObject? parameters=null,data=null;
        string stage="parameters",shape="method";
        try
        {
            try { parameters=wmi.Parameters(method);data=parameters?.Data; }
            catch(Exception e) { TraceLog.Error("msi-parameter-fallback",e,new {method}); }
            if(parameters is null||data is null)
            {
                data?.Dispose();data=null;parameters?.Dispose();parameters=null;
                stage="get-wmi-template";shape="Get_WMI";
                // Get_WMI has no input template requirement. Use its complete
                // output as the input container, matching the OEM/HC contract.
                parameters=wmi.Invoke("Get_WMI",null);data=parameters?.Data;
            }
            if(parameters is null||data is null)throw new IOException("MSI 接口缺少数据结构（Get_WMI）");
            stage="fill-packet";data.Bytes=bytes;parameters.Data=data;
            stage="invoke";
            using var output=wmi.Invoke(method,parameters)??throw new IOException("MSI 接口未返回");
            // Never retry an invoked Get_Data/Set_Data using another shape.
            stage="response";output.CheckReturn();
            using var result=output.Data;
            var reply=result?.Bytes??(method=="Set_Data"?[]:throw new IOException("MSI 接口回执不完整"));
            TraceLog.Write("msi-packet",new {method,shape,block=(int)bytes[0],length=reply.Length,flag=reply.Length>0?(int?)reply[0]:null,raw=method=="Get_Data"&&reply.Length>1?(int?)reply[1]:null,requestHex=Convert.ToHexString(bytes),replyHex=Convert.ToHexString(reply),chargeRaw=method is "Get_AP" or "Set_AP"&&reply.Length>5?(int?)reply[5]:null},method.StartsWith("Set_",StringComparison.Ordinal));
            return reply;
        }
        catch(Exception e)
        {
            TraceLog.Error("msi-packet-error",e,new {method,stage,shape});
            throw new IOException($"MSI {method}/{stage}：{e.Message}（0x{e.HResult:X8}）",e);
        }
        finally {data?.Dispose();parameters?.Dispose();}
    }
}
