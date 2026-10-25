using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Buffers.Binary;
using Mono.Cecil;

namespace HcDeviceCoreTrim;
sealed record ClosureAssembly(string Path,string RelativePath,string Name,string FullName,string[] ReferenceNames,bool Signed,string PublicKeyHex,string PublicKeyToken);
static class ModernClosure
{
    static readonly string[] InitialTargets=["iNKORE.UI.WPF.Modern","iNKORE.UI.WPF.Modern.Controls"];
    static Guid NewMvid(string inputHash,string key)
    {
        byte[] bytes=SHA256.HashData(Encoding.UTF8.GetBytes("YMCC-MODERN-CLOSURE-v1\n"+inputHash+"\n"+key));bytes[7]=(byte)((bytes[7]&15)|64);bytes[8]=(byte)((bytes[8]&63)|128);return new Guid(bytes.AsSpan(0,16));
    }
    static string SafeFile(string root,string relative)
    {
        string full=System.IO.Path.GetFullPath(System.IO.Path.Combine(root,relative));string prefix=System.IO.Path.GetFullPath(root).TrimEnd(System.IO.Path.DirectorySeparatorChar)+System.IO.Path.DirectorySeparatorChar;
        if(!full.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Manifest path escapes runtime");return full;
    }
    static List<ClosureAssembly> Assemblies(string root)
    {
        var result=new List<ClosureAssembly>();
        foreach(string path in Directory.EnumerateFiles(root,"*",SearchOption.AllDirectories).Where(p=>p.EndsWith(".dll",StringComparison.OrdinalIgnoreCase)||p.EndsWith(".exe",StringComparison.OrdinalIgnoreCase)))
        {
            using var file=File.OpenRead(path);PEReader pe;try{pe=new PEReader(file);if(!pe.HasMetadata){pe.Dispose();continue;}}catch(BadImageFormatException){continue;}
            using(pe)
            {
                var md=pe.GetMetadataReader();if(!md.IsAssembly)continue;using var a=Util.Read(path,out var resolver);using var cleanup=resolver;
                result.Add(new(path,System.IO.Path.GetRelativePath(root,path).Replace('\\','/'),a.Name.Name,a.Name.FullName,a.MainModule.AssemblyReferences.Select(r=>r.Name).ToArray(),a.Name.HasPublicKey,Convert.ToHexString(a.Name.PublicKey),Convert.ToHexString(a.Name.PublicKeyToken).ToLowerInvariant()));
            }
        }
        return result;
    }
    static bool HasIdentityText(string path,IdentityRewriter rewriter)
    {
        using var stream=File.OpenRead(path);using var pe=new PEReader(stream);
        foreach(var s in MetadataIdentityEditor.Streams(pe.GetMetadata().GetContent().ToArray()).Where(s=>s.Name is "#Blob" or "#US" or "#Strings"))if(!s.Data.AsSpan().SequenceEqual(rewriter.RewriteBytes(s.Data)))return true;
        using var a=Util.Read(path,out var resolver);using var cleanup=resolver;
        foreach(var r in a.MainModule.Resources.OfType<EmbeddedResource>().Where(r=>r.Name.EndsWith(".resources",StringComparison.Ordinal)))
        {
            byte[] bytes=r.GetResourceData();if(!Encoding.Latin1.GetString(bytes).Contains("iNKORE",StringComparison.Ordinal))continue;
            foreach(var entry in ResourceBlob.Read(bytes).Entries.Where(e=>e.Name.EndsWith(".baml",StringComparison.OrdinalIgnoreCase)||e.TypeCode==1))if(!entry.Raw.AsSpan().SequenceEqual(rewriter.RewriteBytes(entry.Raw)))return true;
        }
        return false;
    }
    static List<object> RewriteResources(byte[] bytes,IdentityRewriter rewriter)
    {
        using var pe=new PEReader(new MemoryStream(bytes,false));var md=pe.GetMetadataReader();var dir=pe.PEHeaders.CorHeader!.ResourcesDirectory;var edits=new List<object>();if(dir.Size==0)return edits;
        int start=PeCompactor.Offset(pe.PEHeaders,dir.RelativeVirtualAddress);
        foreach(var h in md.ManifestResources)
        {
            var row=md.GetManifestResource(h);if(!row.Implementation.IsNil)continue;string name=md.GetString(row.Name);if(!name.EndsWith(".resources",StringComparison.Ordinal))continue;
            int offset=checked(start+(int)row.Offset),length=BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(offset,4));byte[] block=bytes[(offset+4)..(offset+4+length)];
            if(!Encoding.Latin1.GetString(block).Contains("iNKORE",StringComparison.Ordinal))continue;
            foreach(var entry in ResourceBlob.Read(block).Entries.Where(e=>e.Name.EndsWith(".baml",StringComparison.OrdinalIgnoreCase)||e.TypeCode==1))
            {
                byte[] changed=rewriter.RewriteBytes(entry.Raw);if(entry.Raw.AsSpan().SequenceEqual(changed))continue;
                if(changed.Length!=entry.Raw.Length)throw new InvalidDataException("BAML/resource string identity rewrite changed record lengths");
                changed.CopyTo(bytes,offset+4+entry.Offset);edits.Add(new{Container=name,entry.Name,Bytes=entry.Raw.Length,BeforeSha256=Util.Hash(entry.Raw),AfterSha256=Util.Hash(changed),BeforeIdentities=rewriter.TextIdentities(entry.Raw),AfterIdentities=rewriter.TextIdentities(changed)});
            }
        }
        return edits;
    }
    public static void Build(string inputRoot,string inputManifest,string policyPath,string outputDirectory)
    {
        inputRoot=System.IO.Path.GetFullPath(inputRoot);outputDirectory=System.IO.Path.GetFullPath(outputDirectory);
        if(Directory.Exists(outputDirectory)&&Directory.EnumerateFileSystemEntries(outputDirectory).Any())throw new IOException("Closure output must be a new empty directory");
        Directory.CreateDirectory(outputDirectory);string runtime=System.IO.Path.Combine(outputDirectory,"runtime"),evidence=System.IO.Path.Combine(outputDirectory,"evidence");Directory.CreateDirectory(runtime);Directory.CreateDirectory(evidence);
        var policy=JsonSerializer.Deserialize<TrimPolicy>(File.ReadAllText(policyPath),Util.Json)??throw new InvalidDataException("Invalid trim policy");
        if(!policy.HeadlessUiAssets||policy.NewPublicKeyHex==null||policy.SigningKeyPath==null)throw new InvalidDataException("Explicit headless profile and development signing key required");
        using var key=RSA.Create();key.ImportFromPem(File.ReadAllText(policy.SigningKeyPath));byte[] publicKey=DevelopmentSigning.PublicKey(key);
        if(!Convert.ToHexString(publicKey).Equals(policy.NewPublicKeyHex,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Development key/public identity mismatch");
        using var sourceManifest=JsonDocument.Parse(File.ReadAllText(inputManifest));string sourceManifestHash=Util.HashFile(inputManifest);
        var sourceFiles=new List<(string Path,string Sha256,long Bytes)>();
        foreach(var item in sourceManifest.RootElement.GetProperty("files").EnumerateArray())
        {
            string path=item.GetProperty("path").GetString()??throw new InvalidDataException("Empty manifest path"),hash=item.GetProperty("sha256").GetString()!;string source=SafeFile(inputRoot,path),dest=SafeFile(runtime,path);
            byte[] bytes=File.ReadAllBytes(source);if(Util.Hash(bytes)!=hash)throw new InvalidDataException("Input manifest mismatch: "+path);Directory.CreateDirectory(System.IO.Path.GetDirectoryName(dest)!);File.WriteAllBytes(dest,bytes);sourceFiles.Add((path,hash,bytes.LongLength));
        }
        var assemblies=Assemblies(inputRoot);var targets=new List<IdentityTarget>();
        void AddTarget(ClosureAssembly a)
        {
            if(!a.Signed||a.PublicKeyHex.Length!=publicKey.Length*2)throw new InvalidDataException("Rekey target must use a same-size reviewed strong-name key: "+a.Name);
            if(!DevelopmentSigning.Verify(File.ReadAllBytes(a.Path)))throw new InvalidDataException("Source strong-name verification failed: "+a.Name);
            targets.Add(new(a.Name,a.PublicKeyToken,DevelopmentSigning.Token(publicKey),a.PublicKeyHex,Convert.ToHexString(publicKey),a.FullName,a.FullName.Replace(a.PublicKeyToken,DevelopmentSigning.Token(publicKey),StringComparison.Ordinal)));
        }
        foreach(string name in InitialTargets)AddTarget(assemblies.Single(a=>a.Name==name));
        while(true)
        {
            var names=targets.Select(t=>t.Name).ToHashSet(StringComparer.Ordinal);var rw=new IdentityRewriter(targets);var extra=assemblies.Where(a=>a.Signed&&!names.Contains(a.Name)&&(a.ReferenceNames.Any(names.Contains)||HasIdentityText(a.Path,rw))).ToArray();
            if(extra.Length==0)break;foreach(var a in extra)AddTarget(a);
        }
        var rewriter=new IdentityRewriter(targets);var selectedNames=targets.Select(t=>t.Name).ToHashSet(StringComparer.Ordinal);
        var changedAssemblies=assemblies.Where(a=>selectedNames.Contains(a.Name)||a.ReferenceNames.Any(selectedNames.Contains)||HasIdentityText(a.Path,rewriter)).ToArray();
        Util.Save(System.IO.Path.Combine(evidence,"source-assemblies.json"),assemblies);Util.Save(System.IO.Path.Combine(evidence,"identity-map.json"),targets);
        var replacements=new List<object>();
        foreach(var a in changedAssemblies)
        {
            string destination=SafeFile(runtime,a.RelativePath),assemblyEvidence=System.IO.Path.Combine(evidence,a.Name);Directory.CreateDirectory(assemblyEvidence);byte[] source=File.ReadAllBytes(a.Path),working=source.ToArray();
            var target=targets.SingleOrDefault(t=>t.Name==a.Name);bool rekey=target!=null;Guid mvid=NewMvid(Util.Hash(source),policy.NewPublicKeyHex);
            var removals=a.Name=="iNKORE.UI.WPF.Modern"?policy:new TrimPolicy(Util.Hash(source),"Identity closure only; retain every resource",[]);
            if(a.Name=="iNKORE.UI.WPF.Modern")
            {
                if(Util.Hash(source)!=policy.InputSha256)throw new InvalidDataException("Modern input differs from reviewed asset policy");
                using var original=Util.Read(a.Path,out var resolver);using var cleanup=resolver;Util.Save(System.IO.Path.Combine(assemblyEvidence,"headless-resource-policy-evidence.json"),EvidenceRules.Validate(original,policy));
                var replacement=new Dictionary<string,byte[]>();
                foreach(var group in policy.Removals.GroupBy(r=>r.Container))
                {
                    var r=original.MainModule.Resources.OfType<EmbeddedResource>().Single(r=>r.Name==group.Key);var blob=ResourceBlob.Read(r.GetResourceData());var names=group.Select(r=>r.Name).ToHashSet(StringComparer.Ordinal);
                    foreach(var rm in group)if(Util.Hash(blob.Entries.Single(e=>e.Name==rm.Name).Raw)!=rm.Sha256)throw new InvalidDataException("Asset policy hash mismatch");
                    replacement[group.Key]=blob.Write(blob.Entries.Where(e=>!names.Contains(e.Name)));
                }
                working=PeCompactor.Compact(source,replacement,mvid,true);
            }
            var resourceEdits=RewriteResources(working,rewriter);MetadataEdit metadata;
            using(var stage=new PEReader(new MemoryStream(working,false)))metadata=MetadataIdentityEditor.Edit(stage,rewriter,rekey?policy.NewPublicKeyHex:null,mvid);
            byte[] final=MetadataPeRepack.Replace(working,metadata.Bytes);if(rekey)final=DevelopmentSigning.Resign(final,policy.SigningKeyPath,policy.NewPublicKeyHex);
            File.WriteAllBytes(destination,final);
            var verified=ClosureVerification.Compare(a.Path,destination,removals,rewriter,metadata.Bytes,rekey,assemblyEvidence);Util.Save(System.IO.Path.Combine(assemblyEvidence,"verification.json"),verified);
            Util.Save(System.IO.Path.Combine(assemblyEvidence,"metadata-edits.json"),new{metadata.AssemblyRefEdits,metadata.TextEdits,metadata.AppendedBlobBytes,Resources=resourceEdits,Mvid=mvid,MetadataRvaRelocated=true,OriginalCodeAndResourceRvasPreserved=true});
            using var edited=Util.Read(destination,out var finalResolver);using var finalCleanup=finalResolver;
            replacements.Add(new{Path=a.RelativePath,SourceSha256=Util.Hash(source),Sha256=Util.Hash(final),SourceBytes=source.Length,Bytes=final.Length,BeforeIdentity=a.FullName,AfterIdentity=edited.Name.FullName,StrongNameRekeyed=rekey,PublicKeyToken=Convert.ToHexString(edited.Name.PublicKeyToken).ToLowerInvariant(),StrongNameVerified=rekey?DevelopmentSigning.Verify(final):(bool?)null,Authenticode="not signed",Role=a.Name=="iNKORE.UI.WPF.Modern"?"headless assets + signature identity + friend closure":a.Name=="HandheldCompanion"?"AssemblyRef retarget; all original IL/device resources retained":"signature/AssemblyRef/BAML identity closure; every resource entry retained"});
            Console.WriteLine($"REPLACEMENT {a.RelativePath} {final.Length} {Util.Hash(final)}");
        }
        // Audit the entire output set, not only assemblies that were rewritten.
        var finalAssemblies=Assemblies(runtime);var closureRows=new List<object>();
        foreach(var a in finalAssemblies)
        {
            using var assembly=Util.Read(a.Path,out var resolver);using var cleanup=resolver;
            foreach(var r in assembly.MainModule.AssemblyReferences)
            {
                var t=targets.SingleOrDefault(t=>t.Name==r.Name);if(t==null)continue;
                if(Convert.ToHexString(r.PublicKeyToken).ToLowerInvariant()!=t.NewToken)throw new InvalidDataException("Old target AssemblyRef survived closure: "+a.Name+" -> "+r.FullName);
                var resolved=finalAssemblies.Single(x=>x.Name==r.Name);if(resolved.PublicKeyToken!=t.NewToken)throw new InvalidDataException("Target definition identity mismatch");
                closureRows.Add(new{Consumer=a.RelativePath,Reference=r.FullName,Target=resolved.RelativePath,TargetIdentity=resolved.FullName});
            }
            foreach(var attr in assembly.CustomAttributes.Where(c=>c.AttributeType.FullName=="System.Runtime.CompilerServices.InternalsVisibleToAttribute"))
            {
                string friend=attr.ConstructorArguments[0].Value as string??throw new InvalidDataException("Invalid friend attribute");
                foreach(var t in targets.Where(t=>friend.StartsWith(t.Name+",",StringComparison.Ordinal)))
                    if(!friend.EndsWith("PublicKey="+t.NewPublicKeyHex,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Friend public key not retargeted: "+a.Name);
            }
            foreach(var resource in assembly.MainModule.Resources.OfType<EmbeddedResource>().Where(r=>r.Name.EndsWith(".resources",StringComparison.Ordinal)))
            {
                foreach(var entry in ResourceBlob.Read(resource.GetResourceData()).Entries.Where(e=>e.Name.EndsWith(".baml",StringComparison.OrdinalIgnoreCase)||e.TypeCode==1))
                    foreach(var identity in rewriter.TextIdentities(entry.Raw))
                    {
                        var target=targets.SingleOrDefault(t=>t.Name==identity.Name);
                        if(target!=null&&identity.Token!=target.NewToken)throw new InvalidDataException("Old selected BAML/serialized identity: "+a.Name+" "+entry.Name);
                    }
            }
            if(HasIdentityText(a.Path,rewriter))throw new InvalidDataException("Old target textual identity survived in active metadata/BAML: "+a.Name);
        }
        var files=sourceFiles.Select(f=>new{path=f.Path,sha256=Util.HashFile(SafeFile(runtime,f.Path))}).ToArray();
        string id="YMCC-DEVICECORE-MODERN-HEADLESS-20261018-"+Util.Hash(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(files,Util.Json)))[..16];
        Util.Save(System.IO.Path.Combine(runtime,"HandheldCompanion.runtime.json"),new{schemaVersion=1,runtimeId=id,hcVersion=sourceManifest.RootElement.GetProperty("hcVersion").GetString(),hcCommit=sourceManifest.RootElement.GetProperty("hcCommit").GetString(),files,derivative=new{producer="YMCC offline Modern identity closure",sourceRuntimeId=sourceManifest.RootElement.GetProperty("runtimeId").GetString(),sourceManifestSha256=sourceManifestHash,strongNamePurpose="RSA-1024/SHA-1 legacy strong-name binding identity; local development key; NOT Authenticode or publisher/release authentication",publicKeyToken=DevelopmentSigning.Token(publicKey),headlessBoundary="No HC ordinary entry point, device construction, windows, templates, rendering, font measurement/enumeration/glyph access. FontIcon pure constructor must be separately probed.",runtimeValidationPending=true}});
        Util.Save(System.IO.Path.Combine(outputDirectory,"replacement-files.json"),replacements);Util.Save(System.IO.Path.Combine(evidence,"full-runtime-reference-closure.json"),new{Passed=true,ManagedAssemblies=finalAssemblies.Count,SelectedTargets=targets.Select(t=>t.Name).ToArray(),TargetReferences=closureRows,NoOldSelectedIdentityInActiveMetadataOrResourceText=true,UnrelatedAssemblyReferencesPreserved=true,SharedOldTokenBlobsNotOverwritten=true});
        Util.Save(System.IO.Path.Combine(outputDirectory,"build-result.json"),new{Status="STATIC-CLOSURE-PASSED-RUNTIME-GATE-PENDING",RuntimeId=id,RuntimePath=runtime,InputRuntime=inputRoot,InputManifestSha256=sourceManifestHash,SourceFiles=sourceFiles.Count,SourceBytes=sourceFiles.Sum(f=>f.Bytes),CandidateBytes=files.Sum(f=>new FileInfo(SafeFile(runtime,f.path)).Length),Replacements=replacements,Signing=new{PublicKeyToken=DevelopmentSigning.Token(publicKey),KeyBits=key.KeySize,Algorithm="strong-name RSA PKCS#1 v1.5 / SHA-1",PublisherAuthentication=false,ReleaseSignature=false},HardwareExecuted=false});
        foreach(var f in sourceFiles)if(Util.HashFile(SafeFile(inputRoot,f.Path))!=f.Sha256)throw new InvalidDataException("Frozen source changed during build");if(Util.HashFile(inputManifest)!=sourceManifestHash)throw new InvalidDataException("Source manifest changed during build");
        Console.WriteLine($"STATIC CLOSURE PASS {runtime}");
    }
}

