using System.Collections;
using System.Reflection;
using HIDMaestro;

// Selftests exercise the stock catalog and codecs without HMContext's service,
// extraction or owner-sweep prewarm. No controller/context/SDK worker is created.
internal sealed class SdkDataOnlyProfiles : IDisposable
{
    private readonly Dictionary<string, HMProfile> _profiles = new(StringComparer.OrdinalIgnoreCase);
    internal int LoadDefaultProfiles()
    {
        if (_profiles.Count != 0) return 0;
        var sdk = typeof(HMProfile).Assembly;
        if (sdk.ManifestModule.ModuleVersionId != new Guid("86aed9a3-c024-4596-b02c-650082ffb852"))
            throw new InvalidDataException("data-only-catalog-sdk-identity");
        var database = sdk.GetType("HIDMaestro.Internal.ProfileDatabase", true)!;
        var innerType = sdk.GetType("HIDMaestro.Internal.ControllerProfile", true)!;
        var load = database.GetMethod("LoadEmbedded", BindingFlags.Static | BindingFlags.Public, null, Type.EmptyTypes, null)
            ?? throw new MissingMethodException("stock-data-only-catalog-loader");
        var db = load.Invoke(null, null)!;
        var all = (IEnumerable)database.GetProperty("All", BindingFlags.Instance | BindingFlags.Public)!.GetValue(db)!;
        var wrap = typeof(HMProfile).GetConstructor(BindingFlags.Instance | BindingFlags.NonPublic, null, new[] { innerType }, null)
            ?? throw new MissingMethodException("stock-data-only-profile-wrapper");
        foreach (var inner in all)
        {
            var profile = (HMProfile)wrap.Invoke(new[] { inner });
            _profiles.Add(profile.Id, profile);
        }
        return _profiles.Count;
    }
    internal HMProfile? GetProfile(string id) => _profiles.GetValueOrDefault(id);
    public void Dispose() { } // Data model only; no device or SDK context lifetime.
}
