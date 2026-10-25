using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Xml;
using System.Xml.Linq;

// Startup-only official 2.0 preparation. Check mode observes metadata/files only.
// Create preparation and deploy share the SDK Deploy gate; owner claims protect
// in-flight PnP after that short gate. No hand-written signing or cleanup path.
internal static class HidMaestroPackagePreparation
{
    internal const string CoreSha = "8FE7BDB21EC497591F527B7B87C4BC7B41B45C35A06CC211A7A0DCE48838ED2C";
    internal const string CoreMvid = "86aed9a3-c024-4596-b02c-650082ffb852";
    internal const long CoreBytes = 63_076_352;
    internal const string EmbeddedManifest = "894aad9e4aa7365e13b387a3db497ee554a693efcecef636f9760ff0d1c264ff";
    internal const string CurrentVersion = "2.0.0.2201";
    internal const string Previous112Version = "1.12.0.2302";
    internal const string SdkVersion = "2.0.0.0";
    internal const string StagingVersionTag = "2.0.0";
    internal const string LegacyVersion = "1.4.7.12";
    internal const string NativePrefix = "HIDMaestro.Native.x64.";
    internal static readonly string[] Infs = ["hidmaestro.inf", "hidmaestro_xusb.inf"];
    internal const int MaximumInfBytes = 128 * 1024;
    internal const int MaximumDriverRows = 4096;
    internal const int MaximumInventoryCharacters = 4 * 1024 * 1024;
    internal const string ReadyScope = "exact-current-driver-store-packages;raw-driver-and-signed-package-staging-are-independent-observations;not-catalog-signature-validity-or-device-binding-proof";

    internal const int InventoryUnrecognizedExit = 18;
    internal const int MaintenanceDeferredExit = 19;
    internal sealed class InventorySchemaUnrecognizedException : IOException
    {
        internal string ObservedRoot { get; }
        internal InventorySchemaUnrecognizedException(string root) : base("unrecognized driver inventory schema: " + root)
        { ObservedRoot = root; }
    }
    internal sealed record InventoryEvidence(string Path, int OriginalCharacters, int RetainedCharacters,
        bool Truncated, int? ExitCode, bool TimedOut, string WriteStatus, string? Error,
        string Scope = "read-only-pnputil-output;not-empty-store-or-device-readiness-proof");
    internal static InventoryEvidence CaptureInventory(string report, string text, int? exitCode, bool timedOut, string suffix = ".drivers.xml")
    {
        string path = report + suffix;
        int retained = Math.Min(text.Length, MaximumInventoryCharacters);
        try {
            File.WriteAllText(path, text[..retained], new UTF8Encoding(false));
            return new(path, text.Length, retained, retained != text.Length, exitCode, timedOut, "retained", null);
        }
        catch (Exception ex) {
            // Evidence IO cannot change preparation or grant automatic maintenance.
            return new(path, text.Length, 0, retained != text.Length, exitCode, timedOut, "unavailable", ex.GetType().Name);
        }
    }

    internal sealed record Driver(string Published, string Original, string Provider, string RawVersion);
    internal sealed record InfContract(string Name, string Sha256, string RawVersion, string Catalog);
    internal sealed record SdkContract(string Sha256, string Mvid, string Manifest, string Stage,
        string StageSource, IReadOnlyList<InfContract> Infs)
    {
        public string PackageStage { get; init; } = "";
    }
    internal sealed record PackageRow(Driver Driver, string Classification);
    internal sealed record Inventory(string Status, bool CurrentComplete, IReadOnlyList<PackageRow> Packages)
    {
        public bool AtLeastCurrentComplete { get; init; }
        public bool NewerPresent { get; init; }
        public bool PreparationKnown { get; init; }
        public bool AdditionalVersionsUnproven { get; init; }
    }
    internal sealed record CatalogFile(string Name, string Status, long? Bytes);
    internal sealed record StagedInf(string Name, string Path, string Status, long? Bytes, string? Sha256,
        string? RawVersion, IReadOnlyList<CatalogFile> Catalogs);
    internal sealed record Staging(string Stage, string Status, IReadOnlyList<StagedInf> Infs,
        string SignatureVerification = "not-performed", bool WritesIssued = false,
        string ArtifactRole = "raw-driver", bool CatalogRequiredHere = false);
    internal sealed record MaintenanceFacts(bool Admin, bool InstallMutexHeld, bool StartupOwnerValid,
        bool OwnDevices, bool BlockingClients);
    internal sealed record Maintenance(string Status, MaintenanceFacts Observed,
        bool LifecycleExclusive = false, bool OperationDeadlineProven = false,
        bool AutomaticWritesEnabled = false);
    internal sealed record Review(string Status, bool StoreReady, Inventory Inventory, Staging Staging,
        Staging SignedPackage, Maintenance Maintenance, IReadOnlyList<string> SdkCalls, string Scope = ReadyScope,
        bool InstallAttempted = false, bool HardwareVerified = false);

    internal static List<Driver> ParseDrivers(string text)
    {
        if (text.Length > MaximumInventoryCharacters) throw new InvalidDataException("driver inventory exceeds budget");
        using var reader = XmlReader.Create(new StringReader(text), new XmlReaderSettings {
            DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null,
            MaxCharactersInDocument = MaximumInventoryCharacters
        });
        var doc = XDocument.Load(reader);
        var inventoryRoot = doc.Root ?? throw new InventorySchemaUnrecognizedException("missing");
        // Both layouts are observed PnPUtil inventories. The command identifies
        // the payload; the OS version is metadata, never a compatibility ceiling.
        bool legacyFormat = inventoryRoot.Name.LocalName == "Drivers";
        bool pnputilFormat = inventoryRoot.Name.LocalName == "PnpUtil" &&
            string.Equals(inventoryRoot.Attribute("Command")?.Value, "/enum-drivers /format xml", StringComparison.OrdinalIgnoreCase) &&
            inventoryRoot.Attribute("Version")?.Value is { Length: > 0 and <= 64 };
        if (!legacyFormat && !pnputilFormat) {
            string observedRoot = inventoryRoot.Name.ToString();
            throw new InventorySchemaUnrecognizedException(observedRoot[..Math.Min(128, observedRoot.Length)]);
        }
        var rows = inventoryRoot.Elements().ToList();
        if (rows.Count == 0 || rows.Count > MaximumDriverRows || rows.Any(e => e.Name.LocalName != "Driver"))
            throw new InvalidDataException("empty, mixed or over-budget driver inventory");
        string Field(XElement e, string name) {
            var values = e.Elements().Where(x => x.Name.LocalName == name).ToArray();
            if (values.Length > 1) throw new InvalidDataException("duplicate driver field: " + name);
            var value = values.SingleOrDefault()?.Value ?? "";
            if (value.Length > 512) throw new InvalidDataException("driver field exceeds budget");
            return value;
        }
        var drivers = rows.Select(e => new Driver(e.Attribute("DriverName")?.Value ?? "",
            Field(e, "OriginalName"), Field(e, "ProviderName"), Field(e, "DriverVersion"))).ToList();
        if (drivers.Any(d => string.IsNullOrWhiteSpace(d.Published) || d.Published.Length > 512 ||
                string.IsNullOrWhiteSpace(d.Original) || string.IsNullOrWhiteSpace(d.Provider)))
            throw new InvalidDataException("driver identity fields missing; cannot prove an empty owned store");
        return drivers;
    }

    internal static bool Owned(Driver d) => d.Provider.Equals("HIDMaestro", StringComparison.OrdinalIgnoreCase) &&
        Infs.Contains(d.Original, StringComparer.OrdinalIgnoreCase);
    internal static bool HistoricalPairPresent(IEnumerable<Driver> rows) =>
        Infs.All(name => rows.Any(d => Owned(d) && d.Original.Equals(name, StringComparison.OrdinalIgnoreCase)));
    static bool PublishedInf(string value) => value.Length is >= 8 and <= 32 &&
        value.StartsWith("oem", StringComparison.OrdinalIgnoreCase) && value.EndsWith(".inf", StringComparison.OrdinalIgnoreCase) &&
        value.AsSpan(3, value.Length - 7).IndexOfAnyExceptInRange('0', '9') < 0;
    internal static string VersionClass(string raw)
    {
        var fields = raw.Trim().Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        if (fields.Length != 2 || !DateTime.TryParseExact(fields[0], ["MM/dd/yyyy", "M/d/yyyy", "MM/d/yyyy", "M/dd/yyyy"],
                CultureInfo.InvariantCulture, DateTimeStyles.None, out var date)) return "unproven";
        if (date.Date == new DateTime(2026, 10, 19) && fields[1] == CurrentVersion) return "current";
        if (date.Date == new DateTime(2026, 10, 13) && fields[1] == Previous112Version) return "known-legacy-112";
        if (date.Date == new DateTime(2026, 8, 21) && fields[1] == LegacyVersion) return "known-legacy";
        if (Version.TryParse(fields[1], out var version) && version.Build >= 0 && version.Revision >= 0) {
            // Official DriverStamp compares release, date, then revision. A newer
            // store is preserved; this is not a claim its live protocol serves us.
            var release = new Version(version.Major, version.Minor, version.Build);
            int comparison = release.CompareTo(new Version(2, 0, 0));
            if (comparison > 0 || (comparison == 0 &&
                    (date.Date > new DateTime(2026, 10, 19) ||
                    (date.Date == new DateTime(2026, 10, 19) && version.Revision > 2201))))
                return "newer-preserved-protocol-unverified";
        }
        return "unknown-version";
    }
    internal static Inventory InspectInventory(IReadOnlyList<Driver> drivers)
    {
        var selected = drivers.Where(d => Infs.Contains(d.Original, StringComparer.OrdinalIgnoreCase)).ToList();
        var rows = selected.Select(d => new PackageRow(d,
            !Owned(d) || !PublishedInf(d.Published) ? "identity-unproven" : VersionClass(d.RawVersion))).ToList();
        if (rows.Any(r => r.Classification == "identity-unproven") ||
            rows.GroupBy(r => r.Driver.Published, StringComparer.OrdinalIgnoreCase).Any(g => g.Count() != 1))
            return new("DRIVER_PACKAGE_IDENTITY_UNPROVEN", false, rows);
        var complete = Infs.All(name => rows.Any(r => r.Classification == "current" &&
            r.Driver.Original.Equals(name, StringComparison.OrdinalIgnoreCase)));
        var atLeast = Infs.All(name => rows.Any(r => (r.Classification is "current" or "newer-preserved-protocol-unverified") &&
            r.Driver.Original.Equals(name, StringComparison.OrdinalIgnoreCase)));
        bool unresolved = rows.Any(r => r.Classification is "unproven" or "unknown-version");
        // Store supply and automatic maintenance are separate evidence. A complete
        // proved pair stays available when an extra historical version is unknown;
        // preserve every row and forbid maintenance instead of guessing its role.
        // This never proves the selected device binding, protocol or game input.
        if (unresolved && !atLeast)
            return new("DRIVER_PACKAGE_VERSION_UNPROVEN", false, rows);
        bool newer = rows.Any(r => r.Classification == "newer-preserved-protocol-unverified");
        string status = complete ? "CURRENT_DRIVER_PACKAGES" : atLeast ? "NEWER_DRIVER_PACKAGES_PRESERVED" :
            rows.Any(r => r.Classification is "known-legacy" or "known-legacy-112") ? "DRIVER_PACKAGE_VERSION_MISMATCH" :
            rows.Count == 0 ? "MISSING_DRIVER" : "DRIVER_PACKAGE_INCOMPLETE";
        return new(status, complete, rows) { AtLeastCurrentComplete=atLeast,
            NewerPresent=newer, PreparationKnown=rows.Count>0 && !unresolved,
            AdditionalVersionsUnproven=unresolved };
    }

    static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    static byte[] ReadResource(Assembly sdk, string name)
    {
        using var stream = sdk.GetManifestResourceStream(name) ?? throw new InvalidDataException("missing SDK resource: " + name);
        if (stream.Length > 1024 * 1024) throw new InvalidDataException("SDK metadata resource over budget");
        var data = new byte[(int)stream.Length]; stream.ReadExactly(data); return data;
    }
    internal static (string Version, string Catalog) ReadInfVersion(byte[] data)
    {
        if (data.Length > MaximumInfBytes) throw new InvalidDataException("INF exceeds budget");
        using var stream = new MemoryStream(data);
        using var reader = new StreamReader(stream, new UTF8Encoding(false, true), true);
        string? version = null, catalog = null; bool section = false;
        foreach (var raw in reader.ReadToEnd().Split('\n')) {
            var line = raw.Split(';', 2)[0].Trim();
            if (line.StartsWith('[')) { section = line.Equals("[Version]", StringComparison.OrdinalIgnoreCase); continue; }
            if (!section) continue;
            int equals = line.IndexOf('='); if (equals < 0) continue;
            var key = line[..equals].Trim(); var value = line[(equals + 1)..].Trim().Trim('"');
            if (key.Equals("DriverVer", StringComparison.OrdinalIgnoreCase)) {
                if (version is not null) throw new InvalidDataException("duplicate DriverVer"); version = value;
            }
            if (key.Equals("CatalogFile", StringComparison.OrdinalIgnoreCase)) {
                if (catalog is not null) throw new InvalidDataException("duplicate CatalogFile"); catalog = value;
            }
        }
        if (version is null || catalog is null || !SafeCatalog(catalog)) throw new InvalidDataException("INF version/catalog unproven");
        return (version, catalog);
    }
    static bool SafeCatalog(string name) => name.Length is >= 5 and <= 128 &&
        name.IndexOfAny(['/', '\\', ':']) < 0 && Path.GetFileName(name) == name &&
        name.EndsWith(".cat", StringComparison.OrdinalIgnoreCase) && !name.Contains("..", StringComparison.Ordinal);

    // Metadata/resources only. Never invokes BuildDir, EnsureExtracted, HMContext,
    // IsDriverInstalled, SignDrivers, GenerateCatalogs, InstallDrivers or FullDeploy.
    internal static SdkContract ReadSdkMetadata(Assembly sdk, string expectedStage)
    {
        if (sdk.ManifestModule.ModuleVersionId.ToString() != CoreMvid || sdk.GetName().Version?.ToString() != SdkVersion)
            throw new InvalidDataException("SDK metadata identity mismatch");
        var builder = sdk.GetType("HIDMaestro.Internal.DriverBuilder", true)!;
        foreach (var name in new[] { "SignDrivers", "GenerateCatalogs", "InstallDrivers", "IsDriverInstalled" }) {
            var method = builder.GetMethod(name, BindingFlags.Public | BindingFlags.Static);
            if (method is null || method.ReturnType != typeof(bool) || method.GetParameters().Length != 0)
                throw new InvalidDataException("SDK method metadata contract mismatch: " + name);
        }
        var deploy = builder.GetMethod("FullDeploy", BindingFlags.Public | BindingFlags.Static);
        if (deploy is null || deploy.ReturnType != typeof(bool) || deploy.GetParameters().Length != 1 ||
                deploy.GetParameters()[0].ParameterType != typeof(bool))
            throw new InvalidDataException("original first-install SDK metadata contract mismatch");
        ValidateOfficialPreparationEntry(sdk);
        var stageProperty = builder.GetProperty("StagingDir", BindingFlags.Public | BindingFlags.Static);
        if (stageProperty?.PropertyType != typeof(string) || stageProperty.GetMethod?.IsPublic != true ||
                stageProperty.GetIndexParameters().Length != 0)
            throw new InvalidDataException("SDK staging property metadata contract mismatch");
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var name in new[] { "HIDMaestro.dll", "HMXInput.dll", "hidmaestro.inf", "hidmaestro_xusb.inf", "hmswd.exe" }) {
            var resource = NativePrefix + name;
            hash.AppendData(Encoding.UTF8.GetBytes(resource + "\n")); hash.AppendData(ReadResource(sdk, resource));
        }
        var manifest = Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
        if (manifest != EmbeddedManifest) throw new InvalidDataException("SDK payload manifest mismatch");
        var infs = Infs.Select(name => {
            var bytes = ReadResource(sdk, NativePrefix + name); var info = ReadInfVersion(bytes);
            if (info.Version != "10/19/2026,2.0.0.2201" || info.Catalog != Path.ChangeExtension(name, ".cat"))
                throw new InvalidDataException("SDK INF metadata mismatch");
            return new InfContract(name, Hash(bytes), info.Version, info.Catalog);
        }).ToArray();
        return new(CoreSha.ToLowerInvariant(), CoreMvid, manifest, Path.GetFullPath(expectedStage),
            "derived-from-pinned-2.0-ProtectedStaging-ProgramFiles-version-manifest-algorithm;SDK-getters-not-invoked", infs)
        { PackageStage = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(expectedStage))!, "package") };
    }

    internal static string DefaultRawStage() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "HIDMaestro", "Staging",
        StagingVersionTag + "-" + EmbeddedManifest[..16], "driver");

    internal static Staging InspectStaging(SdkContract sdk) => InspectDirectory(sdk, sdk.Stage, false);
    internal static Staging InspectSignedPackage(SdkContract sdk) => InspectDirectory(sdk, sdk.PackageStage, true);
    static Staging InspectDirectory(SdkContract sdk, string directory, bool catalogsRequired)
    {
        var files = new List<StagedInf>();
        foreach (var inf in sdk.Infs) {
            var path = Path.Combine(directory, inf.Name);
            try {
                using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                long before = input.Length;
                if (before > MaximumInfBytes) { files.Add(new(inf.Name,path,"inf-over-budget",before,null,null,[])); continue; }
                var bytes = new byte[(int)before]; input.ReadExactly(bytes);
                if (input.Length != before || input.ReadByte() != -1) {
                    files.Add(new(inf.Name,path,"inf-changed-during-read",before,null,null,[])); continue;
                }
                var info = ReadInfVersion(bytes); var actualHash = Hash(bytes);
                if (actualHash != inf.Sha256 || info.Version != inf.RawVersion || info.Catalog != inf.Catalog) {
                    files.Add(new(inf.Name,path,"inf-identity-mismatch",before,actualHash,info.Version,[])); continue;
                }
                var catalogPath = Path.Combine(directory, inf.Catalog);
                CatalogFile catalog;
                try {
                    using var catalogFile = new FileStream(catalogPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    catalog = new(inf.Catalog, catalogFile.Length > 0 ? "present-unverified" : "empty", catalogFile.Length);
                } catch (FileNotFoundException) { catalog = new(inf.Catalog,"missing",null); }
                catch (DirectoryNotFoundException) { catalog = new(inf.Catalog,"missing",null); }
                catch (Exception ex) { catalog = new(inf.Catalog,"unavailable:" + ex.GetType().Name,null); }
                files.Add(new(inf.Name,path,"current-inf",before,actualHash,info.Version,[catalog]));
            } catch (FileNotFoundException) { files.Add(new(inf.Name,path,"inf-missing",null,null,null,[])); }
            catch (DirectoryNotFoundException) { files.Add(new(inf.Name,path,"staging-missing",null,null,null,[])); }
            catch (Exception ex) { files.Add(new(inf.Name,path,"unavailable:" + ex.GetType().Name,null,null,null,[])); }
        }
        string status = catalogsRequired ? "SIGNED_PACKAGE_FILES_PRESENT_UNVERIFIED" : "CURRENT_RAW_DRIVER_INF_PRESENT";
        if (files.Count != 2 || sdk.Infs.Count != 2 || !Infs.All(n => sdk.Infs.Count(i => i.Name == n) == 1)) status = "SDK_INF_CONTRACT_UNPROVEN";
        else if (files.Any(i => i.Status == "inf-identity-mismatch")) status = "SDK_STAGING_INF_IDENTITY_MISMATCH";
        else if (files.Any(i => i.Status != "current-inf")) status = "SDK_STAGING_INF_UNPROVEN";
        else if (catalogsRequired && files.Any(i => i.Catalogs.Any(c => c.Status == "missing"))) status = "SDK_STAGING_CATALOG_MISSING";
        else if (catalogsRequired && files.Any(i => i.Catalogs.Any(c => c.Status == "empty"))) status = "SDK_STAGING_CATALOG_EMPTY";
        else if (catalogsRequired && files.Any(i => i.Catalogs.Count != 1 || i.Catalogs.Any(c => c.Status != "present-unverified"))) status = "SDK_STAGING_CATALOG_UNPROVEN";
        return new(directory,status,files,ArtifactRole: catalogsRequired ? "signed-package" : "raw-driver", CatalogRequiredHere: catalogsRequired);
    }

    internal static Review Inspect(bool prepare, SdkContract sdk, IReadOnlyList<Driver> drivers, MaintenanceFacts facts)
    {
        if (sdk.Sha256 != CoreSha.ToLowerInvariant() || sdk.Mvid != CoreMvid || sdk.Manifest != EmbeddedManifest)
            throw new InvalidDataException("SDK contract not approved");
        var inventory = InspectInventory(drivers);
        // 2.0 deliberately separates unsigned raw driver staging from the signed
        // package. A raw directory has no required CAT; signed package staging may
        // not exist on a healthy AlreadyCurrent fast path. Neither artifact proves
        // or disproves installed-version readiness or current target binding.
        var staging = InspectStaging(sdk);
        var signedPackage = InspectSignedPackage(sdk);
        var ready = inventory.AtLeastCurrentComplete;
        string status = ready ? (inventory.NewerPresent ? "DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED" : "DRIVER_STORE_READY") : inventory.Status;
        string maintenanceStatus = !prepare ? "check-only-no-SDK-methods" :
            inventory.Packages.Count!=0 && !inventory.PreparationKnown ? "unverified-package-metadata-no-automatic-maintenance" :
            "official-2.0-deploy-policy-eligible-after-guards";
        var maintenance = new Maintenance(maintenanceStatus, facts);
        return new(status, ready, inventory, staging, signedPackage, maintenance, []);
    }
    internal enum OfficialOutcome { AlreadyCurrent=0, Installed=1, Upgraded=2, KeptNewer=3 }
    internal sealed record Observation(IReadOnlyList<Driver> Drivers, MaintenanceFacts Facts,
        bool RepositoryRemnants, bool EnumValid=true, bool SdkIdentity=true);
    internal sealed record OfficialCall(OfficialOutcome Outcome, string Entry, string EquivalentPublicEntry,
        bool FullDeployWrapperInvoked=false);
    internal sealed record PreparationResult(string Status, string Decision, Review Before, Review? After,
        OfficialCall? Official, bool SdkCallAttempted, string BindingEvidence, string? Error=null);
    internal interface IPreparationRuntime
    {
        Observation Observe(string phase);
        OfficialCall PrepareOfficial(SdkContract sdk);
        void Record(string phase, object evidence);
    }
    internal sealed class PreparationRuntime(Func<string, Observation> observe,
        Func<SdkContract, OfficialCall> prepare, Action<string, object> record) : IPreparationRuntime
    {
        public Observation Observe(string phase) => observe(phase);
        public OfficialCall PrepareOfficial(SdkContract sdk) => prepare(sdk);
        public void Record(string phase, object evidence) => record(phase,evidence);
    }

    // FullDeploy ignores its bool and executes Run(null), discards the enum and
    // returns true. Validate the exact pinned IL forwarding contract; using that
    // same official terminal once preserves its real enum without a second deploy.
    // This function only reads metadata; it does not run an SDK method/getter.
    internal static MethodInfo ValidateOfficialPreparationEntry(Assembly sdk)
    {
        if (sdk.ManifestModule.ModuleVersionId.ToString()!=CoreMvid)
            throw new InvalidDataException("SDK preparation identity mismatch");
        var builder=sdk.GetType("HIDMaestro.Internal.DriverBuilder",true)!;
        var deploy=sdk.GetType("HIDMaestro.Internal.DriverDeploy",true)!;
        var outcome=sdk.GetType("HIDMaestro.HMDriverInstallOutcome",true)!;
        var run=deploy.GetMethod("Run",BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.DeclaredOnly);
        var full=builder.GetMethod("FullDeploy",BindingFlags.Public|BindingFlags.Static|BindingFlags.DeclaredOnly);
        if(run is null || !run.IsAssembly || run.ReturnType!=outcome || !outcome.IsEnum ||
            run.GetParameters().Length!=1 || run.GetParameters()[0].ParameterType!=typeof(string) ||
            !run.GetParameters()[0].HasDefaultValue || run.GetParameters()[0].DefaultValue is not null ||
            full is null || full.ReturnType!=typeof(bool) || full.GetParameters().Length!=1 ||
            full.GetParameters()[0].ParameterType!=typeof(bool))
            throw new InvalidDataException("SDK official preparation contract mismatch");
        foreach(var value in Enum.GetValues<OfficialOutcome>()) {
            var field=outcome.GetField(value.ToString(),BindingFlags.Public|BindingFlags.Static);
            if(field is null || Convert.ToInt32(field.GetRawConstantValue(),CultureInfo.InvariantCulture)!=(int)value)
                throw new InvalidDataException("SDK outcome metadata mismatch");
        }
        if(outcome.GetEnumNames().Length!=4)throw new InvalidDataException("SDK outcome metadata changed");
        var il=full.GetMethodBody()?.GetILAsByteArray();
        if(il is null || il.Length!=9 || il[0]!=0x14 || il[1]!=0x28 ||
            il[6]!=0x26 || il[7]!=0x17 || il[8]!=0x2a ||
            full.Module.ResolveMethod(BitConverter.ToInt32(il,2)) is not MethodInfo target ||
            target.Module!=run.Module || target.MetadataToken!=run.MetadataToken)
            throw new InvalidDataException("FullDeploy is not the approved single Run(null) forwarding shim");
        return run;
    }
    internal static OfficialCall InvokeOfficialPreparation(Assembly sdk)
    {
        // Shipping calls this only through Execute after two guard observations.
        // Tests substitute IPreparationRuntime's terminal; they never call here.
        var method=ValidateOfficialPreparationEntry(sdk);
        object? result;
        try { result=method.Invoke(null,[null]); }
        catch(TargetInvocationException ex) {
            throw new InvalidOperationException("Official DriverDeploy.Run failed: "+ex.InnerException,ex.InnerException);
        }
        if(result is null || result.GetType()!=method.ReturnType)
            throw new InvalidDataException("official outcome object unproven");
        var value=(OfficialOutcome)Convert.ToInt32(result,CultureInfo.InvariantCulture);
        if(!Enum.IsDefined(value))throw new InvalidDataException("official outcome not admitted");
        return new(value,"HIDMaestro.Internal.DriverDeploy.Run(null)",
            "HIDMaestro.Internal.DriverBuilder.FullDeploy(bool): verified identical single terminal",false);
    }
    static string Decision(bool prepare, Observation observation, Review review)
    {
        var f=observation.Facts;
        return Program.DecideForPreparation(new Program.Facts(observation.SdkIdentity,f.Admin,f.BlockingClients,
            observation.EnumValid,observation.Drivers.Count(Owned),observation.RepositoryRemnants,f.OwnDevices,
            HistoricalPairPresent(observation.Drivers),HistoricalPairPresent(observation.Drivers)),review,prepare);
    }
    static PreparationResult WithoutOfficialCall(string decision, Review review)
    {
        // Refusing maintenance does not invalidate an independently observed current package pair.
        // Keep the refusal reason and issue no SDK call; binding is still unobserved.
        string status = decision == "CLOSE_CONTROLLER_APPS_FIRST" && review.StoreReady
            ? "DRIVER_MAINTENANCE_DEFERRED" : decision;
        return new(status,decision,review,null,null,false,"not-observed");
    }
    internal static PreparationResult Execute(bool prepare, SdkContract sdk, Observation initial, IPreparationRuntime runtime)
    {
        var before=Inspect(prepare,sdk,initial.Drivers,initial.Facts);
        string decision=Decision(prepare,initial,before);
        runtime.Record("decision",new {decision,prepare,review=before});
        if(decision is not ("INSTALL_FRESH" or "PREPARE_OFFICIAL"))
            return WithoutOfficialCall(decision,before);
        // The second observation narrows obvious owner/client changes; SDK's own
        // gate + owner census, not this snapshot, owns cross-process arbitration.
        var last=runtime.Observe("before-official");
        var recheck=Inspect(true,sdk,last.Drivers,last.Facts);
        decision=Decision(true,last,recheck);
        runtime.Record("preflight-recheck",new {decision,last,review=recheck});
        if(decision is not ("INSTALL_FRESH" or "PREPARE_OFFICIAL"))
            return WithoutOfficialCall(decision,recheck);
        OfficialCall call;
        try {
            runtime.Record("official-call-begin",new {entry="DriverDeploy.Run(null)",equivalent="FullDeploy forwarding body",
                scope="startup-maintenance-only;official-Deploy-lock-and-owner-census"});
            call=runtime.PrepareOfficial(sdk);
            if(!Enum.IsDefined(call.Outcome))throw new InvalidDataException("official outcome outside locked contract");
            runtime.Record("official-call-return",call);
        }catch(Exception ex){
            runtime.Record("official-call-fault",new {error=ex.ToString()});
            return new("OFFICIAL_PREPARATION_FAILED_OR_DEFERRED",decision,recheck,null,null,true,"not-proven",ex.ToString());
        }
        try {
            var afterObservation=runtime.Observe("after-official");
            var after=Inspect(false,sdk,afterObservation.Drivers,afterObservation.Facts);
            runtime.Record("postcondition",new {official=call,observation=afterObservation,review=after});
            if(!afterObservation.EnumValid || !afterObservation.SdkIdentity || !afterObservation.Facts.StartupOwnerValid ||
                    !afterObservation.Facts.InstallMutexHeld || !after.StoreReady ||
                    (call.Outcome==OfficialOutcome.KeptNewer && !after.Inventory.NewerPresent))
                return new("INSTALL_POSTCONDITION_UNPROVEN",decision,recheck,after,call,true,"not-proven");
            bool live=last.Facts.OwnDevices || afterObservation.Facts.OwnDevices || afterObservation.Facts.BlockingClients;
            string binding=live ? "existing-or-in-flight-owner-may-retain-old-driver;installed-versus-added-path-not-observed;binding-unproven" :
                "no-owned-device-observed;physical-binding-and-game-input-unverified";
            string status=call.Outcome==OfficialOutcome.KeptNewer ? "DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED" :
                call.Outcome==OfficialOutcome.AlreadyCurrent ? after.Status :
                live ? "DRIVER_PACKAGES_PREPARED_BINDING_UNPROVEN" : "DRIVER_PREPARED_DEVICE_UNVERIFIED";
            return new(status,decision,recheck,after,call,true,binding);
        }catch(Exception ex){
            runtime.Record("postcondition-fault",new {error=ex.ToString()});
            return new("INSTALL_POSTCONDITION_UNPROVEN",decision,recheck,null,call,true,"not-proven",ex.ToString());
        }
    }

}
