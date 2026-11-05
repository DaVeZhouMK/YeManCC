using System.Collections;
using System.Text.Json;
using YeManBatteryHost;

// One bounded child of the parent's existing BatteryHostTests Test group.
// All reflection types below are inert; Run executes the actual product reader.
internal static class SuperXUmaChargeReaderFixture
{
    private static readonly object FixtureGate = new();
    private static Scenario? active;
    private static Scenario Current => active ?? throw new InvalidOperationException("No UMA fixture scenario");

    internal static int Run()
    {
        lock (FixtureGate)
        {
            int checks = 0;
            void Check(bool value, string reason)
            {
                if (!value) throw new Exception("SuperX UMA: " + reason);
                checks++;
            }
            Exception Reject(Action action, string reason)
            {
                Exception? observed = null;
                try { action(); } catch (Exception error) { observed = error; }
                Check(observed is not null, reason);
                return observed!;
            }
            Scenario Start() { var scenario = new Scenario(); active = scenario; return scenario; }
            void Closed(Scenario scenario)
            {
                foreach (var resource in scenario.Resources)
                {
                    Check(resource.Successes == (resource.Acquired ? 1 : 0), "not returned exactly once: " + resource.Label);
                    Check(resource.InterfaceCalls == 0, "inherited interface bypassed hidden Dispose: " + resource.Label);
                }
                if (scenario.InterfaceOptions is { } fallback)
                    Check(fallback.DisposeCalls == 1, "explicit IDisposable fallback not called exactly once");
            }
            void NoRetry(Scenario scenario, SuperXUmaChargeReader reader)
            {
                int queries = scenario.Queries, calls = scenario.Invocations;
                int parameters = scenario.ParameterCalls, allocations = scenario.Resources.Count;
                Reject(() => reader.ReadLimit(), "failed/debt reader admitted another read");
                Check(scenario.Queries == queries && scenario.Invocations == calls &&
                    scenario.ParameterCalls == parameters && scenario.Resources.Count == allocations,
                    "failed/debt reader reinitialized or acquired another object");
            }
            try
            {
                var dormant = Start();
                var unused = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                Check(dormant.Resources.Count == 0 && dormant.Queries == 0, "constructor opened WMI");
                unused.Dispose(); unused.Dispose();
                Reject(() => unused.ReadLimit(), "disposed dormant reader restarted");
                Check(dormant.Resources.Count == 0, "dormant stop initialized resources");

                foreach (var data in new object[] { (byte)80, (ushort)75, 75, (uint)100, (byte)0, (ushort)0, 255, (ushort)255, (uint)255 })
                {
                    var scenario = Start();
                    scenario.SchemaDataType = scenario.RuntimeDataType = data switch
                    {
                        byte _ => Kind.UInt8, ushort _ => Kind.UInt16, int _ => Kind.SInt32, _ => Kind.UInt32
                    };
                    var selected = new Endpoint();
                    var ignored = new Endpoint("ignored", "setumawmi");
                    scenario.Rows = new object?[] { ignored, selected }; scenario.Data = data;
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    Check(scenario.Queries == 0, "constructor queried injected transport");
                    byte expected = data switch { byte value => value, ushort value => (byte)value, int value => (byte)value, uint value => (byte)value, _ => 0 };
                    Check(reader.ReadLimit() == expected && reader.ReadLimit() == expected, "checked scalar read failed");
                    Check(scenario.Queries == 1 && scenario.Invocations == 2, "reader replaced captured endpoint");
                    Check(scenario.LastIndex is int index && index == 0x04A3, "Index was not boxed int raw 04A3");
                    Check(scenario.MethodNames.All(name => name == "GetEcValue") && scenario.InputWrites == 2, "unexpected setter/method/write");
                    Check(scenario.QueryScope == "root\\WMI" && scenario.QueryText == "SELECT * FROM UMAInterface", "query drifted from source");
                    Check(scenario.QueryOptions?.Timeout == TimeSpan.FromSeconds(3) &&
                        scenario.InvokeOptions.All(options => options.Timeout == TimeSpan.FromSeconds(3)), "timeouts were not three seconds");
                    Check(ReferenceEquals(scenario.MetadataScope, selected.ScopeToken) &&
                        ReferenceEquals(scenario.MetadataPath, selected.ClassPathToken), "metadata did not use captured scope/class path");
                    Check(ignored.Successes == 1 && selected.Successes == 0, "initialization did not retain selected endpoint");
                    reader.Dispose(); reader.Dispose(); Closed(scenario);
                    Check(scenario.Resources.All(resource => resource.Attempts == (resource.Acquired ? 1 : 0)), "successful cleanup replayed");
                }
                foreach (var mode in new[] { "zero", "null-first", "null-after", "multiple", "duplicate", "equal-distinct", "ordinal", "non-string" })
                {
                    var scenario = Start(); Endpoint first = mode == "equal-distinct" ? new EqualEndpoint() : new Endpoint();
                    Endpoint second = mode == "equal-distinct" ? new EqualEndpoint("second") : new Endpoint("second");
                    var untouched = new Endpoint("untouched");
                    scenario.Rows = mode switch
                    {
                        "zero" => Array.Empty<object?>(), "null-first" => new object?[] { null, untouched },
                        "null-after" => new object?[] { first, null, untouched }, "multiple" or "equal-distinct" => new object?[] { first, second, untouched },
                        "duplicate" => new object?[] { first, first, untouched }, _ => new object?[] { first }
                    };
                    if (mode == "ordinal") first.Name = "setumawmi";
                    if (mode == "non-string") first.Name = 123;
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    Reject(() => reader.ReadLimit(), "invalid endpoint set accepted: " + mode);
                    Check(scenario.Invocations == 0 && scenario.ParameterCalls == 0, "invalid endpoint set invoked: " + mode);
                    if (mode is "multiple" or "duplicate" or "equal-distinct" or "null-after") Check(scenario.Yields == 2, "enumeration did not stop early");
                    if (mode == "null-first") Check(scenario.Yields == 1, "enumeration continued after null");
                    Check(untouched.Attempts == 0, "unacquired endpoint disposed");
                    NoRetry(scenario, reader); reader.Dispose(); Closed(scenario);
                }
                foreach (var kind in new[] { Kind.UInt16, Kind.SInt32, Kind.UInt32 })
                {
                    var scenario = Start(); scenario.SchemaIndexType = scenario.RuntimeIndexType = kind;
                    scenario.Rows = new object?[] { new Endpoint() };
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    Check(reader.ReadLimit() == 80 && scenario.LastIndex is int, "VT_I4 compatible schema rejected");
                    reader.Dispose(); Closed(scenario);
                }
                foreach (var kind in new[] { Kind.UInt8, Kind.SInt8, Kind.UInt16, Kind.SInt16, Kind.UInt32, Kind.SInt32, Kind.UInt64, Kind.SInt64 })
                {
                    var scenario = Start(); scenario.SchemaDataType = scenario.RuntimeDataType = kind;
                    // Model the fixed API's managed scalar projection, including
                    // VT_I4 -> UInt16, VT_I2 -> SByte and VT_BSTR -> Int64/UInt64.
                    scenario.Data = kind switch
                    {
                        Kind.UInt8 => (object)(byte)80, Kind.SInt8 => (sbyte)80,
                        Kind.UInt16 => (ushort)80, Kind.SInt16 => (short)80,
                        Kind.UInt32 => (uint)80, Kind.SInt32 => 80,
                        Kind.UInt64 => (ulong)80, _ => (long)80
                    };
                    scenario.Rows = new object?[] { new Endpoint() };
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    if (kind is Kind.UInt8 or Kind.UInt16 or Kind.UInt32 or Kind.SInt32)
                        Check(reader.ReadLimit() == 80, "source-compatible managed Data projection rejected");
                    else
                    {
                        Reject(() => reader.ReadLimit(), "unsupported managed Data projection accepted");
                        Check(scenario.Invocations == 1, "unsupported Data projection caused extra dispatch");
                        NoRetry(scenario, reader);
                    }
                    reader.Dispose(); Closed(scenario);
                }
                foreach (var mode in new[] { "index-8", "index-16", "index-64", "index-string", "index-null", "index-array", "index-shape",
                    "data-bool", "data-real", "data-string", "data-null", "data-array", "missing-index", "missing-data", "missing-method",
                    "null-input", "null-output", "path-empty", "path-white", "path-null", "path-object", "ambiguous-invoke", "missing-invoke", "object-options", "void-invoke" })
                {
                    var scenario = Start();
                    EndpointBase selected = mode switch
                    {
                        "ambiguous-invoke" => new AmbiguousEndpoint(), "missing-invoke" => new ArrayEndpoint(),
                        "object-options" => new ObjectOptionsEndpoint(), "void-invoke" => new VoidEndpoint(), _ => new Endpoint()
                    };
                    scenario.Rows = new object?[] { selected };
                    switch (mode)
                    {
                        case "index-8": scenario.SchemaIndexType = Kind.UInt8; break;
                        case "index-16": scenario.SchemaIndexType = Kind.SInt16; break;
                        case "index-64": scenario.SchemaIndexType = Kind.UInt64; break;
                        case "index-string": scenario.SchemaIndexType = "UInt32"; break;
                        case "index-null": scenario.SchemaIndexType = null; break;
                        case "index-array": scenario.IndexArray = true; break;
                        case "index-shape": scenario.IndexArray = null; break;
                        case "data-bool": scenario.SchemaDataType = Kind.Boolean; break;
                        case "data-real": scenario.SchemaDataType = Kind.Real32; break;
                        case "data-string": scenario.SchemaDataType = "UInt8"; break;
                        case "data-null": scenario.SchemaDataType = null; break;
                        case "data-array": scenario.DataArray = true; break;
                        case "missing-index": scenario.MissingSchemaIndex = true; break;
                        case "missing-data": scenario.MissingSchemaData = true; break;
                        case "missing-method": scenario.MissingMethod = true; break;
                        case "null-input": scenario.NullSchemaInput = true; break;
                        case "null-output": scenario.NullSchemaOutput = true; break;
                        case "path-empty": selected.Path = ""; break;
                        case "path-white": selected.Path = "  "; break;
                        case "path-null": selected.Path = null; break;
                        case "path-object": selected.Path = 23; break;
                    }
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    Reject(() => reader.ReadLimit(), "bad schema/path/invoker accepted: " + mode);
                    Check(scenario.Invocations == 0, "bad schema/path/invoker dispatched: " + mode);
                    NoRetry(scenario, reader); reader.Dispose(); Closed(scenario);
                }
                foreach (var mode in new[] { "after-read", "during-parameters", "during-index", "case-only" })
                {
                    var scenario = Start(); var selected = new Endpoint(); scenario.Rows = new object?[] { selected };
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    if (mode == "after-read") { Check(reader.ReadLimit() == 80, "path setup failed"); selected.Path = "other-endpoint"; }
                    else if (mode == "during-parameters") scenario.OnParameters = () => selected.Path = "other-endpoint";
                    else scenario.OnIndex = () => selected.Path = mode == "case-only" ? "EXACT-PATH" : null;
                    int before = scenario.Invocations;
                    Reject(() => reader.ReadLimit(), "changed path accepted: " + mode);
                    Check(scenario.Invocations == before, "path changed between preparation and dispatch");
                    NoRetry(scenario, reader); reader.Dispose(); Closed(scenario);
                }
                foreach (var data in new object?[] { null, true, false, "80", (short)80, (ushort)80, (sbyte)80, (long)80,
                    (ulong)80, 80.0, 80m, new byte[] { 80 }, -1, 256, (uint)256, uint.MaxValue, new ConvertibleData() })
                {
                    var scenario = Start(); scenario.Data = data; scenario.Rows = new object?[] { new Endpoint() };
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    Reject(() => reader.ReadLimit(), "coerced/out-of-domain Data accepted");
                    Check(scenario.Invocations == 1 && scenario.Coercions == 0, "bad Data caused coercion/extra call/fallback");
                    NoRetry(scenario, reader); reader.Dispose(); Closed(scenario);
                }
                foreach (var data in new[] { (ushort)256, ushort.MaxValue })
                {
                    var scenario = Start(); scenario.SchemaDataType = scenario.RuntimeDataType = Kind.UInt16;
                    scenario.Data = data; scenario.Rows = new object?[] { new Endpoint() };
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    Reject(() => reader.ReadLimit(), "out-of-byte UInt16 projection accepted");
                    Check(scenario.Invocations == 1 && scenario.Coercions == 0, "UInt16 rejection coerced or replayed");
                    NoRetry(scenario, reader); reader.Dispose(); Closed(scenario);
                }
                foreach (var mode in new[] { "query-missing", "query-readonly", "query-wrong-type", "query-ignore", "query-throw",
                    "invoke-missing", "invoke-readonly", "invoke-ignore", "invoke-throw", "runtime-index", "runtime-array", "runtime-data", "null-result", "null-parameters", "invoke-failure" })
                {
                    var scenario = Start();
                    EndpointBase selected = mode switch
                    {
                        "invoke-missing" => new MissingOptionsEndpoint(), "invoke-readonly" => new ReadOnlyOptionsEndpoint(), _ => new Endpoint()
                    };
                    scenario.Rows = new object?[] { selected }; scenario.OptionsMode = mode;
                    if (mode == "runtime-index") scenario.RuntimeIndexType = Kind.SInt16;
                    if (mode == "runtime-array") scenario.RuntimeIndexArray = true;
                    if (mode == "runtime-data") scenario.RuntimeDataType = Kind.String;
                    if (mode == "null-result") scenario.NullResult = true;
                    if (mode == "null-parameters") scenario.NullParameters = true;
                    if (mode == "invoke-failure") scenario.FailAt = "invoke";
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    var error = Reject(() => reader.ReadLimit(), "failed timeout/runtime contract accepted: " + mode);
                    int expected = mode is "runtime-data" or "null-result" or "invoke-failure" ? 1 : 0;
                    Check(scenario.Invocations == expected, "timeout/runtime failure dispatched/fell back: " + mode);
                    if (mode == "invoke-failure") Check(error is IOException && error.Message.Contains("invoke", StringComparison.Ordinal), "invoke failure lost");
                    NoRetry(scenario, reader); reader.Dispose(); Closed(scenario);
                }
                foreach (var stage in new[] { "options", "get", "enumerator", "move-first", "current", "move-end", "name", "path", "scope", "class-path", "class", "methods", "schema-output", "parameters", "null-collection", "null-enumerator" })
                {
                    var scenario = Start(); scenario.FailAt = stage; scenario.Rows = new object?[] { new Endpoint() };
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    Reject(() => reader.ReadLimit(), "partial failure accepted: " + stage);
                    Check(scenario.Invocations == 0, "partial failure dispatched a read: " + stage);
                    NoRetry(scenario, reader); reader.Dispose(); Closed(scenario);
                }
                // Operation and cleanup errors coexist; retries preserve exact identity.
                {
                    var scenario = Start(); scenario.FailAt = "move-end";
                    var endpoint = new Endpoint(); scenario.Rows = new object?[] { endpoint };
                    scenario.ConfigureResource = resource => { if (resource.Label is "query" or "enumerator") resource.Failures = 2; };
                    endpoint.Failures = 2;
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    var error = Reject(() => reader.ReadLimit(), "partial enum cleanup debt accepted");
                    Check(error is AggregateException && error.ToString().Contains("move-end", StringComparison.Ordinal) &&
                        error.ToString().Contains("cleanup:", StringComparison.Ordinal), "operation/cleanup history lost");
                    Check(endpoint.Attempts == 1 && endpoint.Successes == 0, "failed exact endpoint not retained");
                    NoRetry(scenario, reader); Reject(reader.Dispose, "first stop did not report debt");
                    Check(endpoint.Attempts == 2, "stop did not retry exact endpoint");
                    NoRetry(scenario, reader); reader.Dispose(); reader.Dispose(); Closed(scenario);
                    Check(endpoint.Attempts == 3 && endpoint.Successes == 1, "repeated stop failed to close exact debt");
                }
                {
                    var scenario = Start(); var ignored = new Endpoint("nonmatch", "OTHER"); var endpoint = new Endpoint();
                    ignored.Failures = 2; scenario.Rows = new object?[] { ignored, endpoint };
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    Reject(() => reader.ReadLimit(), "init cleanup debt admitted invocation");
                    Check(ignored.Attempts == 1 && scenario.Invocations == 0, "init retried/ignored cleanup failure");
                    NoRetry(scenario, reader); Reject(reader.Dispose, "stop did not retain nonmatching debt");
                    Check(endpoint.Successes == 1 && ignored.Successes == 0, "stop dropped an exact owned object");
                    reader.Dispose(); reader.Dispose(); Closed(scenario);
                }
                foreach (var label in new[] { "query-options", "collection", "enumerator", "scope", "class-path", "class", "schema-input", "schema-output", "input", "output", "invoke-options", "endpoint" })
                {
                    var scenario = Start(); var endpoint = new Endpoint(); scenario.Rows = new object?[] { endpoint };
                    scenario.ConfigureResource = resource => { if (resource.Label == label) resource.Failures = 2; };
                    if (label == "endpoint") endpoint.Failures = 2;
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    if (label == "endpoint")
                    {
                        Check(reader.ReadLimit() == 80, "endpoint stop-debt setup failed");
                        Reject(reader.Dispose, "failed endpoint stop accepted");
                    }
                    else Reject(() => reader.ReadLimit(), "cleanup failure swallowed: " + label);
                    var retained = scenario.Resources.Single(resource => resource.Label == label);
                    Check(retained.Attempts == 1 && retained.Successes == 0, "cleanup retried early/dropped: " + label);
                    NoRetry(scenario, reader); Reject(reader.Dispose, "repeated cleanup failure swallowed: " + label);
                    reader.Dispose(); reader.Dispose(); Closed(scenario);
                    Check(retained.Attempts == 3, "retained object not retried after stopped: " + label);
                    Check(scenario.Resources.Where(resource => !ReferenceEquals(resource, retained)).All(resource =>
                        resource.Attempts == (resource.Acquired ? 1 : 0)), "successful objects disposed again during retry");
                }
                {
                    var scenario = Start(); scenario.FailAt = "invoke";
                    scenario.ConfigureResource = resource => { if (resource.Label == "input") resource.Failures = 1; };
                    scenario.Rows = new object?[] { new Endpoint() };
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    var error = Reject(() => reader.ReadLimit(), "invoke plus cleanup failure accepted");
                    Check(error is AggregateException && error.ToString().Contains("invoke", StringComparison.Ordinal) &&
                        error.ToString().Contains("cleanup:input", StringComparison.Ordinal), "read/cleanup history lost");
                    NoRetry(scenario, reader); reader.Dispose(); reader.Dispose(); Closed(scenario);
                }
                {
                    var scenario = Start(); scenario.OptionsMode = "interface-only"; scenario.Rows = new object?[] { new Endpoint() };
                    var reader = new SuperXUmaChargeReader(typeof(Searcher), typeof(MethodClass));
                    Check(reader.ReadLimit() == 80, "explicit IDisposable fallback setup failed");
                    reader.Dispose(); reader.Dispose(); Closed(scenario);
                }
                {
                    var scenario = Start(); var control = new Endpoint(); ((IDisposable)control).Dispose();
                    Check(control.InterfaceCalls == 1 && control.Successes == 0, "fake does not reproduce inherited interface dispatch");
                    control.Acquired = true; control.Dispose(); Check(control.Successes == 1, "hidden Dispose control failed");
                    Check(scenario.Queries == 0, "control fake queried WMI");
                }
                {
                    // Real task/event ordering through the actual reader, provider,
                    // Controller and InProcessRuntime. Timeout is pending ownership,
                    // never proof that synchronous WMI stopped or returned resources.
                    var scenario=Start();scenario.Rows=new object?[]{new Endpoint()};
                    using var entered=new ManualResetEventSlim(false);
                    using var release=new ManualResetEventSlim(false);
                    scenario.OnInvoke=()=>{entered.Set();if(!release.Wait(TimeSpan.FromSeconds(3)))throw new IOException("fixture:blocked-invoke-release-missing");};
                    string root=Environment.GetEnvironmentVariable("YMCC_SUPERX_FIXTURE_ROOT")??throw new IOException("fresh fixture root required");
                    var reader=new SuperXUmaChargeReader(typeof(Searcher),typeof(MethodClass));
                    var writer=new NeverWriter();var owner=new RuntimeToken();var timer=new RuntimeToken();var events=new RuntimeToken();
                    int owners=0,factories=0;
                    var identity=new Identity("ONE-NETBOOK","ONEXPLAYER SUPER X","untrusted","untrusted","",0,0);
                    var runtime=new InProcessRuntime(createDomains:(_,_,ready)=>new HostDomainRouter(
                        ()=>new Controller(identity,new MemoryStore(),_=>{factories++;return new SuperXChargeProvider(reader,writer);},()=> (true,true,80),ready),
                        _=>throw new Exception("display must stay dormant"),ready),
                        schedule:(_,_)=>timer,subscribe:(_,_)=>events,
                        shutdownTimeout:TimeSpan.FromMilliseconds(100),acquireOwner:()=>{owners++;return owner;});
                    JsonElement Call(string op)
                    {
                        object request=new {op,protocol=RuntimeContract.Protocol,sourceId=RuntimeContract.SourceId};
                        object envelope=op=="shutdown"?new {request}:new {powerRoot=Path.Combine(root,"UmaPower"),stateRoot=Path.Combine(root,"UmaState"),request};
                        using var doc=JsonDocument.Parse(runtime.Invoke(JsonSerializer.SerializeToUtf8Bytes(envelope,Policy.Json)));
                        return doc.RootElement.Clone();
                    }
                    var operation=Task.Run(()=>Call("get"));
                    try
                    {
                        Check(entered.Wait(TimeSpan.FromSeconds(3)),"actual reader never entered blocked invocation");
                        var pending=Call("shutdown");
                        Check(pending.GetProperty("cleanupPending").GetBoolean()&&!pending.GetProperty("cleanupCompleted").GetBoolean()&&owner.Disposes==0,
                            "WMI option timeout was treated as completed cleanup");
                        Check(Call("get").TryGetProperty("error",out _)&&factories==1&&owners==1&&scenario.Invocations==1,
                            "stopped WMI operation allowed another provider/owner/invocation");
                    }
                    finally{release.Set();}
                    Check(operation.Wait(TimeSpan.FromSeconds(3))&&operation.Result.TryGetProperty("error",out _),"late actual WMI read published stopped-generation success");
                    var closed=Call("shutdown");
                    Check(closed.GetProperty("ok").GetBoolean()&&owner.Disposes==1&&timer.Disposes==1&&events.Disposes==1,
                        "actual reader drain did not release exact remaining generation");
                    Check(writer.Writes==0&&scenario.Invocations==1,"blocked read/shutdown replayed or invented a write");
                    Closed(scenario);
                }
                Console.WriteLine($"Super X UMA reader assertions: {checks}");
                return checks;
            }
            finally { active = null; }
        }
    }

    private sealed class RuntimeToken : IDisposable
    {
        internal int Disposes;
        public void Dispose(){Disposes++;}
    }
    private sealed class NeverWriter : ISuperXChargeWriter
    {
        internal int Writes;
        public void WriteLimit(byte value){Writes++;throw new IOException("read-only fixture must not write");}
        public void Dispose(){}
    }
    private sealed class Scenario
    {
        internal List<Resource> Resources { get; } = new();
        internal List<string> MethodNames { get; } = new();
        internal List<InvokeOptions> InvokeOptions { get; } = new();
        internal object?[] Rows { get; set; } = Array.Empty<object?>();
        internal object? Data { get; set; } = (byte)80;
        internal object? LastIndex { get; set; }
        internal object? SchemaIndexType { get; set; } = Kind.UInt32;
        internal object? SchemaDataType { get; set; } = Kind.UInt8;
        internal object? RuntimeIndexType { get; set; } = Kind.UInt32;
        internal object? RuntimeDataType { get; set; } = Kind.UInt8;
        internal object? IndexArray { get; set; } = false;
        internal object? DataArray { get; set; } = false;
        internal bool RuntimeIndexArray { get; set; }
        internal bool MissingSchemaIndex { get; set; }
        internal bool MissingSchemaData { get; set; }
        internal bool MissingMethod { get; set; }
        internal bool NullSchemaInput { get; set; }
        internal bool NullSchemaOutput { get; set; }
        internal bool NullParameters { get; set; }
        internal bool NullResult { get; set; }
        internal int Queries { get; set; }
        internal int Yields { get; set; }
        internal int Invocations { get; set; }
        internal int ParameterCalls { get; set; }
        internal int InputWrites { get; set; }
        internal int Coercions { get; set; }
        internal string? QueryScope { get; set; }
        internal string? QueryText { get; set; }
        internal string? FailAt { get; set; }
        internal string? OptionsMode { get; set; }
        internal QueryOptions? QueryOptions { get; set; }
        internal InterfaceOptions? InterfaceOptions { get; set; }
        internal object? MetadataScope { get; set; }
        internal object? MetadataPath { get; set; }
        internal Action? OnParameters { get; set; }
        internal Action? OnInvoke { get; set; }
        internal Action? OnIndex { get; set; }
        internal Action<Resource>? ConfigureResource { get; set; }
        internal void Fail(string stage) { if (FailAt == stage) throw new IOException("fixture:" + stage); }
    }

    public enum Kind { UInt8 = 17, SInt8 = 16, UInt16 = 18, SInt16 = 2, UInt32 = 19, SInt32 = 3, UInt64 = 21, SInt64 = 20, Boolean = 11, Real32 = 4, String = 8 }
    public enum BagRole { SchemaInput, SchemaOutput, Input, Output }

    public class ComponentLike : IDisposable
    {
        public int InterfaceCalls { get; private set; }
        public void Dispose() => InterfaceCalls++;
    }
    // Model the public new Dispose which System.Management hides from IDisposable.
    public class Resource : ComponentLike
    {
        internal string Label { get; }
        internal bool Acquired { get; set; } = true;
        internal int Attempts { get; private set; }
        internal int Successes { get; private set; }
        internal int Failures { get; set; }
        internal Resource(string label) { Label = label; Current.Resources.Add(this); Current.ConfigureResource?.Invoke(this); }
        public new void Dispose()
        {
            Attempts++;
            if (Failures > 0) { Failures--; throw new IOException("cleanup:" + Label); }
            if (Successes != 0) throw new IOException("double-dispose:" + Label);
            Successes++;
        }
    }
    public sealed class Token : Resource { internal Token(string label) : base(label) { } }
    public sealed class QueryOptions : Resource
    {
        private TimeSpan timeout;
        public QueryOptions() : base("query-options") { Current.QueryOptions = this; }
        public TimeSpan Timeout
        {
            get => timeout;
            set
            {
                if (Current.OptionsMode == "query-throw") throw new IOException("fixture:query-timeout");
                if (Current.OptionsMode != "query-ignore") timeout = value;
            }
        }
    }
    public sealed class InvokeOptions : Resource
    {
        private TimeSpan timeout;
        public InvokeOptions() : base("invoke-options") { Current.InvokeOptions.Add(this); }
        public TimeSpan Timeout
        {
            get => timeout;
            set
            {
                if (Current.OptionsMode == "invoke-throw") throw new IOException("fixture:invoke-timeout");
                if (Current.OptionsMode != "invoke-ignore") timeout = value;
            }
        }
    }
    public sealed class AlternateOptions { public TimeSpan Timeout { get; set; } }
    public sealed class MissingTimeoutOptions : Resource { public MissingTimeoutOptions() : base("missing-timeout") { } }
    public sealed class ReadOnlyTimeoutOptions : Resource
    {
        public ReadOnlyTimeoutOptions() : base("readonly-timeout") { }
        public TimeSpan Timeout => TimeSpan.Zero;
    }
    public sealed class WrongTimeoutOptions : Resource
    {
        public WrongTimeoutOptions() : base("wrong-timeout") { }
        public string Timeout { get; set; } = "";
    }
    public sealed class InterfaceOptions : IDisposable
    {
        public TimeSpan Timeout { get; set; }
        internal int DisposeCalls { get; private set; }
        void IDisposable.Dispose() => DisposeCalls++;
    }
    public sealed class Searcher : Resource
    {
        public Searcher(string scope, string query) : base("query") { Current.QueryScope = scope; Current.QueryText = query; }
        public object Options
        {
            get
            {
                Current.Fail("options");
                if (Current.OptionsMode == "interface-only") return Current.InterfaceOptions = new InterfaceOptions();
                return Current.OptionsMode switch
                {
                    "query-missing" => new MissingTimeoutOptions(), "query-readonly" => new ReadOnlyTimeoutOptions(),
                    "query-wrong-type" => new WrongTimeoutOptions(), _ => new QueryOptions()
                };
            }
        }
        public Collection? Get()
        {
            Current.Queries++; Current.Fail("get");
            return Current.FailAt == "null-collection" ? null : new Collection();
        }
    }
    public sealed class Collection : Resource, IEnumerable
    {
        public Collection() : base("collection") { }
        public IEnumerator GetEnumerator()
        {
            Current.Fail("enumerator");
            return Current.FailAt == "null-enumerator" ? null! : new Enumerator();
        }
    }
    public sealed class Enumerator : Resource, IEnumerator
    {
        private int position = -1;
        public Enumerator() : base("enumerator") { }
        public bool MoveNext()
        {
            var scenario = SuperXUmaChargeReaderFixture.Current;
            if (position == -1) scenario.Fail("move-first");
            position++;
            if (position >= scenario.Rows.Length) { scenario.Fail("move-end"); return false; }
            return true;
        }
        public object Current
        {
            get
            {
                var scenario = SuperXUmaChargeReaderFixture.Current;
                scenario.Fail("current");
                var row = scenario.Rows[position]; scenario.Yields++;
                if (row is Resource resource) resource.Acquired = true;
                return row!;
            }
        }
        public void Reset() => throw new NotSupportedException();
    }
    public class EndpointBase : Resource
    {
        private Token? scope, classPath;
        internal object? Name { get; set; }
        internal object? Path { get; set; } = "exact-path";
        internal Token? ScopeToken => scope;
        internal Token? ClassPathToken => classPath;
        internal EndpointBase(string label = "endpoint", object? name = null) : base(label)
        {
            Acquired = false; Name = name ?? "prefix-SETUMAWMI-suffix";
        }
        public object? this[string name]
        {
            get
            {
                if (name == "InstanceName") { Current.Fail("name"); return Name; }
                if (name == "__PATH") { Current.Fail("path"); return Path; }
                throw new IOException("fixture:unknown-endpoint-property");
            }
        }
        public object Scope { get { Current.Fail("scope"); return scope ??= new Token("scope"); } }
        public object ClassPath { get { Current.Fail("class-path"); return classPath ??= new Token("class-path"); } }
        public Bag? GetMethodParameters(string method)
        {
            Current.ParameterCalls++; Current.MethodNames.Add(method); Current.Fail("parameters");
            if (method != "GetEcValue") throw new IOException("fixture:unexpected-method");
            Current.OnParameters?.Invoke();
            return Current.NullParameters ? null : new Bag(BagRole.Input);
        }
        protected static Bag? Dispatch(string method, Bag input, object options)
        {
            Current.Invocations++; Current.MethodNames.Add(method);
            if (method != "GetEcValue" || input["Index"] is not int index || index != 0x04A3 ||
                options is not InvokeOptions { Timeout: var timeout } || timeout != TimeSpan.FromSeconds(3))
                throw new IOException("fixture:wrong-invocation");
            Current.OnInvoke?.Invoke();
            Current.Fail("invoke");
            return Current.NullResult ? null : new Bag(BagRole.Output);
        }
    }
    public class Endpoint : EndpointBase
    {
        public Endpoint(string label = "endpoint", object? name = null) : base(label, name) { }
        public Bag? InvokeMethod(string method, Bag input, InvokeOptions options) => Dispatch(method, input, options);
    }
    public sealed class EqualEndpoint : Endpoint
    {
        public EqualEndpoint(string label = "endpoint") : base(label) { }
        public override bool Equals(object? other) => other is EqualEndpoint;
        public override int GetHashCode() => 0;
    }
    public sealed class AmbiguousEndpoint : Endpoint
    {
        public Bag? InvokeMethod(string method, Bag input, AlternateOptions options) => Dispatch(method, input, options);
    }
    public sealed class MissingOptionsEndpoint : EndpointBase
    {
        public Bag? InvokeMethod(string method, Bag input, MissingTimeoutOptions options) => Dispatch(method, input, options);
    }
    public sealed class ReadOnlyOptionsEndpoint : EndpointBase
    {
        public Bag? InvokeMethod(string method, Bag input, ReadOnlyTimeoutOptions options) => Dispatch(method, input, options);
    }
    public sealed class ArrayEndpoint : EndpointBase
    {
        public Bag? InvokeMethod(string method, object[] input, InvokeOptions options) => throw new IOException("fixture:array-invoker");
    }
    public sealed class ObjectOptionsEndpoint : EndpointBase
    {
        public Bag? InvokeMethod(string method, Bag input, object options) => Dispatch(method, input, options);
    }
    public sealed class VoidEndpoint : EndpointBase
    {
        public void InvokeMethod(string method, Bag input, InvokeOptions options) => throw new IOException("fixture:void-invoker");
    }
    public sealed class MethodClass : Resource
    {
        public MethodClass(object scope, object path, object? options) : base("class")
        {
            Current.MetadataScope = scope; Current.MetadataPath = path;
            if (options is not null) throw new IOException("fixture:class-options");
            // A throwing constructor never transfers its instance to the caller.
            if (Current.FailAt == "class") { Acquired = false; Current.Fail("class"); }
        }
        public MethodSet Methods { get { Current.Fail("methods"); return new MethodSet(); } }
    }
    public sealed class MethodSet : Resource
    {
        public MethodSet() : base("methods") { }
        public Method? this[string name] => name == "GetEcValue" && !Current.MissingMethod ? new Method() : null;
    }
    public sealed class Method : Resource
    {
        public Method() : base("method") { }
        public Bag? InParameters => Current.NullSchemaInput ? null : new Bag(BagRole.SchemaInput);
        public Bag? OutParameters
        {
            get { Current.Fail("schema-output"); return Current.NullSchemaOutput ? null : new Bag(BagRole.SchemaOutput); }
        }
    }
    public sealed class Bag : Resource
    {
        private readonly BagRole role;
        private object? index;
        internal BagRole Role => role;
        public Bag(BagRole role) : base(role switch
        {
            BagRole.SchemaInput => "schema-input", BagRole.SchemaOutput => "schema-output", BagRole.Input => "input", _ => "output"
        }) { this.role = role; }
        public FieldSet Properties => new(this);
        public object? this[string name]
        {
            get => name switch { "Index" => index, "Data" => Current.Data, _ => throw new IOException("fixture:bag-key") };
            set
            {
                if (role != BagRole.Input || name != "Index") throw new IOException("fixture:unexpected-input-write");
                Current.InputWrites++; Current.LastIndex = index = value; Current.OnIndex?.Invoke();
            }
        }
    }
    public sealed class FieldSet : Resource
    {
        private readonly Bag bag;
        public FieldSet(Bag bag) : base("properties:" + bag.Role) { this.bag = bag; }
        public Field? this[string name]
        {
            get
            {
                bool index = bag.Role is BagRole.Input or BagRole.SchemaInput;
                if (name != (index ? "Index" : "Data") ||
                    bag.Role == BagRole.SchemaInput && Current.MissingSchemaIndex ||
                    bag.Role == BagRole.SchemaOutput && Current.MissingSchemaData) return null;
                return new Field(bag.Role);
            }
        }
    }
    public sealed class Field : Resource
    {
        private readonly BagRole role;
        public Field(BagRole role) : base("field:" + role) { this.role = role; }
        public object? IsArray => role switch
        {
            BagRole.SchemaInput => Current.IndexArray, BagRole.SchemaOutput => Current.DataArray,
            BagRole.Input => Current.RuntimeIndexArray, _ => false
        };
        public object? Type => role switch
        {
            BagRole.SchemaInput => Current.SchemaIndexType, BagRole.SchemaOutput => Current.SchemaDataType,
            BagRole.Input => Current.RuntimeIndexType, _ => Current.RuntimeDataType
        };
    }
    public sealed class ConvertibleData : IConvertible
    {
        private static Exception Refused() { Current.Coercions++; return new IOException("fixture:Data coercion attempted"); }
        public TypeCode GetTypeCode() => throw Refused();
        public bool ToBoolean(IFormatProvider? provider) => throw Refused();
        public byte ToByte(IFormatProvider? provider) => throw Refused();
        public char ToChar(IFormatProvider? provider) => throw Refused();
        public DateTime ToDateTime(IFormatProvider? provider) => throw Refused();
        public decimal ToDecimal(IFormatProvider? provider) => throw Refused();
        public double ToDouble(IFormatProvider? provider) => throw Refused();
        public short ToInt16(IFormatProvider? provider) => throw Refused();
        public int ToInt32(IFormatProvider? provider) => throw Refused();
        public long ToInt64(IFormatProvider? provider) => throw Refused();
        public sbyte ToSByte(IFormatProvider? provider) => throw Refused();
        public float ToSingle(IFormatProvider? provider) => throw Refused();
        public string ToString(IFormatProvider? provider) => throw Refused();
        public object ToType(Type conversionType, IFormatProvider? provider) => throw Refused();
        public ushort ToUInt16(IFormatProvider? provider) => throw Refused();
        public uint ToUInt32(IFormatProvider? provider) => throw Refused();
        public ulong ToUInt64(IFormatProvider? provider) => throw Refused();
    }
}
