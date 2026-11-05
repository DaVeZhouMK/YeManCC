using YeManBatteryHost;
using YeManFanBackend.Capabilities;
internal static class SuperXChargeIdentityFixture
{
    internal static int Run()
    {
        int count=0;
        void Check(bool value,string reason){count++;if(!value)throw new Exception(reason);}
        Identity Captured(string manufacturer,string board,string system="untrusted",string cpu="untrusted")=>WindowsFacts.CaptureIdentity(manufacturer,board,system,cpu,"",0,0);
        foreach(var vendor in new[]{"ONE-NETBOOK","prefix ONE-NETBOOK suffix"," ONE-NETBOOK "})
        {
            var id=Captured(vendor,"ONEXPLAYER SUPER X");var binding=Policy.ResolveCapability(id);
            Check(binding?.Consumer.RouteId=="oxp-superx-dual-ec","source ordinal vendor substring not admitted");
            Check(binding!.Descriptor.Origin=="reviewed-native-extension"&&!binding.Descriptor.DeclaredByFrozenHc&&binding.Descriptor.DeviceId=="ymcc.device.OneXPlayerSuperX","extension was forged as a frozen HC class");
            Check(binding.Consumer.Choices.SequenceEqual(new[]{75,80,85,90,95,100}),"source charge range/choices changed");
        }
        foreach(var board in new[]{"ONEXPLAYER SUPER X "," ONEXPLAYER SUPER X","onexplayer super x","ONEXPLAYER SUPERX","ONEXPLAYER SUPER X\t"})
        {
            var id=Captured("ONE-NETBOOK",board);Check(id.BoardProduct==board.Trim()&&id.BoardProductRaw==board,"capture discarded raw board identity");
            Check(Policy.Resolve(id) is null,"normalized/marketing SuperX product alias admitted");
        }
        foreach(var vendor in new[]{"one-netbook","ONE NETBOOK",""})Check(Policy.Resolve(Captured(vendor,"ONEXPLAYER SUPER X")) is null,"unreviewed vendor predicate admitted");
        Check(Policy.Resolve(Captured("ONE-NETBOOK","untrusted","ONEXPLAYER SUPER X","AMD395")) is null,"system product/CPU alias selected SuperX");
        var old=Captured(" one-netbook "," ONEXPLAYER APEX "," ONEXPLAYER APEX "," AMD395 ");
        Check(Policy.Resolve(old)?.Identity.DeviceClass=="OneXPlayerApex","old normalized exact rule was changed");
        Check(old.Key==new Identity("one-netbook","ONEXPLAYER APEX","ONEXPLAYER APEX","AMD395","",0,0).Key,"raw observations changed existing normalized persistence key");
        var route=Policy.Resolve(Captured("ONE-NETBOOK","ONEXPLAYER SUPER X"))!;
        using(var provider=Providers.Open(route,"memory-only"))Check(provider is SuperXChargeProvider,"catalog route did not reach dormant actual factory");
        Console.WriteLine($"Super X identity assertions: {count}");return count;
    }
}
