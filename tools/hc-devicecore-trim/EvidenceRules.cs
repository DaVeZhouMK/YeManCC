using Mono.Cecil;

namespace HcDeviceCoreTrim;
static class EvidenceRules
{
    static readonly HashSet<string> HookPayloads=new(StringComparer.Ordinal){"xinput1_x86","xinput1_x64","XInputPlusInjector64","XInputPlusInjector"};
    public static object Validate(AssemblyDefinition a,TrimPolicy policy)
    {
        if(a.Name.Name=="iNKORE.UI.WPF.Modern"&&policy.HeadlessUiAssets&&policy.NewPublicKeyHex!=null)
        {
            var allowed=new HashSet<string>(StringComparer.Ordinal){"resources/fonts/fluentsystemicons-regular.ttf","resources/fonts/fluentsystemicons-filled.ttf","resources/images/ui.wpf.modern_main_1280w.png","resources/images/wpflibrary_256w.png"};
            if(policy.Removals.Count==0||policy.Removals.Any(r=>r.Container!="iNKORE.UI.WPF.Modern.g.resources"||!allowed.Contains(r.Name)))throw new InvalidDataException("Unknown Modern asset; retain by default");
            return new{Passed=true,Profile="YMCC device-core-headless Modern assets",Removed=policy.Removals,Contract="Headless-only: no HC window, visual tree, template application, theme rendering, font measurement, font enumeration or FontFamily glyph access. BAML/theme/noise/string resources retained. Original iNKORE signing identity is replaced by a YMCC development key, so consumer binding must be validated separately.",NotCompleteLifecycleProof=true};
        }
        if(a.Name.Name!="HandheldCompanion"&&a.Name.Name!="TrimFixture")throw new InvalidDataException("No reviewed consumption profile for this assembly; inspect and retain by default");
        var audit=new ConsumptionAudit(a);var strings=audit.Strings().ToArray();
        bool Rooted(MethodDefinition m)=>audit.DeviceClosure.Contains(m.FullName)||audit.FactoryConstructorClosure.Contains(m.FullName);
        var evidence=new List<object>();
        foreach(var r in policy.Removals)
        {
            var container=a.MainModule.Resources.OfType<EmbeddedResource>().Single(x=>x.Name==r.Container);
            var entry=ResourceBlob.Read(container.GetResourceData()).Entries.Single(x=>x.Name==r.Name);
            if(Util.Hash(entry.Raw)!=r.Sha256||string.IsNullOrWhiteSpace(r.Evidence))throw new InvalidDataException("Entry hash/evidence mismatch");
            if(a.Name.Name=="TrimFixture")
            {
                if(r.Container!="TrimFixture.g.resources"||r.Name!="ui/art.png")throw new InvalidDataException("Fixture profile only permits its UI sample");
                evidence.Add(new{r.Container,r.Name,Rule="Fixture UI sample; all device/neutral resource keys retained"});continue;
            }
            if(r.Container=="HandheldCompanion.Properties.Resources.resources"&&HookPayloads.Contains(r.Name))
            {
                var getter=audit.Methods.Single(x=>x.DeclaringType.FullName=="HandheldCompanion.Properties.Resources"&&x.Name=="get_"+r.Name);
                var consumers=audit.Methods.Where(m=>m.HasBody&&m.Body.Instructions.Any(i=>i.Operand is MethodReference mr&&mr.FullName==getter.FullName)).ToArray();
                if(consumers.Length==0||consumers.Any(m=>m.DeclaringType.FullName!="HandheldCompanion.XInputPlus"||Rooted(m)))throw new InvalidDataException("Hook payload has a device/factory/unknown consumer: "+r.Name);
                evidence.Add(new{r.Container,r.Name,Rule="Hook payload getters are consumed only by non-rooted XInputPlus methods; device/factory constructor closures do not reach getters",Consumers=consumers.Select(m=>m.FullName).ToArray(),DeviceOrFactoryReachable=false});continue;
            }
            if(r.Container!="HandheldCompanion.g.resources")throw new InvalidDataException("No removal evidence for this container");
            if(r.Name.EndsWith(".baml",StringComparison.Ordinal))
            {
                var all=ResourceBlob.Read(container.GetResourceData()).Entries.Where(e=>e.Name.EndsWith(".baml",StringComparison.Ordinal)).Select(e=>e.Name).ToHashSet(StringComparer.Ordinal);
                var selected=policy.Removals.Where(x=>x.Container==r.Container&&x.Name.EndsWith(".baml",StringComparison.Ordinal)).Select(x=>x.Name).ToHashSet(StringComparer.Ordinal);
                if(!all.SetEquals(selected))throw new InvalidDataException("HC BAML must be reviewed as a complete dependency group; partial removal refused");
                var bamlLoaders=strings.Where(s=>s.Value.Contains(".xaml",StringComparison.OrdinalIgnoreCase)||s.Value.Contains(".baml",StringComparison.OrdinalIgnoreCase)).ToArray();
                if(bamlLoaders.Any(s=>Rooted(s.Method)))throw new InvalidDataException("Device/factory closure includes XAML/BAML URI consumer; retain HC BAML");
                evidence.Add(new{r.Container,r.Name,Rule="Complete HC-owned BAML group; no XAML/BAML URI in all-device or ManagerFactory constructor static closures; HC App/Views entry point excluded",DeviceOrFactoryReachable=false});continue;
            }
            if(r.Name.StartsWith("resources/deviceimages/",StringComparison.Ordinal)&&r.Name.EndsWith(".png",StringComparison.Ordinal))
            {
                var consumers=strings.Where(s=>s.Value.Contains("/DeviceImages/",StringComparison.OrdinalIgnoreCase)).ToArray();
                if(consumers.Length==0||consumers.Any(s=>Rooted(s.Method)||!s.Method.DeclaringType.FullName.StartsWith("HandheldCompanion.ViewModels.",StringComparison.Ordinal)))throw new InvalidDataException("Device illustration has a non-UI consumer");
                evidence.Add(new{r.Container,r.Name,Rule="Device illustration byte streams are consumed only by About/Welcome ViewModels; ProductIllustration calibration strings and all IL preserved",Consumers=consumers.Select(s=>s.Method.FullName).Distinct().ToArray(),DeviceOrFactoryReachable=false});continue;
            }
            if(r.Name.StartsWith("resources/controllers/",StringComparison.Ordinal)||r.Name is "resources/missingcover.png" or "resources/missingartwork.png")
            {
                var consumers=strings.Where(s=>s.Value.Contains(r.Name,StringComparison.OrdinalIgnoreCase)||s.Value.Contains("/"+r.Name["resources/".Length..],StringComparison.OrdinalIgnoreCase)).ToArray();
                if(consumers.Length==0||consumers.Any(s=>s.Method.DeclaringType.FullName!="HandheldCompanion.Managers.LibraryResources"||Rooted(s.Method)))throw new InvalidDataException("Library image has a device/factory/unknown consumer");
                evidence.Add(new{r.Container,r.Name,Rule="LibraryResources image initializer is outside all-device and ManagerFactory constructor closures",Consumers=consumers.Select(s=>s.Method.FullName).Distinct().ToArray(),DeviceOrFactoryReachable=false});continue;
            }
            throw new InvalidDataException("Unknown or operational resource retained by default: "+r.Name);
        }
        return new{Passed=true,InputSha256=policy.InputSha256,Rules=evidence,Audit=audit.Report(a),Contract="Static devicecore consumption profile only. Preserve original small components; do not execute HC ordinary App/UI entry point, ProfileManager.Start, UpdateProfileWrapper, XInputPlus or HC Hook/virtual-output workflows. No complete lifecycle/hardware proof is claimed."};
    }
}

