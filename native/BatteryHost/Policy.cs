using System.Reflection;
using System.Text.Json;
using System.Text.RegularExpressions;
namespace YeManBatteryHost;
internal sealed record Identity(string Manufacturer, string BoardProduct, string SystemProduct, string Processor, string Bios, int EcMajor, int EcMinor)
{
    public string Key => Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes($"{Manufacturer}|{BoardProduct}|{SystemProduct}|{Processor}|{Bios}|{EcMajor}|{EcMinor}")));
}
internal sealed record MatchRule(string[] Manufacturers, string Field, string[] Values, string DeviceClass, string? ProcessorPattern);
internal sealed record FirmwarePair(int BoardId,int Firmware);
internal sealed record FirmwareGate(string Kind,int MajorMin,int MinorMin,string? Bios,int EcMajor,int EcMinor,FirmwarePair[]? Pairs);
internal sealed record Route(string RouteId,string Family,string Kind,MatchRule Identity,FirmwareGate FirmwareGate,int Min)
{
    public bool Software => Kind == "software-bypass-threshold";
    public bool Fixed => Kind == "firmware-fixed";
    public int[] Choices => Fixed ? [80,100] : Enumerable.Range(Math.Max(Min,50)/5, (100-Math.Max(Min,50))/5+1).Select(x=>x*5).ToArray();
}
internal static class Policy
{
    public static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web){WriteIndented=false};
    public static readonly Route[] Routes;
    private static readonly YeManFanBackend.Capabilities.HcBatteryCapabilityDispatcher<Route> capabilityRoutes;
    static Policy()
    {
        using var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("YeManBatteryHost.battery-routes.json")!;
        using var buffer=new MemoryStream();stream.CopyTo(buffer);var bytes=buffer.ToArray();
        using var doc=JsonDocument.Parse(bytes);
        Routes=doc.RootElement.EnumerateArray().Select(x=>new Route(x.GetProperty("route").GetString()!,x.GetProperty("family").GetString()!,x.GetProperty("kind").GetString()!,x.GetProperty("identity").Deserialize<MatchRule>(Json)!,x.GetProperty("firmwareGate").Deserialize<FirmwareGate>(Json)!,x.GetProperty("min").GetInt32())).ToArray();
        capabilityRoutes=YeManFanBackend.Capabilities.HcBatteryCapabilityCatalog.Bind(Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes)),Routes,r=>r.RouteId,r=>r.Identity.DeviceClass);
    }
    static string Norm(string value)=>value.Trim().ToUpperInvariant();
    public static YeManFanBackend.Capabilities.BatteryCapabilityBinding<Route>? ResolveCapability(Identity id)=>capabilityRoutes.Resolve(id.Manufacturer,id.BoardProduct,id.SystemProduct,id.Processor);
    public static Route? Resolve(Identity id)=>ResolveCapability(id)?.Consumer;
    public static bool FirmwareAllowed(Route route,Identity id)
    {
        var g=route.FirmwareGate;
        if(g.Kind=="hc-ec-components") return id.EcMajor>=0&&id.EcMajor<255&&id.EcMinor>=0&&id.EcMinor<255&&id.EcMajor>=g.MajorMin&&id.EcMinor>=g.MinorMin;
        if(g.Kind=="exact-reviewed-firmware") return Regex.IsMatch(id.Bios,@"(?:^|[^0-9])2\.10(?:$|[^0-9])")&&id.EcMajor==g.EcMajor&&id.EcMinor==g.EcMinor;
        return g.Kind is "provider-probe-required" or "exact-board-firmware";
    }
    public static bool ValueAllowed(Route route,int value)=>route.Choices.Contains(value);
    public static int? BypassDecision(int? percent,bool ac,int target,bool paused)
    {
        if(!ac||percent is null||percent<0||percent>100||target==100)return 0;
        if(percent>=target)return 1;
        if(percent<=target-3)return 0;
        return paused?1:0;
    }
    public static int MsiEncode(int raw,int target)=>target==100 ? raw&0x7f : 0x80|target;
    public static string Unavailable(Identity id)=>Norm(id.Manufacturer).Contains("ONE-NETBOOK")||Norm(id.Manufacturer)=="AOKZOE" ? "此型号的 Windows 充电接口尚未验证，暂不能设置" : "此设备暂无可确认的充电上限接口";
}
