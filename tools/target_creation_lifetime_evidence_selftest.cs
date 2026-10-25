using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using YeManInputHost.Diagnostics;

internal static class TargetCreationLifetimeEvidenceSelfTest
{
    private static readonly List<object> Results = new();
    private static int _failures;
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private const string Response = "{\"ok\":true,\"requestId\":\"PRIVATE-REQUEST\",\"runId\":\"PRIVATE-RUN\",\"epoch\":5,\"powerGeneration\":1,\"sequence\":101}";
    private static void Check(bool condition, string message = "assertion failed")
    { if (!condition) throw new InvalidOperationException(message); }
    private static void Equal<T>(T expected, T actual)
    { Check(EqualityComparer<T>.Default.Equals(expected, actual), "expected " + expected + " / got " + actual); }
    private static void Test(string name, Action body)
    {
        try { body(); Results.Add(new { name, status = "pass" }); }
        catch (Exception ex) { ++_failures; Results.Add(new { name, status = "fail", error = ex.GetType().Name + ":" + ex.Message }); }
    }
    private static TargetCreationLifetimeEvidence New(ulong attempt = 0, ulong creation = 0) =>
        TargetCreationLifetimeEvidence.TryCreateForTest(attempt, creation) ?? throw new InvalidOperationException("factory failed");
    private static TargetCreationLifetimeEvidence.Snapshot View(TargetCreationLifetimeEvidence helper) =>
        helper.Capture() ?? throw new InvalidOperationException("capture unexpectedly missing");
    private static TargetCreationLifetimeEvidence Live(out TargetCreationLifetimeEvidence.Attempt attempt)
    {
        var helper = New(); attempt = helper.BeginAttempt(); Check(helper.Created(attempt, new object())); return helper;
    }
    private static void Unknown(TargetCreationLifetimeEvidence helper)
    { var s = View(helper); Equal("unknown", s.State); Check(s.Closed && s.LifetimeToken is null); Boundaries(s); }
    private static void Boundaries(TargetCreationLifetimeEvidence.Snapshot s)
    { Check(!s.OSIncarnationProven && !s.SdkOwnershipProven && !s.SteamConsumptionProven && s.GameInputUnverified && !s.ProtocolTupleIncluded); }
    private static JsonElement Projection(string response)
    {
        using var document = JsonDocument.Parse(response);
        return document.RootElement.GetProperty(TargetCreationLifetimeEvidence.EvidenceField).Clone();
    }
    private static void SameOriginal(TargetCreationLifetimeEvidence helper, string original, string? command = "PREPARE_TARGET", int limit = 16384)
    {
        var result = helper.TryAppend(original, command, limit); Check(ReferenceEquals(original, result));
        Check(Utf8.GetBytes(original).SequenceEqual(Utf8.GetBytes(result!)), "original UTF8 changed");
    }
    private sealed class PoisonObject
    {
        public override string ToString() => throw new InvalidOperationException("SDK object must never be inspected");
        public override int GetHashCode() => throw new InvalidOperationException("SDK object must never be hashed");
    }

    public static int Main()
    {
        Test("initial-missing-creation-is-unknown-closed-zero", () =>
        { var s = View(New()); Equal("unknown", s.State); Check(s.Closed && s.LifetimeToken is null); Equal("0", s.AttemptOrdinal); Equal("0", s.CreationOrdinal); Boundaries(s); });
        Test("successful-object-creation-produces-exact-independent-ordinal-and-token", () =>
        { var h = Live(out var ticket); var s = View(h); Check(ticket.IsValid); Equal("live", s.State); Equal("1", s.AttemptOrdinal); Equal("1", s.CreationOrdinal); Check(!s.Closed && s.LifetimeToken!.StartsWith("target-creation:v1:", StringComparison.Ordinal)); Boundaries(s); });
        Test("production-factory-has-unique-helper-local-namespaces", () =>
        { var a = TargetCreationLifetimeEvidence.TryCreate() ?? throw new InvalidOperationException("production factory missing"); var b = TargetCreationLifetimeEvidence.TryCreate() ?? throw new InvalidOperationException("production factory missing"); Check(a.Created(a.BeginAttempt(), new object())); Check(b.Created(b.BeginAttempt(), new object())); Check(View(a).Namespace != View(b).Namespace); Check(View(a).LifetimeToken != View(b).LifetimeToken); });
        Test("same-protocol-tuple-recreate-increments-only-creation-and-new-token", () =>
        {
            var h = Live(out _); var first = View(h); var oldReply = h.TryAppend(Response, "PREPARE_TARGET", 16384)!;
            h.ReleaseAttempt(); var next = h.BeginAttempt(); Check(h.Created(next, new object())); var second = View(h);
            Equal("2", second.CreationOrdinal); Check(first.LifetimeToken != second.LifetimeToken); Equal(first.LifetimeToken, second.ClosedLifetimeToken);
            var newReply = h.TryAppend(Response, "RECREATE_TARGET", 16384)!;
            using var a = JsonDocument.Parse(oldReply); using var b = JsonDocument.Parse(newReply);
            foreach (var key in new[] { "requestId", "runId", "epoch", "powerGeneration", "sequence" }) Equal(a.RootElement.GetProperty(key).GetRawText(), b.RootElement.GetProperty(key).GetRawText());
            Boundaries(second);
        });
        Test("begin-attempt-closes-old-token-before-any-create-result", () =>
        { var h = Live(out _); var old = View(h); h.BeginAttempt(); var s = View(h); Equal("attempting", s.State); Check(s.Closed && s.LifetimeToken is null); Equal(old.LifetimeToken, s.ClosedLifetimeToken); Equal("1", s.ClosedCreationOrdinal); Equal("1", s.CreationOrdinal); });
        Test("factory-failure-after-new-attempt-cannot-borrow-old-token", () =>
        { var h = Live(out _); var old = View(h); var ticket = h.BeginAttempt(); h.Failure(ticket); Unknown(h); Equal(old.LifetimeToken, View(h).ClosedLifetimeToken); Equal("1", View(h).CreationOrdinal); });
        Test("null-SDK-return-is-unknown-and-does-not-increment-creation", () =>
        { var h = New(); Check(!h.Created(h.BeginAttempt(), null)); Unknown(h); Equal("0", View(h).CreationOrdinal); });
        Test("postcreate-PnP-or-preparation-failure-closes-observed-object", () =>
        { var h = Live(out var ticket); var old = View(h).LifetimeToken; h.Failure(ticket); Unknown(h); Equal(old, View(h).ClosedLifetimeToken); Equal("1", View(h).CreationOrdinal); });
        Test("release-attempt-closes-before-dispose-success-or-failure", () =>
        { var h = Live(out var ticket); var old = View(h).LifetimeToken; h.ReleaseAttempt(); var s = View(h); Equal("closed", s.State); Check(s.Closed); Equal(old, s.ClosedLifetimeToken); h.Failure(ticket); Unknown(h); Equal(old, View(h).ClosedLifetimeToken); });
        Test("repeated-release-and-shutdown-do-not-invent-a-new-creation", () =>
        { var h = Live(out _); h.ReleaseAttempt(); var a = View(h); h.ReleaseAttempt(); var b = View(h); Equal(a.ClosedLifetimeToken, b.ClosedLifetimeToken); Equal(a.CreationOrdinal, b.CreationOrdinal); Equal(a.AttemptOrdinal, b.AttemptOrdinal); Equal("closed", b.State); });
        Test("release-during-attempt-rejects-late-created-callback", () =>
        { var h = New(); var ticket = h.BeginAttempt(); h.ReleaseAttempt(); Check(!h.Created(ticket, new object())); Unknown(h); Equal("0", View(h).CreationOrdinal); });
        Test("default-attempt-cannot-authorize-created", () =>
        { var h = Live(out _); Check(!h.Created(default, new object())); Unknown(h); Equal("1", View(h).CreationOrdinal); });
        Test("foreign-helper-attempt-is-not-a-valid-local-creation", () =>
        { var a = New(); var b = Live(out _); Check(!b.Created(a.BeginAttempt(), new object())); Unknown(b); Equal("1", View(b).CreationOrdinal); });
        Test("stale-attempt-cannot-complete-a-newer-begin", () =>
        { var h = New(); var stale = h.BeginAttempt(); var current = h.BeginAttempt(); Check(!h.Created(stale, new object())); Unknown(h); Check(!h.Created(current, new object())); Unknown(h); Equal("0", View(h).CreationOrdinal); });
        Test("stale-created-callback-closes-current-newer-live-evidence", () =>
        { var h = Live(out var stale); var current = h.BeginAttempt(); Check(h.Created(current, new object())); var live = View(h).LifetimeToken; Check(!h.Created(stale, new object())); Unknown(h); Equal(live, View(h).ClosedLifetimeToken); Equal("2", View(h).CreationOrdinal); });
        Test("duplicate-created-callback-does-not-generate-second-token", () =>
        { var h = Live(out var ticket); var before = View(h); Check(!h.Created(ticket, new object())); Unknown(h); Equal(before.CreationOrdinal, View(h).CreationOrdinal); Equal(before.LifetimeToken, View(h).ClosedLifetimeToken); });
        Test("stale-or-foreign-failure-closes-current-evidence", () =>
        { var h = Live(out _); var other = New(); h.Failure(other.BeginAttempt()); Unknown(h); Equal("missing-foreign-or-stale-failure", View(h).Reason); });
        Test("fresh-attempt-can-recover-diagnostic-unknown-without-reusing-token", () =>
        { var h = Live(out var old); var token = View(h).LifetimeToken; h.Failure(old); Check(h.Created(h.BeginAttempt(), new object())); Check(View(h).LifetimeToken != token); Equal("2", View(h).CreationOrdinal); });
        Test("attempt-counter-overflow-closes-live-and-permanently-unknown", () =>
        { var h = New(ulong.MaxValue - 1); Check(h.Created(h.BeginAttempt(), new object())); var old = View(h).LifetimeToken; Check(!h.BeginAttempt().IsValid); Unknown(h); Check(View(h).Disabled); Equal(ulong.MaxValue.ToString(CultureInfo.InvariantCulture), View(h).AttemptOrdinal); Equal(old, View(h).ClosedLifetimeToken); Check(!h.BeginAttempt().IsValid); Check(!h.Created(default, new object())); Unknown(h); });
        Test("creation-counter-overflow-never-wraps-or-reopens-previous-token", () =>
        { var h = New(0, ulong.MaxValue - 1); Check(h.Created(h.BeginAttempt(), new object())); var old = View(h).LifetimeToken; var next = h.BeginAttempt(); Check(!h.Created(next, new object())); Unknown(h); Check(View(h).Disabled); Equal(ulong.MaxValue.ToString(CultureInfo.InvariantCulture), View(h).CreationOrdinal); Equal(old, View(h).ClosedLifetimeToken); Check(!h.Created(h.BeginAttempt(), new object())); });
        Test("already-exhausted-attempt-counter-never-issues-ticket", () =>
        { var h = New(ulong.MaxValue); Check(!h.BeginAttempt().IsValid); Unknown(h); Equal("attempt-counter-overflow", View(h).Reason); });
        Test("u64-above-2pow53-stays-exact-decimal-JSON-string", () =>
        { const ulong value = 9007199254740993UL; var h = New(value, value); Check(h.Created(h.BeginAttempt(), new object())); var p = Projection(h.TryAppend(Response, "PREPARE_TARGET", 16384)!); Equal(JsonValueKind.String, p.GetProperty("attemptOrdinal").ValueKind); Equal("9007199254740994", p.GetProperty("attemptOrdinal").GetString()); Equal("9007199254740994", p.GetProperty("creationOrdinal").GetString()); });
        Test("counter-strings-are-invariant-under-non-English-culture", () =>
        { var original = CultureInfo.CurrentCulture; try { CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("ar-SA"); var h = New(98, 8); Check(h.Created(h.BeginAttempt(), new object())); Equal("99", View(h).AttemptOrdinal); Equal("9", View(h).CreationOrdinal); } finally { CultureInfo.CurrentCulture = original; } });
        Test("namespace-factory-exception-is-missing-helper-not-protocol-failure", () =>
        { Check(TargetCreationLifetimeEvidence.TryCreateForTest(namespaceSource: () => throw new InvalidOperationException("PRIVATE-ERROR")) is null); });
        Test("missing-malformed-or-oversized-namespace-is-unknown", () =>
        { foreach (var ns in new string?[] { null, "", "A".PadRight(32, 'A'), new('a', 31), new('a', 33), "../PRIVATE" }) Check(TargetCreationLifetimeEvidence.TryCreateForTest(namespaceSource: () => ns) is null); });
        Test("SDK-object-never-inspected-or-hashed-by-helper", () =>
        { var h = New(); Check(h.Created(h.BeginAttempt(), new PoisonObject())); Equal("live", View(h).State); Boundaries(View(h)); });
        Test("snapshot-is-immutable-historical-view-after-live-state-closes", () =>
        { var h = Live(out _); var old = View(h); h.ReleaseAttempt(); Check(!old.Closed && old.LifetimeToken is not null); Check(View(h).Closed && View(h).LifetimeToken is null); });
        Test("closed-and-current-tokens-remain-distinct-after-many-recreates", () =>
        { var h = New(); var tokens = new HashSet<string>(); for (var i = 1; i <= 100; ++i) { var ticket = h.BeginAttempt(); Check(h.Created(ticket, new object())); var s = View(h); Check(tokens.Add(s.LifetimeToken!)); Equal(i.ToString(CultureInfo.InvariantCulture), s.CreationOrdinal); if (i > 1) Check(s.ClosedLifetimeToken != s.LifetimeToken); h.ReleaseAttempt(); } Equal(100, tokens.Count); });

        Test("append-only-four-lifecycle-commands-and-preserves-protocol-fields", () =>
        { var h = Live(out _); foreach (var command in new[] { "PREPARE_TARGET", "RECREATE_TARGET", "RELEASE_TARGET", "SHUTDOWN" }) { var response = h.TryAppend(Response, command, 16384)!; Check(response != Response); using var d = JsonDocument.Parse(response); Equal(1, d.RootElement.GetProperty("powerGeneration").GetInt32()); Equal(5, d.RootElement.GetProperty("epoch").GetInt32()); var p = Projection(response); Equal(1, p.GetProperty("schema").GetInt32()); Equal(1, p.GetProperty("version").GetInt32()); Check(p.GetProperty("gameInputUnverified").GetBoolean()); Check(!p.GetProperty("osIncarnationProven").GetBoolean()); Check(!p.GetProperty("protocolTupleIncluded").GetBoolean()); Check(!p.TryGetProperty("powerGeneration", out _)); } });
        Test("SUBMIT_FRAME-and-all-other-commands-return-same-original-with-no-capture", () =>
        { var h = Live(out _); foreach (var command in new string?[] { "SUBMIT_FRAME", "SUBMIT_NEUTRAL", "HELLO", "GET_STATS", "prepare_target", "", null, "UNKNOWN" }) { SameOriginal(h, Response, command); var invalid = new byte[] { 0xff, 0xfe }; Check(ReferenceEquals(invalid, h.TryAppend(invalid, command, 16384))); } Equal("live", View(h).State); });
        Test("unknown-and-closed-diagnostics-can-append-without-inventing-live-object", () =>
        { var h = New(); var p = Projection(h.TryAppend("{}", "PREPARE_TARGET", 16384)!); Equal("unknown", p.GetProperty("state").GetString()); Check(p.GetProperty("closed").GetBoolean()); Equal(JsonValueKind.Null, p.GetProperty("lifetimeToken").ValueKind); h.ReleaseAttempt(); p = Projection(h.TryAppend("{}", "SHUTDOWN", 16384)!); Equal("closed", p.GetProperty("state").GetString()); });
        Test("string-append-preserves-leading-trailing-whitespace-and-exact-original-prefix", () =>
        { var h = Live(out _); const string original = " \r\n{\"big\":18446744073709551615,\"text\":\"中文\"}\r\n\t "; var result = h.TryAppend(original, "PREPARE_TARGET", 16384)!; var close = original.IndexOf("}\r\n", StringComparison.Ordinal); Check(result.StartsWith(original[..close], StringComparison.Ordinal)); Check(result.EndsWith(original[close..], StringComparison.Ordinal)); using var d = JsonDocument.Parse(result); Equal("18446744073709551615", d.RootElement.GetProperty("big").GetRawText()); });
        Test("size-budget-one-byte-too-small-returns-original-exact-limit-appends", () =>
        { var h = Live(out _); var appended = h.TryAppend(Response, "PREPARE_TARGET", 16384)!; var bytes = Utf8.GetByteCount(appended); SameOriginal(h, Response, limit: bytes - 1); Equal(appended, h.TryAppend(Response, "PREPARE_TARGET", bytes)); });
        Test("UTF8-byte-size-not-char-count-controls-bounded-append", () =>
        { var h = Live(out _); var original = "{\"text\":\"" + new string('中', 200) + "\"}"; var appended = h.TryAppend(original, "PREPARE_TARGET", 16384)!; Check(Utf8.GetByteCount(appended) > appended.Length); SameOriginal(h, original, limit: appended.Length); Equal(appended, h.TryAppend(original, "PREPARE_TARGET", Utf8.GetByteCount(appended))); });
        Test("overlong-original-zero-negative-or-excessive-budget-remains-original", () =>
        { var h = Live(out _); foreach (var limit in new[] { -1, 0, 10, TargetCreationLifetimeEvidence.MaximumResponseBytes + 1 }) SameOriginal(h, Response, limit: limit); SameOriginal(h, new string('x', 16385)); });
        Test("malformed-nonobject-and-excessive-depth-remain-original", () =>
        { var h = Live(out _); foreach (var original in new[] { "", " ", "null", "[]", "42", "{", "{} trailing", "{\"x\":}", "{/*comment*/}", new string('[', 17) + "0" + new string(']', 17) }) SameOriginal(h, original); });
        Test("existing-projection-or-duplicate-root-field-is-never-overwritten", () =>
        { var h = Live(out _); foreach (var original in new[] { "{\"targetCreationLifetimeEvidence\":null}", "{\"targetCreationLifetimeEvidence\":{\"schema\":99}}", "{\"tar\\u0067etCreationLifetimeEvidence\":null}", "{\"ok\":true,\"ok\":false}" }) SameOriginal(h, original); });
        Test("bounded-root-property-count-fails-open-original", () =>
        { var h = Live(out _); var original = "{" + string.Join(",", Enumerable.Range(0, 129).Select(i => "\"p" + i + "\":0")) + "}"; SameOriginal(h, original); });
        Test("invalid-UTF16-or-invalid-UTF8-is-not-repaired", () =>
        { var h = Live(out _); var text = "{\"x\":\"\ud800\"}"; Check(ReferenceEquals(text, h.TryAppend(text, "PREPARE_TARGET", 16384))); foreach (var invalid in new[] { new byte[] { 0xff }, new byte[] { 0xc0, 0xaf }, new byte[] { 0x7b, 0x22, 0x78, 0x22, 0x3a, 0x22, 0xff, 0x22, 0x7d } }) Check(ReferenceEquals(invalid, h.TryAppend(invalid, "PREPARE_TARGET", 16384))); });
        Test("byte-append-preserves-prefix-suffix-and-failure-array-identity", () =>
        { var h = Live(out _); var original = Utf8.GetBytes(Response + "\r\n"); var result = h.TryAppend(original, "PREPARE_TARGET", 16384)!; Check(!ReferenceEquals(original, result)); Check(result.Take(original.Length - 3).SequenceEqual(original.Take(original.Length - 3))); Check(result.TakeLast(3).SequenceEqual(original.TakeLast(3))); Check(ReferenceEquals(original, h.TryAppend(original, "PREPARE_TARGET", result.Length - 1))); Check(ReferenceEquals(original, h.TryAppend(original, "PREPARE_TARGET", original.Length - 1))); });
        Test("null-original-preserves-null-without-throwing", () =>
        { var h = New(); Check(h.TryAppend((string?)null, "PREPARE_TARGET", 16384) is null); Check(h.TryAppend((byte[]?)null, "PREPARE_TARGET", 16384) is null); });
        Test("append-failure-does-not-close-current-lifetime-or-change-protocol-flow", () =>
        { var h = Live(out _); var old = View(h); SameOriginal(h, Response, limit: 1); SameOriginal(h, "{"); Equal(old.LifetimeToken, View(h).LifetimeToken); Equal(old.CreationOrdinal, View(h).CreationOrdinal); Equal("live", View(h).State); });
        Test("diagnostic-output-never-copies-protocol-private-IDs-or-exception-text", () =>
        { var h = Live(out _); var raw = Projection(h.TryAppend(Response, "PREPARE_TARGET", 16384)!).GetRawText(); Check(!raw.Contains("PRIVATE", StringComparison.Ordinal)); foreach (var forbidden in new[] { "runId", "hostInstanceId", "requestId", "powerGeneration", "targetPnpDetail", "interfacePath", "Steam", "HIDMaestro" }) Check(!Projection(h.TryAppend(Response, "PREPARE_TARGET", 16384)!).TryGetProperty(forbidden, out _)); });
        Test("overflow-projection-is-disabled-unknown-closed-not-live", () =>
        { var h = New(ulong.MaxValue); h.BeginAttempt(); var p = Projection(h.TryAppend(Response, "PREPARE_TARGET", 16384)!); Equal("unknown", p.GetProperty("state").GetString()); Check(p.GetProperty("disabled").GetBoolean() && p.GetProperty("closed").GetBoolean()); Equal(JsonValueKind.Null, p.GetProperty("lifetimeToken").ValueKind); });
        Test("fixture-project-has-no-SDK-host-device-project-reference", () =>
        { var names = Assembly.GetExecutingAssembly().GetReferencedAssemblies().Select(x => x.Name ?? "").ToArray(); Check(names.All(n => !n.Contains("HIDMaestro", StringComparison.OrdinalIgnoreCase) && !n.Contains("Nefarius", StringComparison.OrdinalIgnoreCase) && !n.Equals("YeManInputHost", StringComparison.OrdinalIgnoreCase))); });

        Test("failed-append-preserves-identical-valid-JSON-bytes-without-reencoding", () =>
        { var h = Live(out _); var bytes = Utf8.GetBytes(" {\"big\":18446744073709551615,\"unicode\":\"中文\",\"escaped\":\"\\u0061\"}\r\n"); var clone = bytes.ToArray(); Check(ReferenceEquals(bytes, h.TryAppend(bytes, "PREPARE_TARGET", bytes.Length))); Check(bytes.SequenceEqual(clone)); });
        Test("JSON-string-and-byte-success-have-identical-UTF8-projection", () =>
        { var h = Live(out _); var text = h.TryAppend(Response, "RECREATE_TARGET", 16384)!; var bytes = h.TryAppend(Utf8.GetBytes(Response), "RECREATE_TARGET", 16384)!; Check(Utf8.GetBytes(text).SequenceEqual(bytes)); });
        Test("failed-recreate-diagnostic-then-shutdown-stays-closed-unknown-unconsumed", () =>
        { var h = Live(out _); var old = View(h).LifetimeToken; var ticket = h.BeginAttempt(); h.Failure(ticket); h.ReleaseAttempt(); h.Failure(ticket); var p = Projection(h.TryAppend("{\"ok\":false}", "SHUTDOWN", 16384)!); Equal("unknown", p.GetProperty("state").GetString()); Check(p.GetProperty("closed").GetBoolean()); Equal(old, p.GetProperty("closedLifetimeToken").GetString()); Check(!p.GetProperty("osIncarnationProven").GetBoolean() && p.GetProperty("gameInputUnverified").GetBoolean()); });

        var example = TargetCreationLifetimeEvidence.TryCreateForTest(namespaceSource: () => "0123456789abcdef0123456789abcdef")!;
        var initialExample = View(example); var firstAttempt = example.BeginAttempt(); Check(example.Created(firstAttempt, new object()));
        var firstExample = View(example); example.ReleaseAttempt(); var releaseExample = View(example);
        var recreateAttempt = example.BeginAttempt(); Check(example.Created(recreateAttempt, new object())); var recreateExample = View(example);
        example.Failure(recreateAttempt); var failureExample = View(example);

        var result = new { schema = "ymcc-target-creation-lifetime-helper-selftest-v1", pass = _failures == 0,
            tests = Results.Count, failures = _failures, results = Results,
            scope = "exact-helper-linked-pure-fixtures;no-Host-Program-SDK-device-game-process-or-collector-instantiation",
            helperAssembly = Assembly.GetExecutingAssembly().GetName().Name,
            contractExamples = new { fixtureOnly = true, initial = initialExample, created = firstExample, released = releaseExample, recreated = recreateExample, failed = failureExample } };
        Console.WriteLine(JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
        return _failures == 0 ? 0 : 1;
    }
}
