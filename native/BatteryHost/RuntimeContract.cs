using System.Reflection;
using System.Text.Json;
namespace YeManBatteryHost;
internal static class RuntimeContract
{
    static readonly JsonElement contract=Load();
    static JsonElement Load()
    {
        using var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("YeManBatteryHost.runtime-contract.json")!;
        using var doc=JsonDocument.Parse(stream);return doc.RootElement.Clone();
    }
    public static int Protocol=>contract.GetProperty("protocol").GetInt32();
    public static string SourceId=>contract.GetProperty("sourceId").GetString()!;
    // Pure identity query: no WMI, battery probing, journal recovery, owner mutex or state writes.
    public static object Identity()=>new {ok=true,protocol=Protocol,sourceId=SourceId,
        component="YeManBatteryHost",implementation=contract.GetProperty("implementation").GetString(),
        version=typeof(RuntimeContract).Assembly.ManifestModule.ModuleVersionId,
        assemblyPath=typeof(RuntimeContract).Assembly.Location,processPath=Environment.ProcessPath};
    public static void Validate(JsonElement request)
    {
        if(request.ValueKind!=JsonValueKind.Object||!request.TryGetProperty("protocol",out var protocol)||
            protocol.ValueKind!=JsonValueKind.Number||!protocol.TryGetInt32(out var version)||version!=Protocol||
            !request.TryGetProperty("sourceId",out var source)||source.ValueKind!=JsonValueKind.String||source.GetString()!=SourceId)
            throw new IOException("充电组件与主程序版本不一致，请退出 YMCC 后同时更新 YeManCC 和 PowerControl 文件夹");
    }
}
