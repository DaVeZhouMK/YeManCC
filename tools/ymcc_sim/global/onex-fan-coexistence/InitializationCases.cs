using System.IO;
using System.Reflection;
using System.Text.Json;
using HandheldCompanion.Devices;
using HandheldCompanion.Managers;
using Mono.Cecil;
using Mono.Cecil.Cil;
using Trace = ConsumerFixture.Trace;

static partial class Harness
{
    // Locate the exact delegate wired by the compiled factory method, rather
    // than choosing a similarly named reflection entry or a test-side model.
    static MethodDefinition InitializationCallback(TypeDefinition backend)
    {
        var factory = backend.Methods.Single(m => m.Name == "LoadAssemblyAndFactory");
        var code = factory.Body.Instructions;
        var stage = code.Single(i => i.OpCode == OpCodes.Ldstr && Equals(i.Operand, "device.initialize"));
        var cursor = stage.Next;
        MethodReference? callback = null;
        for (; cursor is not null; cursor = cursor.Next)
        {
            if (cursor.OpCode == OpCodes.Ldftn)
            {
                Check(callback is null, "Only one device.initialize delegate is wired");
                callback = (MethodReference)cursor.Operand;
            }
            if (cursor.Operand is MethodReference call && call.Name == "RunHcInitializationStage") break;
            Check(cursor.OpCode != OpCodes.Ret, "Initialization stage cannot bypass the formal stage consumer");
        }
        Check(cursor is not null && callback is not null && (callback.DeclaringType.FullName == backend.FullName || callback.DeclaringType.DeclaringType?.FullName == backend.FullName),
            "Factory initialization action is bound to the actual backend instance");
        Check(callback!.Name.Contains("LoadAssemblyAndFactory", StringComparison.Ordinal), "Callback belongs to the actual factory method");
        var method = callback.Resolve();
        Check(method.HasBody && method.Parameters.Count == 0 && !method.IsStatic,
            "Initialization callback is the actual parameterless instance lambda");
        return method;
    }

    static Action InitializationAction(object backend)
    {
        using var image = AssemblyDefinition.ReadAssembly(Candidate.Location);
        var definition = InitializationCallback(image.MainModule.GetType("YeManFanHost.RealHcBackend"));
        var callback = (MethodInfo)Candidate.ManifestModule.ResolveMethod(definition.MetadataToken.ToInt32())!;
        if (callback.DeclaringType == backend.GetType()) return callback.CreateDelegate<Action>(backend);
        // The compiler shares a display class across factory stages. This
        // callback may read only its captured backend, not assembly-load locals.
        var closureType = callback.DeclaringType!;
        var capturedFields = definition.Body.Instructions.Where(i => i.Operand is FieldReference f && f.DeclaringType.FullName == definition.DeclaringType.FullName)
            .Select(i => (FieldReference)i.Operand).DistinctBy(f => f.FullName).ToArray();
        Check(capturedFields.Length == 1 && capturedFields[0].FieldType.FullName == backend.GetType().FullName,
            "Initialization closure reads only its actual captured backend");
        var owner = closureType.GetFields(All).Single(f => f.Name == capturedFields[0].Name && f.FieldType == backend.GetType());
        var closure = System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(closureType);
        owner.SetValue(closure, backend);
        return callback.CreateDelegate<Action>(closure);
    }
    static void InitializationStage(object backend, Action<string, object?>? diagnostic = null)
    {
        var stage = backend.GetType().GetMethod("RunHcInitializationStage", All)!;
        try { stage.Invoke(null, ["device.initialize", InitializationAction(backend), diagnostic]); }
        catch (TargetInvocationException error) when (error.InnerException is not null) { throw error.InnerException; }
    }
    sealed class InitializationObserver : IDevice
    {
        public int Count;
        public bool? FirstStart, NewUpdate;
        public Exception? Failure;
        public override void Initialize(bool firstStart, bool newUpdate)
        {
            Count++; FirstStart = firstStart; NewUpdate = newUpdate;
            if (Failure is not null) throw Failure;
        }
    }

    static void RunInitializationCases(string originalHc)
    {
        Run("initialization-original-methods-match-pinned-pe", () =>
        {
            using var original = AssemblyDefinition.ReadAssembly(originalHc);
            using var fixture = AssemblyDefinition.ReadAssembly(typeof(IDevice).Assembly.Location);
            foreach (var name in new[] { "HandheldCompanion.Devices.IDevice", "HandheldCompanion.Devices.AYANEOFlipDS", "HandheldCompanion.Devices.AYANEO.AYANEOFlip1SDS" })
            {
                var a = original.MainModule.GetType(name).Methods.Single(m => m.Name == "Initialize");
                var b = fixture.MainModule.GetType(name).Methods.Single(m => m.Name == "Initialize");
                Check(OldBinaryProbe.Ops(a).SequenceEqual(OldBinaryProbe.Ops(b)), "Complete Initialize opcodes match the pinned PE: " + name);
                Check(a.Body.Instructions.Where(i => i.Operand is MethodReference).Select(i => ((MethodReference)i.Operand).FullName)
                    .SequenceEqual(b.Body.Instructions.Where(i => i.Operand is MethodReference).Select(i => ((MethodReference)i.Operand).FullName)), "Original initialization call signatures match: " + name);
                Check(a.Body.Instructions.Where(i => i.Operand is FieldReference).Select(i => ((FieldReference)i.Operand).FullName)
                    .SequenceEqual(b.Body.Instructions.Where(i => i.Operand is FieldReference).Select(i => ((FieldReference)i.Operand).FullName)), "Original initialization field signatures match: " + name);
                Check(a.Body.ExceptionHandlers.Count == b.Body.ExceptionHandlers.Count, "Original initialization exception regions match: " + name);
            }
        });
        Run("initialization-actual-factory-stage-wiring", () =>
        {
            using var image = AssemblyDefinition.ReadAssembly(Candidate.Location);
            var backend = image.MainModule.GetType("YeManFanHost.RealHcBackend");
            var action = InitializationCallback(backend);
            Check(action.Body.Instructions.Count(i => i.OpCode == OpCodes.Ldstr && Equals(i.Operand, "Initialize")) == 1,
                "Compiled factory delegates exactly one Initialize dispatch");
            var call = action.Body.Instructions.Single(i => i.Operand is MethodReference m && m.Name == "Invoke");
            Check(((MethodReference)call.Operand).DeclaringType.FullName == backend.FullName, "Actual callback dispatches through the product reflection boundary");
        });
        Run("initialization-actual-callback-arguments-and-virtual-dispatch", () =>
        {
            var device = new InitializationObserver(); var backend = Backend(device);
            InitializationStage(backend);
            Check(device.Count == 1 && device.FirstStart is false && device.NewUpdate is false,
                "Actual factory callback must dispatch Initialize(false,false) exactly once");
            Check(Trace.DriverInitializations == 0 && Trace.SerialOpenAttempts == 0 && Trace.Calls.Count == 0,
                "Read-only initialization dispatch cannot enter a transport, fan or settings boundary");
        });
        foreach (var deviceType in new[] { typeof(IDevice), typeof(AYANEOFlipDS), typeof(HandheldCompanion.Devices.AYANEO.AYANEOFlip1SDS) })
            Run("initialization-pinned-no-first-start:" + deviceType.Name, () =>
            {
                var device = Model(deviceType); var backend = Backend(device);
                InitializationStage(backend);
                Check(!Trace.Calls.Any(c => c.StartsWith("Settings.Set:", StringComparison.Ordinal)),
                    "Host handshake cannot activate pinned FirstStart settings: " + deviceType.Name);
                Check(Trace.DriverInitializations == 0 && Trace.SerialOpenAttempts == 0 && !IDevice.LibraryInitialized,
                    "Pinned Initialize does not open fan/serial resources");
            });
        Run("initialization-first-start-leaf-is-observable", () =>
        {
            // This positive control invokes only the extracted original methods;
            // SettingsManager.SetProperty is an explicit inert fixture terminal.
            new AYANEOFlipDS().Initialize(true, false);
            Check(Trace.Calls.Count(c => c.StartsWith("Settings.Set:")) == 4, "Original FlipDS first-start branch reaches four inert settings writes");
            Trace.Calls.Clear(); new HandheldCompanion.Devices.AYANEO.AYANEOFlip1SDS().Initialize(true, false);
            Check(Trace.Calls.Count(c => c.StartsWith("Settings.Set:")) == 5, "Original Flip1SDS calls base then its own inert settings write");
        });
        foreach (var diagnosticThrows in new[] { false, true })
            Run("initialization-original-failure-is-preserved:" + diagnosticThrows, () =>
            {
                var error = new IOException("INITIALIZE_ORIGINAL_FAILURE");
                var device = new InitializationObserver { Failure = error }; var backend = Backend(device);
                var logs = new List<(string Name, string Detail)>();
                var observed = Reject(() => InitializationStage(backend, (name, detail) =>
                {
                    logs.Add((name, JsonSerializer.Serialize(detail)));
                    if (diagnosticThrows) throw new InvalidOperationException("DIAGNOSTIC_ONLY_FAILURE");
                }), "INITIALIZE_ORIGINAL_FAILURE");
                Check((ReferenceEquals(observed, error) || observed is TargetInvocationException wrapper && ReferenceEquals(wrapper.InnerException, error)) && device.Count == 1, "Initialization stage preserves the actual original failure chain and never replays it");
                Check(logs.Count == 1 && logs[0].Name == "hc.initialization.failure" && logs[0].Detail.Contains("device.initialize"),
                    "Actual initialization stage retains its structured phase diagnostic");
            });
    }

    static int MutateInitializationFirstStart(string[] args)
    {
        var source = Path.GetFullPath(args[0]); var target = Path.GetFullPath(args[1]);
        using var image = AssemblyDefinition.ReadAssembly(source);
        var method = InitializationCallback(image.MainModule.GetType("YeManFanHost.RealHcBackend"));
        var boxes = method.Body.Instructions.Where(i => i.OpCode == OpCodes.Box && i.Operand is TypeReference t && t.FullName == "System.Boolean").ToArray();
        Check(boxes.Length == 2 && boxes.All(i => i.Previous.OpCode == OpCodes.Ldc_I4_0), "Exact two false initialization arguments are required for the fault");
        boxes[0].Previous.OpCode = OpCodes.Ldc_I4_1;
        Directory.CreateDirectory(Path.GetDirectoryName(target)!); image.Write(target);
        using var original = AssemblyDefinition.ReadAssembly(source); using var after = AssemblyDefinition.ReadAssembly(target);
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> list) => list.SelectMany(t => new[] { t }.Concat(Types(t.NestedTypes)));
        static string Canon(MethodDefinition m) => m.FullName + "\n" + m.Attributes + "\n" + (m.HasBody ? string.Join("\n", m.Body.Instructions.Select(i => i.OpCode.Name + " " + i.Operand)) + "\n" + string.Join("\n", m.Body.ExceptionHandlers.Select(h => h.ToString())) : "no-body");
        var a = Types(original.MainModule.Types).SelectMany(t => t.Methods).ToDictionary(m => m.FullName, Canon);
        var b = Types(after.MainModule.Types).SelectMany(t => t.Methods).ToDictionary(m => m.FullName, Canon);
        Check(a.Keys.Order().SequenceEqual(b.Keys.Order()), "Initialization fault cannot add or remove methods");
        var changed = a.Keys.Where(k => a[k] != b[k]).ToArray();
        Check(changed.Length == 1 && changed[0] == method.FullName, "Only the actual initialization callback argument changes");
        File.WriteAllText(args[2], JsonSerializer.Serialize(new { status = "PASS", sourceSha256 = Hash(source), faultSha256 = Hash(target), changedMethods = changed, mutation = "Original factory callback: FirstStart false becomes true; all other calls and guards retained", nativeIo = false }));
        return 0;
    }
}
