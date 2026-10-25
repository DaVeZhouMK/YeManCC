using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using YeManInputHost.Diagnostics;

// Only a lock carrier is substituted for the parallel, existing logger.
internal static class XboxSdkDiagnostics { internal static readonly object LogLock = new(); }

internal static class FixtureLeaves
{
    internal const string Service = @"SYSTEM\CurrentControlSet\Services\usbip2_ude";
    internal const string Enum = @"SYSTEM\CurrentControlSet\Enum\";
    internal const string Class = @"SYSTEM\CurrentControlSet\Control\Class\";
    internal const string Driver = @"{4d36e97d-e325-11ce-bfc1-08002be10318}\0000";
    internal static string Root = "", WindowsRoot = "", LogRoot = "", Sys = "";
    internal static readonly Dictionary<string, Dictionary<string, object?>> Keys = new(StringComparer.OrdinalIgnoreCase);
    internal static readonly List<(string Path, string[] Names)> RegistryCalls = new();
    internal static readonly List<string> CmCalls = new();
    internal static bool ThrowRegistry, ThrowCm;
    internal static string? ThrowKey;
    internal static (uint Locate, uint Result, uint Status, uint Problem) Cm = (0, 0, 8, 0);
    internal static string Log => Path.Combine(LogRoot, "input-host-xbox-sdk.log");
    internal static string Id(int i) => @"ROOT\USBIP2_UDE\" + i.ToString("D4", CultureInfo.InvariantCulture);
    internal static Dictionary<string, object?> Key(string path)
    {
        var key = new Dictionary<string, object?>(StringComparer.OrdinalIgnoreCase); Keys[path] = key; return key;
    }
    internal static void Reset(string name, int count = 1)
    {
        string area = Path.Combine(Root, "cases", name);
        if (Directory.Exists(area)) throw new InvalidOperationException("fresh-case-required");
        WindowsRoot = Path.Combine(area, "Windows"); LogRoot = Path.Combine(area, "logs");
        Directory.CreateDirectory(Path.Combine(WindowsRoot, "System32", "drivers"));
        Sys = Path.Combine(WindowsRoot, "System32", "drivers", "usbip2_ude.sys"); File.WriteAllBytes(Sys, Encoding.UTF8.GetBytes("abc"));
        Keys.Clear(); RegistryCalls.Clear(); CmCalls.Clear(); ThrowRegistry = ThrowCm = false; ThrowKey = null; Cm = (0, 0, 8, 0);
        var service = Key(Service); service["ImagePath"] = @"\SystemRoot\System32\drivers\usbip2_ude.sys"; service["Type"] = 1; service["Start"] = 3; service["ErrorControl"] = 1;
        var entries = Key(Service + @"\Enum"); entries["Count"] = count;
        for (int i = 0; i < Math.Min(count, 8); ++i)
        {
            entries[i.ToString(CultureInfo.InvariantCulture)] = Id(i);
            var instance = Key(Enum + Id(i)); instance["Service"] = "usbip2_ude"; instance["Driver"] = Driver; instance["ConfigFlags"] = 0; instance["Problem"] = 0; instance["StatusFlags"] = 0;
        }
        var klass = Key(Class + Driver); klass["InfPath"] = "oem42.inf"; klass["InfSection"] = "USBIP2_UDE_Install"; klass["DriverVersion"] = "0.9.8.1"; klass["ProviderName"] = "fixture-provider"; klass["DriverDate"] = "10-21-2026";
    }
    internal static object?[]? ReadRegistryValues(string path, params string[] names)
    {
        RegistryCalls.Add((path, names.ToArray()));
        if (ThrowRegistry || path == ThrowKey) throw new IOException("fixture-registry-denied");
        return Keys.TryGetValue(path, out var key) ? names.Select(n => key.GetValueOrDefault(n)).ToArray() : null;
    }
    internal static (uint Locate, uint Result, uint Status, uint Problem) ReadCmStatus(string id)
    {
        CmCalls.Add(id); if (ThrowCm) throw new IOException("fixture-CM-denied"); return Cm;
    }
}

internal static class Fixture
{
    private const string KnownCore = "8fe7bdb21ec497591f527b7b87c4bc7b41b45c35a06cc211a7a0dce48838ed2c";
    private const int LogLimit = 4 * 1024 * 1024;
    private static readonly List<string> Checks = new(), Failures = new(), Skipped = new();
    private static int Cases;
    private static void Check(bool okay, string text) { Checks.Add(text); if (!okay) Failures.Add(text); }
    private static void Case(string name, Action action)
    {
        ++Cases; try { action(); } catch (Exception ex) { Failures.Add(name + ": " + ex); }
    }
    private static UsbipSetupEvidence Begin(string sha = KnownCore) => UsbipSetupEvidence.TryBegin("steamdeck", "fixture-host", "fixture-run", 17, "PREPARE_TARGET", sha, "12345678-1234-1234-1234-123456789abc") ?? throw new Exception("scope unexpectedly absent");
    private static JsonElement[] Rows() => File.Exists(FixtureLeaves.Log) ? File.ReadAllLines(FixtureLeaves.Log, new UTF8Encoding(false, true)).Select(line => JsonDocument.Parse(line).RootElement.Clone()).ToArray() : Array.Empty<JsonElement>();
    private static string Phase(JsonElement row) => row.GetProperty("phase").GetString()!;
    private static string Detail(JsonElement row) => row.GetProperty("data").GetProperty("detail").GetString()!;
    private static JsonElement[] PhaseRows(string phase) => Rows().Where(r => Phase(r) == phase).ToArray();
    private static int Counter(string name) => int.Parse(Regex.Match(Detail(PhaseRows("scope-end").Single()), @"\b" + name + @"=(\d+)").Groups[1].Value, CultureInfo.InvariantCulture);
    private static string? PathValue(string? raw) => (string?)typeof(UsbipSetupEvidence).GetMethod("ImagePath", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, new object?[] { raw });
    private static string Image(string? raw) => (string)typeof(UsbipSetupEvidence).GetMethod("DiskImage", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, new object?[] { raw })!;
    private static string Version(byte[] bytes) => (string)typeof(UsbipSetupEvidence).GetMethod("DiskVersion", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, new object?[] { bytes })!;
    private static void VerifyRows(string label)
    {
        var rows = Rows();
        Check(rows.Length <= 128, label + ": scope <=128 lines");
        Check(rows.All(r => r.GetProperty("kind").GetString() == "usbip-setup-evidence"), label + ": kind is usbip-setup-evidence");
        Check(rows.FirstOrDefault().ValueKind != JsonValueKind.Undefined && Phase(rows[0]) == "scope-begin" && Phase(rows[^1]) == "scope-end", label + ": scope endpoints retained");
        Check(File.ReadAllLines(FixtureLeaves.Log).All(line => Encoding.UTF8.GetByteCount(line + "\n") <= 1024), label + ": every complete UTF8 line <=1024 bytes");
        Check(rows.All(r => r.GetProperty("persona").GetString() == "steamdeck" && r.GetProperty("epoch").GetUInt64() == 17), label + ": correlation fields retained");
    }
    public static int Main(string[] args)
    {
        FixtureLeaves.Root = args.Single();
        string? diag = Environment.GetEnvironmentVariable("HIDMAESTRO_DIAG"), timing = Environment.GetEnvironmentVariable("HIDMAESTRO_TIMING");
        Case("persona", () => {
            FixtureLeaves.Reset("persona");
            foreach (string persona in new[] { "", "elite", "dualsense-edge", "SteamDeck", "steamdeck ", "desktop" })
                Check(UsbipSetupEvidence.TryBegin(persona, "h", "r", 0, "c", "s", "m") is null, "persona rejected: " + persona);
            Check(FixtureLeaves.RegistryCalls.Count == 0 && FixtureLeaves.CmCalls.Count == 0 && !File.Exists(FixtureLeaves.Log), "non-SD has zero leaves or log writes");
        });
        Case("basic", () => {
            FixtureLeaves.Reset("basic"); var scope = Begin(); scope.Stage("backend-probe-result", "explicit-sdk-observation", false); scope.Report("安装进度 中文🙂\r\nnext");
            FixtureLeaves.Keys[FixtureLeaves.Class + FixtureLeaves.Driver]["DriverVersion"] = "99.100.101.102";
            scope.Dispose(); int before = Rows().Length, calls = FixtureLeaves.RegistryCalls.Count; scope.Dispose(); scope.Stage("late"); scope.Report("late");
            Check(Rows().Length == before && FixtureLeaves.RegistryCalls.Count == calls, "Dispose idempotent; no late writes or snapshots");
            Check(FixtureLeaves.RegistryCalls.Count(x => x.Path == FixtureLeaves.Service) == 2 && FixtureLeaves.RegistryCalls.Count(x => x.Path == FixtureLeaves.Service + @"\Enum") == 2, "exactly two service and Enum samples");
            Check(FixtureLeaves.CmCalls.SequenceEqual(new[] { FixtureLeaves.Id(0), FixtureLeaves.Id(0) }), "exact instance CM sampled at begin and end only");
            Check(PhaseRows("registered-driver").Select(Detail).Any(s => s.Contains("version=0.9.8.1")) && PhaseRows("registered-driver").Select(Detail).Any(s => s.Contains("version=99.100.101.102")), "end sample observes future registered version without policy");
            Check(PhaseRows("registered-driver").All(r => Detail(r).Contains("inf=oem42.inf") && Detail(r).Contains("registered-binding;not-loaded-memory-image")), "INF and version labeled registered binding");
            Check(PhaseRows("registered-binding").All(r => Detail(r).Contains("started=true") && Detail(r).Contains("problem=0")), "CM started/problem facts retained");
            var available = Rows().Where(r => r.GetProperty("data").GetProperty("available").ValueKind != JsonValueKind.Null).ToArray();
            Check(available.Length == 1 && !available[0].GetProperty("data").GetProperty("available").GetBoolean(), "availability only from caller's explicit observation");
            Check(Detail(PhaseRows("scope-begin").Single()).Contains("bundledInstallerVersion=0.9.8.1 migrationBranchObserved=false"), "known core bundled version and migration limitation explicit");
            Check(Detail(PhaseRows("sdk-progress").Single()).Contains("安装进度 中文🙂  next"), "UTF8 progress retained and controls flattened");
            Check(!File.ReadAllBytes(FixtureLeaves.Log).Take(3).SequenceEqual(new byte[] {239, 187, 191}), "log has no UTF8 BOM");
            VerifyRows("basic");
        });
        Case("inherited-process-values", () => {
            string[] names = { "HIDMAESTRO_KEEP_TRANSPORT", "HIDMAESTRO_TIMEOUT_SCALE", "HIDMAESTRO_DIAG", "HIDMAESTRO_TIMING" };
            string?[] original = names.Select(Environment.GetEnvironmentVariable).ToArray();
            try
            {
                foreach (string name in names) Environment.SetEnvironmentVariable(name, null);
                FixtureLeaves.Reset("inherited-unset", 0); using (Begin()) { }
                string unset = Detail(PhaseRows("scope-begin").Single());
                Check(names.All(name => unset.Contains(name + "=<unset>")), "all four inherited unset values explicit in same begin");
                Check(names.All(name => Environment.GetEnvironmentVariable(name) is null), "reading unset inherited values never enables flags");
                string[] values = { "TRUE-KEEP", "0.25-debug-wait", "prior-persona", "inherit-timing" };
                for (int i = 0; i < 4; ++i) Environment.SetEnvironmentVariable(names[i], values[i]);
                FixtureLeaves.Reset("inherited-values", 0); using (Begin()) { }
                string actual = Detail(PhaseRows("scope-begin").Single());
                Check(names.Select((name, i) => actual.Contains(name + "=" + values[i])).All(b => b), "non-default inherited raw values visible");
                Check(names.Select((name, i) => Environment.GetEnvironmentVariable(name) == values[i]).All(b => b), "production leaves all four non-default process values unchanged");
                Check(actual.IndexOf("HIDMAESTRO_KEEP_TRANSPORT=") < actual.IndexOf("coreSha="), "inherited transport/wait values precede core identity");
                Check(PhaseRows("scope-begin").Length == 1 && Rows().Length == 8, "inherited evidence adds no rows to scope budget");
                int[] limits = { 16, 32, 16, 16 };
                for (int i = 0; i < 4; ++i) Environment.SetEnvironmentVariable(names[i], new string((char)('K' + i), 1000));
                FixtureLeaves.Reset("inherited-long", 0); using (Begin()) { }
                string clipped = Detail(PhaseRows("scope-begin").Single());
                for (int i = 0; i < 4; ++i)
                {
                    string expected = new((char)('K' + i), limits[i]);
                    Check(clipped.Contains(names[i] + "=" + expected + " "), names[i] + " raw text clipped exactly at " + limits[i]);
                    Check(Environment.GetEnvironmentVariable(names[i]) == new string((char)('K' + i), 1000), names[i] + " overlong process original untouched");
                }
                Check(PhaseRows("scope-begin").Single().GetProperty("data").GetProperty("textTruncated").GetBoolean() && Counter("textTruncated") >= 1, "inherited truncation marked on begin and final counter");
                VerifyRows("inherited");
            }
            finally { for (int i = 0; i < 4; ++i) Environment.SetEnvironmentVariable(names[i], original[i]); }
        });
        Case("flood-and-reserved-terminals", () => {
            FixtureLeaves.Reset("flood", 1000); var scope = Begin();
            for (int i = 0; i < 10000; ++i) scope.Report(new string('汉', 3000) + "🙂");
            for (int i = 0; i < 1000; ++i) scope.Stage("ordinary-stage", new string('x', 4000));
            for (int i = 0; i < 1000; ++i) { scope.Stage("prepare-exception", "terminal-exception"); scope.Stage("prepare-result", "terminal-result"); }
            scope.Dispose();
            Check(Rows().Length == 128, "full-budget scope exactly128 rows");
            Check(PhaseRows("sdk-progress").Length == 64 && PhaseRows("ordinary-stage").Length == 22, "independent64 Report and22 Stage budgets");
            Check(PhaseRows("prepare-result").Length == 1 && PhaseRows("prepare-exception").Length == 1, "flood preserves each terminal exactly once");
            Check(Array.FindIndex(Rows(), r => Phase(r) == "prepare-result") < Array.FindIndex(Rows(), r => Phase(r) == "scope-end"), "terminal result precedes scope-end");
            Check(Counter("dropped") == (10000 - 64) + (1000 - 22) + 1998 && Counter("progress") == 64 && Counter("stages") == 22, "final dropped/progress/stage counts exact after floods");
            Check(Counter("textTruncated") == 86 + (PhaseRows("scope-begin").Single().GetProperty("data").GetProperty("textTruncated").GetBoolean() ? 1 : 0) && Counter("logErrorsBeforeEnd") == 0 && Counter("attemptedBeforeEnd") == 127, "text truncation and final attempt counters exact");
            Check(FixtureLeaves.CmCalls.Count == 16 && FixtureLeaves.CmCalls.Distinct().Count() == 8, "only8 exact instances per snapshot even Count1000");
            Check(PhaseRows("service-enum").All(r => Detail(r).Contains("omitted=992")), "instance truncation count preserved");
            Check(FixtureLeaves.RegistryCalls.Where(x => x.Path.EndsWith(@"\Enum")).All(x => x.Names.SequenceEqual(new[] { "Count", "0", "1", "2", "3", "4", "5", "6", "7" })), "Enum reads only fixed Count and eight values");
            Check(FixtureLeaves.RegistryCalls.All(x => x.Path == FixtureLeaves.Service || x.Path == FixtureLeaves.Service + @"\Enum" || x.Path == FixtureLeaves.Class + FixtureLeaves.Driver || Enumerable.Range(0, 8).Any(i => x.Path == FixtureLeaves.Enum + FixtureLeaves.Id(i))), "no whole-machine registry scan");
            VerifyRows("flood");
        });
        Case("unknown-and-future-identities", () => {
            FixtureLeaves.Reset("unknown"); FixtureLeaves.Keys[FixtureLeaves.Class + FixtureLeaves.Driver]["DriverVersion"] = null; using (Begin("future-core-identity")) { }
            Check(Detail(PhaseRows("scope-begin").Single()).Contains("coreSha=future-core-identity") && Detail(PhaseRows("scope-begin").Single()).Contains("bundledInstallerVersion=unknown migrationBranchObserved=false"), "unknown core cannot borrow bundled installer identity");
            Check(PhaseRows("registered-driver").All(r => Detail(r).Contains("version=versionunknown")), "missing registered version stays versionunknown");
            Check(PhaseRows("disk-image").All(r => Detail(r).Contains("version=versionunknown") && Detail(r).Contains("sha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")), "malformed PE keeps actual file hash and versionunknown");
            FixtureLeaves.Reset("future"); File.Copy(typeof(Fixture).Assembly.Location, FixtureLeaves.Sys, true);
            using (Begin()) { }
            string expected = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(FixtureLeaves.Sys))).ToLowerInvariant();
            Check(PhaseRows("disk-image").All(r => Detail(r).Contains("version=99.3.4.5") && Detail(r).Contains("sha256=" + expected)), "future disk version and SHA from same bounded actual PE copy");
            Check(Rows().All(r => r.GetProperty("data").GetProperty("available").ValueKind == JsonValueKind.Null), "future disk or registered versions never imply availability");
            Check(PhaseRows("disk-image").All(r => Detail(r).Contains("disk-bytes;not-loaded-memory-image")), "disk version explicitly excludes loaded memory image");
        });
        Case("paths", () => {
            FixtureLeaves.Reset("paths", 0); string w = FixtureLeaves.WindowsRoot;
            string[] accepted = { FixtureLeaves.Sys, @"\SystemRoot\System32\drivers\usbip2_ude.sys", @"%SystemRoot%\System32\drivers\usbip2_ude.sys", @"%windir%\System32\drivers\usbip2_ude.sys", @"System32\drivers\usbip2_ude.sys", "\"" + FixtureLeaves.Sys + "\"", @"\systemroot\System32\drivers\usbip2_ude.SYS" };
            foreach (string path in accepted) Check(string.Equals(PathValue(path), FixtureLeaves.Sys, StringComparison.OrdinalIgnoreCase), "accepted precise Windows SYS form: " + path);
            string?[] rejected = { null, "", @"\\server\share\evil.sys", @"\\?\" + FixtureLeaves.Sys, @"\??\" + FixtureLeaves.Sys, @"\\.\C:\evil.sys", @"\Device\HarddiskVolume1\Windows\evil.sys", Path.Combine(FixtureLeaves.Root, "outside.sys"), w + @"Other\evil.sys", w + @"\..\evil.sys", w + @"\System32\..\evil.sys", w + @"\System32\drivers\evil.sys:stream", w + @"\System32\drivers\evil.sys --install", w + @"\System32\drivers\evil.dll", @"%TEMP%\evil.sys", @"%SystemRoot%\%TEMP%\evil.sys", @"System32\drivers\evil.sys" + '\n', @"C:Windows\evil.sys", w + "/evil.sys", w + @"\System32\drivers\NUL.sys", w + @"\System32\drivers\.. \evil.sys", w + @"\System32\\drivers\evil.sys", "\"" + FixtureLeaves.Sys + "\" arg", new string('a', 513) + ".sys", @"System32\drivers\evil?.sys", @"System32\drivers\evil;whoami.sys" };
            foreach (string? path in rejected) { Check(PathValue(path) is null, "rejected injected/non-Windows path: " + path); Check(Image(path).Contains("image-path-rejected"), "rejected path has no image read: " + path); }
            string outside = Path.Combine(FixtureLeaves.Root, "outside.sys"); File.WriteAllText(outside, "outside-not-a-driver");
            string link = Path.Combine(w, "System32", "drivers", "linked.sys");
            try { File.CreateSymbolicLink(link, outside); Check(Image(link).Contains("reparse-path-rejected"), "real reparse SYS rejected before file bytes read"); }
            catch (Exception ex) when (ex is UnauthorizedAccessException or IOException) { Skipped.Add("Real reparse-path rejection: private output filesystem cannot create symlink: " + ex.GetType().Name + ": " + ex.Message); }
        });
        Case("disk-bounds-and-PE-failures", () => {
            FixtureLeaves.Reset("disk-bounds", 0); File.WriteAllBytes(FixtureLeaves.Sys, new byte[2 * 1024 * 1024 + 1]);
            string over = Image(FixtureLeaves.Sys); Check(over.Contains("image-over-budget") && !over.Contains("sha256="), ">2MiB SYS never read/hashed");
            byte[] exact = new byte[2 * 1024 * 1024]; exact[^1] = 17; File.WriteAllBytes(FixtureLeaves.Sys, exact);
            Check(Image(FixtureLeaves.Sys).Contains("sha256=" + Convert.ToHexString(SHA256.HashData(exact)).ToLowerInvariant()), "exact2MiB image fully hashed");
            File.WriteAllBytes(FixtureLeaves.Sys, Array.Empty<byte>()); Check(Image(FixtureLeaves.Sys).Contains("sha256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "empty disk bytes have actual SHA and unknown version");
            foreach (int length in new[] { 0, 1, 2, 64, 512, 8192 }) Check(Version(Enumerable.Repeat((byte)0xff, length).ToArray()) == "versionunknown", "malformed PE bounded versionunknown length=" + length);
            File.Delete(FixtureLeaves.Sys); using (Begin()) { }
            Check(PhaseRows("disk-image").All(r => Detail(r).Contains("status=unavailable")), "missing SYS failure contained");
        });
        Case("registry-and-CM-failures", () => {
            FixtureLeaves.Reset("registry-fail"); FixtureLeaves.ThrowRegistry = true; using (Begin()) { }
            Check(PhaseRows("registered-service").All(r => Detail(r).Contains("status=unavailable")) && PhaseRows("scope-end").Length == 1, "registry access failure cannot escape scope");
            Check(FixtureLeaves.CmCalls.Count == 0, "failed service Enum causes no CM calls");
            FixtureLeaves.Reset("cm-fail"); FixtureLeaves.ThrowCm = true; using (Begin()) { }
            Check(PhaseRows("registered-binding").All(r => Detail(r).Contains("cmStatus=unavailable started=unknown problem=unknown")), "CM exception preserves unknown started/problem");
            Check(PhaseRows("registered-driver").All(r => Detail(r).Contains("inf=oem42.inf")), "CM failure still captures registered Class facts");
            FixtureLeaves.Reset("cm-locate-missing"); FixtureLeaves.Cm = (13, 13, 8, 0); using (Begin()) { }
            Check(PhaseRows("registered-binding").All(r => Detail(r).Contains("locateResult=13 statusResult=13") && Detail(r).Contains("started=unknown problem=unknown")), "CM locate error never becomes false started or zero problem");
            FixtureLeaves.Reset("cm-problem"); FixtureLeaves.Cm = (0, 0, 0, 28); using (Begin()) { }
            Check(PhaseRows("registered-binding").All(r => Detail(r).Contains("started=false problem=28")), "CM problem28 is direct evidence only");
            Check(Rows().All(r => r.GetProperty("data").GetProperty("available").ValueKind == JsonValueKind.Null), "problem28 does not derive availability");
            FixtureLeaves.Reset("class-fail"); FixtureLeaves.ThrowKey = FixtureLeaves.Class + FixtureLeaves.Driver; using (Begin()) { }
            Check(PhaseRows("registered-driver").All(r => Detail(r).Contains("version=versionunknown error=IOException")), "Class denial stays versionunknown");
            Check(FixtureLeaves.CmCalls.Count == 2, "Class failure does not suppress CM facts");
        });
        Case("registry-path-validation", () => {
            FixtureLeaves.Reset("enum-injection", 8); var values = FixtureLeaves.Keys[FixtureLeaves.Service + @"\Enum"];
            string[] ids = { @"..\Services\evil", @"ROOT\USBIP2_UDE\..", @"ROOT\USBIP2_UDE\0\child", @"ROOT/USBIP2_UDE/0", "ROOT\\USBIP2_UDE\\0\n", new string('a', 513), FixtureLeaves.Id(6), FixtureLeaves.Id(6) };
            for (int i = 0; i < 8; ++i) values[i.ToString()] = ids[i]; using (Begin()) { }
            Check(FixtureLeaves.CmCalls.Count == 2 && FixtureLeaves.CmCalls.All(s => s == FixtureLeaves.Id(6)), "injected or duplicate Enum instances never reach registry/CM");
            Check(PhaseRows("registered-binding").Count(r => Detail(r).Contains("instance-rejected-or-duplicate")) == 14, "bad/duplicate instance accounting retained");
            FixtureLeaves.Reset("driver-injection"); FixtureLeaves.Keys[FixtureLeaves.Enum + FixtureLeaves.Id(0)]["Driver"] = @"..\Services\evil"; using (Begin()) { }
            Check(!FixtureLeaves.RegistryCalls.Any(x => x.Path.StartsWith(FixtureLeaves.Class)) && PhaseRows("registered-driver").All(r => Detail(r).Contains("driver-key-rejected")), "untrusted Driver Class pointer rejected");
            FixtureLeaves.Reset("service-mismatch"); FixtureLeaves.Keys[FixtureLeaves.Enum + FixtureLeaves.Id(0)]["Service"] = "other-service"; using (Begin()) { }
            Check(FixtureLeaves.CmCalls.Count == 0 && !FixtureLeaves.RegistryCalls.Any(x => x.Path.StartsWith(FixtureLeaves.Class)), "mismatched instance service cannot select other Class/CM");
            FixtureLeaves.Reset("count-unknown"); FixtureLeaves.Keys[FixtureLeaves.Service + @"\Enum"]["Count"] = "injected"; using (Begin()) { }
            Check(PhaseRows("service-enum").All(r => Detail(r).Contains("registeredCount=unknown") && Detail(r).Contains("omitted=unknown")), "invalid Count remains unknown and bounded");
            Check(FixtureLeaves.CmCalls.Count == 2, "unknown Count still reads only eight fixed slots");
            FixtureLeaves.Reset("service-absent"); FixtureLeaves.Keys.Clear(); using (Begin()) { }
            Check(PhaseRows("registered-service").All(r => Detail(r).Contains("status=missing")) && FixtureLeaves.CmCalls.Count == 0, "absent service is fact, no inference or device calls");
        });
        Case("logging-faults-and-recovery", () => {
            FixtureLeaves.Reset("log-fault", 0); Directory.CreateDirectory(Path.GetDirectoryName(FixtureLeaves.LogRoot)!); File.WriteAllText(FixtureLeaves.LogRoot, "blocks-log-directory");
            var scope = Begin(); scope.Stage("during-fault"); scope.Report("during-fault"); File.Delete(FixtureLeaves.LogRoot); scope.Stage("recovered"); scope.Dispose(); scope.Dispose();
            Check(PhaseRows("scope-end").Length == 1 && Counter("logErrorsBeforeEnd") == 6, "log failure contained and exact count survives recovery");
            Check(PhaseRows("recovered").Length == 1 && FixtureLeaves.RegistryCalls.Count == 4, "logging failure never suppresses ending read-only snapshot");
            FixtureLeaves.Reset("locked-log", 0); Directory.CreateDirectory(FixtureLeaves.LogRoot);
            var active = Begin(); using (new FileStream(FixtureLeaves.Log, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) { active.Report("cannot append"); }
            active.Dispose(); Check(Counter("logErrorsBeforeEnd") == 1, "exclusive log sharing failure cannot escape callback");
        });
        Case("rotation", () => {
            FixtureLeaves.Reset("rotation", 0); Directory.CreateDirectory(FixtureLeaves.LogRoot); File.WriteAllBytes(FixtureLeaves.Log, new byte[LogLimit]); File.WriteAllText(FixtureLeaves.Log + ".1", "old-backup");
            using (Begin()) { }
            Check(new FileInfo(FixtureLeaves.Log + ".1").Length == LogLimit && new FileInfo(FixtureLeaves.Log).Length <= LogLimit, "4MiB rotation replaces same-name .log.1");
            Check(Directory.GetFiles(FixtureLeaves.LogRoot).Length == 2, "only ordinary log and one backup created");
            VerifyRows("rotation");
            FixtureLeaves.Reset("rotation-newline", 0); var scope = Begin(); scope.Stage("boundary", "x");
            int bytes = Encoding.UTF8.GetByteCount(File.ReadAllLines(FixtureLeaves.Log).Last() + "\n");
            File.WriteAllBytes(FixtureLeaves.Log, new byte[LogLimit - bytes]); scope.Stage("boundary", "x");
            Check(new FileInfo(FixtureLeaves.Log).Length == LogLimit && !File.Exists(FixtureLeaves.Log + ".1"), "exact byte budget including newline does not rotate");
            scope.Stage("boundary", "x"); Check(new FileInfo(FixtureLeaves.Log + ".1").Length == LogLimit, "one additional complete line rotates at exact byte limit"); scope.Dispose();
        });
        Case("lock-concurrency-and-culture", () => {
            FixtureLeaves.Reset("shared-lock", 0); var scope = Begin(); var entered = new ManualResetEventSlim();
            Task task;
            lock (XboxSdkDiagnostics.LogLock) { task = Task.Run(() => { entered.Set(); scope.Report("lock-contender"); }); entered.Wait(); Check(!task.Wait(80), "actual production logger contends on shared Xbox lock"); }
            Check(task.Wait(5000), "shared lock released without deadlock"); scope.Dispose();
            FixtureLeaves.Reset("concurrency", 0); var parallel = Begin(); Parallel.For(0, 1000, i => parallel.Report("并发🙂")); Parallel.For(0, 20, i => parallel.Dispose());
            Check(PhaseRows("scope-end").Length == 1 && PhaseRows("sdk-progress").Length == 64 && Counter("dropped") == 936, "concurrent Report/Dispose has one end and bounded exact counters");
            VerifyRows("concurrency");
            FixtureLeaves.Reset("culture", 0); var before = CultureInfo.CurrentCulture;
            try { CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("fr-FR"); using (Begin()) { } } finally { CultureInfo.CurrentCulture = before; }
            Check(Regex.IsMatch(Detail(PhaseRows("scope-end").Single()), @"elapsedMs=\d+\.\d"), "scope elapsed uses invariant decimal under fr-FR");
            FixtureLeaves.Reset("hostile-text", 0); var hostile = UsbipSetupEvidence.TryBegin("steamdeck", new string('"', 10000), new string('\\', 10000), 17, new string('汉', 10000), KnownCore, "m")!;
            hostile.Stage("🙂", "unused"); hostile.Stage(new string('q', 3000), new string('"', 3000) + "\ud800"); hostile.Dispose();
            Check(File.ReadAllLines(FixtureLeaves.Log).All(line => Encoding.UTF8.GetByteCount(line + "\n") <= 1024), "worst escaped correlation/text still <=1024 UTF8 bytes");
            Check(PhaseRows("scope-end").Length == 1 && Counter("textTruncated") > 0, "hostile text cannot remove final row or truncation count");
        });
        Check(Environment.GetEnvironmentVariable("HIDMAESTRO_DIAG") == diag && Environment.GetEnvironmentVariable("HIDMAESTRO_TIMING") == timing, "no HIDMAESTRO_DIAG/TIMING environment changes");
        Check(!AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name!.StartsWith("HIDMaestro", StringComparison.OrdinalIgnoreCase)), "no real SDK loaded");
        var result = new { pass = Failures.Count == 0, passed = Failures.Count == 0, status = Failures.Count == 0 ? "PASS" : "FAIL", cases = Cases, checks = Checks.Count, failures = Failures,
            assertions = Checks, skipped = Skipped, realSdkLoaded = false, realSdkCalled = false, realDeviceApiCalled = false, hardwareIo = false, realRegistryRead = false, realCmCalled = false, driverInstallCalled = false, normalHostMainExecuted = false,
            leafSubstitutions = new[] { "ReadRegistryValues body", "ReadCmStatus body", "private LocalApplicationData log directory", "trusted Windows directory for private file IO", "XboxSdkDiagnostics same-name static LogLock carrier" } };
        File.WriteAllText(Path.Combine(FixtureLeaves.Root, "FIXTURE-RESULT.json"), JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }), new UTF8Encoding(false));
        Console.WriteLine(JsonSerializer.Serialize(result)); return Failures.Count == 0 ? 0 : 1;
    }
}
