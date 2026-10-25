using System.IO;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Resources;
using System.Security.Cryptography;
using YeManFanHost;
internal static class MsiFanResourcePrerequisiteTest {
    private static byte[] ReadPeResourceOnly(string path) {
        // No Assembly.Load, device construction, module initialization or vendor
        // method invocation: only the PE metadata and managed resource stream.
        using var pe=new PEReader(File.OpenRead(path));var metadata=pe.GetMetadataReader();
        if(metadata.GetString(metadata.GetAssemblyDefinition().Name)!="HandheldCompanion")throw new InvalidOperationException("wrong candidate assembly identity");
        foreach(var handle in metadata.ManifestResources) {
            var resource=metadata.GetManifestResource(handle);
            if(!resource.Implementation.IsNil || !metadata.GetString(resource.Name).EndsWith(".g.resources",StringComparison.Ordinal))continue;
            var section=pe.GetSectionData(pe.PEHeaders.CorHeader!.ResourcesDirectory.RelativeVirtualAddress);
            var count=section.GetReader(checked((int)resource.Offset),4).ReadInt32();
            using var stream=new MemoryStream(section.GetContent(checked((int)resource.Offset+4),count).ToArray());
            using var reader=new ResourceReader(stream);var entries=reader.GetEnumerator();
            while(entries.MoveNext())if(entries.Key is string key && key.Equals("resources/msiapcfg.dll",StringComparison.OrdinalIgnoreCase)) {
                using var bytes=new MemoryStream();((Stream)entries.Value!).CopyTo(bytes);return bytes.ToArray();
            }
        }
        throw new InvalidOperationException("candidate HC embedded MSI payload missing");
    }
    public static int Main(string[] args) {
        if(args.Length!=3)return 2;
        var root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);
        var expected=File.ReadAllBytes(args[1]);var checks=new List<object>();
        void Check(string name,bool ok){checks.Add(new{name,ok});if(!ok)throw new InvalidOperationException(name);}
        var sourcePayload=MsiWmiPayloadResource.ReadEmbeddedPayload(Assembly.GetExecutingAssembly());
        Check("actual resource-loader preserves frozen HC vendor bytes",sourcePayload.SequenceEqual(expected));
        var candidatePayload=ReadPeResourceOnly(args[2]);
        Check("actual shipped HC PE resource matches frozen vendor payload without loading HC",candidatePayload.SequenceEqual(expected));
        var target=Path.Combine(root,"windows-simulation","SysWOW64","msiapcfg.dll");
        Check("missing file prepared with current production helper",MsiWmiPayloadResource.EnsurePresent(Assembly.GetExecutingAssembly(),target)=="hc-embedded-vendor-payload-prepared");
        Check("prepared bytes exactly match current vendor resource",File.ReadAllBytes(target).SequenceEqual(expected));
        var sentinel=new byte[]{1,2,3,4};File.WriteAllBytes(target,sentinel);
        Check("existing OEM file preserved rather than forcibly replaced",MsiWmiPayloadResource.EnsurePresent(Assembly.GetExecutingAssembly(),target)=="existing-oem-file-preserved"&&File.ReadAllBytes(target).SequenceEqual(sentinel));
        Check("resource temporary files cleaned",Directory.GetFiles(Path.GetDirectoryName(target)!,"*.tmp").Length==0);
        bool rejected=false;try{MsiWmiPayloadResource.ReadEmbeddedPayload(typeof(string).Assembly);}catch(InvalidOperationException){rejected=true;}
        Check("foreign resource assembly rejected",rejected);
        foreach(var name in FanLifecycleCloseOrchestrator.RunSelfTest())Check(name,true);
        bool ioFailed=false;var blocker=Path.Combine(root,"path-blocker");File.WriteAllText(blocker,"not a directory");
        try{MsiWmiPayloadResource.EnsurePresent(Assembly.GetExecutingAssembly(),Path.Combine(blocker,"msiapcfg.dll"));}catch(IOException){ioFailed=true;}
        Check("resource prerequisite I/O failure remains explicit",ioFailed);
        var receipt=new{checks,cases=checks.Count,failures=0,vendorSha256=Convert.ToHexString(SHA256.HashData(expected)).ToLowerInvariant(),
            candidatePath=Path.GetFullPath(args[2]),candidateResourceSha256=Convert.ToHexString(SHA256.HashData(candidatePayload)).ToLowerInvariant(),
            actualHcAssemblyLoaded=false,hardwareWrites=false,registryWrites=false,pnpChanges=false,
            scope="Production resource/close helpers; all writes in isolated synthetic Windows directory; PE-only vendor resource inspection; no WMI calls or fan controller"};
        File.WriteAllText(Path.Combine(root,"msi-fan-resource-prerequisite-result.json"),System.Text.Json.JsonSerializer.Serialize(receipt,new System.Text.Json.JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine($"cases={checks.Count} failed=0");return 0;
    }
}
