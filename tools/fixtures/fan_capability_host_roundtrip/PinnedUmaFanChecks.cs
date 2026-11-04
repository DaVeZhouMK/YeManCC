using System.Collections;
using System.IO;
using System.Reflection;
using System.Threading;
using YeManFanBackend.Capabilities;
using YeManFanHost;

// Original fixture object graph at the System.Management reflection boundary.
// These objects never implement IPinnedFanTransport: every operation goes through
// the production PinnedUmaEcFanTransport, catalog session and original Host seams.
internal static class PinnedUmaFanChecks
{
    internal const string ModelClass = "YeManCC.SourceDevices.OneXPlayerSuperX";
    internal const string Token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    internal static PinnedFanDescriptor Descriptor => FanCapabilityCatalog.FindPinnedFanSource(ModelClass)
        ?? throw new InvalidOperationException("Missing registered Super X source descriptor");
    private static readonly AsyncLocal<World?> Active = new();
    private static World Current => Active.Value ?? throw new InvalidOperationException("No UMA fixture world");
    internal sealed record FieldSpec(object? Type, object? IsArray);
    internal sealed class SchemaSpec
    {
        internal bool MissingProperties;
        internal readonly Dictionary<string,FieldSpec?> Fields = new(StringComparer.Ordinal);
        internal static SchemaSpec Index(string type = "SInt32")
        { var value = new SchemaSpec(); value.Fields["Index"] = new(type,false); return value; }
        internal static SchemaSpec Data(string type = "UInt8")
        { var value = new SchemaSpec(); value.Fields["Data"] = new(type,false); return value; }
        internal static SchemaSpec Write(string index = "SInt32",string data = "UInt8")
        { var value = Index(index); value.Fields["Data"] = new(data,false); return value; }
    }
    internal sealed class RowSpec
    {
        internal object? InstanceName = "ACPI\\SETUMAWMI\\0";
        internal object? Path = "\\\\fixture\\root\\WMI:UMAInterface.InstanceName=\"SETUMAWMI_0\"";
        internal readonly object Scope = new();
        internal readonly object ClassPath = new();
    }
    internal sealed record Call(string Method, object? Index, object? Data, RowSpec Row);
    internal sealed class World : IDisposable
    {
        private readonly World? previous = Active.Value;
        private readonly object gate = new();
        private readonly List<FakeResource> resources = new();
        private readonly List<Call> calls = new();
        internal readonly List<RowSpec?> Rows = new() { new RowSpec() };
        internal readonly Dictionary<ushort,byte> Values = new() { [0x044A]=0,[0x044B]=0 };
        internal SchemaSpec? ReadIn = SchemaSpec.Index(), ReadOut = SchemaSpec.Data(), WriteIn = SchemaSpec.Write();
        internal SchemaSpec ActualRead = SchemaSpec.Index(), ActualWrite = SchemaSpec.Write();
        internal bool ReadMethodPresent = true, WriteMethodPresent = true;
        internal bool NullReadResult, MissingData, NullWriteResult = true, IgnoreWrites, IgnoreAutomatic, ThrowAfterWrite;
        internal Func<ushort,object?>? ReadData;
        internal Action<string>? OnStep;
        internal Action<Call>? BeforeInvoke;
        internal string? FailAt;
        internal readonly HashSet<string> FailDisposals = new(StringComparer.Ordinal);
        internal int Searchers;
        internal string? Scope, Query;
        internal TimeSpan? Timeout;
        internal World() => Active.Value = this;
        internal FakeResource[] Resources { get { lock(gate) return resources.ToArray(); } }
        internal Call[] Calls { get { lock(gate) return calls.ToArray(); } }
        internal Call[] Writes => Calls.Where(call => call.Method == "SetEcValue").ToArray();
        internal void Track(FakeResource value) { lock(gate) resources.Add(value); }
        internal void Step(string name)
        { OnStep?.Invoke(name); if (FailAt == name) throw new IOException("injected UMA "+name); }
        internal void Record(Call call) { lock(gate) calls.Add(call); }
        internal PinnedUmaEcFanTransport Transport() => new(Descriptor,typeof(FakeSearcher),typeof(FakeManagementClass));
        internal PinnedSourceFanSession Own(PinnedUmaEcFanTransport transport,Func<bool>? permit = null) =>
            FanCapabilityCatalog.AcquirePinnedFanSource(Descriptor,Token,permit ?? (()=>true),()=>transport);
        internal void ClearFaults()
        {
            FailAt=null; FailDisposals.Clear(); OnStep=null; BeforeInvoke=null;
            IgnoreWrites=false; IgnoreAutomatic=false; ThrowAfterWrite=false;
            NullReadResult=false; MissingData=false; ReadData=null;
            foreach (var resource in Resources) resource.FailOnce=false;
        }
        internal void CheckClosed(Action<bool,string> check,string name)
        {
            var owned=Resources;
            check(owned.Length>0 && owned.All(resource => resource.Closed && resource.SuccessfulCloses==1),
                "uma:all-returned-objects-closed-once-"+name);
        }
        public void Dispose() => Active.Value = previous; // Never clean up on the product's behalf.
    }
    public class FakeResource : IDisposable
    {
        internal readonly World World;
        internal readonly string Kind;
        internal bool Closed, FailOnce;
        internal int Attempts, SuccessfulCloses, BaseInterfaceCalls;
        internal FakeResource(World world,string kind,bool track=true)
        { World=world; Kind=kind; if(track)world.Track(this); }
        internal void CloseOwned()
        {
            Attempts++;
            if(Closed)throw new IOException("Product disposed the same UMA object twice: "+Kind);
            if(FailOnce){FailOnce=false;throw new IOException("one-shot derived Dispose failure");}
            if(World.FailDisposals.Contains(Kind))throw new IOException("injected UMA dispose "+Kind);
            World.Step(Kind+".dispose");Closed=true;SuccessfulCloses++;
        }
        // Intentionally nonvirtual. FakeRow's public new Dispose must win over
        // this inherited interface mapping, as on ManagementBaseObject.
        public void Dispose()
        {
            BaseInterfaceCalls++;
            if(this is FakeRow)return;
            CloseOwned();
        }
    }
    // Options and collection/indexer metadata are borrowed views, matching their
    // parent-owned lifetime. Every returned disposable resource is tracked above;
    // views expose injectable Dispose too, without hiding missing root cleanup.
    public sealed class FakeOptions : IDisposable
    {
        private readonly World world;
        internal FakeOptions(World world)=>this.world=world;
        public TimeSpan Timeout
        {
            get=>world.Timeout ?? TimeSpan.Zero;
            set { world.Step("options.timeout");world.Timeout=value; }
        }
        public void Dispose()=>world.Step("options.dispose");
    }
    public sealed class FakeSearcher : FakeResource
    {
        public FakeSearcher(string scope,string query) : base(Current,"searcher",false)
        {
            World.Step("searcher.ctor");World.Track(this);World.Searchers++;
            World.Scope=scope;World.Query=query;
        }
        public FakeOptions Options { get { World.Step("searcher.options");return new(World); } }
        public FakeCollection Get()
        { World.Step("searcher.get");return new(World); }
    }
    public sealed class FakeCollection : FakeResource, IEnumerable
    {
        internal FakeCollection(World world):base(world,"collection") { }
        public IEnumerator GetEnumerator()
        { World.Step("collection.enumerator");return new FakeEnumerator(World); }
    }
    public sealed class FakeEnumerator : FakeResource, IEnumerator
    {
        private int position=-1;
        private object? current;
        internal FakeEnumerator(World world):base(world,"enumerator") { }
        public object Current=>current!;
        public bool MoveNext()
        {
            World.Step("enumerator.move");
            if(++position>=World.Rows.Count)return false;
            current=World.Rows[position] is { } spec ? new FakeRow(World,spec) : null;
            return true;
        }
        public void Reset()=>throw new NotSupportedException();
    }
    public sealed class FakeRow : FakeResource
    {
        internal readonly RowSpec Spec;
        internal int DerivedDisposeCalls;
        internal FakeRow(World world,RowSpec spec):base(world,"row")=>Spec=spec;
        public object? this[string key]
        {
            get
            {
                World.Step("row."+key);
                return key switch { "InstanceName"=>Spec.InstanceName,"__PATH"=>Spec.Path,_=>null };
            }
        }
        public object Scope { get { World.Step("row.scope");return Spec.Scope; } }
        public object ClassPath { get { World.Step("row.classpath");return Spec.ClassPath; } }
        public FakeInput GetMethodParameters(string method)
        {
            World.Step("input.create");
            return new(World,method=="GetEcValue" ? World.ActualRead : World.ActualWrite);
        }
        public FakeResult? InvokeMethod(string method,FakeInput input,object? options)
        {
            var call=new Call(method,input["Index"],input["Data"],Spec);
            World.Record(call);World.BeforeInvoke?.Invoke(call);
            if(options is not null || call.Index is not int index || index is <0 or >65535)
                throw new IOException("Fixture requires raw Int32 Index and null method options");
            ushort register=(ushort)index;
            if(!World.Values.ContainsKey(register))throw new IOException("Fixture observed nonfan or bank-swapped Index");
            World.Step("invoke."+method);
            if(method=="SetEcValue")
            {
                if(call.Data is not byte value)throw new IOException("Fixture requires byte Data input");
                if(!World.IgnoreWrites && !(World.IgnoreAutomatic && register==0x044A && value==0))World.Values[register]=value;
                if(World.ThrowAfterWrite)throw new IOException("injected issued UMA write uncertainty");
                return World.NullWriteResult ? null : new FakeResult(World,new() { ["ReturnValue"]=-1 });
            }
            if(method!="GetEcValue")throw new IOException("Unexpected UMA method "+method);
            if(World.NullReadResult)return null;
            var values=new Dictionary<string,object?>();
            if(!World.MissingData)values["Data"]=World.ReadData is { } read ? read(register) : World.Values[register];
            return new FakeResult(World,values);
        }
        public new void Dispose() { DerivedDisposeCalls++;CloseOwned(); }
    }
    public sealed class FakeManagementClass : FakeResource
    {
        public FakeManagementClass(object scope,object path,object? options):base(Current,"class",false)
        {
            World.Step("class.ctor");
            if(options is not null || !World.Rows.Any(row => row is not null &&
                ReferenceEquals(row.Scope,scope) && ReferenceEquals(row.ClassPath,path)))
                throw new IOException("ManagementClass must use selected row Scope/ClassPath");
            World.Track(this);
        }
        public FakeMethods Methods { get { World.Step("class.methods");return new(World); } }
    }
    public sealed class FakeMethods : IDisposable
    {
        private readonly World world;
        internal FakeMethods(World world)=>this.world=world;
        public FakeMethod? this[string name]
        {
            get
            {
                world.Step("method."+name);
                return name switch {
                    "GetEcValue" when world.ReadMethodPresent=>new(world,false),
                    "SetEcValue" when world.WriteMethodPresent=>new(world,true),_=>null };
            }
        }
        public void Dispose()=>world.Step("methods.dispose");
    }
    public sealed class FakeMethod : IDisposable
    {
        private readonly World world;
        private readonly bool write;
        internal FakeMethod(World world,bool write){this.world=world;this.write=write;}
        public FakeSchema? InParameters
        {
            get
            {
                world.Step("method.in");var spec=write ? world.WriteIn : world.ReadIn;
                return spec is null ? null : new(world,write ? "metadata-write-in" : "metadata-read-in",spec);
            }
        }
        public FakeSchema? OutParameters
        {
            get
            {
                world.Step("method.out");
                // No write return schema is promised by this protocol.
                if(write)return null;
                return world.ReadOut is { } spec ? new(world,"metadata-read-out",spec) : null;
            }
        }
        public void Dispose()=>world.Step("method.dispose");
    }
    public class FakeSchema : FakeResource
    {
        private readonly SchemaSpec spec;
        internal FakeSchema(World world,string kind,SchemaSpec spec):base(world,kind)=>this.spec=spec;
        public FakeProperties? Properties
        {
            get { World.Step(Kind+".properties");return spec.MissingProperties ? null : new(World,spec); }
        }
    }
    public sealed class FakeProperties : IDisposable
    {
        private readonly World world;
        private readonly SchemaSpec spec;
        internal FakeProperties(World world,SchemaSpec spec){this.world=world;this.spec=spec;}
        public FakeProperty? this[string key]=>spec.Fields.TryGetValue(key,out var field) && field is not null ? new(world,field) : null;
        public void Dispose()=>world.Step("properties.dispose");
    }
    public sealed class FakeProperty : IDisposable
    {
        private readonly World world;
        private readonly FieldSpec spec;
        internal FakeProperty(World world,FieldSpec spec){this.world=world;this.spec=spec;}
        public object? Type { get { world.Step("property.type");return spec.Type; } }
        public object? IsArray { get { world.Step("property.array");return spec.IsArray; } }
        public void Dispose()=>world.Step("property.dispose");
    }
    public sealed class FakeInput : FakeSchema
    {
        private readonly Dictionary<string,object?> values=new(StringComparer.Ordinal);
        internal FakeInput(World world,SchemaSpec spec):base(world,"input",spec) { }
        public object? this[string key]
        {
            get=>values.TryGetValue(key,out var value) ? value : null;
            set { World.Step("input.set."+key);values[key]=value; }
        }
    }
    public sealed class FakeResult : FakeResource
    {
        private readonly Dictionary<string,object?> values;
        internal FakeResult(World world,Dictionary<string,object?> values):base(world,"result")=>this.values=values;
        public object? this[string key]
        { get { World.Step("result."+key);return values.TryGetValue(key,out var value) ? value : null; } }
    }
    internal static void Reject(Action action,Action<bool,string> check,string name)
    {
        bool rejected=false;
        try { action(); }
        catch(Exception error)
        {
            while(error is TargetInvocationException { InnerException: { } inner })error=inner;
            if(error is not (IOException or InvalidOperationException or ArgumentException or UnauthorizedAccessException))throw;
            rejected=true;
        }
        check(rejected,"uma:reject-"+name);
    }
    private static void Exercise(Action<bool,string> check,string name,Action<World,PinnedUmaEcFanTransport> body,Action<World>? setup=null)
    {
        using var world=new World();setup?.Invoke(world);var transport=world.Transport();
        try { body(world,transport); }
        finally { world.ClearFaults();transport.Dispose(); }
        world.CheckClosed(check,name);
    }
    private static void Busy(World world,Action<bool,string> check,string name)
    {
        int opens=world.Searchers;
        Reject(()=> {
            using var unexpected=FanCapabilityCatalog.AcquirePinnedFanSource(Descriptor,Token,()=>true,
                ()=>world.Transport());
            unexpected.Open();
        },check,name);
        check(world.Searchers==opens,"uma:no-successor-searcher-"+name);
    }
    private static void Finish(World world,PinnedSourceFanSession owner)
    {
        world.ClearFaults();
        if(!owner.TryStop())throw new InvalidOperationException("UMA fixture owner cleanup failed: "+owner.LastFailure);
    }
    // One declaration of the case set, used by the original Runner before IO.
    internal static IReadOnlyList<(string Name,Action Run)> Cases(Action<bool,string> check,string directory) =>
        new (string,Action)[] {
            ("catalog-and-dmi",()=>{PinnedFanSourceChecks.CheckCatalog(check);SourceSelection(check);}),
            ("dormant-and-unique-endpoint",()=>DormantAndUnique(check)),
            ("endpoint-selection-refusals",()=>SelectionRefusals(check)),
            ("integer-metadata",()=>IntegerMetadata(check)),
            ("metadata-refusals",()=>MetadataRefusals(check)),
            ("actual-input-schema",()=>ActualInputSchema(check)),
            ("raw-index-and-checked-readback",()=>RawIndexAndReadback(check)),
            ("strict-output-data",()=>StrictData(check)),
            ("write-and-readback-errors",()=>WriteErrors(check)),
            ("initialization-errors",()=>InitializationErrors(check)),
            ("owned-resource-debt",()=>ResourceDebt(check)),
            ("pending-initialize-stop",()=>PendingInitialize(check)),
            ("pending-write-stop-generation",()=>PendingWrite(check)),
            ("return-and-dispose-debt",()=>ReturnDebt(check)),
            ("derived-public-dispose-debt",()=>DerivedDisposeDebt(check)),
            ("operation-revocation",()=>OperationRevocation(check)),
            ("host-curve-return",()=>PinnedFanHostConsumerChecks.RunUmaCurve(check,directory)),
            ("host-return-debt",()=>PinnedFanHostConsumerChecks.RunUmaReturnDebt(check,directory))
        };
    private static void SourceSelection(Action<bool,string> check)
    {
        using var world=new World();
        using var host=new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        check(host.SelectPinnedSourceFanIdentityForSelfTest("Vendor ONE-NETBOOK TECHNOLOGY CO., LTD.","ONEXPLAYER SUPER X"),
            "uma:actual-host-selects-source-board-and-vendor-predicate");
        var handshake=host.ReadPinnedSourceFanHandshakeForSelfTest();
        check(handshake.Supported && handshake.FactoryType is null && handshake.SourceModelClass==ModelClass &&
            handshake.SourceCommit==Descriptor.SourceCommit && !handshake.HardwareWriteAuthorizationGranted && !host.CanWrite,
            "uma:source-handshake-provenance-without-write-authority-or-hc-factory");
        check(handshake.DeviceIdentity?["capabilityTransport"]=="WmiUmaEc16" &&
            handshake.DeviceIdentity?["runtimeFactory"]=="not-constructed" && !host.IsLoaded && !host.IsOpen &&
            !host.HcOpenInvocationStarted && !host.HcOpenEventsInvoked && world.Searchers==0,
            "uma:metadata-selection-dormant-and-honest-lifecycle");
        Reject(()=>host.SelectPinnedSourceFanIdentityForSelfTest("ONE-NETBOOK","ONEXPLAYER X2"),check,"actual-host-source-identity-change");
        Reject(()=>new PinnedUmaEcFanTransport(FanCapabilityCatalog.PinnedSourceDeclarations[0],typeof(FakeSearcher),typeof(FakeManagementClass)),
            check,"non-uma-descriptor-before-searcher");
        check(world.Resources.Length==0,"uma:catalog-and-handshake-open-no-system-management-objects");
    }
    private static void DormantAndUnique(Action<bool,string> check) => Exercise(check,"unique",(world,transport)=> {
        check(!transport.IsReady && world.Searchers==0 && world.Resources.Length==0 && !string.IsNullOrWhiteSpace(transport.EndpointIdentity),
            "uma:constructor-is-dormant-with-stable-owned-identity");
        string identity=transport.EndpointIdentity;
        Reject(()=>transport.ReadByte(0x044A),check,"read-before-initialize");
        Reject(()=>transport.WriteByte(0x044A,1),check,"write-before-initialize");
        transport.Initialize();
        check(transport.IsReady && world.Searchers==1 && world.Scope=="root\\WMI" &&
            world.Query=="SELECT * FROM UMAInterface" && world.Timeout is { } timeout && timeout>TimeSpan.Zero && timeout<=TimeSpan.FromSeconds(3),
            "uma:actual-searcher-scope-query-and-bounded-options-timeout");
        check(transport.EndpointIdentity==identity && transport.DevicePath==world.Rows[1]!.Path as string && world.Calls.Length==0,
            "uma:unique-ordinal-marker-and-captured-path-before-method-io");
        check(world.Resources.Where(resource=>resource is not FakeRow row || row.Spec!=world.Rows[1]).All(resource=>resource.Closed) &&
            world.Resources.OfType<FakeRow>().Single(row=>row.Spec==world.Rows[1]).Closed==false,
            "uma:all-query-class-and-schema-temporaries-close-selected-row-retained");
        transport.WriteByte(0x044B,123);
        check(world.Calls.All(call=>ReferenceEquals(call.Row,world.Rows[1])),"uma:method-io-only-uses-exact-selected-object");
        int count=world.Resources.Length;
        Reject(transport.Initialize,check,"ready-terminal-no-reinitialize");
        transport.Dispose();
        check(!transport.IsReady && world.Searchers==1 && world.Resources.Length==count,"uma:closed-terminal-no-new-objects");
        Reject(transport.Initialize,check,"closed-terminal-no-reinitialize");
        Reject(()=>transport.ReadByte(0x044A),check,"read-after-close");
    },world=> {
        world.Rows.Insert(0,new RowSpec { InstanceName="lower_setumawmi_ignored" });
        world.Rows.Add(new RowSpec { InstanceName="other endpoint" });
    });
    private static void SelectionRefusals(Action<bool,string> check)
    {
        var variants=new (string Name,Action<World> Setup)[] {
            ("zero",world=>world.Rows.Clear()),
            ("no-marker",world=>world.Rows[0]!.InstanceName="UMAInterface"),
            ("lower-marker",world=>world.Rows[0]!.InstanceName="setumawmi"),
            ("nonstring-marker",world=>world.Rows[0]!.InstanceName=123),
            ("null-row",world=>world.Rows[0]=null),
            ("ambiguous",world=>world.Rows.Add(new RowSpec { Path="different endpoint" })),
            ("same-path-two-matches",world=>world.Rows.Add(new RowSpec())),
            ("missing-path",world=>world.Rows[0]!.Path=null),
            ("empty-path",world=>world.Rows[0]!.Path=""),
            ("blank-path",world=>world.Rows[0]!.Path=" \t"),
            ("nonstring-path",world=>world.Rows[0]!.Path=123)
        };
        foreach(var item in variants) Exercise(check,item.Name,(world,transport)=> {
            Reject(transport.Initialize,check,item.Name);
            check(!transport.IsReady && world.Calls.Length==0,"uma:selection-fails-before-hardware-method-"+item.Name);
            int searchers=world.Searchers;Reject(transport.Initialize,check,"failed-init-no-retry-"+item.Name);
            check(world.Searchers==searchers,"uma:failed-init-no-replacement-searcher-"+item.Name);
        },item.Setup);
        foreach(var path in new object?[] { null,"", "changed endpoint" }) Exercise(check,"path-drift",(world,transport)=> {
            transport.Initialize();world.Rows[0]!.Path=path;
            Reject(()=>transport.ReadByte(0x044A),check,"captured-path-changed-read");
            Reject(()=>transport.WriteByte(0x044B,55),check,"captured-path-changed-write");
            check(world.Calls.Length==0,"uma:path-drift-refuses-before-invoke");
        });
        Exercise(check,"path-drift-after-input",(world,transport)=> {
            transport.Initialize();world.OnStep=name=>{if(name=="input.set.Index")world.Rows[0]!.Path="changed while constructing input";};
            Reject(()=>transport.WriteByte(0x044B,55),check,"path-rechecked-after-input-construction");
            check(world.Calls.Length==0,"uma:input-construction-cannot-switch-endpoint");
        });
    }
    private static void IntegerMetadata(Action<bool,string> check)
    {
        foreach(var index in new[]{"UInt16","SInt32","UInt32"})
        foreach(var data in new[]{"UInt8"})
            Exercise(check,index+"-"+data,(world,transport)=> {
                transport.Initialize();transport.WriteByte(0x044B,255);
                check(transport.ReadByte(0x044B)==255 && world.Writes.Single().Index is int raw && raw==0x044B &&
                    world.Writes.Single().Data is byte value && value==255,"uma:metadata-compatible-without-coercing-input-"+index+"-"+data);
            },world=> {
                world.ReadIn=SchemaSpec.Index(index);world.ReadOut=SchemaSpec.Data(data);world.WriteIn=SchemaSpec.Write(index,data);
                world.ActualRead=SchemaSpec.Index(index);world.ActualWrite=SchemaSpec.Write(index,data);
            });
    }
    private static void MetadataRefusals(Action<bool,string> check)
    {
        var variants=new List<(string Name,Action<World> Setup)> {
            ("signed16-index-projects-VT_I2",world=>world.ReadIn=SchemaSpec.Index("SInt16")),
            ("write-data-signed16-not-VT_UI1",world=>world.WriteIn=SchemaSpec.Write("SInt32","SInt16")),
            ("write-data-unsigned16-not-VT_UI1",world=>world.WriteIn=SchemaSpec.Write("SInt32","UInt16")),
            ("write-data-signed32-not-VT_UI1",world=>world.WriteIn=SchemaSpec.Write("SInt32","SInt32")),
            ("write-data-unsigned32-not-VT_UI1",world=>world.WriteIn=SchemaSpec.Write("SInt32","UInt32")),
            ("missing-read-method",world=>world.ReadMethodPresent=false),
            ("missing-write-method",world=>world.WriteMethodPresent=false),
            ("missing-read-input",world=>world.ReadIn=null),
            ("missing-write-input",world=>world.WriteIn=null),
            ("missing-read-output",world=>world.ReadOut=null),
            ("missing-properties",world=>world.ReadIn!.MissingProperties=true),
            ("missing-index",world=>world.WriteIn!.Fields.Remove("Index")),
            ("missing-write-data",world=>world.WriteIn!.Fields.Remove("Data")),
            ("missing-read-data",world=>world.ReadOut!.Fields.Remove("Data")),
            ("null-type",world=>world.ReadOut!.Fields["Data"]=new(null,false)),
            ("array-index",world=>world.ReadIn!.Fields["Index"]=new("SInt32",true)),
            ("array-write-data",world=>world.WriteIn!.Fields["Data"]=new("UInt8",true)),
            ("array-read-data",world=>world.ReadOut!.Fields["Data"]=new("UInt8",true)),
            ("unknown-array",world=>world.ReadIn!.Fields["Index"]=new("SInt32",null)),
            ("wrong-array-type",world=>world.ReadIn!.Fields["Index"]=new("SInt32","false"))
        };
        foreach(var kind in new[]{"UInt8","SInt8","SInt64","UInt64","String","Boolean","Real32"})
        {
            variants.Add(("read-index-"+kind,world=>world.ReadIn=SchemaSpec.Index(kind)));
            variants.Add(("write-index-"+kind,world=>world.WriteIn=SchemaSpec.Write(kind)));
        }
        foreach(var kind in new[]{"SInt8","SInt64","UInt64","String","Boolean","Real64"})
        {
            variants.Add(("write-data-"+kind,world=>world.WriteIn=SchemaSpec.Write(data:kind)));
            variants.Add(("read-data-"+kind,world=>world.ReadOut=SchemaSpec.Data(kind)));
        }
        foreach(var item in variants) Exercise(check,item.Name,(world,transport)=> {
            Reject(transport.Initialize,check,item.Name);
            check(!transport.IsReady && world.Calls.Length==0,"uma:bad-metadata-before-hardware-method-"+item.Name);
        },item.Setup);
    }
    private static void ActualInputSchema(Action<bool,string> check)
    {
        var variants=new (string Name,bool Write,Action<World> Setup)[] {
            ("read-index-type",false,world=>world.ActualRead=SchemaSpec.Index("UInt8")),
            ("read-index-missing",false,world=>world.ActualRead.Fields.Remove("Index")),
            ("read-index-array",false,world=>world.ActualRead.Fields["Index"]=new("SInt32",true)),
            ("read-properties",false,world=>world.ActualRead.MissingProperties=true),
            ("write-index-type",true,world=>world.ActualWrite=SchemaSpec.Write("UInt8")),
            ("write-data-type",true,world=>world.ActualWrite=SchemaSpec.Write(data:"SInt8")),
            ("write-data-missing",true,world=>world.ActualWrite.Fields.Remove("Data")),
            ("write-data-array",true,world=>world.ActualWrite.Fields["Data"]=new("UInt8",true))
        };
        foreach(var item in variants) Exercise(check,item.Name,(world,transport)=> {
            transport.Initialize();item.Setup(world);
            Reject(()=>{if(item.Write)transport.WriteByte(0x044B,42);else transport.ReadByte(0x044A);},check,"actual-input-"+item.Name);
            check(world.Calls.Length==0,"uma:actual-input-metadata-rechecked-before-invoke-"+item.Name);
        });
    }
    private static void RawIndexAndReadback(Action<bool,string> check) => Exercise(check,"raw-index",(world,transport)=> {
        transport.Initialize();
        foreach(var pair in new (ushort Register,byte Value)[]{(0x044A,1),(0x044B,0),(0x044B,255),(0x044A,0)})
        {
            int start=world.Calls.Length;transport.WriteByte(pair.Register,pair.Value);var calls=world.Calls.Skip(start).ToArray();
            check(calls.Length==2 && calls[0].Method=="SetEcValue" && calls[1].Method=="GetEcValue" &&
                calls.All(call=>call.Index is int raw && raw==pair.Register) && calls[0].Data is byte value && value==pair.Value,
                "uma:raw-int32-index-byte-data-and-same-register-readback-"+pair.Register+"-"+pair.Value);
        }
        check(transport.DevicePath==world.Rows[0]!.Path as string && world.Values[0x044A]==0,
            "uma:write-null-result-is-allowed-with-readback");
        world.NullWriteResult=false;transport.WriteByte(0x044B,128);
        check(world.Values[0x044B]==128,"uma:write-result-has-no-assumed-returnvalue-schema");
        int before=world.Calls.Length;
        foreach(ushort register in new ushort[]{0,0x0470,0x044C,0x4A04})
        {
            Reject(()=>transport.ReadByte(register),check,"nonfan-read-"+register);
            Reject(()=>transport.WriteByte(register,10),check,"nonfan-write-"+register);
        }
        foreach(byte value in new byte[]{2,255})Reject(()=>transport.WriteByte(0x044A,value),check,"unadmitted-control-value-"+value);
        check(world.Calls.Length==before,"uma:register-and-mode-refusal-has-no-method-io");
    });
    private static void StrictData(Action<bool,string> check)
    {
        foreach(var data in new object[]{(byte)0,(byte)255,0,255,0u,255u}) Exercise(check,"valid-data",(world,transport)=> {
            transport.Initialize();world.ReadData=_=>data;
            check(transport.ReadByte(0x044B)==Convert.ToByte(data),"uma:accept-exact-output-type-and-range-"+data.GetType().Name+"-"+data);
        });
        var invalid=new object?[]{null,(sbyte)1,(short)1,(ushort)1,1L,1UL,1d,1f,1m,true,"1",new byte[]{1},-1,256,int.MaxValue,256u,uint.MaxValue};
        foreach(var data in invalid) Exercise(check,"invalid-data",(world,transport)=> {
            transport.Initialize();world.ReadData=_=>data;
            Reject(()=>transport.ReadByte(0x044B),check,"strict-output-"+(data?.GetType().Name??"null")+"-"+data);
        });
        foreach(var missing in new[]{"Data","result","getter"}) Exercise(check,"missing-"+missing,(world,transport)=> {
            transport.Initialize();world.MissingData=missing=="Data";world.NullReadResult=missing=="result";
            if(missing=="getter")world.FailAt="result.Data";
            Reject(()=>transport.ReadByte(0x044B),check,"missing-output-"+missing);
        });
    }
    private static void WriteErrors(Action<bool,string> check)
    {
        foreach(ushort register in new ushort[]{0x044A,0x044B}) Exercise(check,"readback-"+register,(world,transport)=> {
            transport.Initialize();world.IgnoreWrites=true;
            Reject(()=>transport.WriteByte(register,1),check,"readback-mismatch-"+register);
            check(world.Writes.Length==1 && world.Calls.Last().Method=="GetEcValue","uma:successful-invoke-is-not-successful-write-"+register);
        });
        foreach(var fault in new[]{"input.create","input.set.Index","input.set.Data","invoke.SetEcValue","invoke.GetEcValue","result.Data"})
            Exercise(check,fault,(world,transport)=> {
                transport.Initialize();world.FailAt=fault;
                Reject(()=>transport.WriteByte(0x044B,42),check,"operation-fault-"+fault);
            });
        Exercise(check,"issued-write-unknown",(world,transport)=> {
            transport.Initialize();world.ThrowAfterWrite=true;
            Reject(()=>transport.WriteByte(0x044B,42),check,"write-fails-after-issued-side-effect");
            check(world.Values[0x044B]==42 && world.Calls.Length==1,"uma:unknown-issued-write-not-replayed-or-claimed-readback");
        });
    }
    private static void InitializationErrors(Action<bool,string> check)
    {
        using(var world=new World())
        {
            var transport=world.Transport();world.FailAt="searcher.ctor";
            try { Reject(transport.Initialize,check,"constructor-fault");Reject(transport.Initialize,check,"constructor-no-retry"); }
            finally {world.ClearFaults();transport.Dispose();}
            check(!transport.IsReady && world.Searchers==0 && world.Resources.Length==0,"uma:unreturned-constructor-no-fabricated-resource-or-readiness");
        }
        foreach(var fault in new[]{"searcher.options","options.timeout","searcher.get","collection.enumerator","enumerator.move",
            "row.InstanceName","row.__PATH","row.scope","row.classpath","class.ctor","class.methods","method.GetEcValue","method.in","method.out","property.type","property.array"})
            Exercise(check,fault,(world,transport)=> {
                world.FailAt=fault;Reject(transport.Initialize,check,"init-fault-"+fault);
                check(!transport.IsReady && world.Calls.Length==0,"uma:partial-initializer-not-ready-and-no-method-io-"+fault);
                Reject(transport.Initialize,check,"partial-init-no-retry-"+fault);
            });
    }
    private static void ResourceDebt(Action<bool,string> check)
    {
        foreach(var kind in new[]{"searcher","collection","enumerator","class","metadata-read-in","metadata-read-out","metadata-write-in","row"})
            Exercise(check,"dispose-"+kind,(world,transport)=> {
                world.FailDisposals.Add(kind);
                Reject(transport.Initialize,check,"temporary-dispose-fault-"+kind);
                var retained=world.Resources.Where(resource=>resource.Kind==kind && !resource.Closed).ToArray();
                check(!transport.IsReady && retained.Length>0 && world.Calls.Length==0,"uma:partial-init-retains-exact-failed-object-"+kind);
                var previousAttempts=retained.Select(resource=>resource.Attempts).ToArray();
                Reject(transport.Dispose,check,"still-failed-dispose-retained-"+kind);
                check(retained.Select((resource,index)=>!resource.Closed && resource.Attempts==previousAttempts[index]+1).All(value=>value),
                    "uma:same-object-dispose-retried-"+kind);
                Reject(transport.Initialize,check,"debt-never-reopens-"+kind);
            },world=>world.Rows.Insert(0,new RowSpec { InstanceName="not selected" }));
    }
    private static void PendingInitialize(Action<bool,string> check)
    {
        using var world=new World();var transport=world.Transport();var owner=world.Own(transport);
        using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();
        world.OnStep=name=> {
            if(name!="searcher.get")return;
            entered.Set();if(!release.Wait(5000))throw new IOException("UMA initializer barrier timeout");
        };
        var opening=Task.Run(()=> {try{owner.Open();return (Exception?)null;}catch(Exception error){return error;}});
        try
        {
            check(entered.Wait(1500) && owner.PendingIo && owner.Phase==PinnedFanPhase.Opening,"uma:actual-initialize-is-captured-pending-io");
            var stopping=Task.Run(owner.TryStop);
            check(stopping.Wait(1000) && !stopping.Result,"uma:stop-during-initialize-is-nonblocking-and-pending");
            Busy(world,check,"pending-initialize-blocks-successor");
        }
        finally
        {
            release.Set();if(!opening.Wait(5000))throw new IOException("UMA initializer did not finish");
            Finish(world,owner);
        }
        check(opening.Result is InvalidOperationException && owner.Phase==PinnedFanPhase.Closed &&
            !owner.PendingIo && !owner.ControlWasAttempted && !owner.AutomaticReturnObserved && world.Writes.Length==0,
            "uma:late-initializer-retires-without-invented-control-or-auto-proof");
        world.CheckClosed(check,"pending-initialize");
    }
    private static void PendingWrite(Action<bool,string> check)
    {
        using var world=new World();var transport=world.Transport();var owner=world.Own(transport);owner.Open();
        using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();
        world.BeforeInvoke=call=> {
            if(call.Method!="SetEcValue" || call.Index is not int index || index!=0x044A || call.Data is not byte value || value!=1)return;
            entered.Set();if(!release.Wait(5000))throw new IOException("UMA issued write barrier timeout");
        };
        var applying=Task.Run(()=> {try{owner.ApplyPercent(70);return (Exception?)null;}catch(Exception error){return error;}});
        try
        {
            check(entered.Wait(1500) && owner.PendingIo && owner.ControlWasAttempted,"uma:actual-reflected-control-call-is-issued-and-pending");
            var stopping=Task.Run(owner.TryStop);
            check(stopping.Wait(1000) && !stopping.Result && owner.Phase==PinnedFanPhase.Retiring,
                "uma:pending-reflected-call-does-not-block-stop-announcement");
            Busy(world,check,"pending-issued-write-blocks-successor");
        }
        finally
        {
            release.Set();if(!applying.Wait(5000))throw new IOException("UMA issued write did not finish");
            world.BeforeInvoke=null;
        }
        try
        {
            check(applying.Result is InvalidOperationException && world.Writes.Length==1 &&
                world.Writes[0].Index is int raw && raw==0x044A && world.Values[0x044A]==1,
                "uma:late-control-return-has-no-following-duty-write");
            check(owner.TryStop() && owner.AutomaticReturnObserved && owner.Phase==PinnedFanPhase.Closed &&
                world.Writes.Length==2 && world.Writes.Last().Index is int automatic && automatic==0x044A &&
                world.Writes.Last().Data is byte zero && zero==0,"uma:actual-stop-returns-mode-on-the-same-owned-row");
            var successorTransport=world.Transport();var successor=world.Own(successorTransport);
            try
            {
                successor.Open();int before=world.Calls.Length;
                check(successor.Generation>owner.Generation && successor.Phase==PinnedFanPhase.Ready,"uma:successor-generation-after-actual-row-close");
                check(owner.TryStop() && world.Calls.Length==before,"uma:retired-stop-cannot-touch-successor-generation");
                Reject(()=>owner.ApplyPercent(50),check,"retired-session-cannot-write-successor");
                check(successor.ReadTemperature() is null && world.Calls.Length==before,"uma:superx-no-guessed-ec-temperature-consumer");
            }
            finally {Finish(world,successor);}
        }
        finally {Finish(world,owner);}
        world.CheckClosed(check,"pending-write-and-generation");
    }
    private static void ReturnDebt(Action<bool,string> check)
    {
        foreach(var kind in new[]{"input","result"})
        {
            using var world=new World();var transport=world.Transport();var owner=world.Own(transport);
            try
            {
                owner.Open();owner.ApplyPercent(50);world.NullWriteResult=false;
                world.BeforeInvoke=call=> {if(call.Method=="SetEcValue" && call.Index is int index && index==0x044B)world.FailDisposals.Add(kind);};
                Reject(()=>owner.ApplyPercent(60),check,"issued-operation-dispose-debt-"+kind);
                world.BeforeInvoke=null;
                var retained=world.Resources.Where(resource=>resource.Kind==kind && !resource.Closed).ToArray();
                check(!transport.IsReady && owner.Phase==PinnedFanPhase.Retiring && retained.Length==1 && world.Values[0x044B]==153,
                    "uma:actual-issued-duty-and-exact-temporary-debt-"+kind);
                int writes=world.Writes.Length;
                check(!owner.TryStop() && !owner.AutomaticReturnObserved && world.Writes.Length==writes,
                    "uma:failed-prepare-retirement-blocks-auto-until-temporary-disposed-"+kind);
                Busy(world,check,"retained-call-resource-blocks-generation-"+kind);
                world.ClearFaults();
                check(owner.TryStop() && retained.Single().Closed && retained.Single().Attempts>=2 &&
                    owner.AutomaticReturnObserved && owner.Phase==PinnedFanPhase.Closed && world.Values[0x044A]==0,
                    "uma:prepare-retirement-retries-same-object-then-observes-auto-"+kind);
            }
            finally {Finish(world,owner);}
            world.CheckClosed(check,"issued-dispose-debt-"+kind);
        }
        using(var world=new World())
        {
            var owner=world.Own(world.Transport());
            try
            {
                owner.Open();owner.ApplyPercent(50);world.IgnoreAutomatic=true;
                check(!owner.TryStop() && owner.Phase==PinnedFanPhase.Retiring && !owner.AutomaticReturnObserved &&
                    world.Values[0x044A]==1 && world.Resources.OfType<FakeRow>().Any(row=>!row.Closed),
                    "uma:failed-auto-readback-retains-selected-row-and-return-debt");
                Busy(world,check,"failed-auto-return-blocks-generation");
                Reject(()=>owner.ApplyPercent(70),check,"retiring-owner-never-revives");
                world.IgnoreAutomatic=false;
                check(owner.TryStop() && owner.AutomaticReturnObserved && world.Values[0x044A]==0,"uma:auto-return-retry-is-observed-before-row-close");
            }
            finally {Finish(world,owner);}
            world.CheckClosed(check,"failed-auto-return");
        }
        using(var world=new World())
        {
            var owner=world.Own(world.Transport());
            try
            {
                owner.Open();world.ThrowAfterWrite=true;
                Reject(()=>owner.ApplyPercent(50),check,"issued-manual-call-uncertain");
                check(owner.ControlWasAttempted && owner.Phase==PinnedFanPhase.Retiring && world.Values[0x044A]==1 &&
                    world.Writes.Length==1,"uma:issued-manual-error-keeps-return-debt-with-no-duty");
                world.ThrowAfterWrite=false;
                check(owner.TryStop() && owner.AutomaticReturnObserved,"uma:issued-manual-error-still-returns-through-same-transport");
            }
            finally {Finish(world,owner);}
            world.CheckClosed(check,"issued-manual-error");
        }
    }
    private static void DerivedDisposeDebt(Action<bool,string> check)
    {
        using var world=new World();var transport=world.Transport();var owner=world.Own(transport);long generation=owner.Generation;
        try
        {
            owner.Open();owner.ApplyPercent(50);
            var selected=world.Resources.OfType<FakeRow>().Single();selected.FailOnce=true;
            check(!owner.TryStop() && owner.Phase==PinnedFanPhase.Retiring && owner.AutomaticReturnObserved &&
                !transport.IsReady && !selected.Closed && selected.DerivedDisposeCalls==1 && selected.BaseInterfaceCalls==0,
                "uma:derived-public-dispose-failure-retains-exact-row-despite-inherited-interface");
            Busy(world,check,"derived-dispose-debt-blocks-generation");
            int before=world.Writes.Length;
            check(owner.TryStop() && owner.Phase==PinnedFanPhase.Closed && selected.Closed &&
                selected.SuccessfulCloses==1 && selected.DerivedDisposeCalls==2 && selected.BaseInterfaceCalls==0 &&
                world.Writes.Length==before,"uma:derived-public-dispose-retry-closes-without-replaying-auto-return");
            var successor=world.Own(world.Transport());
            try { successor.Open();check(successor.Generation>generation,"uma:derived-resource-actually-closed-before-new-generation"); }
            finally {Finish(world,successor);}
        }
        finally {Finish(world,owner);}
        world.CheckClosed(check,"derived-public-dispose");
    }
    private static void OperationRevocation(Action<bool,string> check)
    {
        foreach(bool context in new[]{true,false})
        {
            using var world=new World();bool permit=true,current=true;
            var owner=world.Own(world.Transport(),()=>permit);
            try
            {
                owner.Open();world.BeforeInvoke=call=> {
                    if(call.Method=="SetEcValue" && call.Index is int index && index==0x044A)
                    {if(context)permit=false;else current=false;}
                };
                Reject(()=>owner.ApplyPercent(60,()=>current),check,context ? "context-revoked-after-control" : "operation-superseded-after-control");
                check(world.Writes.Length==1 && owner.ControlWasAttempted && owner.Phase==PinnedFanPhase.Retiring,
                    "uma:revocation-after-issued-control-prevents-following-duty-"+context);
                world.BeforeInvoke=null;
                check(owner.TryStop() && owner.AutomaticReturnObserved && world.Values[0x044A]==0,
                    "uma:owned-retirement-still-works-after-ordinary-permit-revocation-"+context);
            }
            finally {Finish(world,owner);}
            world.CheckClosed(check,"revocation-"+context);
        }
    }
}
