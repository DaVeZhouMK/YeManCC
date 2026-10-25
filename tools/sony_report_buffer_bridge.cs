// Added ONLY to copied Host projects. Calls the original Handle/parser/response,
// and the pinned, unmodified HMController.SubmitRawReport implementation.
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using HIDMaestro;
using SonyReportBufferContracts;

public static class SonyReportBufferTestApi
{
    public static ISonyFixture Create(string persona) => Program.SonyFixtureCreate(persona);
}
internal static class SonyReportBufferFixtureSafety
{
    internal static void Forbidden(string method) => throw new InvalidOperationException("Forbidden product/device/lifecycle path: " + method);
}
internal static partial class Program
{
    internal static ISonyFixture SonyFixtureCreate(string persona) => new SonyFixture(persona);
    private sealed class SonyFixture : ISonyFixture
    {
        private readonly HostSession session;
        private readonly HMController controller;
        private readonly HMContext context;
        private readonly HMProfile profile;
        private readonly AutoResetEvent ownEvent;
        private IntPtr ownMemory;
        private readonly object privateGate;
        private CommandEnvelope? fixtureTuple;
        public string Persona { get; }
        public bool IsCandidate {
            get {
#if SONY_REPORT_BUFFER_CANDIDATE
                return true;
#else
                return false;
#endif
            }
        }
        internal SonyFixture(string persona)
        {
            if (persona is not ("dualshock4" or "dualsense" or "dualsense-edge")) throw new ArgumentException(nameof(persona));
            Persona = persona;
            var assembly = typeof(HMController).Assembly;
            if (new FileInfo(assembly.Location).Length != LockedCoreBytes ||
                Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(assembly.Location))) != LockedCoreSha256 ||
                assembly.ManifestModule.ModuleVersionId.ToString("D") != LockedCoreMvid)
                throw new InvalidOperationException("Pinned SDK identity mismatch; never substitute a fake SDK.");
            profile = LoadActualProfile(persona, 64);
            var inner = typeof(HMProfile).GetProperty("Inner", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(profile)!;
            if ((bool)inner.GetType().GetProperty("RequiresXusbCompanion")!.GetValue(inner)!)
                throw new InvalidOperationException("Unexpected companion profile; fixture is Sony-only.");
            var builder = inner.GetType().GetMethod("GetOrBuildReportBuilder", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(inner, null)!;
            // The two real SDK constructors create mappings/threads or prewarm
            // driver extraction. They are deliberately NEVER called by this fixture.
            context = (HMContext)RuntimeHelpers.GetUninitializedObject(typeof(HMContext));
            Set(typeof(HMContext), context, "_lock", new object());
            Set(typeof(HMContext), context, "_profilesById", new Dictionary<string, HMProfile>(StringComparer.OrdinalIgnoreCase) { [profile.Id] = profile });
            Set(typeof(HMContext), context, "_profiles", new List<HMProfile> { profile });
            Set(typeof(HMContext), context, "_controllers", new Dictionary<int, HMController>());
            ownEvent = new AutoResetEvent(false); // unnamed, fixture-owned event
            ownMemory = Marshal.AllocHGlobal(362); // NOT a named/SDK mapping
            Marshal.Copy(new byte[362], 0, ownMemory, 362);
            controller = (HMController)RuntimeHelpers.GetUninitializedObject(typeof(HMController));
            Set(typeof(HMController), controller, "<Profile>k__BackingField", profile);
            Set(typeof(HMController), controller, "_context", context);
            Set(typeof(HMController), controller, "_rawReportBuffer", new byte[64]);
            Set(typeof(HMController), controller, "_reportBuilder", builder);
            Set(typeof(HMController), controller, "_inputView", ownMemory);
            Set(typeof(HMController), controller, "_inputEvent", ownEvent.SafeWaitHandle.DangerousGetHandle());
            session = (HostSession)RuntimeHelpers.GetUninitializedObject(typeof(HostSession));
            privateGate = new object();
            Set(typeof(HostSession), session, "_lifecycleGate", privateGate);
            Set(typeof(HostSession), session, "_context", context);
            Set(typeof(HostSession), session, "_controller", controller);
            Set(typeof(HostSession), session, "_idempotentReplies", new Dictionary<string, IdempotentReply>(StringComparer.Ordinal));
            Set(typeof(HostSession), session, "_startupNonce", "sony-buffer-nonce");
            Set(typeof(HostSession), session, "_hostInstanceId", "sony-buffer-isolated-host");
            Set(typeof(HostSession), session, "_virtualEndpoint", new VirtualEndpointObservation(false, 0, 0, 0, 0, 0, false, "fixture-private-memory"));
            Set(typeof(HostSession), session, "_assetVerified", true);
            Set(typeof(HostSession), session, "_targetPnpAdmitted", true);
            Set(typeof(HostSession), session, "_phase", Phase.Active);
            Set(typeof(HostSession), session, "_costFlagEnabled", false);
            Set(typeof(HostSession), session, "_costSubmitUsSamples", new long[2048]);
#if SONY_REPORT_BUFFER_CANDIDATE
            Set(typeof(HostSession), session, "_sonyReportBuffer", new byte[63]);
#endif
        }
        private static void Set(Type type, object target, string name, object value)
        {
            var field = type.GetField(name, BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public)
                ?? throw new MissingFieldException(type.FullName, name);
            field.SetValue(target, value);
            if (!Equals(field.GetValue(target), value)) throw new InvalidOperationException("Fixture field assignment rejected: " + name);
        }
        private static object? Get(Type type, object target, string name) =>
            type.GetField(name, BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public)!.GetValue(target);
        private static HMProfile LoadActualProfile(string persona, int size)
        {
            var id = persona == "dualshock4" ? "dualshock-4-v2" : persona;
            var assembly = typeof(HMProfile).Assembly;
            var resource = assembly.GetManifestResourceNames().Single(n =>
                n.StartsWith("HIDMaestro.Profiles.", StringComparison.Ordinal) &&
                (n.EndsWith("\\" + id + ".json", StringComparison.Ordinal) || n.EndsWith("/" + id + ".json", StringComparison.Ordinal) || n.EndsWith("." + id + ".json", StringComparison.Ordinal)));
            using var stream = assembly.GetManifestResourceStream(resource)!;
            using var reader = new StreamReader(stream);
            var innerType = assembly.GetType("HIDMaestro.Internal.ControllerProfile", true)!;
            var optionsType = assembly.GetType("HIDMaestro.Internal.HMLayoutJsonOptions", true)!;
            var options = (JsonSerializerOptions)optionsType.GetField("Default", BindingFlags.Public | BindingFlags.Static)!.GetValue(null)!;
            var inner = JsonSerializer.Deserialize(reader.ReadToEnd(), innerType, options)!;
            if (size != 64) innerType.GetProperty("InputReportSize")!.SetValue(inner, (int?)size);
            return (HMProfile)typeof(HMProfile).GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic)
                .Single(c => c.GetParameters().Length == 1 && c.GetParameters()[0].ParameterType == innerType).Invoke(new[] { inner });
        }
        private CommandEnvelope Envelope(string message)
        {
            if (!TryParseEnvelope(message, out var envelope, out var error)) throw new InvalidDataException(error);
            return envelope!;
        }
        public bool Parse(string message, out string error) => TryParseEnvelope(message, out _, out error);
        public string Handle(string message)
        {
            // Tuple is seeded once without HELLO/PREPARE or target creation.
            if (Get(typeof(HostSession), session, "_tuple") is null)
                Set(typeof(HostSession), session, "_tuple", fixtureTuple = Envelope(message));
            return session.Handle(message, out _);
        }
        public void ResetForSample()
        {
            Set(typeof(HostSession), session, "_lastSequence", 0UL);
            Set(typeof(HostSession), session, "_backendSubmitOk", 0L);
            Set(typeof(HMController), controller, "_inputSeqNo", 0U);
            Set(typeof(HostSession), session, "_tuple", fixtureTuple!);
            // Existing production counters are process-wide; reset only outside
            // a serial measurement. Concurrent tests use explicit builder indices.
            _ds4FrameCounter = _dualSenseFrameCounter = 0;
        }
        public long SuccessfulSubmits => (long)Get(typeof(HostSession), session, "_backendSubmitOk")!;
        public uint SdkSequence => (uint)Get(typeof(HMController), controller, "_inputSeqNo")!;
        public byte[] ReadPayload()
        {
            if (Marshal.ReadInt32(ownMemory, 4) != 63) throw new InvalidOperationException("SDK output length is not 63");
            var result = new byte[63]; Marshal.Copy(ownMemory + 8, result, 0, 63); return result;
        }
        public byte[] ReadSdkBuffer() => ((byte[])Get(typeof(HMController), controller, "_rawReportBuffer")!).ToArray();
        public byte[]? SessionBuffer =>
#if SONY_REPORT_BUFFER_CANDIDATE
            (byte[])Get(typeof(HostSession), session, "_sonyReportBuffer")!;
#else
            null;
#endif
        public byte[] Fresh(string message, long counter, int profileSize = 64)
        {
            var e = Envelope(message); var p = profileSize == 64 ? profile : LoadActualProfile(Persona, profileSize);
            return Persona == "dualshock4" ? BuildDs4V2DataOnlyReport(p, e, counter) :
                BuildDualSenseDataOnlyReport(p, e, counter, Persona == "dualsense-edge" ? MapDsEdgePaddles(e.BackButtons) : 0);
        }
        public void Fill(string message, long counter, byte[]? destination, int profileSize = 64)
        {
#if SONY_REPORT_BUFFER_CANDIDATE
            var e = Envelope(message); var p = profileSize == 64 ? profile : LoadActualProfile(Persona, profileSize);
            if (Persona == "dualshock4") FillDs4V2DataOnlyReport(p, e, counter, destination!);
            else FillDualSenseDataOnlyReport(p, e, counter, destination!, Persona == "dualsense-edge" ? MapDsEdgePaddles(e.BackButtons) : 0);
#else
            throw new NotSupportedException("The baseline has no borrowed-buffer helper.");
#endif
        }
        public void SubmitRaw(byte[] data) => controller.SubmitRawReport(data);
        public void MarkSdkDisposed(bool disposed) => Set(typeof(HMController), controller, "_disposed", disposed);
        public bool SdkDirectlyRetains(byte[] data) => typeof(HMController).GetFields(BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public).Any(f => ReferenceEquals(f.GetValue(controller), data));
        public void SubmitNeutral() => session.SonyFixtureNeutral();
        public void ConcurrentFillAndSubmit(string message, long counter)
        {
            lock (privateGate)
            {
#if SONY_REPORT_BUFFER_CANDIDATE
                Fill(message, counter, SessionBuffer); SubmitRaw(SessionBuffer!);
#else
                SubmitRaw(Fresh(message, counter));
#endif
            }
        }
        public void Dispose()
        {
            // Do NOT call HMController/HMContext/HostSession disposal: those are
            // real lifecycle operations. Only resources acquired above are owned.
            Set(typeof(HMController), controller, "_disposed", true);
            Set(typeof(HMContext), context, "_disposed", true);
            if (ownMemory != IntPtr.Zero) { Marshal.FreeHGlobal(ownMemory); ownMemory = IntPtr.Zero; }
            ownEvent.Dispose();
        }
    }
    private sealed partial class HostSession
    {
        internal void SonyFixtureNeutral()
        {
            lock (_lifecycleGate)
                if (!TrySubmitNeutral(out var detail)) throw new InvalidOperationException(detail);
        }
    }
}
