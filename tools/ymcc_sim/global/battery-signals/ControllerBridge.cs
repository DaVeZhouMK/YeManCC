using System.Text.Json;
using YeManBatteryHost;

// Test transport only. Controller owns all state and recovery; the existing Rig
// supplies memory storage and fake power/provider ports. No production Main.
internal static class BatteryControllerBridge
{
    static Rig Rig = new("lenovo-go-conservation");
    static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    static object Stats() => new { scope = "actual-controller-with-injected-ports", route = Rig.Route.RouteId,
        raw = Rig.Device.Raw, writes = Rig.Device.Writes, reads = Rig.Device.Reads, opens = Rig.Opens,
        memoryRecords = Rig.Store.Data.Count, physicalWrites = 0 };
    static object Execute(string command, JsonElement args)
    {
        if (command == "fixture.reset")
        {
            var route = args.TryGetProperty("route", out var item) ? item.GetString()! : "lenovo-go-conservation";
            if (route is not ("lenovo-go-conservation" or "ayaneo-cec-bypass" or "asus-ally-acpi")) throw new IOException("UNREVIEWED_FIXTURE_ROUTE");
            Rig.Controller.Shutdown(); Rig = new Rig(route);
            return Stats();
        }
        if (command == "fixture.stats") return Stats();
        if (command == "fixture.set")
        {
            if (args.TryGetProperty("raw", out var raw)) Rig.Device.Raw = raw.GetInt32();
            if (args.TryGetProperty("percent", out var percent)) Rig.Percent = percent.GetInt32();
            if (args.TryGetProperty("present", out var present)) Rig.Present = present.GetBoolean();
            if (args.TryGetProperty("ac", out var ac)) Rig.Ac = ac.GetBoolean();
            if (args.TryGetProperty("throwAfterWrite", out var lost)) Rig.Device.ThrowAfterWrite = lost.GetBoolean();
            if (args.TryGetProperty("failRead", out var failed)) Rig.Device.FailRead = failed.GetBoolean();
            return Stats();
        }
        if (command == "fixture.suspend") { Rig.Controller.Suspend(); return Stats(); }
        if (command == "fixture.resume") { Rig.Controller.Resume(); return Stats(); }
        if (command == "fixture.tick") { Rig.Controller.Tick(); return Stats(); }
        var operation = command switch { "fixture.restore" => "restore", "battery.charge.get" => "get", "battery.charge.refresh" => "refresh", "battery.charge.set" => "set", "battery.charge.release" => "release", _ => throw new IOException("UNREVIEWED_BATTERY_COMMAND") };
        // Execute the current production dispatch statement, not a copied switch.
        var fields = new Dictionary<string, object> { ["protocol"] = RuntimeContract.Protocol, ["sourceId"] = RuntimeContract.SourceId, ["op"] = operation };
        foreach(var property in args.EnumerateObject()) if(property.Name is "fence" or "target") fields[property.Name] = property.Value.Clone();
        var envelope = JsonSerializer.SerializeToElement(fields);
        RuntimeContract.Validate(envelope);
        return ProductionBatteryDispatch.Dispatch(Rig.Controller, envelope);
    }
    static async Task<int> Main()
    {
        while (await Console.In.ReadLineAsync() is { } line)
        {
            if (line.Length > 16384) return 2;
            int id = -1;
            try
            {
                using var doc = JsonDocument.Parse(line);
                var root = doc.RootElement; id = root.GetProperty("id").GetInt32();
                var command = root.GetProperty("command").GetString()!;
                if (command == "fixture.shutdown")
                {
                    Rig.Controller.Shutdown(); Console.WriteLine(JsonSerializer.Serialize(new { id, ok = true, reply = Stats() }, JsonOptions)); return 0;
                }
                var args = root.TryGetProperty("args", out var a) ? a : JsonSerializer.SerializeToElement(new { });
                var reply = Execute(command, args);
                Console.WriteLine(JsonSerializer.Serialize(new { id, ok = true, reply }, JsonOptions));
            }
            catch (Exception error)
            {
                Console.WriteLine(JsonSerializer.Serialize(new { id, ok = false, error = error is IOException ? error.Message : "FIXTURE_REQUEST_REJECTED" }, JsonOptions));
            }
        }
        Rig.Controller.Shutdown(); return 0;
    }
}