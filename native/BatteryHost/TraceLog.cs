using System.Management;
using System.Text;
using System.Text.Json;
namespace YeManBatteryHost;
internal static class TraceLog
{
    const long MaxBytes=256*1024;
    static readonly object Gate=new();
    static readonly Dictionary<string,(string Text,long Time)> Recent=[];
    static string? path;
    public static void Configure(string directory) {lock(Gate){path=Path.Combine(directory,"battery-charge-host.log");Recent.Clear();}}
    public static void Write(string eventName,object detail,bool force=false)
    {
        try
        {
            lock(Gate)
            {
                if(path is null)return;
                var text=JsonSerializer.Serialize(detail,Policy.Json);long now=Environment.TickCount64;
                if(!force&&Recent.TryGetValue(eventName,out var previous)&&previous.Text==text&&now-previous.Time<60000)return;
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                if(File.Exists(path)&&new FileInfo(path).Length>=MaxBytes)File.Move(path,path+".1",true);
                var row=JsonSerializer.Serialize(new {schema=1,time=DateTimeOffset.Now,pid=Environment.ProcessId,@event=eventName,detail},Policy.Json);
                File.AppendAllText(path,row+"\n",new UTF8Encoding(false));
                if(Recent.Count>=64)Recent.Clear();Recent[eventName]=(text,now);
            }
        }
        catch { /* Diagnostics must not affect control or restoration. */ }
    }
    public static void Error(string eventName,Exception error,object? context=null)=>Write(eventName,new {type=error.GetType().FullName,message=error.Message,hresult=$"0x{error.HResult:X8}",wmiStatus=error is ManagementException wmi?wmi.ErrorCode.ToString():null,context});
    public static void SecondaryDisplayResult(string operation,SecondaryDisplaySnapshot result)=>Write("secondary-display-result",new {operation,result.Ok,result.Supported,result.Model,result.CapabilityId,result.Raw,result.Brightness,result.Enabled,result.Verification,result.Reason,result.Stage},operation!="secondaryDisplay.get");
    public static void Result(string operation,Snapshot result)=>Write("request-result",new {operation,result.Ok,result.Supported,result.BatteryPresent,result.Model,result.Family,result.Kind,result.CurrentLimit,result.RequestedLimit,result.Managed,result.Verification,result.Reason},operation!="get");
}
