using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using YeManInputHost.Diagnostics;
using Stage = YeManInputHost.Diagnostics.HostReleaseTimingEvidence.Stage;

internal static class HostReleaseTimingEvidenceSelfTest
{
    private static readonly List<object> Results = new();
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private static int _failures;
    private const string Response = "{\"ok\":true,\"requestId\":\"fixture\",\"sequence\":18446744073709551615}";
    private sealed class Clock
    {
        internal long Now;
        internal int Calls;
        internal bool Throw;
        internal long Read()
        {
            ++Calls;
            if (Throw) throw new InvalidOperationException("private-clock-detail");
            return Now;
        }
    }
    private static void Check(bool value, string message = "assertion failed")
    { if (!value) throw new InvalidOperationException(message); }
    private static void Equal<T>(T expected, T actual)
    { Check(EqualityComparer<T>.Default.Equals(expected, actual), $"expected {expected}, actual {actual}"); }
    private static void Test(string name, Action body)
    {
        try { body(); Results.Add(new { name, status = "pass" }); }
        catch (Exception ex) { ++_failures; Results.Add(new { name, status = "fail", error = ex.Message }); }
    }
    private static HostReleaseTimingEvidence New(Clock clock, long frequency = 1_000_000) =>
        HostReleaseTimingEvidence.TryCreateForTest(clock.Read, frequency) ?? throw new InvalidOperationException("factory failed");
    private static JsonElement View(HostReleaseTimingEvidence helper)
    {
        using var doc = JsonDocument.Parse(helper.TryAppend(Response, 16384));
        return doc.RootElement.GetProperty(HostReleaseTimingEvidence.EvidenceField).Clone();
    }
    private static JsonElement Stages(HostReleaseTimingEvidence helper) => View(helper).GetProperty("stages");
    private static void Null(JsonElement value) => Equal(JsonValueKind.Null, value.ValueKind);
    private static void Unknown(HostReleaseTimingEvidence helper, string stage) => Null(Stages(helper).GetProperty(stage));
    private static void Same(HostReleaseTimingEvidence helper, string response, int limit = 16384) =>
        Check(ReferenceEquals(response, helper.TryAppend(response, limit)), "failed append changed original identity");
    private static HostReleaseTimingEvidence Measured(Clock clock)
    {
        var helper = New(clock);
        clock.Now = 10; helper.Begin(Stage.ControllerDispose);
        clock.Now = 25; helper.End(Stage.ControllerDispose);
        clock.Now = 30; helper.Complete(true); helper.Finish();
        return helper;
    }

    public static int Main()
    {
        Test("factory-rejects-invalid-clock-frequency-without-throw", () =>
        {
            Check(HostReleaseTimingEvidence.TryCreateForTest(null) is null);
            Check(HostReleaseTimingEvidence.TryCreateForTest(() => 0, 0) is null);
            Check(HostReleaseTimingEvidence.TryCreateForTest(() => 0, -1) is null);
        });
        Test("untouched-stages-omitted-unfinished-total-and-result-null", () =>
        {
            var helper = New(new Clock()); var view = View(helper);
            Equal(1, view.GetProperty("version").GetInt32()); Check(view.GetProperty("diagnosticOnly").GetBoolean());
            Check(!view.GetProperty("finished").GetBoolean()); Null(view.GetProperty("totalUs")); Null(view.GetProperty("localReleaseOk"));
            Equal(0, view.GetProperty("stages").EnumerateObject().Count()); Check(!view.TryGetProperty("unmeasuredUs", out _));
        });
        Test("finish-without-complete-keeps-logical-result-null", () =>
        {
            var clock = new Clock(); var helper = New(clock); clock.Now = 31; helper.Finish();
            var view = View(helper); Equal(31L, view.GetProperty("totalUs").GetInt64()); Null(view.GetProperty("localReleaseOk"));
            Equal(31L, view.GetProperty("unmeasuredUs").GetInt64()); Equal(0, Stages(helper).EnumerateObject().Count());
        });
        Test("all-fixed-stages-independent-integer-microseconds-and-unmeasured-total", () =>
        {
            var clock = new Clock { Now = 1000 }; var helper = New(clock);
            var names = new[] { "lifecycleGate", "steamDeckIdleStop", "diagnosticsBefore", "neutral", "outputReaderStop", "controllerDispose", "reportChainClose", "removeAllAttempt1", "endpointWaitAttempt1", "removeAllAttempt2", "endpointWaitAttempt2", "contextDispose", "finalObservation" };
            foreach (var stage in Enum.GetValues<Stage>())
            { clock.Now += 7; helper.Begin(stage); clock.Now += 3; helper.End(stage); }
            clock.Now += 11; helper.Complete(true); helper.Finish();
            var view = View(helper); Equal(141L, view.GetProperty("totalUs").GetInt64()); Equal(102L, view.GetProperty("unmeasuredUs").GetInt64());
            Equal(13, Stages(helper).EnumerateObject().Count()); foreach (var name in names) Equal(3L, Stages(helper).GetProperty(name).GetInt64());
            Check(view.GetProperty("localReleaseOk").GetBoolean());
        });
        Test("unexecuted-retry-is-omitted-not-zero", () =>
        {
            var helper = Measured(new Clock()); Equal(15L, Stages(helper).GetProperty("controllerDispose").GetInt64());
            foreach (var name in new[] { "steamDeckIdleStop", "removeAllAttempt2", "endpointWaitAttempt2", "finalObservation" })
                Check(!Stages(helper).TryGetProperty(name, out _));
        });
        Test("genuine-zero-duration-distinct-from-not-run", () =>
        {
            var helper = New(new Clock()); helper.Begin(Stage.Neutral); helper.End(Stage.Neutral); helper.Finish();
            Equal(0L, Stages(helper).GetProperty("neutral").GetInt64()); Check(!Stages(helper).TryGetProperty("controllerDispose", out _));
        });
        Test("frequency-conversion-truncates-after-scaling", () =>
        {
            var clock = new Clock(); var helper = New(clock, 3); helper.Begin(Stage.Neutral); clock.Now = 1;
            helper.End(Stage.Neutral); clock.Now = 3; helper.Finish();
            Equal(333333L, Stages(helper).GetProperty("neutral").GetInt64()); Equal(1_000_000L, View(helper).GetProperty("totalUs").GetInt64());
        });
        Test("large-ticks-do-not-overflow-multiplication", () =>
        {
            var clock = new Clock(); var helper = New(clock, long.MaxValue); helper.Begin(Stage.Neutral);
            clock.Now = long.MaxValue; helper.End(Stage.Neutral); helper.Finish();
            Equal(1_000_000L, Stages(helper).GetProperty("neutral").GetInt64()); Equal(1_000_000L, View(helper).GetProperty("totalUs").GetInt64());
        });
        Test("unrepresentable-duration-is-null-not-overflow-or-zero", () =>
        {
            var clock = new Clock(); var helper = New(clock, 1); helper.Begin(Stage.Neutral); clock.Now = long.MaxValue;
            helper.End(Stage.Neutral); helper.Finish(); Unknown(helper, "neutral"); Null(View(helper).GetProperty("totalUs"));
        });
        Test("unmatched-begin-is-unknown-on-finish", () =>
        {
            var clock = new Clock(); var helper = New(clock); helper.Begin(Stage.ControllerDispose); clock.Now = 50; helper.Finish();
            Unknown(helper, "controllerDispose"); Equal(50L, View(helper).GetProperty("totalUs").GetInt64()); Check(!View(helper).TryGetProperty("unmeasuredUs", out _));
        });
        Test("end-without-begin-is-unknown", () =>
        { var helper = New(new Clock()); helper.End(Stage.Neutral); helper.Finish(); Unknown(helper, "neutral"); });
        Test("duplicate-begin-cannot-be-repaired-by-end", () =>
        {
            var clock = new Clock(); var helper = New(clock); helper.Begin(Stage.Neutral); clock.Now = 5; helper.Begin(Stage.Neutral);
            clock.Now = 10; helper.End(Stage.Neutral); helper.Finish(); Unknown(helper, "neutral");
        });
        Test("duplicate-end-invalidates-previous-measurement", () =>
        {
            var helper = New(new Clock()); helper.Begin(Stage.Neutral); helper.End(Stage.Neutral); helper.End(Stage.Neutral);
            helper.Finish(); Unknown(helper, "neutral");
        });
        Test("repeated-pair-for-same-stage-is-unknown", () =>
        {
            var helper = New(new Clock()); helper.Begin(Stage.Neutral); helper.End(Stage.Neutral); helper.Begin(Stage.Neutral);
            helper.End(Stage.Neutral); helper.Finish(); Unknown(helper, "neutral");
        });
        Test("mismatched-end-marks-both-involved-stages-unknown", () =>
        {
            var helper = New(new Clock()); helper.Begin(Stage.Neutral); helper.End(Stage.ControllerDispose); helper.End(Stage.Neutral);
            helper.Finish(); Unknown(helper, "neutral"); Unknown(helper, "controllerDispose");
        });
        Test("overlapping-begins-cannot-double-count-time", () =>
        {
            var helper = New(new Clock()); helper.Begin(Stage.Neutral); helper.Begin(Stage.ControllerDispose);
            helper.End(Stage.ControllerDispose); helper.End(Stage.Neutral); helper.Finish(); Unknown(helper, "neutral"); Unknown(helper, "controllerDispose");
        });
        Test("late-stage-calls-are-unknown-and-do-not-resample-finish", () =>
        {
            var clock = new Clock(); var helper = Measured(clock); var calls = clock.Calls;
            helper.Begin(Stage.Neutral); helper.End(Stage.Neutral); helper.End(Stage.ControllerDispose);
            Unknown(helper, "neutral"); Unknown(helper, "controllerDispose"); Equal(calls, clock.Calls); Equal(30L, View(helper).GetProperty("totalUs").GetInt64());
        });
        Test("clock-failure-at-creation-produces-explicit-unknown-evidence", () =>
        {
            var clock = new Clock { Throw = true }; var helper = New(clock); helper.Begin(Stage.Neutral); helper.End(Stage.Neutral);
            helper.Finish(); Unknown(helper, "neutral"); Null(View(helper).GetProperty("totalUs"));
            Check(!View(helper).GetRawText().Contains("private-clock-detail", StringComparison.Ordinal));
        });
        Test("clock-failure-at-begin-invalidates-earlier-durations", () =>
        {
            var clock = new Clock(); var helper = New(clock); helper.Begin(Stage.Neutral); clock.Now = 5; helper.End(Stage.Neutral);
            clock.Throw = true; helper.Begin(Stage.ControllerDispose); helper.End(Stage.ControllerDispose); helper.Complete(true); helper.Finish();
            Unknown(helper, "neutral"); Unknown(helper, "controllerDispose"); Null(View(helper).GetProperty("totalUs")); Check(View(helper).GetProperty("localReleaseOk").GetBoolean());
        });
        Test("clock-failure-at-end-is-unknown-with-logical-false-preserved", () =>
        {
            var clock = new Clock(); var helper = New(clock); helper.Begin(Stage.ControllerDispose); clock.Throw = true;
            helper.End(Stage.ControllerDispose); helper.Complete(false); helper.Finish(); Unknown(helper, "controllerDispose");
            Null(View(helper).GetProperty("totalUs")); Check(!View(helper).GetProperty("localReleaseOk").GetBoolean());
        });
        Test("clock-failure-at-finish-invalidates-known-stage-durations", () =>
        {
            var clock = new Clock(); var helper = New(clock); helper.Begin(Stage.Neutral); clock.Now = 5; helper.End(Stage.Neutral);
            clock.Throw = true; helper.Finish(); Unknown(helper, "neutral"); Null(View(helper).GetProperty("totalUs"));
        });
        Test("clock-regression-within-stage-is-unknown", () =>
        {
            var clock = new Clock { Now = 10 }; var helper = New(clock); helper.Begin(Stage.Neutral); clock.Now = 9;
            helper.End(Stage.Neutral); helper.Finish(); Unknown(helper, "neutral"); Null(View(helper).GetProperty("totalUs"));
        });
        Test("clock-regression-between-stages-invalidates-earlier-duration", () =>
        {
            var clock = new Clock(); var helper = New(clock); clock.Now = 10; helper.Begin(Stage.Neutral); clock.Now = 20; helper.End(Stage.Neutral);
            clock.Now = 15; helper.Begin(Stage.ControllerDispose); helper.Finish(); Unknown(helper, "neutral"); Unknown(helper, "controllerDispose");
        });
        Test("negative-tick-and-invalid-enum-never-fabricate-duration", () =>
        {
            var clock = new Clock { Now = -1 }; var helper = New(clock); helper.Finish(); Null(View(helper).GetProperty("totalUs"));
            helper = New(new Clock()); helper.Begin((Stage)999); helper.End((Stage)(-1)); helper.Finish(); Null(View(helper).GetProperty("totalUs"));
        });
        Test("finish-is-idempotent-clock-not-reread-and-json-stable", () =>
        {
            var clock = new Clock(); var helper = Measured(clock); var calls = clock.Calls; var first = helper.TryAppend(Response, 16384);
            clock.Now = 999; clock.Throw = true; helper.Finish(); helper.Finish(); Equal(calls, clock.Calls);
            Equal(first, helper.TryAppend(Response, 16384)); Equal(30L, View(helper).GetProperty("totalUs").GetInt64());
        });
        Test("complete-result-nullable-idempotent-and-conflict-sticky", () =>
        {
            var helper = New(new Clock()); Null(View(helper).GetProperty("localReleaseOk")); helper.Complete(false); helper.Complete(false);
            Check(!View(helper).GetProperty("localReleaseOk").GetBoolean()); helper.Complete(true); Null(View(helper).GetProperty("localReleaseOk"));
            helper.Complete(false); helper.Finish(); Null(View(helper).GetProperty("localReleaseOk"));
        });
        Test("caller-exception-records-its-false-return-without-device-semantics", () =>
        {
            var clock = new Clock(); var helper = New(clock);
            try { helper.Begin(Stage.ControllerDispose); clock.Now = 50; throw new InvalidOperationException("fixture-only-action"); }
            catch (InvalidOperationException) { helper.Complete(false); }
            finally { helper.End(Stage.ControllerDispose); helper.Finish(); }
            Equal(50L, Stages(helper).GetProperty("controllerDispose").GetInt64()); Check(!View(helper).GetProperty("localReleaseOk").GetBoolean());
            Check(!View(helper).GetRawText().Contains("fixture-only-action", StringComparison.Ordinal));
        });
        Test("successful-append-preserves-every-original-character-and-field", () =>
        {
            var helper = Measured(new Clock());
            var original = " \t{ \"ok\": false, \"big\":18446744073709551615,\"unicode\":\"中文😀\",\"escaped\":\"\\u0061\",\"braces\":\"} {\" } \r\n";
            var appended = helper.TryAppend(original, 16384); Check(!ReferenceEquals(original, appended));
            var end = original.LastIndexOf('}'); Check(appended.StartsWith(original[..end], StringComparison.Ordinal)); Check(appended.EndsWith(original[end..], StringComparison.Ordinal));
            using var before = JsonDocument.Parse(original); using var after = JsonDocument.Parse(appended);
            foreach (var property in before.RootElement.EnumerateObject()) Equal(property.Value.GetRawText(), after.RootElement.GetProperty(property.Name).GetRawText());
            Check(!after.RootElement.GetProperty("ok").GetBoolean()); Check(after.RootElement.GetProperty(HostReleaseTimingEvidence.EvidenceField).GetProperty("localReleaseOk").GetBoolean());
        });
        Test("empty-root-and-trailing-whitespace-remain-valid-json", () =>
        {
            var helper = New(new Clock());
            foreach (var original in new[] { "{}", "  { \r\n } \t\r\n" })
            { using var doc = JsonDocument.Parse(helper.TryAppend(original, 16384)); Equal(1, doc.RootElement.EnumerateObject().Count()); }
        });
        Test("existing-evidence-and-duplicate-root-properties-refuse-append", () =>
        {
            var helper = Measured(new Clock()); Same(helper, "{\"releaseTimingEvidence\":null}"); Same(helper, "{\"releaseTiming\\u0045vidence\":7}");
            Same(helper, "{\"ok\":true,\"ok\":false}"); Same(helper, "{\"a\":1,\"\\u0061\":2}");
        });
        Test("malformed-nonobject-and-invalid-utf16-return-exact-original", () =>
        {
            var helper = Measured(new Clock());
            foreach (var text in new[] { "", "{", "{}{}", "[]", "null", "3", "\"object\"", "{\"bad\":NaN}", "{\"x\":1,}", "{\"x\":\"" + '\ud800' + "\"}", "{\"x\":\"" + '\udc00' + "\"}" }) Same(helper, text);
            Check(helper.TryAppend(null!, 16384) is null);
        });
        Test("utf8-byte-budget-exact-boundary-with-multibyte-original", () =>
        {
            var helper = Measured(new Clock()); var original = "{\"unicode\":\"中文😀中文😀\"}\r\n";
            var expected = helper.TryAppend(original, 16384); var exact = Utf8.GetByteCount(expected);
            Equal(expected, helper.TryAppend(original, exact)); Same(helper, original, exact - 1);
            Same(helper, original, original.Length); Same(helper, original, 0); Same(helper, original, -1);
            Same(helper, original, HostReleaseTimingEvidence.MaximumResponseBytes + 1);
        });
        Test("deep-json-and-too-many-root-properties-fail-inertly", () =>
        {
            var helper = Measured(new Clock()); Same(helper, "{\"deep\":" + new string('[', 20) + "0" + new string(']', 20) + "}");
            Same(helper, "{" + string.Join(',', Enumerable.Range(0, 129).Select(i => $"\"p{i}\":{i}")) + "}");
            Same(helper, "{\"long\":\"" + new string('a', HostReleaseTimingEvidence.MaximumResponseBytes) + "\"}", HostReleaseTimingEvidence.MaximumResponseBytes);
        });
        Test("rejected-append-does-not-change-evidence-or-read-clock", () =>
        {
            var clock = new Clock(); var helper = Measured(clock); var first = View(helper).GetRawText(); var calls = clock.Calls;
            Same(helper, "{"); Same(helper, Response, 1); Equal(first, View(helper).GetRawText()); Equal(calls, clock.Calls);
        });
        Test("recording-hot-methods-perform-zero-managed-allocations", () =>
        {
            var warmClock = new Clock(); var warm = Measured(warmClock); warm.Begin(Stage.Neutral); warm.End(Stage.Neutral);
            var clock = new Clock(); var helper = New(clock); var stages = Enum.GetValues<Stage>();
            var before = GC.GetAllocatedBytesForCurrentThread();
            foreach (var stage in stages) { ++clock.Now; helper.Begin(stage); ++clock.Now; helper.End(stage); }
            helper.Complete(true); helper.Finish(); var allocated = GC.GetAllocatedBytesForCurrentThread() - before;
            Equal(0L, allocated);
        });
        Test("fixture-links-only-helper-and-has-no-host-sdk-device-reference", () =>
        {
            var references = Assembly.GetExecutingAssembly().GetReferencedAssemblies().Select(x => x.Name ?? "");
            Check(references.All(n => !n.Contains("HIDMaestro", StringComparison.OrdinalIgnoreCase) && !n.Contains("Nefarius", StringComparison.OrdinalIgnoreCase) && n != "YeManInputHost"));
            Check(typeof(HostReleaseTimingEvidence).GetMethods(BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic).All(m => (m.Attributes & MethodAttributes.PinvokeImpl) == 0));
        });
        Console.WriteLine(JsonSerializer.Serialize(new { version = 1, pureInjected = true, hostOrHardwareStarted = false,
            passed = Results.Count - _failures, failed = _failures, cases = Results }, new JsonSerializerOptions { WriteIndented = true }));
        return _failures == 0 ? 0 : 1;
    }
}