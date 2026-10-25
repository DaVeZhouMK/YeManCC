using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Text.Json;
using System.Security.Cryptography;

internal static class RequestJsonChecks
{
    private static int checks;
    private static readonly List<string> failures = new();
    private static MethodInfo parser = null!;
    private static JsonElement Parse(string? raw) => (JsonElement)parser.Invoke(null, new object?[] { raw })!;
    private static void Check(string name, Action work)
    {
        checks++;
        try { work(); Console.WriteLine("PASS " + name); }
        catch (Exception ex) { failures.Add(name + ": " + ex.GetBaseException().Message); Console.WriteLine("FAIL " + name + ": " + ex.GetBaseException().Message); }
    }
    private static void Require(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
    private static readonly Dictionary<short, OpCode> opCodes = typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static)
        .Where(f => f.FieldType == typeof(OpCode)).Select(f => (OpCode)f.GetValue(null)!).ToDictionary(o => o.Value);
    private static List<MethodBase> Calls(MethodInfo method)
    {
        var result = new List<MethodBase>();
        var bytes = method.GetMethodBody()!.GetILAsByteArray()!;
        for (var pos = 0; pos < bytes.Length;)
        {
            short code = bytes[pos++];
            if (code == 0xfe) code = unchecked((short)(0xfe00 | bytes[pos++]));
            var op = opCodes[code];
            if (op.OperandType == OperandType.InlineMethod)
            {
                var token = BitConverter.ToInt32(bytes, pos);
                result.Add(method.Module.ResolveMethod(token, method.DeclaringType?.GetGenericArguments(), method.IsGenericMethod ? method.GetGenericArguments() : null)!);
            }
            pos += op.OperandType switch
            {
                OperandType.InlineNone => 0,
                OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar => 1,
                OperandType.InlineVar => 2,
                OperandType.InlineI8 or OperandType.InlineR => 8,
                OperandType.InlineSwitch => 4 + BitConverter.ToInt32(bytes, pos) * 4,
                _ => 4,
            };
        }
        return result;
    }
    private static string LegacyEnablePayload<TNodes>(TNodes nodes, string? leaseId, JsonSerializerOptions options) =>
        JsonSerializer.Serialize(new { nodes, leaseId }, options);
    private static void CheckEnablePayload(Assembly assembly)
    {
        const BindingFlags flags = BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance;
        var engine = assembly.GetType("YeManFanHost.FanHostEngine", true)!;
        var factory = engine.GetMethod("BuildEnableRequest", flags);
        var node = assembly.GetType("YeManFanHost.FanNode", true)!;
        var options = (JsonSerializerOptions)assembly.GetType("YeManFanHost.JsonDefaults", true)!.GetField("Options", flags)!.GetValue(null)!;
        var consumers = new[] { "Preset", "CompleteAutomaticResume" };
        foreach (var name in consumers) Check("typed enable body: actual " + name + " uses owned serialization without text parse", () =>
        {
            var calls = Calls(engine.GetMethod(name, flags)!);
            Require(factory is not null && calls.Any(m => m.DeclaringType == engine && m.Name == factory.Name), "production body factory not connected");
            Require(!calls.Any(m => m.DeclaringType == typeof(JsonDocument) && m.Name == "Parse"), "consumer still reparses intermediate text");
            Require(!calls.Any(m => m.DeclaringType == typeof(JsonSerializer) && m.Name == "Serialize"), "consumer still builds intermediate JSON string");
        });
        Check("typed enable body: factory serializes straight into an independent element", () =>
        {
            Require(factory is not null && factory.IsGenericMethodDefinition, "typed factory absent");
            var calls = Calls(factory!);
            Require(calls.Count(m => m.DeclaringType == typeof(JsonSerializer) && m.Name == "SerializeToElement") == 1, "direct element serialization absent");
            Require(!calls.Any(m => m.DeclaringType == typeof(JsonDocument) || m.DeclaringType == typeof(JsonSerializer) && m.Name == "Serialize"), "text/document roundtrip remains");
        });
        JsonElement Build(Array nodes, Type declared, string? lease)
        {
            if (factory is not null) return (JsonElement)factory.MakeGenericMethod(declared).Invoke(null, new object?[] { nodes, lease })!;
            var text = (string)typeof(RequestJsonChecks).GetMethod(nameof(LegacyEnablePayload), flags)!.MakeGenericMethod(declared).Invoke(null, new object?[] { nodes, lease, options })!;
            using var original = JsonDocument.Parse(text); return original.RootElement.Clone();
        }
        var declaredTypes = new[] { node.MakeArrayType(), typeof(IReadOnlyList<>).MakeGenericType(node) };
        foreach (var declared in declaredTypes) Check("typed enable body: original nodes/lease shape retained for " + declared, () =>
        {
            var sets = new[] { new (double T, double D)[] { }, new[] { (0d, 0d), (40.125, 35.75), (100d, 100d) }, new[] { (-0d, 1e-200), (99.999, 50.0001) } };
            foreach (var set in sets) foreach (var lease in new string?[] { null, "", "  Exact-LEASE  ", "风扇-\"\\-\u0000-\U0001F300" })
            {
                var nodes = Array.CreateInstance(node, set.Length);
                for (var i = 0; i < set.Length; i++) nodes.SetValue(Activator.CreateInstance(node, new object?[] { set[i].Item1, set[i].Item2 }), i);
                var expectedText = (string)typeof(RequestJsonChecks).GetMethod(nameof(LegacyEnablePayload), flags)!.MakeGenericMethod(declared).Invoke(null, new object?[] { nodes, lease, options })!;
                var actual = Build(nodes, declared, lease);
                Require(actual.GetRawText() == expectedText, "generated JSON, property order, number or lease changed");
                Require(actual.GetProperty("nodes").GetArrayLength() == set.Length, "nodes omitted");
                Require(lease is null ? !actual.TryGetProperty("leaseId", out _) : actual.GetProperty("leaseId").GetString() == lease, "lease normalized or dropped");
            }
        });
        Check("typed enable body: independent body survives input-array reuse", () =>
        {
            var nodes = Array.CreateInstance(node, 1); nodes.SetValue(Activator.CreateInstance(node, new object?[] { 0d, 37.5 }), 0);
            var first = Build(nodes, node.MakeArrayType(), "first");
            nodes.SetValue(Activator.CreateInstance(node, new object?[] { 0d, 88d }), 0);
            var second = Build(nodes, node.MakeArrayType(), "second");
            Require(first.GetProperty("nodes")[0].GetProperty("dutyPercent").GetDouble() == 37.5 && first.GetProperty("leaseId").GetString() == "first", "first body shares mutable curve or lease");
            Require(second.GetProperty("nodes")[0].GetProperty("dutyPercent").GetDouble() == 88 && second.GetProperty("leaseId").GetString() == "second", "later body lost current values");
        });
    }
    public static async Task<int> Main(string[] args)
    {
        if (args.Length != 1) throw new ArgumentException("one candidate DLL path is required");
        var candidate = Path.GetFullPath(args[0]);
        var assembly = Assembly.LoadFrom(candidate);
        var server = assembly.GetType("YeManFanHost.LocalFanHostServer", true)!;
        parser = server.GetMethod("ParseRequestJson", BindingFlags.NonPublic | BindingFlags.Static)!;
        Check("actual built Host exposes the production request parser", () => Require(parser is not null, "production request parser absent"));
        if (parser is null)
        {
            Console.WriteLine(JsonSerializer.Serialize(new { ok = false, checks, failures, hardwareCalls = 0 }));
            return 1;
        }
        Check("actual async HTTP request reader calls this production parser", () =>
        {
            var reader = server.GetMethod("ReadJsonAsync", BindingFlags.NonPublic | BindingFlags.Static)!;
            var machine = reader.GetCustomAttribute<System.Runtime.CompilerServices.AsyncStateMachineAttribute>()!.StateMachineType;
            var moveNext = machine.GetMethod("MoveNext", BindingFlags.NonPublic | BindingFlags.Instance)!;
            Require(Calls(moveNext).Any(m => m == parser || (m.Name == parser.Name && m.DeclaringType == server)), "parser disconnected from actual HTTP reader");
        });
        var requiredString = server.GetMethod("RequiredString", BindingFlags.NonPublic | BindingFlags.Static)!;
        var requiredStringGetStringCalls = Calls(requiredString)
            .Count(m => m.DeclaringType == typeof(JsonElement) && m.Name == nameof(JsonElement.GetString));
        Check("required heartbeat field decodes its JSON string only once", () =>
            Require(requiredStringGetStringCalls == 1, $"expected one GetString call site, found {requiredStringGetStringCalls}"));
        string Required(JsonElement body, string property = "leaseId") =>
            (string)requiredString.Invoke(null, new object?[] { body, property })!;
        Check("valid required strings preserve exact whitespace, escapes, Unicode and duplicate-key behavior", () =>
        {
            var values = new[] { "fixture-lease", "  fixture-lease  ", "x\ty", "x\r\ny", "x\"\\y", "风扇-fixture-\U0001F300", "\u00a0valid\u2003", "\0valid", new string('x', 4097) };
            foreach (var value in values)
            {
                using var document = JsonDocument.Parse(JsonSerializer.Serialize(new { leaseId = value }));
                Require(Required(document.RootElement) == value, "required string was normalized or altered");
            }
            using var repeated = JsonDocument.Parse("{\"leaseId\":\"old\",\"leaseId\":\"current\"}");
            Require(Required(repeated.RootElement) == "current", "last duplicate string was not selected");
        });
        Check("missing, non-string and Unicode-whitespace required fields keep INVALID_REQUEST 400", () =>
        {
            var fields = new[] { "null", "true", "12", "[]", "{}", "\"\"", "\" \\t\\r\\n\"", "\"\\u00a0\\u2003\"" };
            foreach (var raw in fields.Select(field => "{\"leaseId\":" + field + "}").Prepend("{}"))
            {
                using var document = JsonDocument.Parse(raw);
                try { _ = Required(document.RootElement); throw new Exception("invalid required field was accepted"); }
                catch (TargetInvocationException ex)
                {
                    var error = ex.InnerException!;
                    Require(error.GetType().FullName == "YeManFanHost.FanApiException", "rejection type changed");
                    Require(Equals(error.GetType().GetProperty("StatusCode")!.GetValue(error), 400) &&
                        Equals(error.GetType().GetProperty("Code")!.GetValue(error), "INVALID_REQUEST") &&
                        error.Message == "缺少 leaseId", "structured rejection shape changed");
                }
            }
        });
        Check("non-object roots retain the existing invalid-operation rejection", () =>
        {
            foreach (var raw in new[] { "[]", "null", "7", "\"text\"", "true" })
            {
                using var document = JsonDocument.Parse(raw);
                try { _ = Required(document.RootElement); throw new Exception("non-object body accepted"); }
                catch (TargetInvocationException ex) { Require(ex.InnerException is InvalidOperationException, "root-kind rejection changed"); }
            }
        });
        Check("required string stays request-local and remains readable after document disposal", () =>
        {
            string first;
            using (var document = JsonDocument.Parse("{\"leaseId\":\"request-A\"}")) first = Required(document.RootElement);
            using var other = JsonDocument.Parse("{\"leaseId\":\"request-B\",\"source\":\"caller\"}");
            Require(Required(other.RootElement) == "request-B" && first == "request-A", "previous request string changed");
            Require(Required(other.RootElement, "source") == "caller", "helper ignored its requested property");
        });

        var emptyField = server.GetField("EmptyRequestJson", BindingFlags.NonPublic | BindingFlags.Static)!;
        foreach (var raw in new string?[] { null, "", " ", "\r\n\t", "{}", " { } " })
            Check("empty/object shape " + JsonSerializer.Serialize(raw), () => Require(Parse(raw).ValueKind == JsonValueKind.Object && !Parse(raw).EnumerateObject().Any(), "empty body shape changed"));
        Check("empty owned element is readonly and reused without per-request parsing", () =>
        {
            Require(emptyField.IsInitOnly, "empty cache may be replaced");
            var empty = (JsonElement)emptyField.GetValue(null)!;
            Require(empty.GetRawText() == "{}", "empty wire shape changed");
            Require(Parse(null).Equals(empty) && Parse(" ").Equals(empty) && Parse("").Equals(empty), "empty parser creates another document");
        });
        var parserCalls = Calls(parser);
        Check("nonempty parse directly returns an owned element without caller document/clone", () =>
        {
            var body = parser.GetMethodBody()!;
            Require(!body.LocalVariables.Any(l => l.LocalType == typeof(JsonDocument)), "caller retains a disposable document");
            Require(parserCalls.Count(m => m.DeclaringType == typeof(JsonElement) && m.Name == "Parse") == 1, "one direct owned parse call absent");
            Require(!parserCalls.Any(m => m.DeclaringType == typeof(JsonElement) && m.Name == "Clone"), "caller still clones the parsed tree");
        });
        Check("returned nonempty JSON remains independently readable after parser returns", () =>
        {
            var value = Parse("{\"leaseId\":\"fixture-lease\",\"nodes\":[{\"tempC\":40,\"dutyPercent\":75}],\"nested\":{\"keep\":true}}");
            Require(value.GetProperty("leaseId").GetString() == "fixture-lease", "detached lease body unreadable");
            Require(value.GetProperty("nodes")[0].GetProperty("dutyPercent").GetInt32() == 75, "nested body unreadable");
            Require(value.GetProperty("nested").GetProperty("keep").GetBoolean(), "nested bool changed");
        });
        foreach (var raw in new[] { "{", "[1,", "{\"a\":}", "invalid", "{} trailing", "{\"v\":NaN}" })
            Check("malformed body still throws " + raw, () =>
            {
                try { _ = Parse(raw); throw new InvalidOperationException("malformed JSON was accepted"); }
                catch (TargetInvocationException ex) when (ex.InnerException is JsonException) { }
            });
        foreach (var raw in new[] { "null", "true", "123", "1.25", "\"text\"", "[1,{\"two\":2}]" })
            Check("existing root-kind behavior " + raw, () =>
            {
                using var expected = JsonDocument.Parse(raw);
                var actual = Parse(raw);
                Require(actual.ValueKind == expected.RootElement.ValueKind && actual.GetRawText() == expected.RootElement.GetRawText(), "JSON behavior changed");
            });
        Check("duplicate key resolution remains System.Text.Json behavior", () => Require(Parse("{\"value\":1,\"value\":2}").GetProperty("value").GetInt32() == 2, "duplicate behavior changed"));
        Check("request-specific data is neither cached nor overwritten by a later parse", () =>
        {
            var first = Parse("{\"leaseId\":\"one\",\"generation\":6}");
            var second = Parse("{\"leaseId\":\"two\",\"generation\":7}");
            _ = Parse(null);
            Require(first.GetProperty("leaseId").GetString() == "one" && second.GetProperty("leaseId").GetString() == "two", "requests share mutable state");
            Require(!first.Equals(second), "nonempty request cached");
            Require(Parse(null).EnumerateObject().Count() == 0, "empty cache carries earlier request");
        });
        Check("valid JSON corpus retains the original root, lexical values and property order", () =>
        {
            var corpus = new List<string> { "  {\"LeaseID\":\"UPPER-And-小字\",\"escaped\":\"\\u0041\\u4e2d\",\"n\":-0.0e+12}  ", "[true,false,null,1e-300,9223372036854775808]", "\"\\uD83C\\uDF00\"", "{\"dup\":1,\"dup\":2,\"nested\":[{\"case\":\"MiXeD\"}]}" };
            for (var i = 0; i < 40; i++) corpus.Add(JsonSerializer.Serialize(new { leaseId = "Case-" + i + "-风扇", generation = i, nodes = Enumerable.Range(0, i % 12).Select(n => new { tempC = 20 + n, dutyPercent = 25.25 + n }), text = new string('Z', i * 97), enabled = i % 2 == 0 }));
            foreach (var raw in corpus)
            {
                using var expected = JsonDocument.Parse(raw);
                var actual = Parse(raw);
                Require(actual.ValueKind == expected.RootElement.ValueKind && actual.GetRawText() == expected.RootElement.GetRawText(), "valid corpus changed root or raw text");
                Require(JsonSerializer.Serialize(actual) == JsonSerializer.Serialize(expected.RootElement), "valid corpus changed parsed data");
            }
        });
        Check("strict syntax and default depth retain original JSON exception coordinates", () =>
        {
            var corpus = new[] { "{\"v\":1,}", "[1,]", "/*comment*/{}", "{} //comment", "{}{}", "\uFEFF{}", "{\"v\":\"\\x00\"}", "{\"v\":01}", "{\"v\":1.}", "{\n \"v\":\n}", new string('[', 65) + "0" + new string(']', 65), new string('[', 64) + "0" + new string(']', 64) };
            foreach (var raw in corpus)
            {
                JsonException? expectedError = null;
                try { using var expected = JsonDocument.Parse(raw); }
                catch (JsonException error) { expectedError = error; }
                try { _ = Parse(raw); Require(expectedError is null, "invalid JSON accepted"); }
                catch (TargetInvocationException error) when (error.InnerException is JsonException actual)
                {
                    Require(expectedError is not null, "valid JSON rejected");
                    Require(actual.LineNumber == expectedError!.LineNumber && actual.BytePositionInLine == expectedError.BytePositionInLine && actual.Path == expectedError.Path, "error coordinates changed");
                }
            }
        });
        Check("old owned element survives parser churn and garbage collection", () =>
        {
            var first = Parse("{\"leaseId\":\"Keep-Mixed-Case\",\"nested\":[{\"text\":\"独立\"}]}");
            for (var i = 0; i < 96; i++) _ = Parse(JsonSerializer.Serialize(new { leaseId = "later-" + i, data = new string('q', 1024 + i) }));
            GC.Collect(); GC.WaitForPendingFinalizers();
            Require(first.GetProperty("leaseId").GetString() == "Keep-Mixed-Case" && first.GetProperty("nested")[0].GetProperty("text").GetString() == "独立", "old request lost independent lifetime");
        });
        CheckEnablePayload(assembly);
        var jobs = Enumerable.Range(0, 16).Select(i => Task.Run(() =>
        {
            var parsed = Parse("{\"request\":" + i + ",\"nested\":[1,2,3]}");
            Require(parsed.GetProperty("request").GetInt32() == i && parsed.GetProperty("nested").GetArrayLength() == 3, "parallel parse mixed requests");
            Require(!Parse(" ").EnumerateObject().Any(), "parallel empty parse is dirty");
        })).ToArray();
        try { await Task.WhenAll(jobs); Check("16 concurrent nonempty/empty request parses stay isolated", () => { }); }
        catch (Exception ex) { Check("16 concurrent request isolation", () => throw ex); }
        Console.WriteLine(JsonSerializer.Serialize(new { ok = failures.Count == 0, checks, failures,
            hostDllSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(candidate))).ToLowerInvariant(),
            requiredStringGetStringCallSites = requiredStringGetStringCalls, parserCallSites = parserCalls.Select(m => new { type = m.DeclaringType?.FullName, name = m.Name }).ToArray(), hardwareCalls = 0, scope = "actual-built-Host-private-request-parser-and-required-field; no server/engine/HC/process construction; owned-parser-IL-and-return-lifetime" }));
        return failures.Count == 0 ? 0 : 1;
    }
}
