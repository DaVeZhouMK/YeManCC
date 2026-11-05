using System.IO;
using System.Collections;
using System.Reflection;
using System.Threading;
using YeManFanBackend.Capabilities;
using YeManFanHost;

// Inject only System.Management-shaped objects. Catalog ownership, packed EC
// arguments, return debt and retirement execute the original production code.
internal static class PinnedBankedWmiChecks
{
    private const string Token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    private static readonly AsyncLocal<World?> Active = new();
    private static World Current => Active.Value ?? throw new InvalidOperationException("No banked WMI fixture world");
    private static IEnumerable<PinnedFanDescriptor> Sources => new[] {
        "HandheldCompanion.Devices.OneXPlayerX2", "HandheldCompanion.Devices.OneXPlayer3"
    }.Select(name => FanCapabilityCatalog.FindPinnedFanSource(name)
        ?? throw new InvalidOperationException("Registered banked source missing: " + name));

    internal static IReadOnlyList<(string Name, Action Run)> Cases(Action<bool,string> check, string directory) =>
        new (string,Action)[] {
            ("banked-dormant-endpoint-schema-packing", () => DormantAndPacking(check)),
            ("banked-derived-dispose-return-debt", () => DerivedDisposeDebt(check)),
            ("banked-ambiguous-endpoint-cleanup", () => AmbiguousCleanup(check)),
            ("banked-enumerator-dispose-debt", () => EnumeratorDebt(check)),
            ("banked-pending-invoke-stop-generation", () => PendingInvoke(check)),
            ("banked-host-curve-and-close", () => HostCurve(check,directory))
        };

    internal sealed record Call(string Method, uint Input, FakeRow Row);
    internal sealed class World : IDisposable
    {
        private readonly World? previous = Active.Value;
        private readonly object gate = new();
        private readonly List<FakeResource> resources = new();
        private readonly List<Call> calls = new();
        private readonly Dictionary<ushort,byte> values = new() { [0x044A]=0, [0x044B]=0, [0x0470]=50 };
        internal int RowCount = 1, EnumeratorFailures, Searchers;
        internal bool MissingInvoke;
        internal string? EndpointPath = "fixture:SuRwECRegInterface.InstanceName=onex_0";
        internal string? Namespace, Query;
        internal TimeSpan? Timeout;
        internal Action<Call>? BeforeInvoke;
        internal World() => Active.Value = this;
        internal FakeResource[] Resources { get { lock(gate) return resources.ToArray(); } }
        internal Call[] Calls { get { lock(gate) return calls.ToArray(); } }
        internal Call[] Writes => Calls.Where(call => call.Method == "WriteECReg").ToArray();
        internal FakeRow Row => Resources.OfType<FakeRow>().Single();
        internal void Track(FakeResource value) { lock(gate) resources.Add(value); }
        internal void Record(Call call) { lock(gate) calls.Add(call); }
        internal byte Read(ushort register) { lock(gate) return values[register]; }
        internal void Write(ushort register, byte value) { lock(gate) values[register]=value; }
        internal PinnedWmiFanTransport Transport() => new(new ReflectionFanWmiMethod(typeof(FakeSearcher)));
        internal PinnedSourceFanSession Own(PinnedFanDescriptor source, PinnedWmiFanTransport transport) =>
            FanCapabilityCatalog.AcquirePinnedFanSource(source,Token,() => true,() => transport);
        internal void ClearFaults()
        {
            BeforeInvoke=null;
            foreach(var resource in Resources) resource.FailuresRemaining=0;
        }
        internal void CheckClosed(Action<bool,string> check,string name)
        {
            var owned=Resources;
            check(owned.Length>0 && owned.All(resource => resource.Closed && resource.PublicCloses==1 && resource.InterfaceDisposals==0),
                "banked:"+name+"-all-exact-public-resource-releases");
        }
        public void Dispose() => Active.Value=previous;
    }
    // Mirrors Component's nonvirtual IDisposable map. Calling the interface
    // cannot reach the hidden public WMI release and must fail these assertions.
    public abstract class FakeResource : IDisposable
    {
        private protected readonly World World;
        internal bool Closed;
        internal int PublicAttempts, PublicCloses, InterfaceDisposals, FailuresRemaining;
        private protected FakeResource(World world) { World=world;world.Track(this); }
        public void Dispose() => InterfaceDisposals++;
        protected void Release()
        {
            PublicAttempts++;
            if(FailuresRemaining>0) { FailuresRemaining--;throw new IOException("injected banked public Dispose failure"); }
            if(Closed) throw new IOException("duplicate banked resource release");
            Closed=true;PublicCloses++;
        }
    }
    public sealed class FakeOptions
    {
        private readonly World world;
        private TimeSpan timeout;
        internal FakeOptions(World world) => this.world=world;
        public TimeSpan Timeout { get => timeout; set { timeout=value;world.Timeout=value; } }
    }
    public sealed class FakeSearcher : FakeResource
    {
        public FakeSearcher(string scope,string query) : base(Current)
        { World.Searchers++;World.Namespace=scope;World.Query=query;Options=new FakeOptions(World); }
        public FakeOptions Options { get; }
        public FakeCollection Get() => new(World);
        public new void Dispose() => Release();
    }
    public sealed class FakeCollection : FakeResource, IEnumerable
    {
        private readonly object[] rows;
        internal FakeCollection(World world) : base(world) => rows=Enumerable.Range(0,world.RowCount)
            .Select(_ => world.MissingInvoke ? (object)new NoInvokeRow(world) : new FakeRow(world)).ToArray();
        public IEnumerator GetEnumerator() => new FakeEnumerator(World,rows);
        public new void Dispose() => Release();
    }
    public sealed class FakeEnumerator : FakeResource, IEnumerator
    {
        private readonly object[] rows;
        private int index=-1;
        internal FakeEnumerator(World world,object[] rows) : base(world)
        { this.rows=rows;FailuresRemaining=world.EnumeratorFailures; }
        public object Current => rows[index];
        public bool MoveNext() => ++index<rows.Length;
        public void Reset() => throw new NotSupportedException();
        public new void Dispose() => Release();
    }
    public sealed class FakePath
    {
        public string? Path { get; }
        internal FakePath(string? path) => Path=path;
    }
    public sealed class NoInvokeRow : FakeResource
    {
        internal NoInvokeRow(World world) : base(world) { }
        public FakePath Path => new(World.EndpointPath);
        public new void Dispose() => Release();
    }
    public sealed class FakeRow : FakeResource
    {
        internal FakeRow(World world) : base(world) { }
        public FakePath Path => new(World.EndpointPath);
        public object InvokeMethod(string method,object[] arguments)
        {
            if(Closed || arguments.Length!=2 || arguments[0] is not uint input)
                throw new IOException("banked invocation schema changed");
            var call=new Call(method,input,this);World.Record(call);World.BeforeInvoke?.Invoke(call);
            var register=checked((ushort)(((input&0xff)<<8)|((input>>8)&0xff)));
            if(method=="ReadECReg") arguments[1]="00,"+World.Read(register).ToString("X2");
            else if(method=="WriteECReg")
            { World.Write(register,checked((byte)(input>>16)));arguments[1]="00"; }
            else throw new IOException("unexpected banked WMI method");
            return null!;
        }
        public new void Dispose() => Release();
    }
    private static object? Retained(ReflectionFanWmiMethod method,string field) => typeof(ReflectionFanWmiMethod)
        .GetField(field,BindingFlags.Instance|BindingFlags.NonPublic)?.GetValue(method);
    private static void Reject(Action action,Action<bool,string> check,string name)
    {
        bool rejected=false;
        try { action(); }
        catch(Exception error) when(error is IOException or InvalidOperationException or FanApiException) { rejected=true; }
        check(rejected,"banked:"+name);
    }
    private static void DenySuccessor(World world,PinnedFanDescriptor source,Action<bool,string> check,string name) =>
        Reject(() => { using var unexpected=world.Own(source,world.Transport());unexpected.Open(); },check,name);
    private static void Cleanup(World world,PinnedSourceFanSession owner)
    {
        world.ClearFaults();
        if(!owner.TryStop()) throw new InvalidOperationException("banked fixture owner cleanup failed: "+owner.LastFailure);
    }
    private static void DormantAndPacking(Action<bool,string> check)
    {
        foreach(var source in Sources)
        {
            using(var world=new World())
            {
                var method=new ReflectionFanWmiMethod(typeof(FakeSearcher));
                var transport=new PinnedWmiFanTransport(method);var owner=world.Own(source,transport);
                try
                {
                    check(source.Transport==PinnedFanTransportKind.WmiBankedEc && source.Maximum==184 &&
                        world.Searchers==0 && !transport.IsReady,"banked:dormant-registered-184-"+source.DeviceId);
                    Reject(() => transport.ReadByte(0x044A),check,"read-before-open-"+source.DeviceId);
                    owner.Open();
                    check(world.Searchers==1 && world.Namespace=="root\\WMI" && world.Query=="SELECT * FROM SuRwECRegInterface" &&
                        world.Timeout==TimeSpan.FromSeconds(3) && method.DevicePath==world.EndpointPath,
                        "banked:actual-query-timeout-and-path-"+source.DeviceId);
                    check(world.Resources.Where(r => r is not FakeRow).All(r => r.Closed && r.PublicCloses==1 && r.InterfaceDisposals==0) &&
                        !world.Row.Closed,"banked:enumerator-and-query-resources-retired-row-retained-"+source.DeviceId);
                    var receipt=owner.ApplyPercent(50);
                    check(receipt.RegisterValue==92 && world.Writes.Select(call => call.Input).SequenceEqual(new uint[]{0x00014A04,0x005C4B04}) &&
                        world.Calls[0].Input==0x4A04 && world.Calls.All(call => ReferenceEquals(call.Row,world.Row)),
                        "banked:actual-bank-packing-and-byte-shift-"+source.DeviceId);
                    check(owner.ReadDutyRegister()==92 && owner.ReadTemperature()==50 &&
                        world.Calls.Last().Input==0x7004,"banked:actual-duty-and-temperature-read-packing-"+source.DeviceId);
                    check(owner.TryStop() && owner.AutomaticReturnObserved && owner.Phase==PinnedFanPhase.Closed &&
                        world.Writes.Last().Input==0x4A04 && !transport.IsReady,"banked:observed-auto-and-successful-terminal-close-"+source.DeviceId);
                    world.CheckClosed(check,source.DeviceId);
                    Reject(() => owner.ApplyPercent(50),check,"closed-generation-rejected-"+source.DeviceId);
                }
                finally { Cleanup(world,owner); }
            }
            foreach(var invalid in new[]{"empty","missing-invoke","missing-path"})
            {
                using var world=new World { RowCount=invalid=="empty" ? 0 : 1,
                    MissingInvoke=invalid=="missing-invoke",EndpointPath=invalid=="missing-path" ? "" : "fixture:valid" };
                var owner=world.Own(source,world.Transport());
                try
                {
                    Reject(owner.Open,check,"endpoint-schema-refused-"+invalid+"-"+source.DeviceId);
                    check(world.Calls.Length==0 && !owner.ControlWasAttempted,"banked:failed-admission-has-no-io-"+invalid);
                    check(owner.TryStop(),"banked:failed-admission-resources-settled-"+invalid);
                    world.CheckClosed(check,invalid+"-"+source.DeviceId);
                }
                finally { Cleanup(world,owner); }
            }
        }
    }
    private static void DerivedDisposeDebt(Action<bool,string> check)
    {
        foreach(var source in Sources)
        {
            using var world=new World();var method=new ReflectionFanWmiMethod(typeof(FakeSearcher));
            var owner=world.Own(source,new PinnedWmiFanTransport(method));
            try
            {
                owner.Open();owner.ApplyPercent(50);var row=world.Row;row.FailuresRemaining=1;
                check(!owner.TryStop() && owner.AutomaticReturnObserved && owner.Phase==PinnedFanPhase.Retiring &&
                    ReferenceEquals(Retained(method,"instance"),row) && !row.Closed && row.PublicAttempts==1 && row.InterfaceDisposals==0,
                    "banked:hidden-row-dispose-failure-retains-exact-return-debt-"+source.DeviceId);
                DenySuccessor(world,source,check,"row-dispose-debt-denies-new-generation-"+source.DeviceId);
                check(owner.TryStop() && row.PublicAttempts==2 && row.PublicCloses==1 && Retained(method,"instance") is null &&
                    world.Writes.Count(call => call.Input==0x4A04)==1,"banked:retry-closes-same-row-without-replaying-auto-"+source.DeviceId);
                world.CheckClosed(check,"row-retry-"+source.DeviceId);
                using var nextWorld=new World();using var successor=nextWorld.Own(source,nextWorld.Transport());successor.Open();
                int count=nextWorld.Calls.Length;
                Reject(() => owner.ApplyPercent(80),check,"old-generation-cannot-write-successor-"+source.DeviceId);
                check(successor.Generation>owner.Generation && nextWorld.Calls.Length==count,
                    "banked:only-completed-retirement-allows-new-generation-"+source.DeviceId);
                check(successor.TryStop(),"banked:successor-close-"+source.DeviceId);nextWorld.CheckClosed(check,"successor-"+source.DeviceId);
            }
            finally { Cleanup(world,owner); }
        }
    }
    private static void AmbiguousCleanup(Action<bool,string> check)
    {
        foreach(var source in Sources)
        {
            using var world=new World { RowCount=2 };var method=new ReflectionFanWmiMethod(typeof(FakeSearcher));
            var owner=world.Own(source,new PinnedWmiFanTransport(method));
            try
            {
                Reject(owner.Open,check,"ambiguous-selection-"+source.DeviceId);
                var rows=world.Resources.OfType<FakeRow>().ToArray();rows[1].FailuresRemaining=1;
                check(rows.Length==2 && world.Calls.Length==0 && !owner.ControlWasAttempted,"banked:ambiguous-no-io-"+source.DeviceId);
                check(!owner.TryStop() && !rows[1].Closed && rows[1].PublicAttempts==1 && rows[1].InterfaceDisposals==0 &&
                    Retained(method,"extras") is ICollection extras && extras.Count==1,
                    "banked:ambiguous-extra-hidden-dispose-debt-retained-"+source.DeviceId);
                DenySuccessor(world,source,check,"ambiguous-debt-denies-successor-"+source.DeviceId);
                check(owner.TryStop() && rows[1].PublicAttempts==2 && world.Writes.Length==0,
                    "banked:ambiguous-retry-retire-all-without-control-"+source.DeviceId);
                world.CheckClosed(check,"ambiguous-"+source.DeviceId);
            }
            finally { Cleanup(world,owner); }
        }
    }
    private static void EnumeratorDebt(Action<bool,string> check)
    {
        foreach(var source in Sources)
        {
            using var world=new World { EnumeratorFailures=2 };var method=new ReflectionFanWmiMethod(typeof(FakeSearcher));
            var transport=new PinnedWmiFanTransport(method);var owner=world.Own(source,transport);
            try
            {
                Reject(owner.Open,check,"enumerator-release-fails-admission-"+source.DeviceId);
                var enumerator=world.Resources.OfType<FakeEnumerator>().Single();
                check(ReferenceEquals(Retained(method,"enumerator"),enumerator) && enumerator.PublicAttempts==1 &&
                    !enumerator.Closed && world.Calls.Length==0 && !transport.IsReady,
                    "banked:actual-enumerator-failure-keeps-reference-"+source.DeviceId);
                Reject(transport.Initialize,check,"enumerator-debt-cannot-reinitialize-"+source.DeviceId);
                check(world.Searchers==1 && !owner.TryStop() && ReferenceEquals(Retained(method,"enumerator"),enumerator) &&
                    enumerator.PublicAttempts==2 && !enumerator.Closed,"banked:stop-retains-repeated-enumerator-failure-"+source.DeviceId);
                DenySuccessor(world,source,check,"enumerator-debt-denies-successor-"+source.DeviceId);
                check(owner.TryStop() && enumerator.PublicAttempts==3 && Retained(method,"enumerator") is null &&
                    world.Resources.OfType<FakeRow>().Single().PublicCloses==1,"banked:enumerator-retry-no-double-row-release-"+source.DeviceId);
                world.CheckClosed(check,"enumerator-"+source.DeviceId);
            }
            finally { Cleanup(world,owner); }
        }
    }
    private static void PendingInvoke(Action<bool,string> check)
    {
        foreach(var source in Sources)
        {
            using var world=new World();using var entered=new ManualResetEventSlim();using var finish=new ManualResetEventSlim();
            var owner=world.Own(source,world.Transport());Task<bool>? apply=null;
            try
            {
                owner.Open();
                world.BeforeInvoke=call => {
                    if(call.Method=="WriteECReg" && call.Input==0x00014A04)
                    { entered.Set();if(!finish.Wait(5000))throw new IOException("banked Invoke barrier timeout"); }
                };
                apply=Task.Run(() => { try { owner.ApplyPercent(70);return false; } catch(InvalidOperationException) { return true; } });
                check(entered.Wait(1500) && owner.PendingIo,"banked:actual-reflected-invoke-is-pending-"+source.DeviceId);
                owner.Revoke();var stop=Task.Run(owner.TryStop);
                check(stop.Wait(1000) && !stop.Result,"banked:pending-invoke-stop-is-bounded-and-incomplete-"+source.DeviceId);
                DenySuccessor(world,source,check,"pending-invoke-denies-new-generation-"+source.DeviceId);
                finish.Set();check(apply.Wait(1500) && apply.Result && world.Writes.Length==1 && world.Writes[0].Input==0x00014A04,
                    "banked:late-control-return-cannot-issue-following-duty-"+source.DeviceId);
                world.BeforeInvoke=null;
                check(owner.TryStop() && owner.AutomaticReturnObserved && world.Writes.Length==2 && world.Writes[1].Input==0x4A04,
                    "banked:pending-control-followed-only-by-owned-auto-return-"+source.DeviceId);
                world.CheckClosed(check,"pending-"+source.DeviceId);
                using var nextWorld=new World();using var successor=nextWorld.Own(source,nextWorld.Transport());successor.Open();
                int count=nextWorld.Calls.Length;
                Reject(() => owner.ApplyPercent(70),check,"late-owner-cannot-write-new-session-"+source.DeviceId);
                check(successor.Generation>owner.Generation && nextWorld.Calls.Length==count,"banked:pending-retired-generation-is-inert-"+source.DeviceId);
                check(successor.TryStop(),"banked:pending-successor-retires-"+source.DeviceId);nextWorld.CheckClosed(check,"pending-successor-"+source.DeviceId);
            }
            finally
            {
                finish.Set();
                if(apply is not null && !apply.Wait(2000))throw new IOException("banked Invoke worker did not retire");
                Cleanup(world,owner);
            }
        }
    }
    private static void HostCurve(Action<bool,string> check,string evidenceRoot)
    {
        var directory=Path.Combine(evidenceRoot,"banked-host-consumer");Directory.CreateDirectory(directory);
        foreach(var source in Sources)
        {
            var authorization=Path.Combine(directory,source.DeviceId+"-auth.txt");
            File.WriteAllText(authorization,"hardwareWriteAuthorizationGranted: true\nallMappedFanRoutesAuthorized: true\n");
            using var world=new World();
            using var host=new RealHcBackend(HostOptions.Parse(Array.Empty<string>()) with {
                AllowHardwareWrites=true,AuthorizationPath=authorization,Confirmation=Token,SessionToken=Token });
            host.InstallPinnedSourceConsumerForSelfTest(source.Manufacturers[0],source.Products[0],world.Transport);
            try
            {
                var handshake=host.LoadAndHandshake();
                check(handshake.Supported && handshake.FactoryType is null && handshake.SourceModelClass==source.ModelClass &&
                    world.Searchers==0,"banked:actual-host-source-handshake-is-dormant-"+source.DeviceId);
                host.Open();host.OpenEvents();
                var curve=new[]{new FanNode(0,0),new FanNode(30,20),new FanNode(70,70),new FanNode(100,100)};
                check(host.ApplyCurve(curve) && world.Writes.Length==0,"banked:actual-host-arms-without-invented-temperature-"+source.DeviceId);
                host.DeliverPinnedSourceTemperatureForSelfTest(50);
                check(world.Writes.Select(call => call.Input).SequenceEqual(new uint[]{0x00014A04,0x00534B04}) &&
                    host.SourceWritesObserved,"banked:actual-host-curve-to-184-packing-"+source.DeviceId);
                int count=world.Calls.Length;host.DeliverPinnedSourceTemperatureForSelfTest(50);
                host.DeliverPinnedSourceTemperatureForSelfTest(65,foreignMonitor:true);
                check(world.Calls.Length==count,"banked:actual-host-duplicate-and-foreign-sensor-guards-"+source.DeviceId);
                host.DeliverPinnedSourceTemperatureForSelfTest(95);
                check(world.Writes.Last().Input==0x00B84B04,"banked:actual-host-thermal-trip-max184-"+source.DeviceId);
                host.ApplyCurve(curve);host.DeliverPinnedSourceTemperatureForSelfTest(85);
                check(world.Writes.Last().Input==0x00B84B04,"banked:actual-host-curve-edit-preserves-thermal-latch-"+source.DeviceId);
                var oldCallback=host.CapturePinnedSourceTemperatureForSelfTest();host.BlockWritesForClose();host.Close();
                check(host.SourceTransportCloseConfirmed && host.SourceAutomaticReturnObserved && !host.IsOpen &&
                    world.Writes.Last().Input==0x4A04 && !host.HcVirtualCloseReturned,
                    "banked:actual-host-return-and-hidden-row-disposal-"+source.DeviceId);
                count=world.Calls.Length;host.Close();oldCallback(100);
                check(world.Calls.Length==count,"banked:closed-host-callback-and-duplicate-close-inert-"+source.DeviceId);
                world.CheckClosed(check,"host-"+source.DeviceId);
            }
            finally { world.ClearFaults();host.Close(); }
        }
    }
}
