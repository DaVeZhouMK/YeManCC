using System;

using System.Collections.Generic;

using System.Linq;

using YeManFanHost;

using System.IO;

using System.Threading;

using System.Threading.Tasks;

using YeManFanBackend.Capabilities;



internal static class PinnedFanSourceChecks

{

    private const string Token="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    private sealed class MemoryTransport : IPinnedFanTransport

    {

        public string EndpointIdentity {get;set;}="memory:exact-owned-ec";

        public bool IsReady {get;private set;}=true;

        public readonly Dictionary<ushort,byte> Values=new();

        public readonly List<(ushort Register,byte Value)> Writes=new();

        public ushort Control;

        public bool IgnoreAutomatic,FailDispose,FailWrite;

        public Action<ushort,byte>? BeforeWrite;

        public int Reads,Disposes;

        public MemoryTransport(PinnedFanDescriptor d){Control=d.ControlRegister;Values[d.ControlRegister]=0;Values[d.DutyRegister]=0;if(d.TemperatureRegister is ushort t)Values[t]=50;}

        public byte ReadByte(ushort register){if(!IsReady)throw new IOException("closed");Reads++;if(!Values.TryGetValue(register,out var value))throw new IOException("Unexpected source register");return value;}

        public void WriteByte(ushort register,byte value){if(!IsReady)throw new IOException("closed");Writes.Add((register,value));BeforeWrite?.Invoke(register,value);if(FailWrite)throw new IOException("injected issued write failure");if(!(IgnoreAutomatic&&register==Control&&value==0))Values[register]=value;}

        public void Dispose(){Disposes++;if(FailDispose)throw new IOException("injected close failure");IsReady=false;}

    }

    private static void Reject(Action body,Action<bool,string> check,string name)

    {bool rejected=false;try{body();}catch(Exception ex)when(ex is InvalidOperationException or ArgumentException or IOException){rejected=true;}check(rejected,name);}

    private static PinnedFanDescriptor Model(string name)=>FanCapabilityCatalog.FindPinnedFanSource("HandheldCompanion.Devices."+name)??throw new InvalidOperationException(name);

    private static PinnedSourceFanSession Own(PinnedFanDescriptor d,MemoryTransport t,Func<bool>? permit=null)=>

        FanCapabilityCatalog.AcquirePinnedFanSource(d,Token,permit??(()=>true),()=>t);

    private sealed class MemoryWmi : IPinnedFanWmiMethod

    {

        public string EndpointIdentity=>"memory:wmi-method";

        public bool IsReady {get;private set;}=true;

        public string? Response="0,0";

        public readonly List<(string,uint)> Calls=new();

        public string? Invoke(string name,object[] args){Calls.Add((name,(uint)args[0]));return Response;}

        public void Dispose()=>IsReady=false;

    }

    private sealed class MemoryBus : IPinnedEcBusLease

    {

        internal bool FailEnter,FailDispose;

        internal int Enters,Releases,Disposes;

        public IDisposable Enter(){Enters++;if(FailEnter)throw new IOException("bus busy");return new BusRelease(this);}

        private sealed class BusRelease(MemoryBus owner):IDisposable {public void Dispose()=>owner.Releases++;}

        public void Dispose(){Disposes++;if(FailDispose)throw new IOException("bus close failed");}

    }

    private sealed class MemoryPorts : IPinnedSuperIo

    {

        public string EndpointIdentity=>"memory:raw-superio";

        public bool IsReady {get;private set;}=true;

        internal bool FailRead,FailEnter,FailExit;

        internal readonly List<string> Trace=new();

        public void Enter(ushort status,ushort data){Trace.Add($"enter:{status:X4}:{data:X4}");if(FailEnter)throw new IOException("enter failed");}

        public void Exit(ushort status,ushort data){Trace.Add($"exit:{status:X4}:{data:X4}");if(FailExit)throw new IOException("exit failed");}

        public void Write(ushort port,byte value)=>Trace.Add($"w:{port:X4}:{value:X2}");

        public byte Read(ushort port){Trace.Add($"r:{port:X4}");if(FailRead)throw new IOException("read failed");return 0xB8;}

        public void Dispose()=>IsReady=false;

    }

    public sealed class FakePawn
    {
        // ownsHandle=false: closing this marker never invokes a native close.
        private readonly Microsoft.Win32.SafeHandles.SafeFileHandle _handle=new(new IntPtr(1),false);
        public Microsoft.Win32.SafeHandles.SafeFileHandle Handle=>_handle;
        public bool FailClose {get;set;}
        public bool IgnoreClose {get;set;}
        public int Closes {get;private set;}
        public void Close(){if(FailClose)throw new IOException("raw close failed");if(!IgnoreClose){_handle.Close();Closes++;}}
    }
    public sealed class FakeRawPort
    {
        private readonly FakePawn _pawnIO;
        public static readonly List<FakeRawPort> Instances=new();
        public static bool FailConstructor {get;set;}
        public FakeRawPort(){if(FailConstructor)throw new IOException("constructor uncertainty");_pawnIO=new();Instances.Add(this);}
        public FakePawn Pawn=>_pawnIO;
        public int Reads {get;private set;}
        public readonly Dictionary<ushort,byte> Values=new();
        private byte command,selector,upper,lower;
        public readonly List<(ushort,byte)> Writes=new();
        public void SelectSlot(int slot){if(slot!=1)throw new IOException("wrong fixed port slot");}
        public byte ReadPort(ushort port){Reads++;return Values.GetValueOrDefault((ushort)((upper<<8)|lower));}
        public void WritePort(ushort port,byte value)
        {
            Writes.Add((port,value));
            if(port==0x4e)command=value;
            else if(port==0x4f&&command==0x2e)selector=value;
            else if(port==0x4f&&command==0x2f)
            {
                if(selector==0x11)upper=value;
                else if(selector==0x10)lower=value;
                else if(selector==0x12)Values[(ushort)((upper<<8)|lower)]=value;
            }
        }
        public void Close()=>_pawnIO.Close();
    }
    public sealed class FakeConfigPort
    {
        private readonly FakeRawPort _pawnModule;
        public static readonly List<FakeConfigPort> Instances=new();
        public static Action<FakeConfigPort>? BeforeEnter {get;set;}
        public FakeConfigPort(ushort status,ushort data){if(status!=0x4e||data!=0x4f)throw new IOException("bad config ports");_pawnModule=new();Instances.Add(this);}
        public FakeRawPort Port=>_pawnModule;
        public bool FailEnter {get;set;}
        public bool FailExit {get;set;}
        public int Enters {get;private set;}
        public int Exits {get;private set;}
        public void IT87Enter(){Enters++;BeforeEnter?.Invoke(this);if(FailEnter)throw new IOException("config enter failed");}
        public void IT87Exit(){Exits++;if(FailExit)throw new IOException("config exit failed");}
        public void Close()=>_pawnModule.Close();
    }
    private static ReflectionFanSuperIo RawTerminal()=>new(typeof(FakeRawPort),typeof(FakeConfigPort));
    private static void ResetRaw(){FakeRawPort.Instances.Clear();FakeConfigPort.Instances.Clear();FakeConfigPort.BeforeEnter=null;FakeRawPort.FailConstructor=false;}
    private static void RunTerminals(Action<bool,string> check)

    {

        using(var method=new MemoryWmi())

        using(var terminal=new PinnedWmiFanTransport(method))

        {

            method.Response=" 0x00 , 0xb8 ";check(terminal.ReadByte(0x044A)==184,"pinned-fan:wmi-fixed-read-format");

            check(method.Calls.Last()==("ReadECReg",0x4A04u),"pinned-fan:wmi-fixed-bank-offset-packing");

            method.Response="00";terminal.WriteByte(0x044B,184);

            check(method.Calls.Last()==("WriteECReg",0xB84B04u),"pinned-fan:wmi-fixed-write-byte-packing");

            method.Response="0,0";check(terminal.ReadByte(0x0470)==0,"pinned-fan:wmi-zero-is-data");

            foreach(var bad in new string?[]{null,"","0","1,10","00,GG","00,100","0001,00"})

            {method.Response=bad;Reject(()=>terminal.ReadByte(0x044A),check,"pinned-fan:wmi-invalid-read-"+(bad??"missing"));}

            foreach(var bad in new string?[]{null,"","1","GG","100"})

            {method.Response=bad;Reject(()=>terminal.WriteByte(0x044B,42),check,"pinned-fan:wmi-invalid-write-"+(bad??"missing"));}

            method.Dispose();int before=method.Calls.Count;Reject(()=>terminal.ReadByte(0),check,"pinned-fan:wmi-closed-read-denied");

            check(method.Calls.Count==before,"pinned-fan:wmi-closed-no-method-call");

        }

        // Full independent fixed IDevice read/write port trace. Effective

        // register values separately come from each class's constructor chain.

        foreach(var d in FanCapabilityCatalog.PinnedSourceDeclarations.Where(x=>x.Transport==PinnedFanTransportKind.SuperIo16))

        {

            var ports=new MemoryPorts();var bus=new MemoryBus();using var t=new PinnedSuperIoFanTransport(ports,d,bus);

            var prefix=$"enter:004E:004F,w:004E:2E,w:004F:11,w:004E:2F,w:004F:{d.ControlRegister>>8:X2},w:004E:2E,w:004F:10,w:004E:2F,w:004F:{(byte)d.ControlRegister:X2},w:004E:2E,w:004F:12,w:004E:2F";

            check(t.ReadByte(d.ControlRegister)==184,"pinned-fan:raw-read-value-"+d.DeviceId);

            check(string.Join(',',ports.Trace)==prefix+",r:004F,exit:004E:004F","pinned-fan:full-fixed-read-sequence-"+d.DeviceId);

            ports.Trace.Clear();t.WriteByte(d.ControlRegister,d.ControlOn);

            check(string.Join(',',ports.Trace)==prefix+$",w:004F:{d.ControlOn:X2},exit:004E:004F","pinned-fan:full-fixed-write-sequence-"+d.DeviceId);

            check(bus.Enters==2&&bus.Releases==2,"pinned-fan:each-transaction-releases-bus-"+d.DeviceId);

        }

        var source=Model("AYANEOAIR2");

        {

            var ports=new MemoryPorts{FailRead=true};var bus=new MemoryBus();using var t=new PinnedSuperIoFanTransport(ports,source,bus);

            Reject(()=>t.ReadByte(source.ControlRegister),check,"pinned-fan:raw-read-exception-is-not-zero");

            check(ports.Trace.Last()=="exit:004E:004F"&&bus.Releases==1,"pinned-fan:read-failure-exits-config-and-bus");

            ports.FailExit=true;bool both=false;try{t.ReadByte(source.ControlRegister);}catch(AggregateException ex){both=ex.InnerExceptions.Count==2;}

            check(both&&bus.Releases==2,"pinned-fan:read-and-exit-failures-both-preserved");

        }

        {

            var ports=new MemoryPorts{FailEnter=true};var bus=new MemoryBus();using var t=new PinnedSuperIoFanTransport(ports,source,bus);

            Reject(()=>t.ReadByte(0),check,"pinned-fan:enter-failure-rejected");

            check(ports.Trace.Count==2&&bus.Releases==1,"pinned-fan:failed-enter-has-no-register-access");

        }

        {

            var ports=new MemoryPorts();var bus=new MemoryBus{FailEnter=true};using var t=new PinnedSuperIoFanTransport(ports,source,bus);

            Reject(()=>t.ReadByte(0),check,"pinned-fan:busy-bus-rejected");check(ports.Trace.Count==0,"pinned-fan:busy-bus-no-io");bus.FailEnter=false;

        }

        {

            var ports=new MemoryPorts{FailExit=true};var bus=new MemoryBus();using var t=new PinnedSuperIoFanTransport(ports,source,bus);

            Reject(()=>t.ReadByte(0),check,"pinned-fan:exit-failure-cannot-publish-read-success");

        }

        {

            var ports=new MemoryPorts();var bus=new MemoryBus{FailDispose=true};var t=new PinnedSuperIoFanTransport(ports,source,bus);

            Reject(t.Dispose,check,"pinned-fan:bus-handle-close-failure-propagates");

            bus.FailDispose=false;t.Dispose();check(bus.Disposes==2&&!t.IsReady,"pinned-fan:bus-handle-close-retry-completes");

        }

        {
            ResetRaw();var raw=RawTerminal();
            check(!raw.IsReady&&FakeRawPort.Instances.Count==0,"pinned-fan:binding-construction-is-dormant");
            raw.Initialize();var port=FakeRawPort.Instances.Single();port.Pawn.FailClose=true;
            Reject(raw.Dispose,check,"pinned-fan:raw-close-failure-propagates");
            check(raw.IsReady&&!port.Pawn.Handle.IsClosed,"pinned-fan:wrapper-cannot-hide-failed-close");
            port.Pawn.FailClose=false;port.Pawn.IgnoreClose=true;
            Reject(raw.Dispose,check,"pinned-fan:void-close-with-open-handle-is-rejected");
            check(raw.IsReady,"pinned-fan:quiet-close-retains-exact-remaining-resource");
            port.Pawn.IgnoreClose=false;raw.Dispose();raw.Dispose();
            check(port.Pawn.Closes==1&&!raw.IsReady,"pinned-fan:retry-does-not-double-close-completed-resource");
        }
        {
            ResetRaw();var raw=RawTerminal();var bus=new MemoryBus();using var t=new PinnedSuperIoFanTransport(raw,source,bus);t.Initialize();
            var primary=FakeRawPort.Instances.Single();t.ReadByte(source.ControlRegister);t.WriteByte(source.DutyRegister,100);
            check(FakeConfigPort.Instances.Count==2,"pinned-fan:one-config-resource-per-fixed-transaction");
            check(FakeConfigPort.Instances.All(x=>x.Port.Pawn.Handle.IsClosed&&x.Port.Pawn.Closes==1&&x.Exits==1),"pinned-fan:each-config-closed-before-next-transaction");
            check(!primary.Pawn.Handle.IsClosed,"pinned-fan:raw-port-retained-through-owned-transactions");
            t.Dispose();check(primary.Pawn.Handle.IsClosed&&bus.Enters==3&&bus.Releases==3,"pinned-fan:final-raw-close-uses-bus-and-is-observed");
        }
        {
            ResetRaw();FakeConfigPort.BeforeEnter=x=>x.Port.Pawn.FailClose=true;
            var raw=RawTerminal();var bus=new MemoryBus();var t=new PinnedSuperIoFanTransport(raw,source,bus);
            var owner=FanCapabilityCatalog.AcquirePinnedFanSource(source,Token,()=>true,()=>t);
            try
            {
                Reject(owner.Open,check,"pinned-fan:partial-initializer-fails-owned-open");var config=FakeConfigPort.Instances.Single();
                check(!owner.TryStop(),"pinned-fan:partial-initializer-close-failure-retains-lease");
                Reject(()=>Own(source,new MemoryTransport(source)),check,"pinned-fan:partial-initializer-debt-denies-successor");
                check(FakeConfigPort.Instances.Count==1&&!config.Port.Pawn.Handle.IsClosed,"pinned-fan:pending-config-is-never-overwritten");
                config.Port.Pawn.FailClose=false;
                check(owner.TryStop()&&config.Port.Pawn.Closes==1&&FakeRawPort.Instances.All(x=>x.Pawn.Handle.IsClosed)&&bus.Disposes==1,"pinned-fan:partial-initializer-cleanup-observed-before-release");
            }
            finally{FakeConfigPort.BeforeEnter=null;FakeConfigPort.Instances.Single().Port.Pawn.FailClose=false;owner.Dispose();}
        }
        {
            ResetRaw();FakeConfigPort.BeforeEnter=x=>x.FailEnter=true;
            var raw=RawTerminal();var bus=new MemoryBus();using var t=new PinnedSuperIoFanTransport(raw,source,bus);t.Initialize();
            var primary=FakeRawPort.Instances.Single();Reject(()=>t.ReadByte(0),check,"pinned-fan:reflected-config-enter-failure-rejected");
            var config=FakeConfigPort.Instances.Single();check(primary.Reads==0&&config.Exits==1&&config.Port.Pawn.Handle.IsClosed,"pinned-fan:failed-enter-retires-captured-config-without-data-read");FakeConfigPort.BeforeEnter=null;
        }
        {
            ResetRaw();FakeConfigPort.BeforeEnter=x=>x.FailExit=true;
            var raw=RawTerminal();var bus=new MemoryBus();var t=new PinnedSuperIoFanTransport(raw,source,bus);t.Initialize();
            Reject(()=>t.ReadByte(0),check,"pinned-fan:reflected-exit-failure-not-read-success");
            var config=FakeConfigPort.Instances.Single();Reject(t.Dispose,check,"pinned-fan:failed-config-exit-retains-resource");
            check(!config.Port.Pawn.Handle.IsClosed,"pinned-fan:failed-exit-not-fake-close");
            config.FailExit=false;FakeConfigPort.BeforeEnter=null;t.Dispose();check(config.Port.Pawn.Handle.IsClosed,"pinned-fan:config-exit-retry-settles-resource");
        }
        {
            ResetRaw();FakeConfigPort.BeforeEnter=x=>{if(FakeConfigPort.Instances.Count==3)x.Port.Pawn.FailClose=true;};
            var raw=RawTerminal();var bus=new MemoryBus();var t=new PinnedSuperIoFanTransport(raw,source,bus);
            var owner=FanCapabilityCatalog.AcquirePinnedFanSource(source,Token,()=>true,()=>t);owner.Open();
            var primary=FakeRawPort.Instances[0];
            try
            {
                Reject(()=>owner.ApplyPercent(60),check,"pinned-fan:issued-control-with-config-close-failure-retains-debt");
                var config=FakeConfigPort.Instances[2];
                check(primary.Values[source.ControlRegister]==source.ControlOn&&!primary.Values.ContainsKey(source.DutyRegister),"pinned-fan:issued-mode-is-real-but-following-duty-refused");
                check(!owner.TryStop()&&FakeConfigPort.Instances.Count==3,"pinned-fan:failed-config-retirement-denies-next-return-transaction");
                Reject(()=>Own(source,new MemoryTransport(source)),check,"pinned-fan:issued-config-debt-denies-successor");
                config.Port.Pawn.FailClose=false;FakeConfigPort.BeforeEnter=null;
                check(owner.TryStop()&&owner.AutomaticReturnObserved&&primary.Values[source.ControlRegister]==0,"pinned-fan:config-retired-before-same-owner-auto-return");
                check(FakeRawPort.Instances.All(x=>x.Pawn.Handle.IsClosed),"pinned-fan:issued-config-failure-all-owned-handles-eventually-closed");
            }
            finally{FakeConfigPort.BeforeEnter=null;foreach(var x in FakeRawPort.Instances)x.Pawn.FailClose=false;owner.Dispose();}
        }
        {
            ResetRaw();FakeRawPort.FailConstructor=true;var raw=RawTerminal();
            Reject(raw.Initialize,check,"pinned-fan:unreturned-constructor-is-failure");
            Reject(raw.Dispose,check,"pinned-fan:unknown-constructor-cleanup-not-claimed-closed");
            check(!raw.IsReady&&FakeRawPort.Instances.Count==0,"pinned-fan:unknown-constructor-has-no-ordinary-port-io");FakeRawPort.FailConstructor=false;
        }
    }

    // Independent PREIMAGE tuples keep all nine existing source identities exact.
    // The UMA directed group reuses this assertion without executing the old IO groups.
    internal static void CheckCatalog(Action<bool,string> check)
    {
        var oldIdentity = new[]
        {
            "HandheldCompanion.Devices.AYANEO3|ymcc.device.AYANEO3|HandheldCompanion/Devices/AYANEO/AYANEO3.cs|bee53cd4d730013900da5c542c751b018f006ee09b886c55926f0ac7c17ee88b|f2e58468d9917189d4be7ccfc242385ce2c3035b|Valkirie/HandheldCompanion|SuperIo16|044A|044B|1|100|004E|004F|null|AYADEVICE,AYANEO,AYA|AYANEO 3,AB07",
            "HandheldCompanion.Devices.AYANEOAIR2|ymcc.device.AYANEOAIR2|HandheldCompanion/Devices/AYANEO/AYANEOAIR2.cs|0bde655963a66375a30d17191a6f08a23d1ec39f7b0732c3aa28cf321a65a67c|f2e58468d9917189d4be7ccfc242385ce2c3035b|Valkirie/HandheldCompanion|SuperIo16|02F1|1809|1|184|004E|004F|null|AYADEVICE,AYANEO,AYA|AIR 2,AIR2,AYANEO AIR 2,AB10",
            "HandheldCompanion.Devices.AYANEOAIRPlusNxx|ymcc.device.AYANEOAIRPlusNxx|HandheldCompanion/Devices/AYANEO/AYANEOAIRPlusNxx.cs|5b9f3d00c3d70a7c38ed3c72418dfd47c8b37f9c7ac25a96cfdb5dedf5f6dd07|f2e58468d9917189d4be7ccfc242385ce2c3035b|Valkirie/HandheldCompanion|SuperIo16|D1C8|1802|165|255|004E|004F|null|AYADEVICE,AYANEO,AYA|AB05N,AB05NXX,AIR Plus Nxx",
            "HandheldCompanion.Devices.AYANEOAIRPro1S|ymcc.device.AYANEOAIRPro1S|HandheldCompanion/Devices/AYANEO/AYANEOAIRPro1S.cs|43a7f1ef360a8ff9ce362974e530ea4b61361118cc93d9de6167c51c3574e652|f2e58468d9917189d4be7ccfc242385ce2c3035b|Valkirie/HandheldCompanion|SuperIo16|044A|044B|1|100|004E|004F|null|AYADEVICE,AYANEO,AYA|AIR Pro 1S,AYANEO AIR Pro 1S",
            "HandheldCompanion.Devices.AYANEOFlip11|ymcc.device.AYANEOFlip11|HandheldCompanion/Devices/AYANEO/AYANEOFlip11.cs|8ebe1ca0c53c740ccb82bc19beac83aef91636b32e67998fd28309787c104d36|f2e58468d9917189d4be7ccfc242385ce2c3035b|Valkirie/HandheldCompanion|SuperIo16|044A|044B|1|100|004E|004F|null|AYADEVICE,AYANEO,AYA|FLIP 11,AYANEO FLIP 11",
            "HandheldCompanion.Devices.AYANEONEXT2|ymcc.device.AYANEONEXT2|HandheldCompanion/Devices/AYANEO/AYANEONEXT2.cs|b47a90a52912eebd2568e920da7c1d0a264a286bb06657f9e274feb7f5da91c2|f2e58468d9917189d4be7ccfc242385ce2c3035b|Valkirie/HandheldCompanion|SuperIo16|044A|044B|1|100|004E|004F|null|AYADEVICE,AYANEO,AYA|NEXT II,NEXT 2,AYANEO NEXT II,AYANEO NEXT 2,AB09",
            "HandheldCompanion.Devices.OneXPlayer3|ymcc.device.OneXPlayer3|HandheldCompanion/Devices/OneXPlayer/OneXPlayer3.cs|e3a6275e34db52fa60c7b5a421655af24d8580611fe9ebb875986817891809dc|f2e58468d9917189d4be7ccfc242385ce2c3035b|Valkirie/HandheldCompanion|WmiBankedEc|044A|044B|1|184|004E|004F|0470|ONE-NETBOOK TECHNOLOGY CO., LTD.,ONE-NETBOOK|ONEXPLAYER 3,ONEXPLAYER-3",
            "HandheldCompanion.Devices.OneXPlayerX2|ymcc.device.OneXPlayerX2|HandheldCompanion/Devices/OneXPlayer/OneXPlayerX2.cs|41cf648a9d9247c2e1a5da29bed748f0aed0a8407202a690b0b866ae2d5c369b|f2e58468d9917189d4be7ccfc242385ce2c3035b|Valkirie/HandheldCompanion|WmiBankedEc|044A|044B|1|184|004E|004F|0470|ONE-NETBOOK TECHNOLOGY CO., LTD.,ONE-NETBOOK|ONEXPLAYER X2",
            "HandheldCompanion.Devices.OneXPlayerX2MiniPro|ymcc.device.OneXPlayerX2MiniPro|HandheldCompanion/Devices/OneXPlayer/OneXPlayerX2MiniPro.cs|7f06350daeb1d1c7157ae21f1dc231637394008256b3fc4c22c747109e2c7b67|f2e58468d9917189d4be7ccfc242385ce2c3035b|Valkirie/HandheldCompanion|SuperIo16|044A|044B|1|255|004E|004F|0470|ONE-NETBOOK TECHNOLOGY CO., LTD.,ONE-NETBOOK|ONEXPLAYER X2Mini PRO",
        };
        var oldRows = FanCapabilityCatalog.PinnedSourceDeclarations
            .Where(row => row.ModelClass != PinnedUmaFanChecks.ModelClass).ToArray();
        string Identity(PinnedFanDescriptor row) => string.Join("|", row.ModelClass, row.DeviceId,
            row.SourceFile, row.SourceSha256, row.SourceCommit, row.SourceRepository, row.Transport,
            row.ControlRegister.ToString("X4"), row.DutyRegister.ToString("X4"), row.ControlOn,
            row.Maximum, row.StatusPort.ToString("X4"), row.DataPort.ToString("X4"),
            row.TemperatureRegister?.ToString("X4") ?? "null", string.Join(",",row.Manufacturers), string.Join(",",row.Products));
        check(oldRows.Length == 9, "pinned-fan:source-nine-declarations");
        check(oldRows.Select(Identity).SequenceEqual(oldIdentity, StringComparer.Ordinal),
            "pinned-fan:original-nine-exact-identity-tuples");
        foreach (var row in oldRows)
        {
            check(!row.ManufacturerContains && row.ControlOff == 0 && row.Minimum == 0 &&
                row.ThermalTripTemperatureC == 95 && !row.PhysicalAvailabilityProven,
                "pinned-fan:original-nine-policy-"+row.DeviceId);
            check(row.Manufacturers.All(vendor => row.Products.All(board =>
                ReferenceEquals(FanCapabilityCatalog.MatchPinnedFanSource(vendor,board),row) &&
                FanCapabilityCatalog.MatchPinnedFanSource("prefix "+vendor+" suffix",board) is null &&
                FanCapabilityCatalog.MatchPinnedFanSource(vendor,board+" suffix") is null)),
                "pinned-fan:original-nine-exact-dmi-"+row.DeviceId);
        }
        var uma = PinnedUmaFanChecks.Descriptor;
        check(FanCapabilityCatalog.PinnedSourceDeclarations.Count == 10 &&
            FanCapabilityCatalog.PinnedSourceDeclarations.Last() == uma, "pinned-fan:tenth-source-is-superx");
        check(uma.ModelClass == "YeManCC.SourceDevices.OneXPlayerSuperX" &&
            uma.DeviceId == "ymcc.device.OneXPlayerSuperX" && uma.Transport == PinnedFanTransportKind.WmiUmaEc16 &&
            uma.ControlRegister == 0x044A && uma.DutyRegister == 0x044B && uma.ControlOn == 1 &&
            uma.ControlOff == 0 && uma.Minimum == 0 && uma.Maximum == 255 && uma.TemperatureRegister is null &&
            uma.StatusPort == 0 && uma.DataPort == 0 && !uma.PhysicalAvailabilityProven,
            "pinned-fan:superx-explicit-uma-register-contract");
        check(uma.SourceCommit == "6d728795511c2f9f683d1757e72481236de9b23a" &&
            uma.SourceRepository == "the-reengineers/XmaX" &&
            uma.SourceFile == "capabilities/upstream/xmax-6d728795/SuperXFan.source-facts.json" &&
            uma.SourceSha256 == "2d1a55bafb6a1cd906758d92cb1a05877a38a70143ee9fdb7c6e2b89b091c027",
            "pinned-fan:superx-independent-source-provenance");
        check(uma.ManufacturerContains && uma.Manufacturers.SequenceEqual(new[]{"ONE-NETBOOK"}) &&
            uma.Products.SequenceEqual(new[]{"ONEXPLAYER SUPER X"}) &&
            ReferenceEquals(FanCapabilityCatalog.MatchPinnedFanSource("Vendor ONE-NETBOOK TECHNOLOGY CO., LTD.","ONEXPLAYER SUPER X"),uma) &&
            ReferenceEquals(FanCapabilityCatalog.MatchPinnedFanSource("ONE-NETBOOK","ONEXPLAYER SUPER X"),uma),
            "pinned-fan:superx-source-vendor-substring-paired-with-exact-board");
        foreach (var board in new[]{"ONEXPLAYER SUPER X suffix","ONEXPLAYER SUPER","ONEXPLAYER Super X"," ONEXPLAYER SUPER X","ONEXPLAYER SUPER X ","ONEXPLAYER X2 invented"})
            check(FanCapabilityCatalog.MatchPinnedFanSource("ONE-NETBOOK",board) is null,
                "pinned-fan:superx-refuses-guessed-board-"+board);
        check(FanCapabilityCatalog.MatchPinnedFanSource("OTHER","ONEXPLAYER SUPER X") is null &&
            FanCapabilityCatalog.MatchPinnedFanSource("one-netbook","ONEXPLAYER SUPER X") is null &&
            FanCapabilityCatalog.MatchPinnedFanSource(null,"ONEXPLAYER SUPER X") is null &&
            FanCapabilityCatalog.MatchPinnedFanSource("ONE-NETBOOK",null) is null &&
            FanCapabilityCatalog.FindPinnedFanSource("HandheldCompanion.Devices.OneXPlayerSuperX") is null,
            "pinned-fan:superx-no-case-folded-vendor-or-runtime-factory-alias");
    }

    public static void Run(Action<bool,string> check)

    {

        RunTerminals(check);
        PinnedFanCurveChecks.Run(check);

        var expected=new (string Model,ushort Control,ushort Duty,byte On,byte Max)[]{

            ("AYANEO3",0x044A,0x044B,1,100),("AYANEOAIR2",0x02F1,0x1809,1,184),

            ("AYANEOAIRPlusNxx",0xD1C8,0x1802,0xA5,255),("AYANEOAIRPro1S",0x044A,0x044B,1,100),

            ("AYANEOFlip11",0x044A,0x044B,1,100),("AYANEONEXT2",0x044A,0x044B,1,100),

            ("OneXPlayer3",0x044A,0x044B,1,184),("OneXPlayerX2",0x044A,0x044B,1,184),

            ("OneXPlayerX2MiniPro",0x044A,0x044B,1,255)};

        foreach(var row in FanCapabilityCatalog.PinnedSourceDeclarations)
        {
            using var host=new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
            check(host.SelectPinnedSourceFanIdentityForSelfTest(row.Manufacturers[0],row.Products[0]),"pinned-fan:real-host-source-selector-"+row.DeviceId);
            var handshake=host.ReadPinnedSourceFanHandshakeForSelfTest();
            check(handshake.FactoryType is null&&handshake.SourceModelClass==row.ModelClass&&handshake.SourceCommit==row.SourceCommit,"pinned-fan:logical-source-never-fakes-runtime-factory-"+row.DeviceId);
            check(handshake.Supported&&handshake.HardwareCapable&&!handshake.HardwareWriteAuthorizationGranted&&!host.CanWrite&&!host.IsLoaded&&!host.IsOpen,"pinned-fan:source-capability-does-not-authorize-write-"+row.DeviceId);
            check(!host.HcOpenInvocationStarted&&!host.HcOpenEventsInvoked&&!host.HcVirtualCloseReturned&&host.HcDeviceIsOpenObserved is null,"pinned-fan:metadata-no-hc-lifecycle-receipts-"+row.DeviceId);
            check(handshake.DeviceIdentity?["runtimeFactory"]=="not-constructed"&&handshake.DeviceIdentity?["capabilityTransport"]==row.Transport.ToString(),"pinned-fan:metadata-exposes-actual-source-terminal-kind-"+row.DeviceId);
            Reject(()=>host.SelectPinnedSourceFanIdentityForSelfTest("unrelated","unknown"),check,"pinned-fan:host-source-identity-cannot-revive-under-other-dmi-"+row.DeviceId);
        }
        {
            using var host=new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
            check(!host.SelectPinnedSourceFanIdentityForSelfTest("ONE-NETBOOK","ONEXPLAYER X2 invented")&&host.SourceModelClass is null,"pinned-fan:host-unknown-product-keeps-original-factory-path");
        }
        CheckCatalog(check);

        check(HcDeviceCapabilityCatalog.Devices.Count==92,"pinned-fan:frozen-root92-retained");

        check(FanCapabilityCatalog.FindPinnedFanSource("HandheldCompanion.Devices.Unknown")==null,"pinned-fan:unknown-model-denied");

        check(FanCapabilityCatalog.MatchPinnedFanSource("AYN","AB07")==null,"pinned-fan:wrong-manufacturer-denied");

        check(FanCapabilityCatalog.MatchPinnedFanSource("ONE-NETBOOK","ONEXPLAYER X2")?.ModelClass=="HandheldCompanion.Devices.OneXPlayerX2","pinned-fan:exact-source-dmi-selected");

        check(FanCapabilityCatalog.MatchPinnedFanSource("ONE-NETBOOK","ONEXPLAYER X2 anything")==null,"pinned-fan:no-product-prefix-guess");

        foreach(var row in expected)

        {

            var d=Model(row.Model);check(d.ControlRegister==row.Control&&d.DutyRegister==row.Duty&&d.ControlOn==row.On&&d.Maximum==row.Max,"pinned-fan:independent-source-registers-"+row.Model);

            var t=new MemoryTransport(d);using var owner=Own(d,t);check(t.Reads==0&&t.Writes.Count==0,"pinned-fan:reservation-no-io-"+row.Model);

            Reject(()=>FanCapabilityCatalog.AcquireAsusCurve("HandheldCompanion.Devices.ROGAlly"),check,"pinned-fan:shared-asus-writer-busy-"+row.Model);

            owner.Open();check(owner.Phase==PinnedFanPhase.Ready&&t.Writes.Count==0,"pinned-fan:open-read-only-"+row.Model);

            var result=owner.ApplyPercent(50);byte half=row.Max==255?(byte)128:row.Max==184?(byte)92:(byte)50;

            check(result.RegisterValue==half&&!result.PhysicalEffectVerified&&result.DeviceId==d.DeviceId,"pinned-fan:source-half-rounding-"+row.Model);

            check(t.Writes.Count==2&&t.Writes[0]==(row.Control,row.On)&&t.Writes[1]==(row.Duty,half),"pinned-fan:mode-then-source-duty-"+row.Model);

            check(owner.ReadDutyRegister()==half,"pinned-fan:raw-duty-is-register-value-"+row.Model);

            check(owner.TryStop()&&owner.Phase==PinnedFanPhase.Closed&&owner.AutomaticReturnObserved&&!t.IsReady,"pinned-fan:stop-observed-return-and-handle-close-"+row.Model);

            int calls=t.Reads+t.Writes.Count;check(owner.TryStop()&&calls==t.Reads+t.Writes.Count,"pinned-fan:repeat-stop-no-io-"+row.Model);

            Reject(()=>owner.ApplyPercent(60),check,"pinned-fan:retired-callback-rejected-"+row.Model);

            var manual=new MemoryTransport(d);manual.Values[d.ControlRegister]=d.ControlOn;
            using(var claimed=Own(d,manual))
            {
                claimed.Open();check(manual.Writes.Count==0&&!claimed.ControlWasAttempted,"pinned-fan:manual-mode-is-not-owner-proof-"+row.Model);
                claimed.ApplyPercent(50);
                check(manual.Writes.Count==2&&manual.Writes[0]==(d.ControlRegister,d.ControlOn)&&manual.Writes[1]==(d.DutyRegister,half),"pinned-fan:explicit-manual-mode-reasserts-before-duty-"+row.Model);
                check(claimed.TryStop()&&claimed.AutomaticReturnObserved,"pinned-fan:manual-entry-owes-current-return-"+row.Model);
            }
            var cancelledManual=new MemoryTransport(d);cancelledManual.Values[d.ControlRegister]=d.ControlOn;
            using(var cancelled=Own(d,cancelledManual))
            {
                cancelled.Open();int operationChecks=0;
                Reject(()=>cancelled.ApplyPercent(50,()=>++operationChecks<3),check,"pinned-fan:manual-entry-final-guard-cancels-before-first-write-"+row.Model);
                check(operationChecks==3&&!cancelled.ControlWasAttempted&&cancelledManual.Writes.Count==0,"pinned-fan:cancelled-manual-entry-has-no-issued-debt-"+row.Model);
                check(cancelled.TryStop()&&cancelled.Phase==PinnedFanPhase.Closed&&!cancelledManual.IsReady&&cancelledManual.Writes.Count==0,"pinned-fan:cancelled-manual-entry-only-disposes-"+row.Model);
            }
            var untouchedManual=new MemoryTransport(d);untouchedManual.Values[d.ControlRegister]=d.ControlOn;
            using(var untouched=Own(d,untouchedManual))
            {
                untouched.Open();
                check(untouched.TryStop()&&untouched.Phase==PinnedFanPhase.Closed&&untouchedManual.Writes.Count==0
                    &&untouchedManual.Values[d.ControlRegister]==d.ControlOn&&!untouched.ControlWasAttempted
                    &&!untouched.AutomaticReturnObserved&&untouchedManual.Disposes==1,"pinned-fan:read-only-manual-session-does-not-return-foreign-mode-"+row.Model);
            }
            var changedMode=new MemoryTransport(d);
            using(var changed=Own(d,changedMode))
            {
                changed.Open();changedMode.Values[d.ControlRegister]=0xff;
                Reject(()=>changed.ApplyPercent(50),check,"pinned-fan:unknown-mode-between-open-and-first-write-refused-"+row.Model);
                check(!changed.ControlWasAttempted&&changedMode.Writes.Count==0&&changed.LastFailure!.Contains("0xFF")
                    &&changed.TryStop()&&changedMode.Writes.Count==0,"pinned-fan:unknown-prewrite-mode-does-not-create-return-debt-"+row.Model);
            }
            // Active ownership loss never guesses a bitmask or writes through an unknown mode.
            var lostMode=new MemoryTransport(d);
            using(var lost=Own(d,lostMode))
            {
                try
                {
                lost.Open();lost.ApplyPercent(50);int successfulWrites=lostMode.Writes.Count;
                lostMode.Values[d.ControlRegister]=0xff;
                Reject(()=>lost.ApplyPercent(75),check,"pinned-fan:active-mode-drift-refuses-following-duty-"+row.Model);
                check(lost.LastFailure!.Contains("PINNED_FAN_OWNERSHIP_LOST")&&lost.LastFailure.Contains("observed=0xFF")
                    &&lost.LastFailure.Contains($"register=0x{d.ControlRegister:X4}")&&lostMode.Writes.Count==successfulWrites,
                    "pinned-fan:active-mode-drift-records-byte-without-write-"+row.Model);
                check(!lost.TryStop()&&lost.Phase==PinnedFanPhase.Retiring&&lostMode.Disposes==0
                    &&lost.LastFailure!.Contains("PINNED_FAN_FOREIGN_RETURN_MODE")&&lost.LastFailure.Contains("observed=0xFF")
                    &&lostMode.Writes.Count==successfulWrites,"pinned-fan:unknown-active-mode-retains-exact-return-owner-"+row.Model);
                lostMode.Values[d.ControlRegister]=d.ControlOff;
                check(lost.TryStop()&&lost.AutomaticReturnObserved&&lost.Phase==PinnedFanPhase.Closed
                    &&lostMode.Writes.Count==successfulWrites,"pinned-fan:observed-auto-settles-without-extra-mode-write-"+row.Model);
                var recoveredMode=new MemoryTransport(d);
                using var recovered=Own(d,recoveredMode);
                recovered.Open();recovered.ApplyPercent(75);
                check(recoveredMode.Writes.Count==2&&recoveredMode.Writes[0]==(d.ControlRegister,d.ControlOn)
                    &&recoveredMode.Writes[1]==(d.DutyRegister,recovered.ScalePercent(75)),
                    "pinned-fan:retired-active-owner-can-open-and-control-again-"+row.Model);
                check(recovered.TryStop(),"pinned-fan:recovered-active-owner-closes-"+row.Model);
                }
                finally
                {
                    // Preserve a failed assertion; unknown fixture mode must not mask it in Dispose.
                    lostMode.Values[d.ControlRegister]=d.ControlOff;
                    if(lost.Phase!=PinnedFanPhase.Closed)lost.TryStop();
                }
            }
            var issuedManual=new MemoryTransport(d);issuedManual.Values[d.ControlRegister]=d.ControlOn;
            using(var issued=Own(d,issuedManual))
            {
                issued.Open();issuedManual.BeforeWrite=(register,value)=>{if(register==d.ControlRegister&&value==d.ControlOn)issuedManual.FailWrite=true;};
                Reject(()=>issued.ApplyPercent(50),check,"pinned-fan:issued-manual-control-write-can-fail-before-receipt-"+row.Model);
                check(issued.ControlWasAttempted&&issuedManual.Writes.Count==1
                    &&issuedManual.Writes[0]==(d.ControlRegister,d.ControlOn),"pinned-fan:issued-manual-control-failure-retains-debt-without-duty-"+row.Model);
                issuedManual.BeforeWrite=null;issuedManual.FailWrite=false;
                check(issued.TryStop()&&issued.AutomaticReturnObserved&&issuedManual.Writes.Count==2
                    &&issuedManual.Writes[1]==(d.ControlRegister,d.ControlOff)&&issuedManual.Disposes==1,"pinned-fan:issued-manual-control-failure-returns-once-"+row.Model);
                int terminalCalls=issuedManual.Reads+issuedManual.Writes.Count+issuedManual.Disposes;
                check(issued.TryStop()&&terminalCalls==issuedManual.Reads+issuedManual.Writes.Count+issuedManual.Disposes,"pinned-fan:issued-manual-control-repeat-stop-is-inert-"+row.Model);
            }
            var unknown=new MemoryTransport(d);unknown.Values[d.ControlRegister]=0xff;
            using(var refused=Own(d,unknown))
            {
                Reject(()=>refused.Open(),check,"pinned-fan:unknown-mode-refused-without-write-"+row.Model);
                check(unknown.Writes.Count==0&&!refused.ControlWasAttempted&&refused.LastFailure!.Contains("0xFF")&&refused.TryStop(),"pinned-fan:unknown-mode-value-and-no-return-write-"+row.Model);
            }


        }

        var source=Model("OneXPlayer3");

        {

            var t=new MemoryTransport(source);using var owner=Own(source,t);owner.Open();int before=t.Reads+t.Writes.Count;

            foreach(var bad in new[]{double.NaN,double.PositiveInfinity,-1d,101d})Reject(()=>owner.ApplyPercent(bad),check,"pinned-fan:invalid-percent-before-io-"+bad);

            check(before==t.Reads+t.Writes.Count,"pinned-fan:invalid-percent-no-io");

        }

        {

            bool permit=true;var t=new MemoryTransport(source);using var owner=Own(source,t,()=>permit);owner.Open();owner.ApplyPercent(40);permit=false;

            int count=t.Writes.Count;Reject(()=>owner.ApplyPercent(60),check,"pinned-fan:context-revocation-rejects-new-write");

            check(count==t.Writes.Count&&owner.TryStop()&&owner.AutomaticReturnObserved,"pinned-fan:revocation-still-permits-owned-retirement");

        }

        {

            bool operation=true;var t=new MemoryTransport(source);using var owner=Own(source,t);owner.Open();

            t.BeforeWrite=(register,value)=>{if(register==source.ControlRegister)operation=false;};

            Reject(()=>owner.ApplyPercent(60,()=>operation),check,"pinned-fan:current-operation-invalidates-after-issued-control");

            check(t.Writes.Count==1&&owner.Phase==PinnedFanPhase.Retiring,"pinned-fan:old-operation-cannot-issue-following-duty");

            t.BeforeWrite=null;check(owner.TryStop(),"pinned-fan:superseded-operation-retains-owned-return-path");

        }

        {

            var t=new MemoryTransport(source){IgnoreAutomatic=true};var owner=Own(source,t);owner.Open();owner.ApplyPercent(50);

            try

            {

                check(!owner.TryStop()&&owner.Phase==PinnedFanPhase.Retiring&&t.Disposes==0,"pinned-fan:failed-return-retains-exact-transport");

                Reject(()=>Own(source,new MemoryTransport(source)),check,"pinned-fan:failed-return-denies-successor");

                Reject(()=>owner.ApplyPercent(50),check,"pinned-fan:failed-return-no-revival");

            }

            finally{t.IgnoreAutomatic=false;if(!owner.TryStop())throw new InvalidOperationException("fixture source owner cleanup failed");}

        }

        {

            var t=new MemoryTransport(source){FailDispose=true};var owner=Own(source,t);owner.Open();owner.ApplyPercent(40);

            try

            {

                check(!owner.TryStop()&&owner.AutomaticReturnObserved&&t.IsReady,"pinned-fan:close-exception-is-unreturned-handle");

                Reject(()=>Own(source,new MemoryTransport(source)),check,"pinned-fan:unclosed-handle-denies-successor");

            }

            finally{t.FailDispose=false;if(!owner.TryStop())throw new InvalidOperationException("fixture close cleanup failed");}

        }

        {

            var t=new MemoryTransport(source){FailWrite=true};var owner=Own(source,t);owner.Open();

            Reject(()=>owner.ApplyPercent(50),check,"pinned-fan:issued-control-failure-not-success");

            check(owner.ControlWasAttempted&&owner.Phase==PinnedFanPhase.Retiring,"pinned-fan:issued-failure-keeps-return-debt");

            t.FailWrite=false;check(owner.TryStop(),"pinned-fan:actual-auto-state-settles-issued-failure");

        }

        {

            using var entered=new ManualResetEventSlim();using var finish=new ManualResetEventSlim();

            var t=new MemoryTransport(source);var owner=Own(source,t);owner.Open();

            t.BeforeWrite=(register,value)=>{if(register==source.ControlRegister&&value==source.ControlOn){entered.Set();if(!finish.Wait(5000))throw new IOException("fixture pending barrier timeout");}};

            var apply=Task.Run(()=>{try{owner.ApplyPercent(70);return false;}catch(InvalidOperationException){return true;}});

            try

            {

                check(entered.Wait(1500)&&owner.PendingIo,"pinned-fan:actual-issued-write-barrier");

                var stop=Task.Run(owner.TryStop);check(stop.Wait(1000)&&!stop.Result,"pinned-fan:stop-announcement-not-blocked-by-io-lock");

                Reject(()=>Own(source,new MemoryTransport(source)),check,"pinned-fan:pending-io-denies-new-owner");

            }

            finally{finish.Set();if(!apply.Wait(1500))throw new IOException("fixture source apply did not return");}

            check(apply.Result&&t.Writes.Count==1,"pinned-fan:late-issued-return-has-no-following-duty-write");

            t.BeforeWrite=null;check(owner.TryStop()&&t.Writes.Count==2&&t.Writes[1]==(source.ControlRegister,(byte)0),"pinned-fan:pending-write-observed-then-single-auto-return");

            var successorTransport=new MemoryTransport(source);using var successor=Own(source,successorTransport);successor.Open();

            check(successor.Generation>owner.Generation,"pinned-fan:new-generation-after-actual-retirement");int old=t.Writes.Count;

            check(owner.TryStop()&&old==t.Writes.Count,"pinned-fan:old-stop-cannot-touch-successor");

        }

        {

            var t=new MemoryTransport(source);using var owner=Own(source,t);owner.Open();

            t.Values[0x0470]=0;check(owner.ReadTemperature()==null,"pinned-fan:zero-temperature-invalid");

            t.Values[0x0470]=111;check(owner.ReadTemperature()==null,"pinned-fan:over110-temperature-invalid");

            t.Values[0x0470]=110;check(owner.ReadTemperature()==110,"pinned-fan:source-temperature-upper-bound");

        }

    }

}