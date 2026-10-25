// Pure BCL fixtures only. The production helpers are linked without the Host or SDK.
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using YeManInputHost.Diagnostics;
using Identity = YeManInputHost.Diagnostics.MsiSdkDeclaredIdentityDiagnostic;
using Release = YeManInputHost.Diagnostics.MsiNonSdReleaseDiagnostic;
using Census = YeManInputHost.Diagnostics.MsiPnpCensusDiagnostic;

public static class Program
{
    private const string IndexName = "<Index>k__BackingField", IdName = "<InstanceId>k__BackingField";
    private const string LockedMvid = "86aed9a3-c024-4596-b02c-650082ffb852";
    private const BindingFlags AllFields = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, WriteIndented = true };
    private static readonly HashSet<string> ProofNames = new(StringComparer.OrdinalIgnoreCase)
    { "exactOwnedIdsPinned", "classificationComplete", "exactOwnedAbsenceProven", "physicalReturnVerified", "steamConsumptionVerified" };
    private static readonly List<CaseResult> Results = [];
    private static readonly List<object> Captures = [];

    private sealed class FakeController(int index, string? instanceId)
    {
        internal int Index { get; } = index;
        internal string? InstanceId { get; } = instanceId;
    }
    private class UnsealedController
    {
        internal int Index { get; } = 1;
        internal string? InstanceId { get; } = "FAKE\\UNSEALED";
    }
    private sealed class GenericController<T>
    {
        internal int Index { get; } = 1;
        internal string? InstanceId { get; } = "FAKE\\GENERIC";
    }
    private readonly struct StructController
    {
        internal int Index { get; } = 1;
        internal string? InstanceId { get; } = "FAKE\\STRUCT";
        public StructController() { }
    }
    private class InheritedControllerBase
    {
        internal int Index { get; } = 1;
        internal string? InstanceId { get; } = "FAKE\\INHERITED";
    }
    private sealed class InheritedController : InheritedControllerBase { }

    // These getters exist only on emitted fake classes and must never be called.
    public static class GetterTrap
    {
        public static int Calls;
        public static int Index() { ++Calls; throw new InvalidOperationException("Fake Index getter invoked"); }
        public static string? Id() { ++Calls; throw new InvalidOperationException("Fake InstanceId getter invoked"); }
    }
    private sealed record FieldSpec(string Name, Type Type, FieldAttributes Attributes);
    private sealed record EmittedFake(object Controller, Identity.MetadataContract Contract);
    private sealed class CaseResult(string name)
    {
        public string Name { get; } = name;
        public int CheckCount { get; set; }
        public List<string> Failures { get; } = [];
        public bool Passed => Failures.Count == 0;
        internal void Check(bool condition, string description)
        {
            ++CheckCount;
            if (!condition) Failures.Add(description);
        }
        internal void Equal<T>(T expected, T actual, string description) =>
            Check(EqualityComparer<T>.Default.Equals(expected, actual), $"{description}: expected={expected}, actual={actual}");
    }
    private static void Case(string name, Action<CaseResult> body)
    {
        var result = new CaseResult(name);
        Results.Add(result);
        try { body(result); }
        catch (Exception error) { result.Check(false, $"Unhandled fixture exception: {error}"); }
    }
    private static Identity.MetadataContract ContractFor(Type type, string indexName = IndexName, string idName = IdName) =>
        new(type.FullName!, type.Module.ModuleVersionId, type.MetadataToken,
            type.GetField(indexName, AllFields)!.MetadataToken, type.GetField(idName, AllFields)!.MetadataToken);

    private static EmittedFake EmitFake(string suffix, FieldSpec? changedIndex = null, FieldSpec? changedId = null, string? name = null)
    {
        var module = AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("PureIdentityFake_" + suffix), AssemblyBuilderAccess.Run)
            .DefineDynamicModule("PureFakeModule");
        var builder = module.DefineType(name ?? "PureFake." + suffix, TypeAttributes.Public | TypeAttributes.Sealed);
        var ordinary = FieldAttributes.Private | FieldAttributes.InitOnly;
        var indexSpec = changedIndex ?? new(IndexName, typeof(int), ordinary);
        var idSpec = changedId ?? new(IdName, typeof(string), ordinary);
        var indexField = builder.DefineField(indexSpec.Name, indexSpec.Type, indexSpec.Attributes);
        var idField = builder.DefineField(idSpec.Name, idSpec.Type, idSpec.Attributes);
        var constructor = builder.DefineConstructor(MethodAttributes.Public, CallingConventions.Standard, [typeof(int), typeof(string)]);
        var il = constructor.GetILGenerator();
        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Call, typeof(object).GetConstructor(Type.EmptyTypes)!);
        if ((indexSpec.Attributes & FieldAttributes.Static) == 0)
        {
            il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Ldarg_1);
            if (indexSpec.Type == typeof(long)) il.Emit(OpCodes.Conv_I8);
            il.Emit(OpCodes.Stfld, indexField);
        }
        if ((idSpec.Attributes & FieldAttributes.Static) == 0)
        {
            il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Ldarg_2); il.Emit(OpCodes.Stfld, idField);
        }
        il.Emit(OpCodes.Ret);
        foreach (var item in new[] { (Name: "Index", Return: typeof(int), Trap: nameof(GetterTrap.Index)),
                                    (Name: "InstanceId", Return: typeof(string), Trap: nameof(GetterTrap.Id)) })
        {
            var getter = builder.DefineMethod("get_" + item.Name, MethodAttributes.Public | MethodAttributes.HideBySig | MethodAttributes.SpecialName,
                item.Return, Type.EmptyTypes);
            var getterIl = getter.GetILGenerator();
            getterIl.Emit(OpCodes.Call, typeof(GetterTrap).GetMethod(item.Trap)!); getterIl.Emit(OpCodes.Ret);
            builder.DefineProperty(item.Name, PropertyAttributes.None, item.Return, Type.EmptyTypes).SetGetMethod(getter);
        }
        var type = builder.CreateType()!;
        return new(Activator.CreateInstance(type, 17, "FAKE\\EMITTED")!, ContractFor(type, indexSpec.Name, idSpec.Name));
    }
    private static void ProofsFalse(CaseResult c, object receipt)
    {
        using var document = JsonDocument.Parse(JsonSerializer.Serialize(receipt, receipt.GetType(), JsonOptions));
        void Visit(JsonElement value)
        {
            if (value.ValueKind == JsonValueKind.Object)
                foreach (var property in value.EnumerateObject())
                {
                    if (ProofNames.Contains(property.Name)) c.Check(property.Value.ValueKind == JsonValueKind.False, property.Name + " must stay false");
                    Visit(property.Value);
                }
            else if (value.ValueKind == JsonValueKind.Array)
                foreach (var item in value.EnumerateArray()) Visit(item);
        }
        Visit(document.RootElement);
    }
    private static Identity.Receipt Require(CaseResult c, Identity.Receipt? receipt)
    {
        c.Check(receipt is not null, "capture returns a diagnostic receipt");
        if (receipt is null) throw new InvalidOperationException("No receipt available");
        c.Equal(1, receipt.Version, "receipt version");
        ProofsFalse(c, receipt);
        return receipt;
    }
    private static Identity.Receipt Capture(CaseResult c, object controller, Identity.MetadataContract? contract = null, bool verified = true)
    {
        contract ??= ContractFor(controller.GetType());
        var before = GetterTrap.Calls;
        var receipt = Require(c, Identity.TryCaptureAgainstContract(controller, verified, 123, contract));
        c.Equal(before, GetterTrap.Calls, "no fake property getter invoked");
        c.Equal(123L, receipt.CapturedAtTick, "input capture tick preserved");
        Captures.Add(new { test = c.Name, fixtureType = controller.GetType().FullName, contract, receipt });
        return receipt;
    }
    private static void Unknown(CaseResult c, Identity.Receipt receipt, string state, bool verified = true)
    {
        c.Equal(state, receipt.ObservationState, "explicit unknown reason");
        c.Equal(verified, receipt.AssetVerified, "asset verification state");
        c.Check(!receipt.MetadataMatched && !receipt.IndexCaptured && receipt.SdkControllerIndex is null, "unknown metadata never retains controller index");
        c.Check(!receipt.InstanceIdCaptured && receipt.SdkSelectedInstanceId is null && !receipt.InstanceIdComplete && !receipt.InstanceIdTruncated,
            "unknown metadata never retains instance identity");
    }
    private static void ValueCase(string name, int index, string? original, string? expected, string state, bool complete, bool truncated)
    {
        Case(name, c =>
        {
            var controller = new FakeController(index, original);
            var contract = ContractFor(controller.GetType());
            var receipt = Capture(c, controller, contract);
            c.Check(receipt.AssetVerified && receipt.MetadataMatched && receipt.IndexCaptured && receipt.InstanceIdCaptured,
                "exact fake contract admits ordinary constructor fields");
            c.Equal((int?)index, receipt.SdkControllerIndex, "SDK-declared index preserved, including negatives");
            c.Equal(expected, receipt.SdkSelectedInstanceId, "bounded ID exact code units");
            c.Equal(state, receipt.ObservationState, "ID observation state");
            c.Equal(complete, receipt.InstanceIdComplete, "ID completeness");
            c.Equal(truncated, receipt.InstanceIdTruncated, "ID truncation flag");
            c.Check(receipt.Scope.Contains("main or companion", StringComparison.Ordinal) &&
                receipt.Scope.Contains("no complete ownership", StringComparison.Ordinal), "scope does not claim primary or owned identity");
            foreach (var field in controller.GetType().GetFields(AllFields))
                c.Check(field.IsPrivate && field.IsInitOnly && !field.IsStatic && field.DeclaringType == controller.GetType(), "actual fake fields private readonly instance");
            c.Equal(index, (int)controller.GetType().GetField(IndexName, AllFields)!.GetValue(controller)!, "capture did not modify index field");
            c.Equal(original, (string?)controller.GetType().GetField(IdName, AllFields)!.GetValue(controller), "capture did not modify original ID field");
            if (expected is not null)
            {
                c.Check(expected.Length <= Identity.InstanceIdLimit, "bounded ID code units");
                _ = new UTF8Encoding(false, true).GetByteCount(receipt.SdkSelectedInstanceId!);
                c.Check(true, "captured ID is well-formed UTF16 for strict UTF8");
            }
        });
    }

    private static Census.Receipt CensusFixture(CaseResult c, long tick, string candidate)
    {
        using var requested = new Census.Scope(true);
        using var observation = new Census.ObservationScope(tick);
        c.Check(observation.Capture is not null, "pure census collector enabled");
        var capture = observation.Capture!;
        capture.TryNodeEnumerated(); capture.TryNamedNode(); capture.TryEnumerationEnd();
        capture.TryCandidates("fake-observed-candidate", [candidate]);
        var receipt = capture.TryComplete(true, tick + 1);
        c.Check(receipt is not null, "pure census receipt created");
        ProofsFalse(c, receipt!);
        return receipt!;
    }
    private static Release.Snapshot Snapshot(Census.Receipt? census = null) => new(true, 0, 0, 0, 0, 0, true, "fake count-only data") { ExistingPnpCensus = census };
    private static Release.Receipt CompleteRelease(CaseResult c, Release diagnostic, Census.Receipt? before = null, Census.Receipt? poll = null)
    {
        diagnostic.TrySetBeforeCreateCensus(before);
        diagnostic.TryBegin(Snapshot(), 200, true, true);
        diagnostic.TryObserveExistingPoll(Snapshot(poll), 201, 202);
        diagnostic.TryComplete(true, 203);
        c.Check(diagnostic.Current is not null, "release receipt created");
        var receipt = diagnostic.Current!;
        ProofsFalse(c, receipt);
        return receipt;
    }
    private static void RunCases()
    {
        ValueCase("exact-readonly-constructor-fields", 7, "ROOT\\FAKE_MAIN\\001", "ROOT\\FAKE_MAIN\\001", "sdk-declaration-captured", true, false);
        ValueCase("null-ID-preserved-unavailable", 0, null, null, "sdk-null-instance-id", false, false);
        ValueCase("empty-ID-preserved-unavailable", 0, "", "", "sdk-empty-instance-id", false, false);
        ValueCase("whitespace-ID-preserved-unavailable", 0, " \t\r\n", " \t\r\n", "sdk-empty-instance-id", false, false);
        ValueCase("negative-index-is-only-a-declaration", -23, "FAKE\\COMPANION", "FAKE\\COMPANION", "sdk-declaration-captured", true, false);
        ValueCase("long-ID-bounded", 4, new string('A', 400), new string('A', 256), "sdk-instance-id-truncated", false, true);
        ValueCase("surrogate-split-at-limit-removed", 4, new string('B', 255) + "\U0001F642" + "tail", new string('B', 255), "sdk-instance-id-truncated", false, true);
        ValueCase("surrogate-pair-within-limit-preserved", 4, new string('C', 254) + "\U0001F642" + "tail", new string('C', 254) + "\U0001F642", "sdk-instance-id-truncated", false, true);
        ValueCase("exact-256-units-complete", 4, new string('D', 254) + "\U0001F642", new string('D', 254) + "\U0001F642", "sdk-declaration-captured", true, false);
        ValueCase("lone-high-surrogate-explicit-unknown", 1, "FAKE\uD800", null, "sdk-instance-id-invalid-utf16", false, false);
        ValueCase("lone-low-surrogate-explicit-unknown", 1, "FAKE\uDC00", null, "sdk-instance-id-invalid-utf16", false, false);
        ValueCase("invalid-UTF16-discarded-suffix-keeps-prefix-incomplete", 1, new string('E', 300) + "\uD800", new string('E', 256), "sdk-instance-id-truncated", false, true);
        ValueCase("invalid-UTF16-in-retained-prefix-explicit-unknown", 1, new string('F', 100) + "\uD800" + new string('G', 200), null, "sdk-instance-id-invalid-utf16", false, true);
        ValueCase("long-whitespace-stays-truncated-not-complete", 1, new string(' ', 400), new string(' ', 256), "sdk-instance-id-truncated", false, true);

        var good = new FakeController(99, "FAKE\\MUST_NOT_LEAK");
        var goodContract = ContractFor(good.GetType());
        Case("asset-not-verified", c => Unknown(c, Capture(c, good, goodContract, false), "asset-not-verified", false));
        Case("missing-controller", c => Unknown(c, Require(c, Identity.TryCaptureAgainstContract(null, true, 123, goodContract)), "controller-object-missing"));
        var mismatches = new (string Name, Identity.MetadataContract Contract, string State)[]
        {
            ("type-name", goodContract with { TypeName = goodContract.TypeName + "_Wrong" }, "runtime-type-metadata-mismatch"),
            ("module-MVID", goodContract with { ModuleMvid = Guid.Empty }, "runtime-type-metadata-mismatch"),
            ("type-token", goodContract with { TypeToken = goodContract.TypeToken ^ 1 }, "runtime-type-metadata-mismatch"),
            ("index-field-token", goodContract with { IndexFieldToken = goodContract.InstanceIdFieldToken }, "backing-field-metadata-mismatch"),
            ("ID-field-token", goodContract with { InstanceIdFieldToken = goodContract.IndexFieldToken }, "backing-field-metadata-mismatch")
        };
        foreach (var mismatch in mismatches)
            Case("reject-contract-" + mismatch.Name, c => Unknown(c, Capture(c, good, mismatch.Contract), mismatch.State));
        Case("locked-capture-rejects-fake-even-with-claimed-locked-MVID", c =>
            Unknown(c, Require(c, Identity.TryCapture(good, true, LockedMvid, 123)), "runtime-type-metadata-mismatch"));
        Case("locked-capture-rejects-fake-module-MVID", c =>
            Unknown(c, Require(c, Identity.TryCapture(good, true, good.GetType().Module.ModuleVersionId.ToString(), 123)), "verified-mvid-mismatch"));
        Case("locked-capture-rejects-malformed-verified-MVID", c =>
            Unknown(c, Require(c, Identity.TryCapture(good, true, "not-a-guid", 123)), "verified-mvid-mismatch"));

        var attributes = FieldAttributes.Private | FieldAttributes.InitOnly;
        foreach (var field in new[] { "Index", "InstanceId" })
        {
            var fieldName = field == "Index" ? IndexName : IdName;
            var fieldType = field == "Index" ? typeof(int) : typeof(string);
            var mutations = new (string Name, FieldSpec Spec)[]
            {
                ("private", new(fieldName, fieldType, FieldAttributes.Public | FieldAttributes.InitOnly)),
                ("readonly", new(fieldName, fieldType, FieldAttributes.Private)),
                ("instance", new(fieldName, fieldType, attributes | FieldAttributes.Static)),
                ("type", new(fieldName, field == "Index" ? typeof(long) : typeof(object), attributes)),
                ("name", new(fieldName + "_Wrong", fieldType, attributes))
            };
            foreach (var mutation in mutations)
                Case("reject-" + field + "-field-" + mutation.Name, c =>
                {
                    var fake = EmitFake(field + "_" + mutation.Name,
                        field == "Index" ? mutation.Spec : null, field == "InstanceId" ? mutation.Spec : null);
                    Unknown(c, Capture(c, fake.Controller, fake.Contract), "backing-field-metadata-mismatch");
                });
        }
        Case("fake-poison-getters-not-invoked", c =>
        {
            var fake = EmitFake("PoisonGetters");
            var receipt = Capture(c, fake.Controller, fake.Contract);
            c.Check(receipt.MetadataMatched && receipt.IndexCaptured && receipt.InstanceIdComplete, "exact emitted fake fields capture succeeds");
            c.Equal((int?)17, receipt.SdkControllerIndex, "actual emitted index read");
            c.Equal("FAKE\\EMITTED", receipt.SdkSelectedInstanceId, "actual emitted ID read");
            c.Equal(0, GetterTrap.Calls, "poison getter invocation count remains zero");
        });
        Case("locked-capture-rejects-spoofed-SDK-type-name", c =>
        {
            var fake = EmitFake("SpoofedName", name: "HIDMaestro.HMController");
            c.Check(Capture(c, fake.Controller, fake.Contract).MetadataMatched, "same-name fake admitted only by its real fixture contract");
            Unknown(c, Require(c, Identity.TryCapture(fake.Controller, true, LockedMvid, 123)), "runtime-type-metadata-mismatch");
        });
        foreach (var item in new (string Name, object Value)[] { ("nonsealed", new UnsealedController()),
            ("generic", new GenericController<int>()), ("struct", new StructController()) })
            Case("reject-runtime-type-" + item.Name, c => Unknown(c, Capture(c, item.Value), "runtime-type-metadata-mismatch"));
        Case("inherited-fields-not-admitted-as-declared", c =>
        {
            var type = typeof(InheritedController);
            var contract = new Identity.MetadataContract(type.FullName!, type.Module.ModuleVersionId, type.MetadataToken,
                typeof(InheritedControllerBase).GetField(IndexName, AllFields)!.MetadataToken,
                typeof(InheritedControllerBase).GetField(IdName, AllFields)!.MetadataToken);
            Unknown(c, Capture(c, new InheritedController(), contract), "backing-field-metadata-mismatch");
        });
        Case("sequential-capture-never-reuses-earlier-identity", c =>
        {
            var first = Capture(c, good);
            var second = Capture(c, new FakeController(-1, null));
            c.Equal("FAKE\\MUST_NOT_LEAK", first.SdkSelectedInstanceId, "first captured value stable");
            c.Check(second.SdkSelectedInstanceId is null && !second.InstanceIdComplete, "second capture cannot borrow earlier ID");
            Unknown(c, Capture(c, good, goodContract with { ModuleMvid = Guid.Empty }), "runtime-type-metadata-mismatch");
        });
        Case("release-receipt-propagates-identity-and-census", c =>
        {
            var identity = Capture(c, new FakeController(-2, "ROOT\\FAKE_DECLARED\\九"));
            var before = CensusFixture(c, 100, "FAKE\\BEFORE");
            var poll = CensusFixture(c, 201, "FAKE\\POLL");
            var release = new Release("xbox360");
            release.TrySetSdkDeclaredIdentity(identity);
            var receipt = CompleteRelease(c, release, before, poll);
            c.Check(ReferenceEquals(identity, receipt.SdkDeclaredIdentity), "release retains exact captured declaration receipt");
            c.Check(ReferenceEquals(before, receipt.BeforeCreateCensus), "before-create census retained independently");
            c.Check(ReferenceEquals(poll, receipt.FreshObservations[0].Observation.ExistingPnpCensus), "poll census retained independently");
            const string response = "{\"ok\":true,\"status\":\"existing\",\"count\":0,\"message\":\"九\"}";
            var output = release.TryAppend(response, 65536);
            using var original = JsonDocument.Parse(response);
            using var document = JsonDocument.Parse(output);
            foreach (var property in original.RootElement.EnumerateObject())
                c.Equal(property.Value.GetRawText(), document.RootElement.GetProperty(property.Name).GetRawText(), "original response field " + property.Name + " preserved");
            var evidence = document.RootElement.GetProperty(Release.Field);
            c.Equal("ROOT\\FAKE_DECLARED\\九", evidence.GetProperty("sdkDeclaredIdentity").GetProperty("sdkSelectedInstanceId").GetString(), "wire identity propagates without primary claim");
            c.Equal(-2, evidence.GetProperty("sdkDeclaredIdentity").GetProperty("sdkControllerIndex").GetInt32(), "wire negative declared index preserved");
            c.Equal("FAKE\\BEFORE", evidence.GetProperty("beforeCreateCensus").GetProperty("observedCandidates")[0].GetProperty("instanceId").GetString(), "wire before-create census preserved");
            c.Check(!Census.Requested && Census.Current is null, "census fixture scopes restore without leak");
            Captures.Add(new { test = c.Name, releaseReceipt = receipt, response = output });
        });
        Case("identity-increase-obeys-UTF8-budget-and-omission", c =>
        {
            var release = new Release("dualsense");
            var identity = Capture(c, new FakeController(2, new string('界', 256)));
            release.TrySetSdkDeclaredIdentity(identity);
            CompleteRelease(c, release);
            const string response = "{\"ok\":false,\"status\":\"unchanged\",\"message\":\"界\"}";
            var full = release.TryAppend(response, 65536);
            var fullBytes = Encoding.UTF8.GetByteCount(full);
            c.Equal(full, release.TryAppend(response, fullBytes), "full receipt admitted at exact UTF8 byte budget");
            var omitted = release.TryAppend(response, fullBytes - 1);
            using var omission = JsonDocument.Parse(omitted);
            var minimal = omission.RootElement.GetProperty(Release.Field);
            c.Equal("omitted-message-budget", minimal.GetProperty("observationState").GetString(), "one byte below full budget emits explicit omission");
            c.Check(!minimal.TryGetProperty("sdkDeclaredIdentity", out _), "omission cannot retain stale identity");
            c.Check(!minimal.GetProperty("exactOwnedAbsenceProven").GetBoolean(), "omission never proves absence");
            var omissionBytes = Encoding.UTF8.GetByteCount(omitted);
            c.Equal(omitted, release.TryAppend(response, omissionBytes), "minimal omission fits exact byte budget");
            c.Equal(response, release.TryAppend(response, omissionBytes - 1), "insufficient omission budget returns exact original response");
            c.Equal(response, release.TryAppend(response, Encoding.UTF8.GetByteCount(response)), "base-response budget cannot expose identity");
            c.Check(ReferenceEquals(identity, release.Current!.SdkDeclaredIdentity), "serialization budget does not mutate current declaration");
        });
        Case("new-release-diagnostic-does-not-reuse-identity", c =>
        {
            var old = new Release("elite");
            var oldIdentity = Capture(c, new FakeController(8, "FAKE\\OLD"));
            old.TrySetSdkDeclaredIdentity(oldIdentity);
            var oldReceipt = CompleteRelease(c, old);
            var fresh = new Release("elite");
            var freshReceipt = CompleteRelease(c, fresh);
            c.Check(freshReceipt.SdkDeclaredIdentity is null && freshReceipt.BeforeCreateCensus is null && freshReceipt.Previous is null,
                "new helper has no identity/census/prior receipt from old helper");
            using var serialized = JsonDocument.Parse(fresh.TryAppend("{}", 65536));
            c.Check(serialized.RootElement.GetProperty(Release.Field).GetProperty("sdkDeclaredIdentity").ValueKind == JsonValueKind.Null,
                "new helper wire receipt has explicit null identity");
            c.Check(ReferenceEquals(oldIdentity, oldReceipt.SdkDeclaredIdentity), "old receipt remains unchanged");
            fresh.TrySetSdkDeclaredIdentity(oldIdentity);
            fresh.TrySetSdkDeclaredIdentity(null);
            c.Check(CompleteRelease(c, fresh).SdkDeclaredIdentity is null, "explicit null update clears pending identity");
            var unavailable = Capture(c, new FakeController(9, null));
            fresh.TrySetSdkDeclaredIdentity(unavailable);
            var third = CompleteRelease(c, fresh);
            c.Check(third.SdkDeclaredIdentity!.SdkSelectedInstanceId is null && !third.SdkDeclaredIdentity.InstanceIdComplete,
                "unavailable replacement does not borrow old ID");
            using var final = JsonDocument.Parse(JsonSerializer.Serialize(third, JsonOptions));
            c.Check(!final.RootElement.GetProperty("previous").TryGetProperty("sdkDeclaredIdentity", out _), "previous brief has no recursive identity");
        });
    }
    private static string SourceFile([CallerFilePath] string path = "") => path;
    private static object[] SourceBindings()
    {
        var fixture = Path.GetFullPath(SourceFile());
        var directory = Path.GetDirectoryName(fixture)!;
        var repo = Path.GetFullPath(Path.Combine(directory, "..", ".."));
        return new[] { Path.Combine(repo, "InputHost", "MsiSdkDeclaredIdentityDiagnostic.cs"),
            Path.Combine(repo, "InputHost", "MsiNonSdReleaseDiagnostic.cs"), Path.Combine(repo, "InputHost", "MsiPnpCensusDiagnostic.cs"),
            fixture, Path.Combine(directory, "MsiSdkDeclaredIdentityDiagnosticSelfTest.csproj"),
            Path.Combine(repo, "tools", "verify-msi-sdk-declared-identity.mjs") }
            .Select(path => (object)new { path, sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))) }).ToArray();
    }
    private static int Main(string[] args)
    {
        if (args.Length != 1) { Console.Error.WriteLine("usage: MsiSdkDeclaredIdentityDiagnosticSelfTest report.json"); return 2; }
        RunCases();
        var report = new
        {
            utc = DateTimeOffset.UtcNow, passed = Results.All(result => result.Passed),
            scope = "Actual linked diagnostic helpers and ordinary/emitted BCL fake controller classes only; no Host or SDK execution.",
            caseCount = Results.Count, checkCount = Results.Sum(result => result.CheckCount),
            failureCount = Results.Sum(result => result.Failures.Count), cases = Results,
            captures = Captures, fakeContract = ContractFor(typeof(FakeController)), sourceBindings = SourceBindings(),
            propertyGetterCalls = GetterTrap.Calls, sdkReferences = 0, sdkAssembliesLoaded = 0,
            realSdkObjectReads = 0, liveDeviceApis = 0
        };
        var reportPath = Path.GetFullPath(args[0]);
        Directory.CreateDirectory(Path.GetDirectoryName(reportPath)!);
        File.WriteAllText(reportPath, JsonSerializer.Serialize(report, JsonOptions) + "\n", new UTF8Encoding(false));
        Console.WriteLine($"PURE_SDK_DECLARED_IDENTITY passed={report.passed} cases={report.caseCount} checks={report.checkCount} failures={report.failureCount} propertyGetters={GetterTrap.Calls}");
        foreach (var failed in Results.Where(result => !result.Passed))
            Console.Error.WriteLine(failed.Name + ": " + string.Join(" | ", failed.Failures));
        return report.passed ? 0 : 1;
    }
}
