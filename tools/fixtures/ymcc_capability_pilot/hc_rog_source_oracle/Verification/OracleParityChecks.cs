using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Ymcc.CapabilityPilot.HcReference;
using ActualWriter = YeManFanBackend.Asus.AsusFanCurveWriter;
using Oracle = Ymcc.CapabilityPilot.HcReference.HcRogSourceOracle;

namespace Ymcc.CapabilityPilot.HcReference.Verification;

internal sealed record WriteEvidence(int Ordinal, string Fan, string CurveHex, int BufferIdentity, int SyntheticReturnValue);

internal sealed class CaseEvidence
{
    public required string Name { get; init; }
    public required string Category { get; init; }
    public bool Passed { get; set; }
    public string? Error { get; set; }
    public double[]? Input { get; set; }
    public string? OracleCurveHex { get; set; }
    public string? ActualCurveHex { get; set; }
    public WriteEvidence[] OracleWrites { get; set; } = Array.Empty<WriteEvidence>();
    public WriteEvidence[] ActualWrites { get; set; } = Array.Empty<WriteEvidence>();
}

internal static class OracleParityChecks
{
    private static readonly List<CaseEvidence> Cases = new();
    private const string Temps = "141E28323C46505A";
    private const string DefaultCpu = "3A3D4044484D51620811161A22293045";
    private const string DefaultGpu = "3A3D4044484D51620C161D1F262D344A";

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static void Run(string name, string category, Action<CaseEvidence> test)
    {
        var evidence = new CaseEvidence { Name = name, Category = category };
        try { test(evidence); evidence.Passed = true; }
        catch (Exception exception) { evidence.Error = exception.ToString(); }
        Cases.Add(evidence);
    }

    private static WriteEvidence[] Snapshot(RecordedFanCurveRun run) => run.Writes.Select(write =>
        new WriteEvidence(write.Ordinal, write.Fan, Convert.ToHexString(write.Curve), write.BufferIdentity, write.SyntheticReturnValue)).ToArray();

    private static void CompareWrites(CaseEvidence evidence, RecordedFanCurveRun oracle, RecordedFanCurveRun actual, bool software)
    {
        evidence.OracleWrites = Snapshot(oracle);
        evidence.ActualWrites = Snapshot(actual);
        Require(evidence.OracleWrites.SequenceEqual(evidence.ActualWrites), "Captured call sequence, curve bytes or input-array identity differs.");
        Require(actual.LegacyReturnValue == 1003, "Actual writer must return the final Mid call's synthetic sentinel.");
        Require(oracle.LegacyReturnValue is null, "The extracted HC methods return void, not a fabricated legacy integer.");
        Require(oracle.Writes.Select(write => write.Fan).SequenceEqual(new[] { "CPU", "GPU", "Mid" }), "Expected CPU/GPU/Mid order.");
        Require(oracle.Writes.Select(write => write.BufferIdentity).SequenceEqual(software ? new[] { 1, 1, 1 } : new[] { 1, 2, 1 }), "Original source array aliasing was not retained.");
    }

    private static void CurveCase(string name, string category, double[]? input, string? expected = null)
    {
        Run(name, category, evidence =>
        {
            evidence.Input = input;
            double[]? before = input is null ? null : (double[])input.Clone();
            byte[] oracle = Oracle.ConvertCurve(input);
            byte[] actual = ActualWriter.ToAsusCurve(input);
            evidence.OracleCurveHex = Convert.ToHexString(oracle);
            evidence.ActualCurveHex = Convert.ToHexString(actual);
            Require(oracle.SequenceEqual(actual), "Conversion differs from the extracted HC method.");
            if (expected is not null)
                Require(evidence.OracleCurveHex == expected, "Independent expected bytes differ.");
            var referenceRun = Oracle.ApplySoftware(input);
            var actualRun = CurveWriteCapture.CaptureReturning(() => ActualWriter.ApplySoftwareCurve(ActualWriter.ToAsusCurve(input)));
            CompareWrites(evidence, referenceRun, actualRun, true);
            Require(referenceRun.Writes.All(write => write.Curve.SequenceEqual(oracle)), "Software branch did not submit the converted curve to all fans.");
            Require(input is null || input.SequenceEqual(before!), "Conversion or recording modified the input fan-speed vector.");
        });
    }

    private static string HashFile(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)));

    private static string HashResource(Assembly assembly, string name)
    {
        using Stream stream = assembly.GetManifestResourceStream(name) ?? throw new InvalidOperationException($"Missing compiled source resource: {name}");
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    public static int Main(string[] args)
    {
        if (args.Length != 2)
            throw new ArgumentException("Expected fixture directory and evidence directory.");
        string fixture = Path.GetFullPath(args[0]);
        string evidenceRoot = Path.GetFullPath(args[1]);
        Require(evidenceRoot.StartsWith(fixture + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase), "Evidence must remain inside this fixture.");
        string repo = Path.GetFullPath(Path.Combine(fixture, "../../../.."));
        string actualWriterPath = Path.Combine(repo, "FanLab/fan-backend/Asus/AsusFanCurveWriter.cs");
        string referenceSourcePath = Path.Combine(repo, "deps/handheldcompanion-runtime/source/Devices/ASUS/ROGAlly.cs");
        string generatedPath = Path.Combine(fixture, "Generated/HcRogSourceOracle.g.cs");
        string sourceHashBefore = HashFile(referenceSourcePath);
        string actualHashBefore = HashFile(actualWriterPath);
        string generatedHashBefore = HashFile(generatedPath);

        Run("source-and-compiled-input-bindings", "provenance", evidence =>
        {
            using var provenance = JsonDocument.Parse(File.ReadAllText(Path.Combine(fixture, "HcRogSourceOracle.provenance.json")));
            Require(sourceHashBefore == "04097428F3D62DE75F8160EB47398E1C61606F9D6681EB605D100B384814AA00", "ROG source changed.");
            Require(generatedHashBefore == provenance.RootElement.GetProperty("generatedSha256").GetString(), "Generated reference differs from provenance.");
            Require(HashResource(typeof(Oracle).Assembly, "Reference.HcRogSourceOracle.g.cs") == generatedHashBefore, "Compiled oracle source is stale.");
            Require(HashResource(Assembly.GetExecutingAssembly(), "Actual.AsusFanCurveWriter.cs") == actualHashBefore, "Compiled actual writer source is stale.");
        });

        CurveCase("normal-11-points", "normal11", Enumerable.Range(0, 11).Select(index => (double)index * 10).ToArray(), Temps + "141E28323C46505A");
        CurveCase("mapped-source-indices-2-through-9", "normal11", new double[] { 333, -333, 1, 2, 4, 8, 16, 32, 64, 100, -999 }, Temps + "0102040810204064");
        CurveCase("null-default-cpu", "invalidLength", null, DefaultCpu);
        foreach (int length in new[] { 0, 1, 8, 10, 12, 16 })
            CurveCase($"invalid-length-{length}", "invalidLength", Enumerable.Repeat(100d, length).ToArray(), DefaultCpu);
        CurveCase("all-zero", "boundary", new double[11], Temps + new string('0', 16));
        CurveCase("all-100-before-acpi", "boundary", Enumerable.Repeat(100d, 11).ToArray(), Temps + "6464646464646464");
        CurveCase("negative-clamp-and-over-100", "boundary", new double[] { -5000, 4000, -1, 0, 0.49, 0.5, 1.5, 99.5, 100.5, 999, -999 }, Temps + "0000000002646464");
        CurveCase("midpoint-to-even-rounding", "boundary", new double[] { 555, -555, 0.5, 1.5, 2.5, 3.5, 98.5, 99.5, 100.5, -0.5, 999 }, Temps + "0002020462646464");
        CurveCase("descending-duties-become-monotonic", "boundary", new double[] { 0, 0, 90, 80, 70, 60, 50, 40, 30, 20, 0 }, Temps + "5A5A5A5A5A5A5A5A");
        CurveCase("finite-extremes-and-infinities", "boundary", new double[] { 0, 0, double.NegativeInfinity, double.MinValue, -0.5, 0.5, 99.5, double.PositiveInfinity, double.MaxValue, 100, 0 }, Temps + "0000000064646464");
        CurveCase("nan-runtime-parity", "boundary", new double[] { 0, 0, double.NaN, 0, 0, 0, 0, 0, 0, 0, 0 });
        CurveCase("ignored-endpoints-and-index-1", "boundary", new double[] { double.NaN, double.NegativeInfinity, 20, 30, 40, 50, 60, 70, 80, 90, double.PositiveInfinity }, Temps + "141E28323C46505A");

        var random = new Random(20261018);
        for (int sample = 0; sample < 32; sample++)
        {
            var input = new double[11];
            for (int index = 0; index < input.Length; index++)
                input[index] = sample % 2 == 0 ? random.Next(-100, 201) + 0.5 : random.NextDouble() * 300 - 100;
            CurveCase($"deterministic-vector-{sample:D2}", "sampled11", input);
        }

        Run("default-tables-and-restore", "restore", evidence =>
        {
            Require(Convert.ToHexString(Oracle.DefaultCpuCurve) == DefaultCpu, "Source CPU default table differs.");
            Require(Convert.ToHexString(Oracle.DefaultGpuCurve) == DefaultGpu, "Source GPU default table differs.");
            Require(Oracle.DefaultCpuCurve.SequenceEqual(ActualWriter.DefaultCpuCurve), "CPU defaults differ.");
            Require(Oracle.DefaultGpuCurve.SequenceEqual(ActualWriter.DefaultGpuCurve), "GPU defaults differ.");
            CompareWrites(evidence, Oracle.RestoreDefaultCurves(), CurveWriteCapture.CaptureReturning(ActualWriter.RestoreDefaultCurves), false);
            Require(evidence.OracleWrites[0].CurveHex == DefaultCpu && evidence.OracleWrites[1].CurveHex == DefaultGpu && evidence.OracleWrites[2].CurveHex == DefaultCpu, "Expected CPU default / GPU default / CPU default restore payloads.");
        });
        Run("hardware-profile-restores-defaults", "restore", evidence =>
            CompareWrites(evidence, Oracle.ApplyProfile(FanMode.Hardware, null), CurveWriteCapture.CaptureReturning(ActualWriter.RestoreDefaultCurves), false));
        Run("unknown-fanmode-takes-original-else", "restore", evidence =>
            CompareWrites(evidence, Oracle.ApplyProfile((FanMode)99, null), CurveWriteCapture.CaptureReturning(ActualWriter.RestoreDefaultCurves), false));
        Run("closed-restore-and-enable-noop", "sourceGuard", evidence =>
        {
            Require(Oracle.RestoreDefaultCurves(false).Writes.Count == 0, "Closed reference must not restore defaults.");
            Require(Oracle.ApplyProfile(FanMode.Hardware, null, false).Writes.Count == 0, "Hardware branch must respect source IsOpen guard.");
            Require(Oracle.SetFanControlCaptured(true).Writes.Count == 0, "SetFanControl(true) must retain the source no-op.");
            Require(Oracle.SetFanControlCaptured(true, false).Writes.Count == 0, "Closed SetFanControl(true) must retain the source no-op.");
        });
        Run("software-source-does-not-check-isopen", "sourceGuard", evidence =>
        {
            double[] input = Enumerable.Repeat(50d, 11).ToArray();
            CompareWrites(evidence, Oracle.ApplySoftware(input, false), CurveWriteCapture.CaptureReturning(() => ActualWriter.ApplySoftwareCurve(ActualWriter.ToAsusCurve(input))), true);
        });
        foreach (UpdateSource source in Enum.GetValues<UpdateSource>().Append(UpdateSource.ProfilesPage | UpdateSource.LayoutPage))
        {
            Run($"software-update-source-{(int)source}", "profileSource", evidence =>
            {
                double[] input = Enumerable.Repeat(37.5, 11).ToArray();
                CompareWrites(evidence, Oracle.ApplyProfile(FanMode.Software, input, true, source), CurveWriteCapture.CaptureReturning(() => ActualWriter.ApplySoftwareCurve(ActualWriter.ToAsusCurve(input))), true);
            });
        }
        Run("invalid-input-default-array-aliasing", "aliasing", evidence =>
        {
            Require(ReferenceEquals(Oracle.ConvertCurve(null), Oracle.ConvertCurve(new double[10])), "Reference must retain source fallback-array aliasing.");
            Require(ReferenceEquals(ActualWriter.ToAsusCurve(null), ActualWriter.ToAsusCurve(new double[10])), "Actual writer fallback-array aliasing differs.");
            Require(!ReferenceEquals(Oracle.ConvertCurve(new double[11]), Oracle.ConvertCurve(new double[11])), "Valid reference conversions should allocate separate curves.");
        });
        Run("default-accessors-return-defensive-copies", "aliasing", evidence =>
        {
            byte[] oracleCpu = Oracle.DefaultCpuCurve;
            byte[] oracleGpu = Oracle.DefaultGpuCurve;
            byte[] actualCpu = ActualWriter.DefaultCpuCurve;
            byte[] actualGpu = ActualWriter.DefaultGpuCurve;
            oracleCpu[0] = oracleGpu[0] = actualCpu[0] = actualGpu[0] = 255;
            Require(Convert.ToHexString(Oracle.DefaultCpuCurve) == DefaultCpu && Convert.ToHexString(ActualWriter.DefaultCpuCurve) == DefaultCpu, "CPU accessors exposed mutable defaults.");
            Require(Convert.ToHexString(Oracle.DefaultGpuCurve) == DefaultGpu && Convert.ToHexString(ActualWriter.DefaultGpuCurve) == DefaultGpu, "GPU accessors exposed mutable defaults.");
        });
        Run("recorder-snapshots-input-and-fails-closed", "recording", evidence =>
        {
            byte[] input = new byte[] { 1, 2, 3 };
            var run = CurveWriteCapture.Capture(() => { CurveWriteCapture.Record("CPU", input); input[0] = 99; });
            Require(run.Writes[0].Curve[0] == 1, "Capture retained a mutable reference instead of a call-time snapshot.");
            bool rejected = false;
            try { CurveWriteCapture.Record("CPU", input); }
            catch (InvalidOperationException) { rejected = true; }
            Require(rejected, "Recorder should require a memory capture session.");
        });
        Run("pure-reference-assembly-boundary", "isolation", evidence =>
        {
            foreach (Assembly assembly in new[] { typeof(Oracle).Assembly, Assembly.GetExecutingAssembly() })
            {
                foreach (AssemblyName reference in assembly.GetReferencedAssemblies())
                {
                    string name = reference.Name ?? string.Empty;
                    Require(name == "HcRogSourceOracle" || name.StartsWith("System", StringComparison.Ordinal), $"Unexpected non-BCL dependency: {name}");
                }
                Require(!assembly.GetTypes().SelectMany(type => type.GetMethods(BindingFlags.Static | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)).Any(method => (method.Attributes & MethodAttributes.PinvokeImpl) != 0), "Fixture must contain no native-import methods.");
            }
        });
        Run("source-inputs-unchanged-after-verification", "provenance", evidence =>
        {
            Require(HashFile(referenceSourcePath) == sourceHashBefore, "HC source changed during verification.");
            Require(HashFile(actualWriterPath) == actualHashBefore, "Actual writer changed during verification.");
            Require(HashFile(generatedPath) == generatedHashBefore, "Generated oracle changed during verification.");
        });

        int failed = Cases.Count(test => !test.Passed);
        var report = new
        {
            schemaVersion = 1,
            completedUtc = DateTimeOffset.UtcNow,
            label = "HC source reference oracle, not a real HC binary",
            captureBoundary = "AsusACPI.SetFanCurve/SetFanCurveObserved arguments before downstream ACPI clamping and wire packing",
            passed = failed == 0,
            total = Cases.Count,
            failed,
            categories = Cases.GroupBy(test => test.Category).ToDictionary(group => group.Key, group => group.Count()),
            referenceSourcePath,
            referenceSourceSha256 = sourceHashBefore,
            generatedSourcePath = generatedPath,
            generatedSourceSha256 = generatedHashBefore,
            actualWriterPath,
            actualWriterSha256 = actualHashBefore,
            provenancePath = Path.Combine(fixture, "HcRogSourceOracle.provenance.json"),
            runtime = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,
            processArchitecture = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
            hardwareAccess = false,
            productStarted = false,
            syntheticReturnValues = "1001/1002/1003 identify call order only; no firmware success is implied",
            limitations = new[]
            {
                "Only the linked actual AsusFanCurveWriter.cs leaf is covered; no provider/capability/session/product chain is executed.",
                "No AsusACPI post-processing, 100-to-99 clamp, driver IOCTL payload, hardware response, or real HC binary is tested.",
                "NaN is compared on this unchecked .NET runtime; this is not a cross-runtime numeric portability claim.",
            },
            cases = Cases,
        };
        Directory.CreateDirectory(evidenceRoot);
        var options = new JsonSerializerOptions { WriteIndented = true, NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
        string reportPath = Path.Combine(evidenceRoot, "oracle-parity-results.json");
        File.WriteAllText(reportPath, JsonSerializer.Serialize(report, options) + Environment.NewLine);
        Console.WriteLine($"HC source oracle parity: {Cases.Count - failed}/{Cases.Count} passed; report={reportPath}");
        foreach (var failure in Cases.Where(test => !test.Passed)) Console.Error.WriteLine($"FAIL {failure.Name}: {failure.Error}");
        return failed == 0 ? 0 : 1;
    }
}