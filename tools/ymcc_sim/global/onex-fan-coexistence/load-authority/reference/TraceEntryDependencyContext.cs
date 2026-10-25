using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;

// Review tool only: preserves the selected producer's dependency metadata when
// the outer entry assembly is the MTA/STA tracing executable. No assembly load.
static class TraceEntryDependencyContext
{
    // Exact DependencyContextPaths.Create IL uses char 59, not a platform-derived delimiter.
    const char DependencyListSeparator = ';';
    public sealed record Binding(string tool, string depsPath, string depsSha256,
        string entryAssembly, string previous, string effective)
    {
        public bool verified { get; set; }
        public bool metadataOnly => true;
    }

    static string Hash(string file) => Convert.ToHexString(
        SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant();

    static bool Same(string a, string b) => string.Equals(
        Path.GetFullPath(a), Path.GetFullPath(b), StringComparison.OrdinalIgnoreCase);

    public static Binding Bind(string tool, string expectedDepsSha256, string workspace)
    {
        tool = Path.GetFullPath(tool);
        workspace = Path.GetFullPath(workspace);
        var name = Path.GetFileName(tool);
        if (name != "OneXFanCoexistence.dll" && name != "JoinedFactoryProbe.dll")
            throw new InvalidOperationException("TRACE_UNREVIEWED_ENTRY_METADATA_TOOL");
        var deps = Path.ChangeExtension(tool, ".deps.json");
        if (!deps.StartsWith(workspace + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
            || deps.Contains(DependencyListSeparator) || !File.Exists(deps)
            || (File.GetAttributes(deps) & FileAttributes.ReparsePoint) != 0
            || expectedDepsSha256.Length != 64 || Hash(deps) != expectedDepsSha256)
            throw new InvalidOperationException("TRACE_ENTRY_DEPS_IDENTITY_MISMATCH");

        // A cached default dependency context cannot be repaired safely after use.
        if (AppDomain.CurrentDomain.GetAssemblies().Any(a =>
            a.GetName().Name == "Microsoft.Extensions.DependencyModel"))
            throw new InvalidOperationException("TRACE_ENTRY_DEPS_ALREADY_CACHED_OR_LOADED");
        var entry = Assembly.GetEntryAssembly()?.Location
            ?? throw new InvalidOperationException("TRACE_ENTRY_ASSEMBLY_MISSING");
        var entryName = Path.GetFileName(entry);
        if (entryName != "MTA.dll" && entryName != "STA.dll")
            throw new InvalidOperationException("TRACE_UNREVIEWED_TRACER_ENTRY");
        var tracerDeps = Path.ChangeExtension(entry, ".deps.json");
        if (File.Exists(tracerDeps))
            throw new InvalidOperationException("TRACE_UNEXPECTED_TRACER_DEPS_SIDECAR");
        var previous = AppContext.GetData("APP_CONTEXT_DEPS_FILES") as string
            ?? throw new InvalidOperationException("TRACE_ENTRY_DEPS_CONTEXT_MISSING");
        var entries = previous.Split(DependencyListSeparator);
        var application = entries.FirstOrDefault(p => !string.IsNullOrEmpty(p));
        if (application is null || !Same(application, tracerDeps))
            throw new InvalidOperationException("TRACE_ENTRY_APPLICATION_DEPS_NOT_TRACER");
        if (entries.Count(p => !string.IsNullOrEmpty(p) && Same(p, tracerDeps)) != 1
            || entries.Any(p => !string.IsNullOrEmpty(p) && Same(p, deps)))
            throw new InvalidOperationException("TRACE_ENTRY_DEPS_CONTEXT_SHAPE_CHANGED");
        var effective = string.Join(DependencyListSeparator, entries.Select(p =>
            !string.IsNullOrEmpty(p) && Same(p, tracerDeps) ? deps : p));
        AppContext.SetData("APP_CONTEXT_DEPS_FILES", effective);
        return new Binding(tool, deps, expectedDepsSha256, entry, previous, effective);
    }

    public static void Verify(Binding binding)
    {
        if (Hash(binding.depsPath) != binding.depsSha256
            || !string.Equals(AppContext.GetData("APP_CONTEXT_DEPS_FILES") as string,
                binding.effective, StringComparison.Ordinal))
            throw new InvalidOperationException("TRACE_ENTRY_DEPS_CHANGED_DURING_TOOL");
        binding.verified = true;
    }
}
