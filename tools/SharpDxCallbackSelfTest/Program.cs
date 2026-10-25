using System.Diagnostics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using Mono.Cecil;

public sealed record Check(string Id, bool Passed, string Detail);
public sealed record ProbeResult(string Kind, string Root, string Status, List<Check> Checks, object? Data, string[] NativeAttempts, string[] NewTargetNativeModules);
public sealed record FilePin(string Path, long Bytes, string Sha256);
public static class Program
{
    public const string ExpectedManifest = "559d2f90fe7f1c4585d00e0d3d6896141013018bade203eb8ec46b87fead81df";
    public static readonly string[] Names = ["SharpDX.dll", "SharpDX.DirectInput.dll", "SharpDX.Direct3D9.dll"];
    public static readonly JsonSerializerOptions Json = new() { WriteIndented = true, PropertyNameCaseInsensitive = true };
    public static IEnumerable<TypeDefinition> Types(ModuleDefinition m) => m.Types.SelectMany(Types);
    public static IEnumerable<TypeDefinition> Types(TypeDefinition t) => new[] { t }.Concat(t.NestedTypes.SelectMany(Types));
    public static string Scope(TypeReference t) => (t is TypeSpecification s ? s.ElementType : t).Scope?.Name?.Replace(".dll", "") ?? "";
    public static bool Target(string s) => Names.Any(x => Path.GetFileNameWithoutExtension(x) == s);
    public static void Write(string p, object o) { Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(p))!); File.WriteAllText(p, JsonSerializer.Serialize(o, Json) + "\n"); }
    public static FilePin Pin(string p) => new(Path.GetFullPath(p), new FileInfo(p).Length, Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(p))));
    public static void Require(bool ok, string detail) { if (!ok) throw new InvalidOperationException(detail); }
    public static Exception Unwrap(Exception e) => e is TargetInvocationException { InnerException: not null } t ? Unwrap(t.InnerException!) : e;
    static int Main(string[] a)
    {
        try
        {
            if (a.Length == 4 && a[0] == "probe")
            {
                var result = ManagedProbe.Run(a[1], a[2]); Write(a[3], result);
                Console.WriteLine($"{a[1]} {result.Status}: {string.Join("; ", result.Checks.Where(c => !c.Passed).Select(c => c.Id + " " + c.Detail))}");
                return result.Status == "passed" ? 0 : 1;
            }
            if (a.Length == 8 && a[0] == "run") return Run(a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
            if (a.Length == 5 && a[0] == "inspect") { Metadata.Inspect(a[1], a[2], a[3], a[4]); return 0; }
            Console.Error.WriteLine("run runtime shared upstream evidence expectedManifestSha256 full|compat knownTrimmedRoot | probe enum|format|shadow root result.json | inspect runtime shared upstream evidence");
            return 2;
        }
        catch (Exception e) { Console.Error.WriteLine(Unwrap(e)); return 2; }
    }
    static List<FilePin> Snapshot(params string[] roots) => roots.SelectMany(r => Directory.EnumerateFiles(r, "*", SearchOption.AllDirectories)).Order(StringComparer.OrdinalIgnoreCase).Select(Pin).ToList();
    static int Run(string runtime, string shared, string upstream, string evidence, string expectedManifest, string scope, string knownTrimmed)
    {
        Require(scope is "full" or "compat", "Scope must be full or compat");
        runtime=Path.GetFullPath(runtime); shared=Path.GetFullPath(shared); upstream=Path.GetFullPath(upstream); evidence=Path.GetFullPath(evidence); knownTrimmed=Path.GetFullPath(knownTrimmed);
        // Evidence must never be placed inside an input runtime or upstream tree.
        foreach (var root in new[]{runtime,shared,upstream,knownTrimmed}) Require(!evidence.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) && evidence != root, "Evidence directory overlaps an input tree");
        Directory.CreateDirectory(evidence);
        var checks = new List<Check>(); var before=Snapshot(runtime,shared); Write(Path.Combine(evidence,"inputs-before.json"),before);
        var manifestPath=Path.Combine(runtime,"HandheldCompanion.runtime.json");
        Require(Pin(manifestPath).Sha256==expectedManifest, "Runtime manifest differs from the explicitly pinned input; select the intended runtime before running");
        using (var manifest=JsonDocument.Parse(File.ReadAllText(manifestPath)))
        {
            foreach (var f in manifest.RootElement.GetProperty("files").EnumerateArray())
            {
                var p=Path.GetFullPath(Path.Combine(runtime,f.GetProperty("path").GetString()!));
                Require(p.StartsWith(runtime+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase),"Manifest escapes runtime");
                Require(Pin(p).Sha256==f.GetProperty("sha256").GetString(),"Manifest entry mismatch: "+p);
            }
            checks.Add(new("manifest-input",true,$"{Pin(manifestPath).Sha256}; {manifest.RootElement.GetProperty("files").GetArrayLength()} entries verified"));
        }
        Write(Path.Combine(evidence,"originals.json"), Names.Select(n=>Pin(Path.Combine(upstream,n))).ToArray());
        Metadata.Inspect(runtime,shared,upstream,evidence);
        var consumer=Metadata.ConsumerAudit(runtime,shared,upstream);
        Write(Path.Combine(evidence,"consumer-closure.json"),consumer);
        checks.Add(new("real-hc-metadata-closure",consumer.Failures.Count==0,$"{consumer.ManagedFiles} managed files; {consumer.HcEdges.Count} HC IL edges; {consumer.Reachable.Count} target methods; {consumer.Failures.Count} unresolved references"));
        var contracts=Metadata.Compare(upstream,runtime);
        Write(Path.Combine(evidence,"current-contracts.json"),contracts);
        checks.Add(new("current-enum-contract",contracts.Enum.Count==0,string.Join("; ",contracts.Enum)));
        checks.Add(new("current-format-contract",contracts.Format.Count==0,string.Join("; ",contracts.Format)));
        // Deleted upstream shadow APIs are reported separately; they are not invented HC roots.
        var baselineEnum=Child("enum",upstream,Path.Combine(evidence,"positive-upstream-enum.json"));
        var baselineFormat=Child("format",upstream,Path.Combine(evidence,"positive-upstream-format.json"));
        var baselineShadow=Child("shadow",upstream,Path.Combine(evidence,"positive-upstream-shadow.json"));
        var currentEnum=Child("enum",runtime,Path.Combine(evidence,"positive-current-enum.json"));
        var currentFormat=Child("format",runtime,Path.Combine(evidence,"positive-current-format.json"));
        var currentShadow=Child("shadow",runtime,Path.Combine(evidence,"current-upstream-shadow-surface.json"));
        foreach (var (name,result) in new[]{("upstream-enum",baselineEnum),("upstream-format",baselineFormat),("upstream-shadow",baselineShadow),("current-enum",currentEnum),("current-format",currentFormat)})
            checks.Add(new(name,result.Status=="passed",string.Join("; ",result.Checks.Where(c=>!c.Passed).Select(c=>c.Detail))));
        checks.Add(new("format-baseline-equality",baselineFormat.Status=="passed"&&currentFormat.Status=="passed"&&JsonSerializer.SerializeToElement(baselineFormat.Data,Json).GetProperty("Payload").GetRawText()==JsonSerializer.SerializeToElement(currentFormat.Data,Json).GetProperty("Payload").GetRawText(),"Compare every generated data-object name, GUID, offset, flag and instance, plus raw structure size"));
        bool fullShadowPassed=contracts.Shadow.Count==0&&currentShadow.Status=="passed";
        checks.Add(new("current-shadow-scope",scope=="compat"||fullShadowPassed,$"scope={scope}; Include metadata={contracts.Shadow.Count==0}; managed initialization/binding={currentShadow.Status}"));
        var knownTrimmedBefore=Names.Select(n=>Pin(Path.Combine(knownTrimmed,n))).ToArray();
        var knownTrimmedContracts=Metadata.Compare(upstream,knownTrimmed);
        var knownTrimmedShadow=Child("shadow",knownTrimmed,Path.Combine(evidence,"known-trimmed-shadow.json"));
        bool knownTrimmedRejected=knownTrimmedShadow.Status=="failed" && knownTrimmedContracts.Shadow.Any(e=>e.StartsWith("Type missing: SharpDX.Direct3D9.Include",StringComparison.Ordinal)||e.Contains("SharpDX.CppObject.ToCallbackPtr",StringComparison.Ordinal))
            && knownTrimmedShadow.Checks.Any(c=>!c.Passed&&c.Id=="safety"&&(c.Detail.Contains("Safe root missing: SharpDX.Direct3D9.IncludeShadow",StringComparison.Ordinal)||c.Detail.Contains("Safe method missing: SharpDX.CppObject.ToCallbackPtr",StringComparison.Ordinal)));
        checks.Add(new("known-trimmed-shadow-rejected",knownTrimmedRejected,"Historical trimmed three-DLL input must lack the tested Include shadow roots; arbitrary exceptions do not qualify"));
        var knownTrimmedAfter=Names.Select(n=>Pin(Path.Combine(knownTrimmed,n))).ToArray();
        checks.Add(new("known-trimmed-input-unchanged",knownTrimmedBefore.SequenceEqual(knownTrimmedAfter),"Historical negative input is read-only"));
        Write(Path.Combine(evidence,"known-trimmed-input.json"),new{Root=knownTrimmed,Files=knownTrimmedBefore,ShadowContractFailures=knownTrimmedContracts.Shadow,Rejected=knownTrimmedRejected,Unchanged=knownTrimmedBefore.SequenceEqual(knownTrimmedAfter)});
        var negativeRows=new List<object>();
        foreach (var spec in Fixtures.Specs)
        {
            var root=Path.Combine(evidence,"fixtures",spec.Name); Directory.CreateDirectory(root);
            foreach(var name in Names) File.Copy(Path.Combine(spec.Kind=="shadow"?upstream:runtime,name),Path.Combine(root,name),true);
            Fixtures.Mutate(root,spec.Name);
            var comparison=Metadata.Compare(upstream,root); Write(Path.Combine(root,"contract.json"),comparison);
            var errors=spec.Kind switch { "enum"=>comparison.Enum, "format"=>comparison.Format, _=>comparison.Shadow };
            ProbeResult? probe=null;
            if(spec.Execute) probe=Child(spec.Kind,root,Path.Combine(root,"probe.json"));
            bool rejected=errors.Count>0 || probe?.Status=="failed";
            bool matched=(!spec.Execute || probe?.Checks.Any(c=>c.Id==spec.ExpectedFailure && !c.Passed)==true)
                && (spec.Execute || errors.Any(x=>x.Contains(spec.ExpectedFailure,StringComparison.Ordinal)));
            checks.Add(new("negative-"+spec.Name,rejected&&matched,$"metadata={errors.Count}; executed={spec.Execute}; expected={spec.ExpectedFailure}; observed={string.Join("; ",errors.Take(2))}"));
            negativeRows.Add(new{spec.Name,spec.Kind,spec.Execute,spec.ExpectedFailure,Rejected=rejected,Matched=matched,Errors=errors,Probe=probe,Files=Names.Select(n=>Pin(Path.Combine(root,n))).ToArray()});
        }
        Write(Path.Combine(evidence,"negative-controls.json"),negativeRows);
        var matrix=new List<object>();
        // This pair-necessity experiment applies to a current trimmed surface. With restored inputs,
        // the ordinary current-shadow positive check proves the requested full binding surface.
        if(!fullShadowPassed)
        foreach(var combo in new[]{("core-original-only",true,false),("d3d-original-only",false,true),("core-and-d3d-original",true,true)})
        {
            var root=Path.Combine(evidence,"fixtures",combo.Item1); Directory.CreateDirectory(root);
            foreach(var name in Names) File.Copy(Path.Combine(name=="SharpDX.dll"&&combo.Item2 || name=="SharpDX.Direct3D9.dll"&&combo.Item3?upstream:runtime,name),Path.Combine(root,name),true);
            var p=Child("shadow",root,Path.Combine(root,"probe.json"));
            checks.Add(new("matrix-"+combo.Item1,combo.Item2&&combo.Item3?p.Status=="passed":p.Status=="failed",p.Status));
            matrix.Add(new{ Name=combo.Item1,Result=p });
        }
        if(fullShadowPassed)matrix.Add(new{Status="not-applicable",Reason="Current Include shadow is already complete; current managed binding is the positive control"});
        Write(Path.Combine(evidence,"shadow-restoration-matrix.json"),matrix);
        var after=Snapshot(runtime,shared); Write(Path.Combine(evidence,"inputs-after.json"),after);
        checks.Add(new("input-trees-unchanged",before.SequenceEqual(after),"All runtime/shared file paths, SHA256 hashes and byte counts compared before/after"));
        var status=checks.All(c=>c.Passed)?"passed":"failed";
        Write(Path.Combine(evidence,"result.json"),new { schemaVersion=2,status,passed=status=="passed",generatedAt=DateTimeOffset.Now,
            runtimeRoot=runtime,sharedRuntimeRoot=shared,upstreamRuntimeRoot=upstream,runtimeManifestSha256=Pin(manifestPath).Sha256,
            scope,checks,positiveChecksPassed=checks.Where(c=>!c.Id.StartsWith("negative-")).All(c=>c.Passed),
            knownBadCount=Fixtures.Specs.Length,knownBadRejected=checks.Count(c=>c.Id.StartsWith("negative-")&&c.Passed),
            currentHcConsumerClosurePassed=consumer.Failures.Count==0&&currentEnum.Status=="passed"&&currentFormat.Status=="passed",
            currentCallbacks=new{enumPassed=currentEnum.Status=="passed",formatPassed=currentFormat.Status=="passed"},
            knownTrimmedRoot=knownTrimmed,knownTrimmedShadowRejected=knownTrimmedRejected,
            currentShadowSurface=new{metadataPassed=contracts.Shadow.Count==0,bindingPassed=currentShadow.Status=="passed",passed=fullShadowPassed,details="Include shadow initialization/cache and both delegate bindings; Open/Close bodies not invoked"},
            targetCodeExecuted=true,targetNativePolicy="deny",hardwareAccess=false,
            targetNativeAttempts=new[]{baselineEnum,baselineFormat,baselineShadow,currentEnum,currentFormat,currentShadow,knownTrimmedShadow}.Sum(p=>p.NativeAttempts.Length),
            sourceInputsUnchanged=before.SequenceEqual(after),
            restorationAssessment=new{confirmedDefectInObservedHcConsumers=false,
                rationale="Restoring original SharpDX files preserves broader dynamic ABI and compatibility margin; current HC enum/format did not demonstrate a defect in the former trimmed files",
                observedHcConsumerRestoreRequired=consumer.Failures.Count==0&&currentEnum.Status=="passed"&&currentFormat.Status=="passed"?Array.Empty<string>():null,
                includeShadowOwnerPair=new[]{Pin(Path.Combine(upstream,"SharpDX.dll")),Pin(Path.Combine(upstream,"SharpDX.Direct3D9.dll"))}},
            limits=new[]{"No HC/Host/driver start, no DirectInput enumeration, no Direct3D/device creation", "Only framework heap, GCHandle and managed delegate thunks; all target DllImport resolution is denied", "Include Open/Close delegates are bound but not invoked; full shader, COM, Inspectable and ComStream execution is not covered", "Static reachability is bounded to current runtime/shared binaries; future consumers require a fresh audit"} });
        Console.WriteLine($"{status}: {checks.Count(c=>c.Passed)}/{checks.Count} checks; evidence {evidence}");
        foreach(var c in checks.Where(c=>!c.Passed)) Console.WriteLine(c.Id+": "+c.Detail);
        return status=="passed"?0:1;
    }
    static ProbeResult Child(string kind,string root,string output)
    {
        var info=new ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true };
        info.ArgumentList.Add(Assembly.GetExecutingAssembly().Location); info.ArgumentList.Add("probe"); info.ArgumentList.Add(kind); info.ArgumentList.Add(root); info.ArgumentList.Add(output);
        using var p=Process.Start(info)!;
        var stdout=p.StandardOutput.ReadToEndAsync(); var stderr=p.StandardError.ReadToEndAsync();
        if(!p.WaitForExit(30000)) { p.Kill(true); throw new TimeoutException("Bounded probe exceeded 30 seconds: "+kind); }
        Task.WaitAll(stdout,stderr);
        File.WriteAllText(Path.ChangeExtension(output,"log"),stdout.Result+stderr.Result);
        Require(File.Exists(output),$"Probe crashed ({p.ExitCode}): {stderr.Result}");
        var result=JsonSerializer.Deserialize<ProbeResult>(File.ReadAllText(output),Json)!;
        Require(p.ExitCode==(result.Status=="passed"?0:1),"Probe exit code/report mismatch");
        Require(result.NativeAttempts.Length==0&&result.NewTargetNativeModules.Length==0,"Target native policy violated, including in a negative control");
        return result;
    }
}





