using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using YeManFanBackend.Capabilities;

// Scratch entry for the existing ReplayConsumer. Program/Main, catalog,
// DeviceCoreConsumer and owner lease are the actual product sources. Only the
// owner IPC names and the DeviceCore's HID terminal are private fixture seams.
internal static class X2MiniReplayEntry
{
    private const string Model = "HandheldCompanion.Devices.OneXPlayerX2MiniPro";
    private const string X1 = "HandheldCompanion.Devices.OneXPlayerX1Mini";
    private const string Endpoint = @"\\?\hid#vid_1a86&pid_fe00#offline-x2";
    private const string Generation = "71:83:97";
    private static readonly string[] Blocked = ["HandheldCompanion", "Shared", "PresentationCore", "PresentationFramework", "WindowsBase", "Microsoft.Windows.SDK.NET", "WinRT.Runtime", "HidLibrary"];
    private static string corePath = "", logPath = "";
    private static int checks;
    private static readonly AssemblyLoadContext directCoreContext = new("X2Mini-Direct-Core-Fixture", isCollectible: true);
    private static readonly List<object> operations = new();
    private sealed record Scenario(string Name, string Group, Action Run);
    private sealed class FixtureAssertion(string message) : Exception(message);
    private static void Check(bool condition, string message)
    { if (!condition) throw new FixtureAssertion(message); checks++; }
    private static JsonElement Parse(string json) => JsonSerializer.Deserialize<JsonElement>(json);
    private static JsonElement[] Events()
    {
        if(!File.Exists(logPath)) return [];
        using var stream=new FileStream(logPath,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);
        using var reader=new StreamReader(stream);var events=new List<JsonElement>();
        while(reader.ReadLine() is {} line) {
            if(line.Length==0)continue;
            try {events.Add(Parse(line));}catch(JsonException){ /* writer may still own its final line */ }
        }
        return events.ToArray();
    }
    private static string[] Reports() => Events().Where(e => e.GetProperty("eventName").GetString() == "write")
        .Select(e => e.GetProperty("hex").GetString()!).ToArray();
    private static void Terminal(string mode = "normal")
    {
        File.WriteAllText(logPath, "");
        if(File.Exists(logPath+".resume"))File.Delete(logPath+".resume");
        Environment.SetEnvironmentVariable("YMCC_LIGHT_REPLAY_MODE", mode);
    }
    private static void AssertReleased(bool opened = true, bool attempted = false)
    {
        var events = Events();
        Check(events.Count(e => e.GetProperty("eventName").GetString() == "open-x2") == (opened || attempted ? 1 : 0), "exact X2 open count");
        Check(events.Count(e => e.GetProperty("eventName").GetString() == "release") == (opened ? 1 : 0), "exact release count");
        if (opened) Check(events[^1].GetProperty("eventName").GetString() == "release", "release is final event");
        Check(!events.Any(e => e.GetProperty("eventName").GetString() == "open"), "no X1 terminal fallback");
        Check(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>Blocked.Contains(a.GetName().Name)),"no HC/UI/SDK dependency after consumer");
        foreach (var e in events)
            Check(!e.GetProperty("loaded").EnumerateArray().Any(a => Blocked.Contains(a.GetString())), "no HC/UI/SDK dependency loaded");
    }
    private static string[] Expected(string action, int brightness = 50, OracleLevel level = OracleLevel.SolidColor,
        OracleColor? main = null, OracleColor? secondary = null, bool includeBrightness = false, int speed = 100)
    {
        var oracle = new FixedSourceOracle();
        var primary = main ?? new OracleColor(10, 20, 30);
        var second = secondary ?? new OracleColor(99, 88, 77);
        if (action == "brightness") Check(oracle.SetLedBrightness(Math.Clamp(brightness, 0, 100)), "source brightness accepted");
        else if (action == "off")
        {
            Check(oracle.SetLedBrightness(0), "source off brightness accepted");
            Check(oracle.SetLedColor(new(0, 0, 0), new(0, 0, 0), OracleLevel.SolidColor), "source off black accepted");
        }
        else
        {
            Check(oracle.SetLedColor(primary, second, level, speed), "source color accepted");
            if (includeBrightness) Check(oracle.SetLedBrightness(Math.Clamp(brightness, 0, 100)), "source combined brightness accepted");
        }
        Check(OracleThread.Delays.Count == oracle.Terminal.Reports.Count && OracleThread.Delays.All(x => x == 200), "fixed source delay after every zone including last");
        Check(oracle.Terminal.Reports.All(r => r.Length == 65 && r[0] == 0), "fixed source 1+64 reports");
        return oracle.Terminal.Reports.Select(Convert.ToHexString).ToArray();
    }
    private static string[] ColorArgs(string level = "solid", string? brightness = null, int speed = 23)
    {
        var args = new List<string> { "--level", level, "--r", "10", "--g", "20", "--b", "30", "--r2", "99", "--g2", "88", "--b2", "77", "--speed", speed.ToString() };
        if (brightness is not null) args.AddRange(["--brightness", brightness]);
        return args.ToArray();
    }
    private static (int Exit, JsonElement Receipt) Helper(string action, string[]? extra = null,
        string model = Model, bool withCore = true, bool withOwner = true, string? component = null)
    {
        var args = new List<string> { "--expected-device-class", model, "--action", action };
        if (withCore) args.AddRange(["--device-core-assembly", component ?? corePath]);
        if (withOwner) args.AddRange(["--owner-interface-path", Endpoint, "--owner-generation", Generation]);
        if (extra is not null) args.AddRange(extra);
        using var stdout = new StringWriter();
        var previous = Console.Out;
        int exit;
        try
        {
            Console.SetOut(stdout);
            exit = (int)typeof(Program).GetMethod("Main", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, [args.ToArray()])!;
        }
        finally { Console.SetOut(previous); }
        var raw = stdout.ToString();
        var receipt = Parse(raw);
        operations.Add(new { call = "actual-Program.Main", args, exitCode = exit, raw, events = Events() });
        return (exit, receipt);
    }
    private static void Success(string action, string[] args, string[] expected, string note = "ok", string mode = "normal", bool useOwner = true, string? firstFailure = null)
    {
        Terminal(mode);
        using var owner = useOwner ? new PrivateOwner() : null;
        var elapsed=Stopwatch.StartNew();
        var result = Helper(action, args, withOwner: useOwner);
        Check(result.Exit == (note == "ok" ? 0 : 1), "actual helper exit reflects software write result: " + result.Receipt.GetRawText());
        Check(result.Receipt.GetProperty("ok").GetBoolean() == (note == "ok"), "actual helper ok matches note: " + result.Receipt.GetRawText());
        Check(elapsed.Elapsed.TotalMilliseconds+10 >= expected.Length*200,"actual source delay after every zone including last");
        var receipt = result.Receipt.GetProperty("result");
        Check(receipt.GetProperty("note").GetString() == note, "actual core note " + note);
        Check(receipt.GetProperty("backend").GetString() == "ymcc-onex-hid-v1", "same admitted independent provider");
        var levelIndex=Array.IndexOf(args,"--level");
        var effect=levelIndex>=0?args[levelIndex+1].ToLowerInvariant():"solid";
        Check(receipt.GetProperty("capabilityOperation").GetString() == "lighting." + action + (action=="color"?"."+effect:""), "operation identity preserved");
        Check(receipt.GetProperty("deviceClass").GetString() == Model, "exact model receipt");
        Check(receipt.GetProperty("writeAttempts").GetInt32() == expected.Length, "actual write attempts");
        Check(receipt.GetProperty("released").GetBoolean() && !receipt.GetProperty("physicalEffectVerified").GetBoolean() && !receipt.GetProperty("hcLoaded").GetBoolean(), "software release and physical boundary");
        Check(receipt.GetProperty("sourceSha256").GetString() == "7f06350daeb1d1c7157ae21f1dc231637394008256b3fc4c22c747109e2c7b67" && receipt.GetProperty("sourceAdapter").GetString() == "onex-x2mini-hid-v1", "fixed source identity");
        Check(receipt.GetProperty("fixedSpeed").GetBoolean() && receipt.GetProperty("speedIgnored").GetBoolean() && !receipt.GetProperty("speedApplied").GetBoolean(), "writer ignores speed, no source flag inference");
        Check(Reports().SequenceEqual(expected), "actual helper complete fixed-source reports");
        if (note != "ok") Check(receipt.GetProperty("firstFailure").GetString() == (firstFailure ?? (action == "off" ? "setLedBrightness-false" : note)), "first failed operation preserved");
        AssertReleased();
    }
    private static void Failure(string action, string[]? extra = null, string? error = null, string mode = "normal",
        int writes = 0, bool opened = false, bool withOwner = true, string model = Model, bool withCore = true, bool publishOwner = true, string? component = null)
    {
        Terminal(mode);
        using var owner = publishOwner && withOwner ? new PrivateOwner() : null;
        var result = Helper(action, extra, model, withCore, withOwner, component);
        Check(result.Exit == 1 && !result.Receipt.GetProperty("ok").GetBoolean(), "actual helper rejected operation");
        var text = result.Receipt.TryGetProperty("error", out var e) ? e.GetString() ?? "" : "";
        if (error is not null) Check(text.Contains(error, StringComparison.OrdinalIgnoreCase), "expected error " + error + "; got " + text);
        Check(Reports().Length == writes, "exact reports before failure");
        AssertReleased(opened, mode=="no-device");
    }
    private static void MissingCurrentOwner()
    {
        Terminal();
        var result = Helper("brightness");
        Check(result.Exit == 1 && !result.Receipt.GetProperty("ok").GetBoolean(), "missing native owner rejects actual helper");
        var failures = result.Receipt.GetProperty("dependencyFailures").EnumerateArray().ToArray();
        Check(failures.Any(f => f.GetProperty("Depth").GetInt32() == 0 &&
            f.GetProperty("ExceptionType").GetString() == "System.IO.IOException" &&
            f.GetProperty("Stage").GetString() == "lighting.execute" &&
            f.GetProperty("TargetSite").GetString() == "Oxp3OwnerLease.Acquire"), "typed owner acquire failure retained");
        Check(failures.Any(f => f.GetProperty("ExceptionType").GetString() == "System.IO.FileNotFoundException" &&
            f.GetProperty("HResultHex").GetString() == "0x80070002" &&
            f.GetProperty("Stage").GetString() == "lighting.execute" &&
            (f.GetProperty("TargetSite").GetString()?.StartsWith("System.IO.MemoryMappedFiles.MemoryMappedFile.", StringComparison.Ordinal) ?? false)), "typed missing handoff cause retained");
        Check(Reports().Length == 0, "no writes without published owner");
        AssertReleased(false);
    }
    private static Type CoreApi() => directCoreContext.LoadFromAssemblyPath(Path.GetFullPath(corePath))
        .GetType("YeManCC.Lighting.X2MiniProDeviceCore", true)!;
    private static string Direct(string request, CancellationToken token = default, Action? current = null, string model = Model)
    {
        var method = CoreApi().GetMethod("ExecuteOwned", BindingFlags.Public | BindingFlags.Static)!;
        try { return (string)method.Invoke(null, [model, request, Endpoint, token, current ?? (() => { })])!; }
        catch (TargetInvocationException e) when (e.InnerException is not null) { throw e.InnerException; }
    }
    private static void DirectSolidControl()
    {
        Terminal();
        var raw = Direct("{\"Action\":\"color\",\"Level\":\"solid\",\"R\":10,\"G\":20,\"B\":30,\"R2\":99,\"G2\":88,\"B2\":77}");
        operations.Add(new { call = "actual-X2MiniProDeviceCore.ExecuteOwned", raw, events = Events() });
        Check(Reports().SequenceEqual(Expected("color")), "semantic-control complete five-zone RGB/source oracle equality");
        AssertReleased();
    }
    private static void Matrix()
    {
        var api = CoreApi();
        var x1api = api.Assembly.GetType("YeManCC.Lighting.LightingDeviceCore", true)!;
        foreach (var tuple in new (string Model, string Action, string? Level, bool Allowed)[] {
            (Model,"brightness",(string?)null,true), (Model,"color","solid",true), (Model,"color","breathing",true),
            (Model,"off",null,true), (Model,"color","rainbow",false), (Model,"status",null,false), (Model,"preset",null,false),
            ("HandheldCompanion.Devices.Unknown","brightness",null,false) })
        {
            Check((bool)api.GetMethod("Supports")!.Invoke(null, [tuple.Item1,tuple.Item2,tuple.Item3])! == tuple.Item4, "actual X2 Supports matrix " + tuple);
            if (tuple.Item4)
            {
                var admitted = HcLightingCapabilityCatalog.Require(tuple.Item1, null, tuple.Item2, tuple.Item3);
                Check(admitted.PreferredProvider == HcLightingCapabilityCatalog.IndependentProvider, "catalog operation admits same provider");
            }
        }
        var binding=HcLightingCapabilityCatalog.Resolve(Model)!;
        Check(binding.Consumer.SourceAdapter==LightingSourceAdapter.OneXX2MiniHid&&binding.Consumer.IndependentCore&&binding.Consumer.PresetDeclared&&!binding.Consumer.PresetAvailable&&!binding.Consumer.StatusAvailable,"ancestor preset declaration remains distinct from admitted writer");
        Check((bool)x1api.GetMethod("Supports")!.Invoke(null,[X1,"color","rainbow"])!, "X1 rainbow retained in same assembly");
        Check(!(bool)x1api.GetMethod("Supports")!.Invoke(null,[X1,"color","breathing"])!, "X1 breathing remains unimplemented");
        Check(HcLightingCapabilityCatalog.Require(X1,null,"color","rainbow").PreferredProvider == HcLightingCapabilityCatalog.IndependentProvider, "X1 model scoped action retained");
        Terminal();
        try { _ = Direct("{\"Action\":\"color\",\"Level\":\"rainbow\"}"); throw new FixtureAssertion("direct unsupported effect accepted"); }
        catch (NotSupportedException) { checks++; }
        AssertReleased(false);
    }
    private static List<Scenario> Scenarios()
    {
        var cases = new List<Scenario>();
        void Add(string name, string group, Action action) => cases.Add(new(name, group, action));
        Add("matrix-same-assembly-model-operation-admission", "admission", Matrix);
        Add("core-five-zone-secondary-solid", "wire", DirectSolidControl);
        Add("core-64-byte-builder-and-65-byte-report-boundaries", "wire", () => {
            var builder=CoreApi().Assembly.GetType("YeManCC.Lighting.PinnedOneXProtocol",true)!;
            foreach(var length in new[]{0,1,58,59,60,100}) {
                var command=Enumerable.Range(0,length).Select(x=>(byte)(x+1)).ToList();
                var expectedPayload=OracleBase.BuildV1Command(0xB8,0x01,command);
                var payload=(byte[])builder.GetMethod("BuildV1Command")!.Invoke(null,[(byte)0xB8,(byte)0x01,command])!;
                Check(payload.Length==64&&payload.SequenceEqual(expectedPayload),"actual 64-byte payload/truncation from independent fixed method");
                var report=(byte[])builder.GetMethod("WithReportID")!.Invoke(null,[payload,(byte)0,64])!;
                Check(report.Length==65&&report.SequenceEqual(OracleBase.WithReportID(expectedPayload)),"actual report-id plus 64 bytes from independent IDevice");
            }
        });
        Add("helper-solid-secondary-and-brightness", "wire", () => Success("color",ColorArgs(brightness:"50"),Expected("color",includeBrightness:true)));
        Add("helper-breathing-secondary", "wire", () => Success("color",ColorArgs("BrEaThInG"),Expected("color",level:OracleLevel.Breathing)));
        Add("helper-default-red-and-secondary-fallback", "wire", () => Success("color",[],Expected("color",main:new(255,0,0),secondary:new(255,0,0))));
        Add("helper-secondary-per-channel-fallback", "wire", () => Success("color",["--r","10","--g","20","--b","30","--r2","99"],Expected("color",secondary:new(99,20,30))));
        Add("helper-clamped-primary-secondary", "wire", () => Success("color",["--r","-1","--g","256","--b","30","--r2","999","--g2","-50","--b2","77"],Expected("color",main:new(0,255,30),secondary:new(255,0,77))));
        foreach (int brightness in new[] { -1,0,1,32,33,34,65,66,67,99,100,101,255 })
        {
            var value = brightness;
            Add("helper-brightness-"+value,"wire",()=>Success("brightness",["--brightness",value.ToString()],Expected("brightness",value)));
        }
        Add("helper-default-brightness", "wire", () => Success("brightness",[],Expected("brightness")));
        foreach (int speed in new[] { int.MinValue,0,1,100,255,int.MaxValue })
        {
            var value = speed;
            Add("helper-speed-ignored-"+value,"wire",()=>Success("color",ColorArgs(speed:value),Expected("color",speed:value)));
        }
        Add("helper-off-five-zero-then-five-black", "wire", () => Success("off",[],Expected("off")));
        Add("helper-color-first-failure-continues-ten", "wire", () => Success("color",ColorArgs(brightness:"50"),Expected("color",includeBrightness:true),"setLedColor-false","false:all"));
        Add("helper-brightness-after-color-failure", "wire", () => Success("color",ColorArgs(brightness:"50"),Expected("color",includeBrightness:true),"setLedBrightness-false","false:6"));
        Add("helper-off-partial-preserves-first-failure", "wire", () => Success("off",[],Expected("off"),"off-partial","false:all"));
        Add("helper-off-color-failure-after-zero-brightness", "wire", () => Success("off",[],Expected("off"),"off-partial","false:6",firstFailure:"setLedColor-false"));
        Add("helper-throw-first-no-legacy-retry", "wire", () => Failure("color",ColorArgs().Concat(new[]{"--hc-assembly","never-load-HC.dll"}).ToArray(),"fixture write failure","throw:1",1,true));
        Add("helper-short-write-no-success", "wire", () => Failure("color",ColorArgs(),"fixture incomplete report write","short:2",2,true));
        Add("helper-disconnect-releases", "wire", () => Failure("brightness",error:"write-attempt mismatch",mode:"disconnected",opened:true));
        Add("helper-missing-device-no-unowned-release", "wire", () => Failure("brightness",error:"fixture HID endpoint missing",mode:"no-device"));
        Add("helper-dispose-exception-is-failure", "wire", () => Failure("brightness",error:"fixture release failure",mode:"release-throw",writes:5,opened:true));
        Add("helper-deadline-cancels-blocked-write", "wire", () => Failure("color",["--timeout-ms","150"],mode:"block:1",writes:1,opened:true));
        Add("core-unowned-api-rejects-owned-model", "admission", () => {
            Terminal();var api=CoreApi();
            foreach(var type in new[]{api,api.Assembly.GetType("YeManCC.Lighting.LightingDeviceCore",true)!}) {
                try { _=type.GetMethod("Execute")!.Invoke(null,[Model,"{\"Action\":\"brightness\"}"]);throw new FixtureAssertion("unowned Execute accepted X2"); }
                catch(TargetInvocationException e) when(e.InnerException is NotSupportedException) {checks++;}
            }
            AssertReleased(false);
        });
        Add("core-precancel-no-handle", "admission", () => {
            Terminal(); using var stop=new CancellationTokenSource(); stop.Cancel();
            try { _=Direct("{\"Action\":\"brightness\"}",stop.Token); throw new FixtureAssertion("cancel accepted"); }
            catch (OperationCanceledException) { checks++; } AssertReleased(false);
        });
        Add("core-owner-invalid-no-handle", "admission", () => {
            Terminal(); try { _=Direct("{\"Action\":\"brightness\"}",current:()=>throw new IOException("fixture owner obsolete")); throw new FixtureAssertion("obsolete accepted"); }
            catch(IOException e) when(e.Message=="fixture owner obsolete") { checks++; } AssertReleased(false);
        });
        Add("core-owner-revoked-between-zones", "admission", () => {
            Terminal(); try { _=Direct("{\"Action\":\"brightness\"}",current:()=> { if(Reports().Length>0) throw new IOException("fixture owner obsolete"); }); throw new FixtureAssertion("obsolete accepted"); }
            catch(IOException e) when(e.Message=="fixture owner obsolete") { checks++; } Check(Reports().Length==1,"revocation stops next zone"); AssertReleased();
        });
        foreach (var action in new[] { "status","preset" })
        {
            var value=action;
            Add("helper-preopen-unsupported-"+value,"admission",()=>Failure(value,value=="status"?["--status","on"]:["--preset-id","not-admitted"],error:"LIGHTING_OPERATION_UNAVAILABLE",withOwner:false));
        }
        foreach (var effect in new[] { "rainbow","pulse","LEDPreset" })
        {
            var value=effect;
            Add("helper-preopen-unsupported-effect-"+value,"admission",()=>Failure("color",["--level",value],error:value=="rainbow"?"LIGHTING_OPERATION_UNAVAILABLE":"unsupported color level",withOwner:false));
        }
        Add("helper-preopen-unknown-model", "admission",()=>Failure("brightness",model:"HandheldCompanion.Devices.Unknown",error:"LIGHTING_CAPABILITY_UNAVAILABLE",withOwner:false));
        Add("helper-preopen-x2-without-component", "admission",()=>Failure("brightness",error:"requires the pinned independent device core",withCore:false,withOwner:false));
        Add("helper-preopen-missing-owner", "admission",()=>Failure("brightness",error:"current native OneX owner",withOwner:false));
        Add("helper-preopen-no-current-owner-no-discovery", "admission", MissingCurrentOwner);
        Add("helper-preopen-wrong-caller-pin", "admission",()=>Failure("brightness",["--device-core-sha256",new string('0',64)],error:"not an allowed pin",withOwner:false));
        Add("helper-preopen-tampered-component", "admission",()=> {
            var altered=Path.Combine(Path.GetDirectoryName(logPath)!,"tampered-x2.dll");
            File.WriteAllBytes(altered,File.ReadAllBytes(corePath).Concat(new byte[]{0}).ToArray());
            Failure("brightness",error:"assembly SHA-256 mismatch",withOwner:false,component:altered);
        });
        Add("helper-private-owner-heartbeat-expiry", "admission",()=> {
            Terminal(); using var owner=new PrivateOwner(stale:true);
            var result=Helper("brightness"); Check(result.Exit==1 && !result.Receipt.GetProperty("ok").GetBoolean(),"expired heartbeat rejected"); AssertReleased(false);
        });
        Add("helper-private-owner-ambiguous-path", "admission",()=> {
            Terminal(); using var owner=new PrivateOwner(path:Endpoint+"-other");
            var result=Helper("brightness"); Check(result.Exit==1 && !result.Receipt.GetProperty("ok").GetBoolean(),"caller cannot substitute ambiguous endpoint"); AssertReleased(false);
        });
        Add("helper-private-owner-revoke-blocked-write", "admission",()=> {
            Terminal("block:1"); using var owner=new PrivateOwner();
            var trigger=Task.Run(()=> { if(!SpinWait.SpinUntil(()=>Reports().Length==1,2000)) return false; owner.Revoke(); return true; });
            var result=Helper("color",["--timeout-ms","3000"]);
            Check(trigger.GetAwaiter().GetResult(),"owned first write observed before revocation");
            Check(result.Exit==1 && !result.Receipt.GetProperty("ok").GetBoolean(),"owner revocation fails helper"); Check(Reports().Length==1,"revocation stops blocked and later writes"); AssertReleased();
        });
        Add("helper-private-owner-generation-change-between-zones", "admission", () => {
            Terminal("pause:1");using var owner=new PrivateOwner();
            var trigger=Task.Run(()=> {if(!SpinWait.SpinUntil(()=>Reports().Length==1,2000))return false;owner.Supersede();File.WriteAllText(logPath+".resume","");return true;});
            var result=Helper("color",["--timeout-ms","3000"]);
            Check(trigger.GetAwaiter().GetResult(),"exact first write precedes generation change");
            Check(result.Exit==1&&!result.Receipt.GetProperty("ok").GetBoolean(),"actual lease denies superseded generation");
            Check(Reports().Length==1,"generation change prevents all later zones");AssertReleased();
        });
        Add("helper-owner-v2-wrong-allowed-model-tag", "admission", () => {
            Terminal();using var owner=new PrivateOwner(modelTag:Oxp3OwnerLease.OneXPlayer3ModelTag);
            var result=Helper("brightness");Check(result.Exit==1&&!result.Receipt.GetProperty("ok").GetBoolean(),"Mini cannot borrow OXP3 record with same path/token");AssertReleased(false);
        });
        Add("helper-owner-v2-missing-model-tag", "admission", () => {
            Terminal();using var owner=new PrivateOwner(modelTag:0);
            var result=Helper("brightness");Check(result.Exit==1&&!result.Receipt.GetProperty("ok").GetBoolean(),"v2 missing model tag rejected");AssertReleased(false);
        });
        Add("helper-owner-v1-with-mini-tag-rejected", "admission", () => {
            Terminal();using var owner=new PrivateOwner(version:1);
            var result=Helper("brightness");Check(result.Exit==1&&!result.Receipt.GetProperty("ok").GetBoolean(),"old record version rejected despite matching model tag");AssertReleased(false);
        });
        Add("helper-owner-v1-missing-model-tag-rejected", "admission", () => {
            Terminal();using var owner=new PrivateOwner(version:1,modelTag:0);
            var result=Helper("brightness");Check(result.Exit==1&&!result.Receipt.GetProperty("ok").GetBoolean(),"original v1 untagged record cannot admit v2 helper");AssertReleased(false);
        });
        Add("helper-owner-v2-unknown-model-tag", "admission", () => {
            Terminal();using var owner=new PrivateOwner(modelTag:4);
            var result=Helper("brightness");Check(result.Exit==1&&!result.Receipt.GetProperty("ok").GetBoolean(),"unknown record model tag rejected");AssertReleased(false);
        });
        Add("owner-v2-old-oxp3-call-shape-admits-tag3", "admission", () => {
            Terminal();using var owner=new PrivateOwner(modelTag:Oxp3OwnerLease.OneXPlayer3ModelTag);
            using var lease=Oxp3OwnerLease.Acquire(Endpoint,Generation);
            Check(lease.ExpectedModelTag==Oxp3OwnerLease.OneXPlayer3ModelTag&&lease.IsCurrent,"old Acquire shape retains explicit default OXP3 tag");AssertReleased(false);
        });
        Add("owner-v2-old-oxp3-reader-cannot-borrow-mini-tag2", "admission", () => {
            Terminal();using var owner=new PrivateOwner();
            try {using var lease=Oxp3OwnerLease.Acquire(Endpoint,Generation);throw new FixtureAssertion("OXP3 borrowed Mini tag");}
            catch(IOException){checks++;}AssertReleased(false);
        });
        Add("owner-v2-invalid-expected-tags-rejected-preopen", "admission", () => {
            Terminal();foreach(var tag in new uint[]{0,1,4}){
                try {using var lease=Oxp3OwnerLease.Acquire(Endpoint,Generation,expectedModelTag:tag);throw new FixtureAssertion("invalid expected model tag accepted");}
                catch(ArgumentOutOfRangeException){checks++;}
            }AssertReleased(false);
        });
        Add("helper-owner-model-tag-change-stops-later-zones", "admission", () => {
            Terminal("pause:1");using var owner=new PrivateOwner();
            var trigger=Task.Run(()=>{if(!SpinWait.SpinUntil(()=>Reports().Length==1,2000))return false;owner.ChangeModelTag(Oxp3OwnerLease.OneXPlayer3ModelTag);File.WriteAllText(logPath+".resume","");return true;});
            var result=Helper("color",["--timeout-ms","3000"]);
            Check(trigger.GetAwaiter().GetResult(),"first write observed before source model tag changes");
            Check(result.Exit==1&&!result.Receipt.GetProperty("ok").GetBoolean(),"actual RecordCurrent denies changed model tag with same path/token");
            Check(Reports().Length==1,"changed source tag prevents every later zone");AssertReleased();
        });
        Add("helper-private-owner-resources-returned", "admission", () => {
            Terminal();using(var owner=new PrivateOwner()){}using(var owner=new PrivateOwner()){}
            Check(true,"exclusive private owner mapping can be recreated after leases returned");AssertReleased(false);
        });
        return cases;
    }
    [STAThread]
    public static int Main(string[] args)
    {
        try { return RunSelected(args); }
        finally { directCoreContext.Unload(); }
    }
    private static int RunSelected(string[] args)
    {
        if ((args.Length != 6 && args.Length != 8) || args[0]!="--case" || args[2]!="--core" || args[4]!="--log" || (args.Length == 8 && args[6] != "--cases"))
        { Console.Error.WriteLine("exact --case/--core/--log and optional --cases required"); return 64; }
        var selector=args[1]; corePath=Path.GetFullPath(args[3]); logPath=Path.GetFullPath(args[5]);
        Environment.SetEnvironmentVariable("YMCC_LIGHT_REPLAY_LOG",logPath);
        var all=Scenarios();
        var selected=selector switch {
            "x2mini" => all,
            "x2mini-wire" => all.Where(c=>c.Group=="wire").ToList(),
            "x2mini-admission" => all.Where(c=>c.Group=="admission").ToList(),
            "x2mini-control" => new List<Scenario>{ new("core-five-zone-secondary-solid","wire",DirectSolidControl) },
            _ => new List<Scenario>() };
        if(selected.Count==0) { Console.Error.WriteLine("unknown or empty selection"); return 64; }
        if (args.Length == 8)
        {
            var names = args[7].Split(',');
            if (names.Length == 0 || names.Any(string.IsNullOrEmpty) || names.Distinct().Count() != names.Length || names.Any(n => !selected.Any(c => c.Name == n)))
            { Console.Error.WriteLine("unknown empty or duplicate exact case list"); return 64; }
            selected = names.Select(n => selected.Single(c => c.Name == n)).ToList();
        }
        File.WriteAllText(logPath+".selection.json",JsonSerializer.Serialize(new{selector,selected=selected.Select(c=>c.Name),groups=selected.Select(c=>c.Group)}));
        var completed=new List<object>();
        var watch=Stopwatch.StartNew();
        foreach(var scenario in selected)
        {
            var before=operations.Count; var started=Stopwatch.StartNew();
            try { Terminal(); scenario.Run(); completed.Add(new { name=scenario.Name,group=scenario.Group,status="PASS",milliseconds=started.Elapsed.TotalMilliseconds,operations=operations.Skip(before).ToArray(),events=Events() }); }
            catch(Exception e) {
                completed.Add(new { name=scenario.Name,group=scenario.Group,status="FAIL",failureKind=e is FixtureAssertion?"ASSERTION":"UNEXPECTED_EXCEPTION",error=e.ToString(),milliseconds=started.Elapsed.TotalMilliseconds,operations=operations.Skip(before).ToArray(),events=Events() });
                Console.WriteLine(JsonSerializer.Serialize(new { ok=false,selector,selected=selected.Select(c=>c.Name),cases=completed,checks,milliseconds=watch.Elapsed.TotalMilliseconds,hardwareIo=false })); return 2;
            }
        }
        Console.WriteLine(JsonSerializer.Serialize(new { ok=true,selector,selected=selected.Select(c=>c.Name),cases=completed,checks,milliseconds=watch.Elapsed.TotalMilliseconds,hardwareIo=false,physicalEffectVerified=false }));
        return 0;
    }
    private sealed class PrivateOwner : IDisposable
    {
        private readonly MemoryMappedFile mapping;
        private readonly MemoryMappedViewAccessor view;
        private readonly EventWaitHandle revoked;
        private readonly Timer? timer;
        internal PrivateOwner(string path=Endpoint,bool stale=false,uint modelTag=Oxp3OwnerLease.X2MiniProModelTag,uint version=Oxp3OwnerLease.HandoffVersion)
        {
            // No global product IPC is opened or overwritten. Names were
            // rewritten to a fresh scratch identifier before compilation.
            mapping=MemoryMappedFile.CreateNew(Oxp3OwnerLease.HandoffRecordName,Oxp3OwnerLease.HandoffRecordSize);
            view=mapping.CreateViewAccessor();
            revoked=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor(Generation));
            view.Write(Oxp3OwnerLease.MagicOffset,Oxp3OwnerLease.HandoffMagic);
            view.Write(Oxp3OwnerLease.VersionOffset,version);
            view.Write(Oxp3OwnerLease.ModelTagOffset,modelTag);
            view.Write(Oxp3OwnerLease.SequenceOffset,2L);
            view.Write(Oxp3OwnerLease.AdmissionOffset,97L);
            view.Write(Oxp3OwnerLease.StateOffset,Oxp3OwnerLease.StateActive);
            view.Write(Oxp3OwnerLease.HeartbeatOffset,Environment.TickCount64-(stale?3000:0));
            var token=Encoding.ASCII.GetBytes(Generation); view.WriteArray(Oxp3OwnerLease.TokenOffset,token,0,token.Length);
            var wide=Encoding.Unicode.GetBytes(path); view.WriteArray(Oxp3OwnerLease.PathOffset,wide,0,wide.Length);
            if(!stale) timer=new Timer(_=>view.Write(Oxp3OwnerLease.HeartbeatOffset,Environment.TickCount64),null,200,200);
        }
        internal void Revoke()=>revoked.Set();
        internal void ChangeModelTag(uint modelTag)
        {
            view.Write(Oxp3OwnerLease.SequenceOffset,3L);
            view.Write(Oxp3OwnerLease.ModelTagOffset,modelTag);
            view.Write(Oxp3OwnerLease.SequenceOffset,4L);
        }
        internal void Supersede()
        {
            view.Write(Oxp3OwnerLease.SequenceOffset,3L);
            view.Write(Oxp3OwnerLease.AdmissionOffset,98L);
            var bytes=new byte[Oxp3OwnerLease.TokenLength];var token=Encoding.ASCII.GetBytes("71:83:98");Array.Copy(token,bytes,token.Length);
            view.WriteArray(Oxp3OwnerLease.TokenOffset,bytes,0,bytes.Length);
            view.Write(Oxp3OwnerLease.SequenceOffset,4L);
        }
        public void Dispose()
        {
            if(timer is not null) { using var done=new ManualResetEvent(false); timer.Dispose(done); done.WaitOne(); }
            revoked.Dispose(); view.Dispose(); mapping.Dispose();
        }
    }
}
