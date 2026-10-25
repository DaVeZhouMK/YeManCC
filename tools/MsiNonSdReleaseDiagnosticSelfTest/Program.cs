using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Diagnostic = YeManInputHost.Diagnostics.MsiNonSdReleaseDiagnostic;

internal static class Program
{
    private const int MaxMessageBytes = 16 * 1024;
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };
    private static readonly string[] Personas = ["xbox360", "elite", "dualshock4", "dualsense", "dualsense-edge"];
    private static readonly string BaseResponse = "{\"status\":\"release-ok\",\"ok\":true,\"sequence\":2147483647,\"baseBody\":{\"中文\":\"汉🙂\",\"null\":null,\"array\":[1,false,{\"nested\":\"unchanged\"}]}}";

    private sealed class Case(string name, string category)
    {
        internal readonly List<object> Checks = [];
        internal readonly Dictionary<string, int> Calls = new()
        {
            ["constructor"] = 0, ["ShouldObserve"] = 0, ["Snapshot"] = 0,
            ["TryBegin"] = 0, ["TryObserveExistingPoll"] = 0, ["TryComplete"] = 0,
            ["Current"] = 0, ["TryAppend"] = 0
        };
        internal readonly List<object> Evidence = [];
        internal int Failures, AuthorityChecks;
        internal string Name => name;
        internal string Category => category;
        internal void Check(string check, bool passed)
        {
            Checks.Add(new { check, passed });
            if (!passed) ++Failures;
        }
        internal bool Gate(bool requested, string persona)
        {
            ++Calls["ShouldObserve"];
            return Diagnostic.ShouldObserve(requested, persona);
        }
        internal Diagnostic New(string persona = "xbox360")
        {
            ++Calls["constructor"];
            return new(persona);
        }
        internal Diagnostic.Snapshot Snap(bool success = true, int primary = 0, int companion = 0,
            int children = 0, int deck = 0, int deckChildren = 0, bool started = true, string? detail = "fixture")
        {
            ++Calls["Snapshot"];
            return new(success, primary, companion, children, deck, deckChildren, started, detail!);
        }
        internal void Begin(Diagnostic d, Diagnostic.Snapshot cached, long tick = 100,
            bool controller = true, bool context = true)
        {
            ++Calls["TryBegin"];
            d.TryBegin(cached, tick, controller, context);
            Authority(d);
        }
        internal void Poll(Diagnostic d, Diagnostic.Snapshot observed, long start = 101, long end = 102)
        {
            ++Calls["TryObserveExistingPoll"];
            d.TryObserveExistingPoll(observed, start, end);
            Authority(d);
        }
        internal Diagnostic.Receipt? Complete(Diagnostic d, bool localOk = true, long end = 110)
        {
            ++Calls["TryComplete"];
            d.TryComplete(localOk, end);
            return Current(d);
        }
        internal Diagnostic.Receipt? Current(Diagnostic d)
        {
            ++Calls["Current"];
            var current = d.Current;
            if (current is not null)
            {
                ++AuthorityChecks;
                Check("all exact-ownership authority flags remain false",
                    !current.ExactOwnedIdsPinned && !current.ClassificationComplete && !current.ExactOwnedAbsenceProven);
            }
            return current;
        }
        private void Authority(Diagnostic d) => _ = Current(d);
        internal string Append(Diagnostic d, string response = "", int maxBytes = MaxMessageBytes)
        {
            ++Calls["TryAppend"];
            var result = d.TryAppend(response, maxBytes);
            Authority(d);
            Evidence.Add(new { inputUtf8Bytes = Utf8(response), outputUtf8Bytes = Utf8(result), maxBytes, unchanged = result == response, outputPrefix = result[..Math.Min(200, result.Length)] });
            return result;
        }
        internal void Record(Diagnostic.Receipt? receipt) => Evidence.Add(new { receipt });
    }

    private sealed class Suite
    {
        internal readonly List<object> Cases = [];
        internal readonly Dictionary<string, (int Cases, int Failures)> Categories = new();
        internal readonly Dictionary<string, int> Calls = new();
        internal int Failures, CheckCount, AuthorityChecks;
        internal void Run(string name, string category, Action<Case> test)
        {
            var c = new Case(name, category);
            try { test(c); }
            catch (Exception e) { c.Check("unexpected exception: " + e.GetType().Name + ": " + e.Message, false); }
            bool passed = c.Failures == 0;
            if (!passed) { ++Failures; Console.Error.WriteLine("FAIL " + name); }
            CheckCount += c.Checks.Count;
            AuthorityChecks += c.AuthorityChecks;
            foreach (var (api, count) in c.Calls) Calls[api] = Calls.GetValueOrDefault(api) + count;
            var old = Categories.GetValueOrDefault(category);
            Categories[category] = (old.Cases + 1, old.Failures + (passed ? 0 : 1));
            Cases.Add(new { @case = c.Name, category = c.Category, passed,
                failureCount = c.Failures, authorityFlagChecks = c.AuthorityChecks,
                apiCalls = c.Calls, checks = c.Checks, evidence = c.Evidence });
        }
    }

    private static int Utf8(string value) => Encoding.UTF8.GetByteCount(value);
    private static bool WellFormed(string value)
    {
        for (int i = 0; i < value.Length; ++i)
        {
            if (char.IsHighSurrogate(value[i]))
            {
                if (++i >= value.Length || !char.IsLowSurrogate(value[i])) return false;
            }
            else if (char.IsLowSurrogate(value[i])) return false;
        }
        return true;
    }
    private static Diagnostic Ready(Case c, string persona = "xbox360", string detail = "cached")
    {
        var d = c.New(persona);
        c.Begin(d, c.Snap(primary: 1, children: 2, detail: detail));
        c.Poll(d, c.Snap(detail: "existing-poll"));
        c.Complete(d);
        return d;
    }
    private static void PreservedObject(Case c, string original, string output, int maxBytes,
        bool requireDiagnostic = true)
    {
        using var before = JsonDocument.Parse(original);
        using var after = JsonDocument.Parse(output);
        c.Check("appended response remains a JSON object", after.RootElement.ValueKind == JsonValueKind.Object);
        if (after.RootElement.ValueKind != JsonValueKind.Object) return;
        var names = after.RootElement.EnumerateObject().Select(p => p.Name).ToArray();
        c.Check("output has no duplicate fields", names.Distinct(StringComparer.Ordinal).Count() == names.Length);
        foreach (var property in before.RootElement.EnumerateObject())
        {
            bool found = after.RootElement.TryGetProperty(property.Name, out var current);
            c.Check("original field unchanged: " + property.Name, found &&
                JsonNode.DeepEquals(JsonNode.Parse(property.Value.GetRawText()), JsonNode.Parse(current.GetRawText())));
        }
        if (Utf8(original) <= maxBytes)
            c.Check("output respects UTF8 wire byte budget", Utf8(output) <= maxBytes);
        if (requireDiagnostic) c.Check("diagnostic appended when budget permits", after.RootElement.TryGetProperty(Diagnostic.Field, out _));
        if (after.RootElement.TryGetProperty(Diagnostic.Field, out var evidence))
            CheckWireAuthority(c, evidence);
    }
    private static void CheckWireAuthority(Case c, JsonElement value)
    {
        if (value.ValueKind == JsonValueKind.Object)
            foreach (var property in value.EnumerateObject())
            {
                if (property.Name is "exactOwnedIdsPinned" or "classificationComplete" or "exactOwnedAbsenceProven")
                    c.Check("serialized authority flag false: " + property.Name, property.Value.ValueKind == JsonValueKind.False);
                CheckWireAuthority(c, property.Value);
            }
        else if (value.ValueKind == JsonValueKind.Array)
            foreach (var item in value.EnumerateArray()) CheckWireAuthority(c, item);
    }
    private static void RunTests(Suite suite)
    {
        foreach (var persona in Personas)
        {
            foreach (bool requested in new[] { false, true })
                suite.Run($"gate-{persona}-msi-{requested}", "authorization", c =>
                {
                    c.Check("only native MSI opt-in authorizes non-SD observation", c.Gate(requested, persona) == requested);
                    c.Check("ROG/non-MSI transport represented by false MSI request is unauthorized", !c.Gate(false, persona));
                });
            foreach (int polls in new[] { 0, 1, 2 })
                suite.Run($"authorized-{persona}-polls-{polls}", "fresh-vs-cache", c =>
                {
                    c.Check("persona authorized", c.Gate(true, persona));
                    var d = c.New(persona);
                    c.Begin(d, c.Snap(primary: 1, companion: 2, children: 3, detail: "prepare-cache"));
                    for (int i = 0; i < polls; ++i) c.Poll(d, c.Snap(detail: "poll-" + i), 101 + i, 102 + i);
                    var r = c.Complete(d)!;
                    c.Check("cached-before never counted as fresh", r.FreshObservations.Count == polls);
                    c.Check("cache source identifies its limited role", r.CachedBeforeSource == "existing-response-snapshot-not-fresh-proof");
                    c.Check("prepare cache preserved separately", r.CachedBefore.PrimaryRootCount == 1 && r.CachedBefore.Detail == "prepare-cache");
                    c.Check("observation state reflects actual poll count", r.ObservationState == (polls == 0 ? "no-fresh-poll" : "existing-poll-observed"));
                    c.Check("fresh data uses existing poll source", r.FreshObservations.All(p => p.Source == "existing-release-poll" && p.Observation.Detail.StartsWith("poll-", StringComparison.Ordinal)));
                    c.Check("no inferred prior receipt", r.Previous is null);
                    c.Check("begin presence captured", r.ControllerObjectPresentAtBegin && r.ContextObjectPresentAtBegin);
                    var appended = c.Append(d, BaseResponse);
                    PreservedObject(c, BaseResponse, appended, MaxMessageBytes);
                    c.Record(r);
                });
        }
        foreach (var persona in new[] { "steamdeck", "steam-deck", "steam-deck-composite", "rog", "ROG", "", "unknown", "Xbox360", " xbox360", "xbox360 " })
            foreach (bool requested in new[] { false, true })
                suite.Run($"gate-reject-{persona}-msi-{requested}", "authorization", c =>
                    c.Check("SD/noncanonical/unknown persona has no authorization", !c.Gate(requested, persona)));
        suite.Run("gate-null-persona", "authorization", c =>
        {
            c.Check("null opt-in persona unauthorized", !c.Gate(true, null!));
            c.Check("null non-optin persona unauthorized", !c.Gate(false, null!));
        });
        suite.Run("cached-absent-fresh-present", "fresh-vs-cache", c =>
        {
            var d = c.New(); c.Begin(d, c.Snap(detail: "cached-absent"));
            c.Poll(d, c.Snap(primary: 2, companion: 1, children: 4, detail: "real-existing-poll"));
            var r = c.Complete(d)!;
            c.Check("cached absent is only generic count evidence", r.CachedBefore.GenericCountOnlyAbsent);
            c.Check("fresh present contradicts cached absence and is retained", !r.FreshObservations[0].Observation.GenericCountOnlyAbsent && r.FreshObservations[0].Observation.PrimaryRootCount == 2);
            c.Check("fresh and cached data not conflated", r.FreshObservations[0].Observation.Detail != r.CachedBefore.Detail);
            c.Record(r);
        });
        suite.Run("probe-failure-never-count-only-absence", "invalid-observation", c =>
        {
            var d = c.New(); var failed = c.Snap(success: false);
            c.Check("failed probe does not claim generic absence", !failed.GenericCountOnlyAbsent);
            c.Begin(d, failed); c.Poll(d, failed);
            var r = c.Complete(d)!;
            c.Check("failure preserved in cached and fresh observations", !r.CachedBefore.ProbeSucceeded && !r.FreshObservations[0].Observation.ProbeSucceeded);
            c.Check("poll observed does not mean probe succeeded", r.ObservationState == "existing-poll-observed");
            c.Record(r);
        });
        for (int field = 0; field < 5; ++field)
            foreach (int negative in new[] { -1, int.MinValue })
                suite.Run($"negative-count-field-{field}-value-{negative}", "invalid-observation", c =>
                {
                    int[] counts = [0, 0, 0, 0, 0]; counts[field] = negative;
                    var s = c.Snap(primary: counts[0], companion: counts[1], children: counts[2], deck: counts[3], deckChildren: counts[4]);
                    c.Check("negative count invalidates count fields", !s.CountFieldsValid);
                    c.Check("negative count cannot produce generic absence", !s.GenericCountOnlyAbsent);
                    var d = c.New(); c.Begin(d, s); c.Poll(d, s); var r = c.Complete(d)!;
                    c.Check("invalid count remains evidence without normalization", r.CachedBefore == s && r.FreshObservations[0].Observation == s);
                    c.Record(r);
                });
        suite.Run("generic-count-absence-not-classification", "invalid-observation", c =>
        {
            var s = c.Snap(deck: 2, deckChildren: 3, started: false);
            c.Check("non-SD generic counts can be zero despite SD evidence", s.GenericCountOnlyAbsent);
            var d = c.New(); c.Begin(d, s); c.Poll(d, s); var r = c.Complete(d)!;
            c.Check("SD counts and started state preserved", r.FreshObservations[0].Observation.SteamDeckRootCount == 2 && !r.FreshObservations[0].Observation.AllStarted);
            c.Record(r);
        });
        suite.Run("local-disposal-failure-is-not-absence-proof", "invalid-observation", c =>
        {
            var d = c.New(); c.Begin(d, c.Snap()); c.Poll(d, c.Snap());
            var r = c.Complete(d, false)!;
            c.Check("local disposal failure retained", !r.LocalDisposalReportedOk);
            c.Check("generic count-only absence stays limited", r.FreshObservations[0].Observation.GenericCountOnlyAbsent);
            c.Record(r);
        });
        foreach (var clock in new (long Begin, long End, bool Known)[]
            { (-1, 110, false), (100, 99, false), (100, 100, true), (0, 0, true), (long.MaxValue, long.MaxValue, true), (long.MinValue, long.MaxValue, false) })
            suite.Run($"release-clock-{clock.Begin}-{clock.End}", "clock", c =>
            {
                var d = c.New(); c.Begin(d, c.Snap(), clock.Begin); var r = c.Complete(d, true, clock.End)!;
                c.Check("release time window validity explicit", r.TimeWindowKnown == clock.Known);
                c.Check("original release ticks preserved", r.BeginTick == clock.Begin && r.EndTick == clock.End);
                c.Check("missing fresh poll not synthesized by completion", r.FreshObservations.Count == 0 && r.ObservationState == "no-fresh-poll");
                c.Record(r);
            });
        foreach (var clock in new (long Begin, long End, bool Known)[]
            { (99, 102, false), (101, 100, false), (-1, 0, false), (100, 100, true), (101, 102, true) })
            suite.Run($"poll-clock-{clock.Begin}-{clock.End}", "clock", c =>
            {
                var d = c.New(); c.Begin(d, c.Snap()); c.Poll(d, c.Snap(), clock.Begin, clock.End); var r = c.Complete(d)!;
                var p = r.FreshObservations.Single();
                c.Check("poll clock validity explicit", p.TimeWindowKnown == clock.Known);
                c.Check("poll original ticks retained", p.BeginTick == clock.Begin && p.EndTick == clock.End);
                c.Record(r);
            });
        suite.Run("poll-and-complete-before-begin", "lifecycle", c =>
        {
            var d = c.New(); c.Check("Current initially null", c.Current(d) is null);
            c.Poll(d, c.Snap()); c.Check("complete without begin produces no receipt", c.Complete(d) is null);
            c.Check("no receipt cannot append diagnostic", c.Append(d, BaseResponse) == BaseResponse);
        });
        suite.Run("poll-after-complete-and-second-complete", "lifecycle", c =>
        {
            var d = Ready(c); var saved = c.Current(d)!;
            c.Poll(d, c.Snap(primary: 99), 111, 112);
            c.Check("post-complete poll does not mutate existing receipt", ReferenceEquals(c.Current(d), saved) && saved.FreshObservations.Count == 1);
            c.Check("second complete cannot manufacture a new observation", c.Complete(d, true, 120) is null);
        });
        suite.Run("second-shutdown-null-sdk-no-fresh-poll", "lifecycle", c =>
        {
            var d = Ready(c); var first = c.Current(d)!;
            c.Begin(d, c.Snap(detail: "prepare-cache-for-null-sdk"), 200, false, false);
            var second = c.Complete(d, true, 201)!;
            c.Check("null controller/context captured without implied absence", !second.ControllerObjectPresentAtBegin && !second.ContextObjectPresentAtBegin);
            c.Check("no actual SDK poll means no fresh observation", second.FreshObservations.Count == 0 && second.ObservationState == "no-fresh-poll");
            c.Check("cached zero does not become fresh data", second.CachedBefore.GenericCountOnlyAbsent && second.CachedBeforeSource == "existing-response-snapshot-not-fresh-proof");
            c.Check("previous has only earlier poll count", second.Previous?.FreshObservationCount == first.FreshObservations.Count);
            c.Record(second);
        });
        suite.Run("third-poll-exhausts-budget", "lifecycle", c =>
        {
            var d = c.New(); c.Begin(d, c.Snap());
            c.Poll(d, c.Snap(primary: 1, detail: "first"), 101, 102);
            c.Poll(d, c.Snap(primary: 2, detail: "second"), 103, 104);
            for (int i = 0; i < 100; ++i) c.Poll(d, c.Snap(primary: 100 + i), 105, 106);
            var r = c.Complete(d)!;
            c.Check("fresh storage bounded to two real observations", r.FreshObservations.Count == 2);
            c.Check("budget exhaustion explicit", r.ObservationState == "diagnostic-incomplete");
            c.Check("first two observations retained without replacement", r.FreshObservations[0].Observation.Detail == "first" && r.FreshObservations[1].Observation.Detail == "second");
            c.Record(r);
        });
        suite.Run("begin-clears-fresh-budget-failure", "lifecycle", c =>
        {
            var d = c.New(); c.Begin(d, c.Snap(detail: "old-cache"));
            for (int i = 0; i < 3; ++i) c.Poll(d, c.Snap(), 101, 102);
            var old = c.Complete(d)!;
            c.Begin(d, c.Snap(primary: 9, detail: "new-cache"), 200, false, true);
            c.Poll(d, c.Snap(primary: 7, detail: "new-poll"), 201, 202); var r = c.Complete(d, false, 203)!;
            c.Check("new begin clears previous failed poll budget", r.ObservationState == "existing-poll-observed" && r.FreshObservations.Count == 1);
            c.Check("new cache/presence/local result replaces old operation", r.CachedBefore.PrimaryRootCount == 9 && !r.ControllerObjectPresentAtBegin && r.ContextObjectPresentAtBegin && !r.LocalDisposalReportedOk);
            c.Check("only new poll included", r.FreshObservations[0].Observation.Detail == "new-poll");
            c.Check("previous receipt remains immutable", old.FreshObservations.Count == 2 && old.ObservationState == "diagnostic-incomplete");
            c.Check("previous stored as brief only", r.Previous?.FreshObservationCount == 2);
            c.Record(r);
        });
        suite.Run("begin-over-active-operation-clears-unfinished-data", "lifecycle", c =>
        {
            var d = c.New(); c.Begin(d, c.Snap(detail: "unfinished")); c.Poll(d, c.Snap(primary: 8));
            c.Begin(d, c.Snap(detail: "replacement"), 200, false, false); var r = c.Complete(d, true, 201)!;
            c.Check("unfinished poll not carried across begin", r.FreshObservations.Count == 0 && r.CachedBefore.Detail == "replacement");
            c.Check("unfinished operation is not previous receipt", r.Previous is null);
            c.Record(r);
        });
        suite.Run("failed-begin-clears-completion-authority", "lifecycle", c =>
        {
            var d = Ready(c); c.Begin(d, null!, 200);
            c.Check("failed begin cannot publish old receipt at completion", c.Complete(d, true, 201) is null);
            c.Check("failed begin cannot append stale receipt after completion", c.Append(d, BaseResponse) == BaseResponse);
        });
        suite.Run("null-poll-snapshot-marks-incomplete", "lifecycle", c =>
        {
            var d = c.New(); c.Begin(d, c.Snap()); c.Poll(d, null!);
            c.Poll(d, c.Snap(), 103, 104); var r = c.Complete(d)!;
            c.Check("bad poll input contained as diagnostic loss", r.ObservationState == "diagnostic-incomplete" && r.FreshObservations.Count == 0);
            c.Record(r);
        });
        suite.Run("previous-100-completions-is-brief-not-recursive", "lifecycle", c =>
        {
            var d = c.New(); Diagnostic.Receipt? last = null;
            for (int i = 0; i < 100; ++i)
            {
                long tick = 1000 + i * 10;
                c.Begin(d, c.Snap(detail: "iteration-" + i), tick);
                c.Poll(d, c.Snap(), tick + 1, tick + 2);
                var next = c.Complete(d, i % 2 == 0, tick + 3)!;
                c.Check("previous is immediately preceding completed operation", last is null ? next.Previous is null :
                    next.Previous is { } prior && prior.BeginTick == last.BeginTick && prior.EndTick == last.EndTick &&
                    prior.LocalDisposalReportedOk == last.LocalDisposalReportedOk && prior.FreshObservationCount == last.FreshObservations.Count);
                last = next;
            }
            var appended = c.Append(d, BaseResponse);
            PreservedObject(c, BaseResponse, appended, MaxMessageBytes);
            using var parsed = JsonDocument.Parse(appended);
            var previous = parsed.RootElement.GetProperty(Diagnostic.Field).GetProperty("previous");
            c.Check("previous serialized with exactly four brief fields", previous.EnumerateObject().Count() == 4);
            c.Check("previous contains no recursive receipt/cache/poll payload", !previous.TryGetProperty("previous", out _) &&
                !previous.TryGetProperty("cachedBefore", out _) && !previous.TryGetProperty("freshObservations", out _));
            c.Check("history cannot grow diagnostic without bound", Utf8(appended) < 8192);
            c.Record(last);
        });
        var details = new (string Name, string? Text, int Length, bool Truncated)[]
        {
            ("null", null, 0, false), ("empty", "", 0, false),
            ("511-ascii", new string('a', 511), 511, false),
            ("512-ascii", new string('a', 512), 512, false),
            ("513-ascii", new string('a', 513), 512, true),
            ("512-BMP", new string('汉', 512), 512, false),
            ("pair-fits-exactly", new string('a', 510) + "🙂", 512, false),
            ("pair-crosses-cut", new string('a', 511) + "🙂", 511, true),
            ("pair-after-cut", new string('a', 512) + "🙂", 512, true),
            ("pair-before-cut", new string('a', 509) + "🙂ab", 512, true),
            ("256-surrogate-pairs", string.Concat(Enumerable.Repeat("🙂", 256)), 512, false),
            ("257-surrogate-pairs", string.Concat(Enumerable.Repeat("🙂", 257)), 512, true),
            ("huge-detail", new string('a', 1_000_000), 512, true)
        };
        foreach (var detail in details)
            suite.Run("bounded-detail-" + detail.Name, "detail-and-unicode", c =>
            {
                var source = c.Snap(detail: detail.Text); var d = c.New();
                c.Begin(d, source); c.Poll(d, source); var r = c.Complete(d)!;
                foreach (var s in new[] { r.CachedBefore, r.FreshObservations[0].Observation })
                {
                    c.Check("detail length bounded at valid UTF16 boundary", s.Detail.Length == detail.Length && WellFormed(s.Detail));
                    c.Check("truncation flag matches original input", s.DetailTruncated == detail.Truncated);
                }
                c.Check("source snapshot never mutated", source.Detail == detail.Text && !source.DetailTruncated);
                var output = c.Append(d, BaseResponse); PreservedObject(c, BaseResponse, output, MaxMessageBytes);
                using var parsed = JsonDocument.Parse(output);
                var serialized = parsed.RootElement.GetProperty(Diagnostic.Field);
                var decoded = serialized.GetProperty("cachedBefore").GetProperty("detail").GetString()!;
                c.Check("JSON round-trip preserves bounded valid UTF16", WellFormed(decoded) && decoded == r.CachedBefore.Detail);
                c.Record(r);
            });
        foreach (var response in new[]
        {
            BaseResponse,
            "{\"status\":\"error\",\"ok\":false,\"error\":{\"code\":5},\"ids\":[\"opaque-a\",\"opaque-b\"]}",
            "{\"status\":null,\"int\":9223372036854775807,\"decimal\":1.2300,\"escaped\":\"quote\\\"slash\\\\line\\n\"}",
            "{}", "{ }", "{\n\t}",
            "{\"status\":\"keep\",\"msiNonSdReleaseEvidence\":{\"sentinel\":\"original\"}}"
        })
            suite.Run("append-preserves-response-" + response, "response-integrity", c =>
            {
                var d = Ready(c); var output = c.Append(d, response);
                PreservedObject(c, response, output, MaxMessageBytes);
            });
        suite.Run("append-twice-no-duplicate-diagnostic-field", "response-integrity", c =>
        {
            var d = Ready(c); var first = c.Append(d, BaseResponse); var second = c.Append(d, first);
            c.Check("already appended response remains unchanged", second == first);
            PreservedObject(c, first, second, MaxMessageBytes);
        });
        foreach (var response in new[] { "", " ", "[]", "[1,2]", "null", "true", "\"text}\"", "broken}", "{\"status\":1}}" })
            suite.Run("append-invalid-or-nonobject-" + response, "response-integrity", c =>
            {
                var d = Ready(c);
                c.Check("unusable input not modified", c.Append(d, response) == response);
            });
        suite.Run("append-valid-object-with-trailing-whitespace", "response-integrity", c =>
        {
            var d = Ready(c); var response = BaseResponse + " \r\n";
            var output = c.Append(d, response);
            PreservedObject(c, response, output, MaxMessageBytes, false);
        });
        suite.Run("append-with-no-current", "response-integrity", c =>
        {
            var d = c.New(); c.Check("no current leaves entire response untouched", c.Append(d, BaseResponse) == BaseResponse);
        });
        foreach (int budget in new[] { 0, -1, int.MinValue })
            suite.Run("append-invalid-budget-" + budget, "utf8-budget", c =>
            {
                var d = Ready(c); c.Check("invalid budget leaves response untouched", c.Append(d, BaseResponse, budget) == BaseResponse);
            });
        foreach (var empty in new[] { "{}", "{ }", "{\n\t}" })
            suite.Run("empty-object-summary-budget-" + empty, "response-integrity", c =>
            {
                var d = Ready(c);
                var full = c.Append(d, empty, int.MaxValue);
                int budget = Utf8(full) - 1;
                var summary = c.Append(d, empty, budget);
                PreservedObject(c, empty, summary, budget);
                using var parsed = JsonDocument.Parse(summary);
                c.Check("empty-object fallback is valid budget summary", parsed.RootElement.GetProperty(Diagnostic.Field).GetProperty("observationState").GetString() == "omitted-message-budget");
                c.Check("empty-object summary fits exact byte limit", c.Append(d, empty, Utf8(summary)) == summary);
                c.Check("empty-object summary one byte over preserves original", c.Append(d, empty, Utf8(summary) - 1) == empty);
            });
        suite.Run("UTF8-full-summary-and-original-exact-boundaries", "utf8-budget", c =>
        {
            var d = Ready(c); var full = c.Append(d, BaseResponse, int.MaxValue);
            PreservedObject(c, BaseResponse, full, int.MaxValue);
            int fullBytes = Utf8(full);
            c.Check("fixture has multibyte UTF8 beyond character count", Utf8(BaseResponse) > BaseResponse.Length);
            var exactFull = c.Append(d, BaseResponse, fullBytes);
            c.Check("full detail fits at exact UTF8 byte limit", exactFull == full);
            var summary = c.Append(d, BaseResponse, fullBytes - 1);
            PreservedObject(c, BaseResponse, summary, fullBytes - 1);
            using var parsed = JsonDocument.Parse(summary);
            c.Check("one byte below full detail chooses bounded summary", parsed.RootElement.GetProperty(Diagnostic.Field).GetProperty("observationState").GetString() == "omitted-message-budget");
            int summaryBytes = Utf8(summary);
            c.Check("summary smaller than full receipt", summaryBytes < fullBytes);
            c.Check("summary fits at exact UTF8 byte limit", c.Append(d, BaseResponse, summaryBytes) == summary);
            c.Check("one byte below summary preserves original", c.Append(d, BaseResponse, summaryBytes - 1) == BaseResponse);
            c.Check("original at exact byte limit remains intact", c.Append(d, BaseResponse, Utf8(BaseResponse)) == BaseResponse);
            c.Check("already over-budget original never enlarged", c.Append(d, BaseResponse, Utf8(BaseResponse) - 1) == BaseResponse);
        });
        foreach (int delta in new[] { -1, 0, 1 })
            suite.Run("MaxMessage-full-receipt-boundary-delta-" + delta, "utf8-budget", c =>
            {
                var d = Ready(c);
                const string head = "{\"status\":\"unchanged\",\"ok\":false,\"baseBody\":\"汉🙂";
                const string tail = "\"}";
                var empty = head + tail;
                var emptyFull = c.Append(d, empty, int.MaxValue);
                int padding = MaxMessageBytes + delta - Utf8(emptyFull);
                c.Check("MaxMessage fixture padding positive", padding > 0);
                var original = head + new string('a', padding) + tail;
                var unlimited = c.Append(d, original, int.MaxValue);
                c.Check("full receipt fixture exact byte size", Utf8(unlimited) == MaxMessageBytes + delta);
                var bounded = c.Append(d, original, MaxMessageBytes);
                PreservedObject(c, original, bounded, MaxMessageBytes);
                c.Check("full data admitted only within wire limit", delta <= 0 ? bounded == unlimited : bounded != unlimited);
                if (delta > 0)
                {
                    using var parsed = JsonDocument.Parse(bounded);
                    c.Check("overfull detail becomes summary", parsed.RootElement.GetProperty(Diagnostic.Field).GetProperty("observationState").GetString() == "omitted-message-budget");
                }
            });
        foreach (int originalBytes in new[] { MaxMessageBytes, MaxMessageBytes + 1, 1_048_576 })
            suite.Run("large-baseBody-original-bytes-" + originalBytes, "utf8-budget", c =>
            {
                var d = Ready(c);
                const string head = "{\"status\":\"unchanged\",\"baseBody\":\"汉🙂";
                const string tail = "\"}";
                var original = head + new string('a', originalBytes - Utf8(head + tail)) + tail;
                c.Check("baseBody fixture exact UTF8 byte length", Utf8(original) == originalBytes);
                var bounded = c.Append(d, original, MaxMessageBytes);
                c.Check("huge/at-limit original returned byte-for-byte", bounded == original);
                PreservedObject(c, original, bounded, MaxMessageBytes, false);
            });
    }

    public static int Main(string[] args)
    {
        if (args.Length != 1)
        {
            Console.Error.WriteLine("Usage: dotnet MsiNonSdReleaseDiagnosticSelfTest.dll report.json");
            return 2;
        }
        var suite = new Suite();
        RunTests(suite);
        var categories = suite.Categories.ToDictionary(x => x.Key,
            x => new { caseCount = x.Value.Cases, failureCount = x.Value.Failures });
        var report = new
        {
            testKind = "linked-pure-BCL-helper",
            hardwareSdkExecuted = false,
            inputHostExecuted = false,
            deviceApiCalls = 0, registryApiCalls = 0, serviceApiCalls = 0,
            linkedSource = "InputHost/MsiNonSdReleaseDiagnostic.cs",
            caseCount = suite.Cases.Count, checkCount = suite.CheckCount,
            failureCount = suite.Failures, authorityFlagChecks = suite.AuthorityChecks,
            passed = suite.Failures == 0, maxMessageBytesFixture = MaxMessageBytes,
            authorizationScope = "false nativeMsiRequested represents non-MSI/ROG/non-optin transport; no hardware identity is probed",
            authorityScope = "every observed Current receipt and serialized diagnostic proof field is checked; no exact-owned/classification/absence authority is inferred",
            apiCallTotals = suite.Calls, categoryTotals = categories, cases = suite.Cases
        };
        try
        {
            var path = Path.GetFullPath(args[0]);
            if (Path.GetDirectoryName(path) is { Length: > 0 } directory) Directory.CreateDirectory(directory);
            File.WriteAllText(path, JsonSerializer.Serialize(report, JsonOptions) + Environment.NewLine, new UTF8Encoding(false));
        }
        catch (Exception e)
        {
            Console.Error.WriteLine("Cannot write report: " + e.Message);
            return 2;
        }
        Console.WriteLine($"MSI_NON_SD_RELEASE_DIAGNOSTIC_SELFTEST cases={suite.Cases.Count} checks={suite.CheckCount} failures={suite.Failures} passed={suite.Failures == 0}");
        Console.WriteLine("API_CALL_TOTALS " + JsonSerializer.Serialize(suite.Calls));
        return suite.Failures == 0 ? 0 : 1;
    }
}
