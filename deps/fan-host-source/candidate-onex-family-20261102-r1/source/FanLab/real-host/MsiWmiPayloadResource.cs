using System.IO;
using System.Reflection;
using System.Resources;

namespace YeManFanHost;

/** HC ClawA1M.CheckAndDeployWmiAcpi supplies this exact embedded vendor payload.
 * A standalone Host has neither HC's WPF entry assembly nor its pack URI setup.
 * Prepare the same missing file before HC Open; leave registry/PnP/methods to HC.
 * Existing OEM files are preserved, and tests use a temporary directory. */
internal static class MsiWmiPayloadResource
{
    internal static byte[] ReadEmbeddedPayload(Assembly hcAssembly)
    {
        if (hcAssembly.GetName().Name != "HandheldCompanion")
            throw new InvalidOperationException("MSI WMI payload requires the authenticated HC assembly");
        var resources = hcAssembly.GetManifestResourceNames()
            .Single(name => name.EndsWith(".g.resources", StringComparison.Ordinal));
        using var container = hcAssembly.GetManifestResourceStream(resources)
            ?? throw new InvalidOperationException("HC embedded resources unavailable");
        using var reader = new ResourceReader(container);
        var entries = reader.GetEnumerator();
        while (entries.MoveNext())
        {
            if (entries.Key is not string key ||
                !key.Equals("resources/msiapcfg.dll", StringComparison.OrdinalIgnoreCase)) continue;
            if (entries.Value is not Stream payload)
                throw new InvalidOperationException("HC MSI WMI resource is not a stream");
            using var bytes = new MemoryStream();
            payload.CopyTo(bytes);
            if (bytes.Length < 2 || bytes.Length > 1_048_576)
                throw new InvalidOperationException("HC MSI WMI resource size invalid");
            var data = bytes.ToArray();
            if (data[0] != 'M' || data[1] != 'Z')
                throw new InvalidOperationException("HC MSI WMI resource is not a PE payload");
            return data;
        }
        throw new InvalidOperationException("HC embedded MSI WMI resource missing");
    }

    internal static string EnsurePresent(Assembly hcAssembly, string target)
    {
        // Match HC: do not replace a file already supplied by the OEM.
        if (File.Exists(target)) return "existing-oem-file-preserved";
        var bytes = ReadEmbeddedPayload(hcAssembly);
        var directory = Path.GetDirectoryName(Path.GetFullPath(target))!;
        Directory.CreateDirectory(directory);
        var temporary = Path.Combine(directory, ".msiapcfg-" + Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            File.WriteAllBytes(temporary, bytes);
            try { File.Move(temporary, target, overwrite: false); }
            catch (IOException) when (File.Exists(target)) { return "concurrent-oem-file-preserved"; }
            return "hc-embedded-vendor-payload-prepared";
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}

/** One existing close owner. A returned release error must not prevent resource
 * cleanup; an HC operation still running must not receive a second lifecycle call.
 * No release/ownership success is manufactured by this orchestration. */
internal static class FanLifecycleCloseOrchestrator
{
    internal static void Execute(Action? release, Action close, Func<bool> releaseStillInFlight,
        Action<Exception> recordReturnedReleaseFailure)
    {
        if (release is not null)
        {
            try { release(); }
            catch (Exception error) when (!releaseStillInFlight())
            {
                recordReturnedReleaseFailure(error);
            }
        }
        close();
    }

    internal static IReadOnlyList<string> RunSelfTest()
    {
        var checks = new List<string>();
        int closed = 0, failures = 0;
        Execute(() => { }, () => closed++, () => false, _ => failures++);
        if (closed != 1 || failures != 0) throw new InvalidOperationException("normal close owner duplicated");
        checks.Add("fan948-release-success-one-lifecycle-close");
        closed = failures = 0;
        Execute(() => throw new InvalidOperationException("returned-wmi-failure"), () => closed++,
            () => false, _ => failures++);
        if (closed != 1 || failures != 1) throw new InvalidOperationException("returned release failure suppressed lifecycle Close");
        checks.Add("fan948-returned-release-failure-still-closes-resources");
        closed = failures = 0;
        try
        {
            Execute(() => throw new TimeoutException("still-in-flight"), () => closed++, () => true, _ => failures++);
            throw new InvalidOperationException("in-flight timeout lost");
        }
        catch (TimeoutException) { }
        if (closed != 0 || failures != 0) throw new InvalidOperationException("in-flight HC operation received concurrent Close");
        checks.Add("fan948-inflight-release-never-reentered-with-close");
        bool closeFailed = false;
        try { Execute(null, () => throw new IOException("close-failed"), () => false, _ => failures++); }
        catch (IOException) { closeFailed = true; }
        if (!closeFailed) throw new InvalidOperationException("Close failure was hidden");
        checks.Add("fan948-close-failure-retained-not-release-success");
        return checks;
    }
}