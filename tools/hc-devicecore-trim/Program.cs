using Mono.Cecil;
using HcDeviceCoreTrim;

try
{
    if(args.Length==0)throw new ArgumentException("Commands: inspect <dll> <json>; trim <dll> <policy.json> <upstream-manifest.json> <output-directory>; verify <original> <candidate> <policy> <output-directory>");
    if(args[0]=="sdl-touchpad-guard"&&args.Length==4) { SdlTouchpadGuard.Build(args[1],args[2],args[3]); return; }
    if(args[0]=="sdl-timer-patch"&&args.Length==4) { SdlTimerPatch.Build(args[1],args[2],args[3]); return; }
    if(args[0]=="sdl-timer-audit"&&args.Length==3) { SdlTimerAudit.Run(args[1],args[2]); return; }
    if(args[0]=="modern-closure"&&args.Length==5)
    {
        try { ModernClosure.Build(args[1],args[2],args[3],args[4]); }
        catch(Exception error) { if(Directory.Exists(args[4]))Util.Save(Path.Combine(args[4],"build-failure.json"),new{Status="failed",Exception=error.ToString(),HardwareExecuted=false});throw; }
        return;
    }
    if(args[0]=="inspect"&&args.Length==3)
    {
        using var a=Util.Read(args[1],out var resolver); using var cleanup=resolver;
        var methods=Util.Types(a.MainModule).SelectMany(t=>t.Methods).Where(m=>m.HasBody).ToArray();
        var strings=methods.SelectMany(m=>m.Body.Instructions.Where(i=>i.Operand is string).Select(i=>new{Method=m.FullName,Type=m.DeclaringType.FullName,Offset=i.Offset,Value=(string)i.Operand})).ToArray();
        var resources=a.MainModule.Resources.Select(r=> r is EmbeddedResource e ? (object)new {
            r.Name,Kind=r.ResourceType.ToString(),Bytes=e.GetResourceData().Length,Sha256=Util.Hash(e.GetResourceData()),
            Entries=r.Name.EndsWith(".resources",StringComparison.Ordinal)? ResourceBlob.Read(e.GetResourceData()).Entries.OrderByDescending(x=>x.Bytes).Select(x=>new{x.Name,x.Bytes,x.Type,Sha256=Util.Hash(x.Raw),StringConsumers=strings.Where(s=>s.Value.Contains(x.Name,StringComparison.OrdinalIgnoreCase)).Select(s=>s.Method).Distinct().ToArray()}).ToArray():null
        }:new{r.Name,Kind=r.ResourceType.ToString()}).ToArray();
        Util.Save(args[2],new{Input=Path.GetFullPath(args[1]),Sha256=Util.HashFile(args[1]),Assembly=a.Name.FullName,a.MainModule.Mvid,Resources=resources,ResourcePathStrings=strings.Where(s=>s.Value.Contains("Resources/",StringComparison.OrdinalIgnoreCase)||s.Value.Contains(";component/",StringComparison.OrdinalIgnoreCase)).ToArray()});
        Console.WriteLine(args[2]);return;
    }
    if(args[0]=="resource-check"&&args.Length==3)
    {
        Util.Save(args[2],RuntimeResourceCheck.Check(args[1]));Console.WriteLine(args[2]);return;
    }
    if(args[0]=="keygen"&&args.Length==3)
    {
        if(File.Exists(args[1]))throw new IOException("Refuse key overwrite");Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);
        using var rsa=System.Security.Cryptography.RSA.Create(1024);File.WriteAllText(args[1],rsa.ExportRSAPrivateKeyPem());
        var key=DevelopmentSigning.PublicKey(rsa);Util.Save(args[2],new{Purpose="YMCC DeviceCore local development signing identity; not upstream",PublicKeyHex=Convert.ToHexString(key),PublicKeyToken=DevelopmentSigning.Token(key),Bits=rsa.KeySize});Console.WriteLine(args[2]);return;
    }
    if(args[0]=="signature"&&args.Length==3)
    {
        Util.Save(args[2],new{Sha256=Util.HashFile(args[1]),RsaSignatureValid=DevelopmentSigning.Verify(File.ReadAllBytes(args[1])),Windows=DevelopmentSigning.WindowsVerify(args[1])});Console.WriteLine(args[2]);return;
    }
    if(args[0]=="audit"&&args.Length==3)
    {
        using var a=Util.Read(args[1],out var resolver);using var cleanup=resolver;
        Util.Save(args[2],new ConsumptionAudit(a).Report(a));Console.WriteLine(args[2]);return;
    }
    if(args[0]=="trim"&&args.Length==5)
    {
        string input=Path.GetFullPath(args[1]), folder=Path.GetFullPath(args[4]);
        var policy=System.Text.Json.JsonSerializer.Deserialize<TrimPolicy>(File.ReadAllText(args[2]),Util.Json)??throw new InvalidDataException("Invalid policy");
        byte[] original=File.ReadAllBytes(input);if(Util.Hash(original)!=policy.InputSha256)throw new InvalidDataException("Input hash differs from policy");
        using var manifest=System.Text.Json.JsonDocument.Parse(File.ReadAllText(args[3]));
        var file=manifest.RootElement.GetProperty("files").EnumerateArray().Single(x=>x.GetProperty("path").GetString()==Path.GetFileName(input));
        if(file.GetProperty("sha256").GetString()!=Util.Hash(original))throw new InvalidDataException("Upstream runtime manifest hash mismatch");
        if(Directory.Exists(folder)&&Directory.EnumerateFileSystemEntries(folder).Any())throw new IOException("Candidate directory must be empty; refuse overwrite");
        Directory.CreateDirectory(folder);
        var replacements=new Dictionary<string,byte[]>();
        using(var a=Util.Read(input,out var resolver))
        using(resolver)
        {
            Util.Save(Path.Combine(folder,"consumption-evidence.json"),EvidenceRules.Validate(a,policy));
            foreach(var group in policy.Removals.GroupBy(r=>r.Container))
            {
                if(group.Select(r=>r.Name).Distinct(StringComparer.Ordinal).Count()!=group.Count())throw new InvalidDataException("Duplicate policy entry");
                var resource=a.MainModule.Resources.OfType<EmbeddedResource>().Single(r=>r.Name==group.Key);var blob=ResourceBlob.Read(resource.GetResourceData());
                foreach(var removal in group)
                {
                    var e=blob.Entries.Single(e=>e.Name==removal.Name);
                    if(Util.Hash(e.Raw)!=removal.Sha256||string.IsNullOrWhiteSpace(removal.Evidence))throw new InvalidDataException("Missing evidence or entry hash mismatch");
                    if(removal.Name.Contains("led_presets",StringComparison.OrdinalIgnoreCase)||removal.Name.EndsWith(".dll",StringComparison.OrdinalIgnoreCase)||removal.Name.EndsWith(".bin",StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Protected device/native/ResourceManager resources may not be removed");
                    if(group.Key!="HandheldCompanion.Properties.Resources.resources"&&!removal.Name.EndsWith(".png",StringComparison.OrdinalIgnoreCase)&&!removal.Name.EndsWith(".baml",StringComparison.OrdinalIgnoreCase)&&!removal.Name.EndsWith(".ttf",StringComparison.OrdinalIgnoreCase)&&!removal.Name.EndsWith(".otf",StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Only reviewed UI asset types can be removed");
                }
                var set=group.Select(r=>r.Name).ToHashSet(StringComparer.Ordinal);replacements[group.Key]=blob.Write(blob.Entries.Where(e=>!set.Contains(e.Name)));
            }
        }
        byte[] identity=System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes("YMCC-DEVICECORE-RESOURCE-TRIM-v1\n"+policy.InputSha256+"\n"+(policy.NewPublicKeyHex==null?System.Text.Json.JsonSerializer.Serialize(new{policy.InputSha256,policy.Purpose,policy.Removals},Util.Json):System.Text.Json.JsonSerializer.Serialize(policy,Util.Json))));
        var mvid=new Guid(identity.AsSpan(0,16));byte[] candidate=PeCompactor.Compact(original,replacements,mvid,policy.NewPublicKeyHex!=null);
        if(policy.NewPublicKeyHex!=null) candidate=DevelopmentSigning.Resign(candidate,policy.SigningKeyPath??throw new InvalidDataException("Signing key path required"),policy.NewPublicKeyHex);
        string dll=Path.Combine(folder,Path.GetFileName(input));File.WriteAllBytes(dll,candidate);
        var verification=Verification.Compare(input,dll,policy,Path.Combine(folder,"method-comparison.json"));Util.Save(Path.Combine(folder,"verification.json"),verification);
        Util.Save(Path.Combine(folder,"YMCC-DEVICECORE.candidate.json"),new{SchemaVersion=1,Deployable=false,Readiness=policy.NewPublicKeyHex==null?"static-verified-devicecore-candidate-requires-host-integration-and-callback-gate":"BLOCKED-signing-identity-reference-closure-not-retargeted",DependencyClosureRetargeted=false,RuntimeId="YMCC-DEVICECORE-RESOURCE-TRIM-"+Util.Hash(candidate)[..16],UpstreamRuntimeId=manifest.RootElement.GetProperty("runtimeId").GetString(),Origin="YMCC modified scratch candidate; not the upstream package",UpstreamSha256=Util.Hash(original),UpstreamManifestSha256=Util.HashFile(args[3]),CandidateSha256=Util.Hash(candidate),Bytes=candidate.Length,SavedBytes=original.Length-candidate.Length,NewModuleMvid=mvid,AssemblyBindingIdentityPreserved=policy.NewPublicKeyHex==null,StrongName=policy.NewPublicKeyHex==null?"unsigned; signed inputs refused without explicit new YMCC development identity":"YMCC development key; PublicKeyToken="+DevelopmentSigning.Token(Convert.FromHexString(policy.NewPublicKeyHex)),Authenticode="unsigned; certificate-bearing inputs refused",ConsumerContract="Devicecore UI-isolated candidate only. Upstream manifest hashes do not authorize this candidate. Host must opt in to the new identity and exclude HC ordinary UI entry point.",Removed=policy.Removals,Validation=verification});
        File.Copy(args[2],Path.Combine(folder,"policy.json"));Console.WriteLine($"CANDIDATE {dll}\nBYTES {candidate.Length}\nSAVED {original.Length-candidate.Length}");return;
    }
    if(args[0]=="verify"&&args.Length==5)
    {
        var policy=System.Text.Json.JsonSerializer.Deserialize<TrimPolicy>(File.ReadAllText(args[3]),Util.Json)??throw new InvalidDataException("Invalid policy");
        Directory.CreateDirectory(args[4]);Util.Save(Path.Combine(args[4],"verification.json"),Verification.Compare(args[1],args[2],policy,Path.Combine(args[4],"method-comparison.json")));Console.WriteLine("PASS");return;
    }
    throw new ArgumentException("Invalid command/argument count");
}
catch(Exception error){Console.Error.WriteLine(error);Environment.ExitCode=1;}






