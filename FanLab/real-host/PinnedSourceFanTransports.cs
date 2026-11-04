using System;

using System.Collections;

using System.Collections.Generic;

using System.Globalization;

using System.IO;

using System.Reflection;

using System.Runtime.ExceptionServices;

using System.Threading;

using YeManFanBackend.Capabilities;

namespace YeManFanHost;



// Construction is dormant. The owner captures this terminal before Initialize

// can open an external resource. No HC device model or product Main is used.

internal static class PinnedSourceFanTransports

{

    internal static IPinnedFanTransport Open(Assembly approvedHc,PinnedFanDescriptor descriptor)

    {

        ArgumentNullException.ThrowIfNull(approvedHc);

        if(!ReferenceEquals(FanCapabilityCatalog.FindPinnedFanSource(descriptor.ModelClass),descriptor))

            throw new IOException("Pinned fan descriptor is not registered");

        return descriptor.Transport switch

        {

            PinnedFanTransportKind.WmiBankedEc => new PinnedWmiFanTransport(new ReflectionFanWmiMethod()),

            PinnedFanTransportKind.SuperIo16 => new PinnedSuperIoFanTransport(new ReflectionFanSuperIo(approvedHc),descriptor,new FanEcMutex()),

            PinnedFanTransportKind.WmiUmaEc16 => new PinnedUmaEcFanTransport(descriptor),

            _ => throw new NotSupportedException("Pinned fan transport is not connected")

        };

    }

    internal static object? Invoke(MethodInfo method,object target,params object?[] arguments)

    {try{return method.Invoke(target,arguments);}catch(TargetInvocationException e)when(e.InnerException is not null){ExceptionDispatchInfo.Capture(e.InnerException).Throw();throw;}}

}

internal interface IPinnedFanWmiMethod : IDisposable

{

    string EndpointIdentity {get;}

    bool IsReady {get;}

    void Initialize() { }

    string? Invoke(string method,object[] arguments);

}

internal sealed class PinnedWmiFanTransport : IPinnedFanTransport

{

    private readonly IPinnedFanWmiMethod method;

    internal PinnedWmiFanTransport(IPinnedFanWmiMethod method)=>this.method=method;

    public string EndpointIdentity=>method.EndpointIdentity;

    public bool IsReady=>method.IsReady;

    public void Initialize()=>method.Initialize();

    internal static uint PackAddress(ushort register)=>(uint)(register>>8)|((uint)(byte)register<<8);

    private static byte Hex(string field)

    {var value=field.StartsWith("0x",StringComparison.OrdinalIgnoreCase)?field[2..]:field;if(byte.TryParse(value,NumberStyles.HexNumber,CultureInfo.InvariantCulture,out var parsed))return parsed;throw new IOException("Invalid pinned WMI EC byte: "+field);}

    private void Check(){if(!IsReady)throw new IOException("Owned WMI terminal unavailable");}

    public byte ReadByte(ushort register)

    {

        Check();var args=new object[]{PackAddress(register),null!};

        var response=method.Invoke("ReadECReg",args)??throw new IOException("ReadECReg returned no response");

        var fields=response.Split(',',StringSplitOptions.TrimEntries);

        if(fields.Length<2||Hex(fields[0])!=0)throw new IOException("ReadECReg failed: "+response);

        return Hex(fields[1]);

    }

    public void WriteByte(ushort register,byte value)

    {

        Check();var args=new object[]{PackAddress(register)|((uint)value<<16),null!};

        var response=method.Invoke("WriteECReg",args)??throw new IOException("WriteECReg returned no response");

        var fields=response.Split(',',StringSplitOptions.TrimEntries);

        if(fields.Length==0||Hex(fields[0])!=0)throw new IOException("WriteECReg failed: "+response);

    }

    public void Dispose()=>method.Dispose();

}

internal sealed class ReflectionFanWmiMethod : IPinnedFanWmiMethod

{

    private object? instance,searcher,collection;

    private MethodInfo? invoke;

    private bool initialized,closed;

    private readonly List<object> extras=new();

    private readonly string identity="owned-wmi:"+Guid.NewGuid().ToString("N");

    internal string? DevicePath {get;private set;}

    public string EndpointIdentity=>identity;

    public bool IsReady=>initialized&&!closed&&instance is not null;

    public void Initialize()

    {

        if(closed||searcher is not null||instance is not null)throw new IOException("WMI initialization cannot be repeated");

        var system=Assembly.Load("System.Management");

        var searcherType=system.GetType("System.Management.ManagementObjectSearcher",true)!;

        searcher=Activator.CreateInstance(searcherType,new object[]{"root\\WMI","SELECT * FROM SuRwECRegInterface"})??throw new IOException("WMI searcher missing");

        var options=searcherType.GetProperty("Options")?.GetValue(searcher);

        options?.GetType().GetProperty("Timeout")?.SetValue(options,TimeSpan.FromSeconds(3));

        collection=PinnedSourceFanTransports.Invoke(searcherType.GetMethod("Get",Type.EmptyTypes)!,searcher)??throw new IOException("WMI result missing");

        foreach(var item in (IEnumerable)collection)

        {

            if(instance is not null){extras.Add(item);throw new IOException("Ambiguous SuRwECRegInterface instances");}

            instance=item;

        }

        if(instance is null)throw new IOException("SuRwECRegInterface unavailable");

        var type=instance.GetType();

        invoke=type.GetMethod("InvokeMethod",new[]{typeof(string),typeof(object[])})??throw new IOException("WMI invocation contract changed");

        var path=type.GetProperty("Path")?.GetValue(instance);

        DevicePath=path?.GetType().GetProperty("Path")?.GetValue(path) as string;

        if(string.IsNullOrWhiteSpace(DevicePath))throw new IOException("WMI endpoint path missing");

        DisposeOne(ref collection);DisposeOne(ref searcher);initialized=true;

    }

    private static void DisposeOne(ref object? value)

    {

        if(value is null)return;

        if(value is not IDisposable disposable)throw new IOException("WMI disposal contract changed");

        disposable.Dispose();value=null;

    }

    public string? Invoke(string method,object[] arguments)

    {

        if(!IsReady||invoke is null)throw new IOException("WMI terminal not ready");

        PinnedSourceFanTransports.Invoke(invoke,instance!,method,arguments);

        return arguments.Length>1?arguments[1] as string:null;

    }

    public void Dispose()

    {

        initialized=false;

        while(extras.Count!=0){object? item=extras[^1];DisposeOne(ref item);extras.RemoveAt(extras.Count-1);}

        DisposeOne(ref instance);DisposeOne(ref collection);DisposeOne(ref searcher);closed=true;

    }

}

internal interface IPinnedSuperIo : IDisposable

{

    string EndpointIdentity {get;}

    bool IsReady {get;}

    void Initialize() { }
    void PrepareRetirement() { }

    void Enter(ushort status,ushort data);

    void Exit(ushort status,ushort data);

    void Write(ushort port,byte value);

    byte Read(ushort port);

}

internal interface IPinnedEcBusLease : IDisposable { IDisposable Enter(); }

internal sealed class FanEcMutex : IPinnedEcBusLease

{

    private Mutex? mutex;

    private bool closed;

    public IDisposable Enter()

    {

        if(closed)throw new ObjectDisposedException(nameof(FanEcMutex));

        var current=mutex??=new Mutex(false,@"Global\Access_EC");

        bool held=false;

        try

        {

            try{held=current.WaitOne(250);}catch(AbandonedMutexException){held=true;throw new IOException("EC bus owner abandoned; no automatic write recovery");}

            if(!held)throw new IOException("EC bus is busy");

            return new Release(current);

        }

        catch{if(held)current.ReleaseMutex();throw;}

    }

    private sealed class Release(Mutex target):IDisposable

    {private bool released;public void Dispose(){if(!released){target.ReleaseMutex();released=true;}}}

    public void Dispose(){mutex?.Dispose();mutex=null;closed=true;}

}

internal sealed class PinnedSuperIoFanTransport : IPinnedFanTransport

{

    private readonly IPinnedSuperIo io;

    private readonly PinnedFanDescriptor descriptor;

    private readonly IPinnedEcBusLease bus;

    private bool closed;

    internal PinnedSuperIoFanTransport(IPinnedSuperIo io,PinnedFanDescriptor descriptor,IPinnedEcBusLease bus)

    {this.io=io;this.descriptor=descriptor;this.bus=bus;}

    public string EndpointIdentity=>io.EndpointIdentity;

    public bool IsReady=>!closed&&io.IsReady;

    public void Initialize()=>io.Initialize();
    public void PrepareRetirement(){if(closed)return;using var lease=bus.Enter();io.PrepareRetirement();}

    private void Check(){if(!IsReady)throw new IOException("Owned SuperIO transport unavailable");}

    private void Select(ushort register)

    {

        io.Write(descriptor.StatusPort,0x2e);io.Write(descriptor.DataPort,0x11);

        io.Write(descriptor.StatusPort,0x2f);io.Write(descriptor.DataPort,(byte)(register>>8));

        io.Write(descriptor.StatusPort,0x2e);io.Write(descriptor.DataPort,0x10);

        io.Write(descriptor.StatusPort,0x2f);io.Write(descriptor.DataPort,(byte)register);

        io.Write(descriptor.StatusPort,0x2e);io.Write(descriptor.DataPort,0x12);

        io.Write(descriptor.StatusPort,0x2f);

    }

    private T Access<T>(ushort register,Func<T> access)

    {

        Check();using var lease=bus.Enter();Exception? failure=null;

        try{io.Enter(descriptor.StatusPort,descriptor.DataPort);Select(register);return access();}

        catch(Exception error){failure=error;throw;}

        finally

        {

            try{io.Exit(descriptor.StatusPort,descriptor.DataPort);}

            catch(Exception cleanup){if(failure is not null)throw new AggregateException(failure,cleanup);throw;}

        }

    }

    public byte ReadByte(ushort register)=>Access(register,()=>io.Read(descriptor.DataPort));

    public void WriteByte(ushort register,byte value)=>Access(register,()=>{io.Write(descriptor.DataPort,value);return true;});

    public void Dispose(){if(closed)return;using(var lease=bus.Enter())io.Dispose();bus.Dispose();closed=true;}

}

// Source-owned16-bit ports use only the already-approved LHM port terminals.
// Frozen OpenLib.InitializeOls also constructs an ACPI EC and retains its mutex;
// that unrelated sensor/ACPI owner must not block the changed-temperature thread.
internal sealed class ReflectionFanSuperIo : IPinnedSuperIo
{
    private readonly Assembly? approvedHc;
    private Type? rawType;
    private ConstructorInfo? rawConstructor,configConstructor;
    private MethodInfo? selectSlot,write,read,enterConfig,exitConfig;
    private ResourceBinding? rawBinding,configBinding;
    private object? raw,configuration;
    private System.Runtime.InteropServices.SafeHandle? rawHandle;
    private bool initializeAttempted,closed,rawConstructionUnobserved,configConstructionUnobserved,configExitReturned;
    private ushort statusPort,dataPort;
    private readonly string identity="owned-source-superio:"+Guid.NewGuid().ToString("N");
    internal ReflectionFanSuperIo(Assembly approvedHc)=>this.approvedHc=approvedHc;
    internal ReflectionFanSuperIo(Type rawType,Type configType)=>Bind(rawType,configType);
    private static MethodInfo Require(Type type,string name,Type result,params Type[] arguments)
    {
        var method=type.GetMethod(name,BindingFlags.Instance|BindingFlags.Public,null,arguments,null);
        return method?.ReturnType==result&&method.DeclaringType==type?method:throw new IOException("Pinned port contract changed: "+name);
    }
    private void Bind(Type rawType,Type configType)
    {
        this.rawType=rawType;
        rawConstructor=rawType.GetConstructor(Type.EmptyTypes)??throw new IOException("Raw port constructor missing");
        configConstructor=configType.GetConstructor(new[]{typeof(ushort),typeof(ushort)})??throw new IOException("Config port constructor missing");
        selectSlot=Require(rawType,"SelectSlot",typeof(void),typeof(int));
        write=Require(rawType,"WritePort",typeof(void),typeof(ushort),typeof(byte));read=Require(rawType,"ReadPort",typeof(byte),typeof(ushort));
        enterConfig=Require(configType,"IT87Enter",typeof(void));exitConfig=Require(configType,"IT87Exit",typeof(void));
        rawBinding=new ResourceBinding(rawType,"_pawnIO","_handle");
        configBinding=new ResourceBinding(configType,"_pawnModule","_pawnIO","_handle");
    }
    private static object Construct(ConstructorInfo constructor,params object[] arguments)
    {
        try{return constructor.Invoke(arguments);}
        catch(TargetInvocationException error)when(error.InnerException is not null)
        {ExceptionDispatchInfo.Capture(error.InnerException).Throw();throw;}
    }
    private sealed class ResourceBinding
    {
        private readonly FieldInfo[] chain;
        private readonly MethodInfo close;
        internal ResourceBinding(Type type,params string[] path)
        {
            close=Require(type,"Close",typeof(void));var fields=new List<FieldInfo>();
            foreach(var name in path)
            {
                var field=type.GetField(name,BindingFlags.Instance|BindingFlags.NonPublic|BindingFlags.DeclaredOnly)
                    ??throw new IOException("Pinned owned handle field missing: "+name);
                fields.Add(field);type=field.FieldType;
            }
            if(!typeof(System.Runtime.InteropServices.SafeHandle).IsAssignableFrom(type))
                throw new IOException("Pinned resource no longer exposes its owned SafeHandle");
            chain=fields.ToArray();
        }
        internal System.Runtime.InteropServices.SafeHandle Handle(object resource)
        {
            object value=resource;
            foreach(var field in chain)value=field.GetValue(value)??throw new IOException("Pinned owned handle chain lost");
            return (System.Runtime.InteropServices.SafeHandle)value;
        }
        internal void CloseExact(object resource)
        {
            var handle=Handle(resource);
            if(!handle.IsClosed)
            {
                PinnedSourceFanTransports.Invoke(close,resource);
                // An invalid SafeHandle owns no OS resource, but the wrapper's
                // IsLoaded guard may skip closing its managed lifetime marker.
                if(!handle.IsClosed&&handle.IsInvalid)handle.Close();
            }
            if(!ReferenceEquals(handle,Handle(resource))||!handle.IsClosed)
                throw new IOException("Pinned terminal close not observed");
        }
    }
    public void Initialize()
    {
        if(closed||initializeAttempted)throw new IOException("Port initialization cannot be repeated");
        initializeAttempted=true;
        if(rawType is null)
        {
            AssemblyName? reference=null;
            foreach(var candidate in approvedHc!.GetReferencedAssemblies())
                if(candidate.Name=="LibreHardwareMonitorLib")
                {if(reference is not null)throw new IOException("Ambiguous port assembly reference");reference=candidate;}
            if(reference is null)throw new IOException("Approved HC port assembly reference missing");
            var library=Assembly.Load(reference);
            if(library.GetName().FullName!=reference.FullName)throw new IOException("Resolved port assembly identity changed");
            Bind(library.GetType("LibreHardwareMonitor.PawnIo.LpcIo",true)!,
                library.GetType("LibreHardwareMonitor.Hardware.Motherboard.Lpc.LpcPort",true)!);
        }
        rawConstructionUnobserved=true;
        raw=Construct(rawConstructor!);rawConstructionUnobserved=false;
        rawHandle=rawBinding!.Handle(raw);
        if(!IsReady)throw new IOException("Owned raw port handle unavailable");
    }
    public string EndpointIdentity=>identity;
    public bool IsReady=>!closed&&raw is not null&&rawHandle is not null&&
        ReferenceEquals(rawBinding!.Handle(raw),rawHandle)&&!rawHandle.IsClosed&&!rawHandle.IsInvalid;
    private void Check(){if(!IsReady)throw new IOException("Owned raw port handle unavailable");}
    public void Enter(ushort status,ushort data)
    {
        Check();
        if(status!=0x4e||data!=0x4f)throw new IOException("Source catalog SuperIO ports changed");
        if(configuration is not null||configConstructionUnobserved)throw new IOException("Configuration retirement pending");
        statusPort=status;dataPort=data;configExitReturned=false;
        configConstructionUnobserved=true;
        configuration=Construct(configConstructor!,status,data);configConstructionUnobserved=false;
        var handle=configBinding!.Handle(configuration);
        if(handle.IsClosed||handle.IsInvalid||ReferenceEquals(handle,rawHandle))throw new IOException("Config port handle unavailable or aliased");
        PinnedSourceFanTransports.Invoke(enterConfig!,configuration);
    }
    public void Exit(ushort status,ushort data)
    {
        if(configuration is null)return;
        if(status!=statusPort||data!=dataPort)throw new IOException("Config return ports changed");
        var captured=configuration;
        if(!configExitReturned){PinnedSourceFanTransports.Invoke(exitConfig!,captured);configExitReturned=true;}
        configBinding!.CloseExact(captured);
        if(!ReferenceEquals(captured,configuration))throw new IOException("Config owner changed during close");
        configuration=null;
    }
    private void Select(ushort port)
    {
        Check();if(configuration is null||configExitReturned)throw new IOException("No owned config transaction");
        if(port!=statusPort&&port!=dataPort)throw new IOException("Port is outside current source transaction");
        // Exact frozen OpenLib mapping. This source catalog only admits4E/4F.
        PinnedSourceFanTransports.Invoke(selectSlot!,raw!,port==0x2e?0:1);
    }
    public void Write(ushort port,byte value){Select(port);PinnedSourceFanTransports.Invoke(write!,raw!,port,value);}
    public byte Read(ushort port){Select(port);return (byte)(PinnedSourceFanTransports.Invoke(read!,raw!,port)??throw new IOException("Port read returned no byte"));}
    public void PrepareRetirement()
    {
        if(closed)return;
        if(rawConstructionUnobserved||configConstructionUnobserved)
            throw new IOException("Constructor failed without observable terminal cleanup; retain owner uncertainty");
        Exit(statusPort,dataPort);
    }
    public void Dispose()
    {
        if(closed)return;
        if(rawConstructionUnobserved||configConstructionUnobserved)
            throw new IOException("Constructor failed without observable terminal cleanup; retain owner uncertainty");
        PrepareRetirement();
        if(raw is not null){rawBinding!.CloseExact(raw);raw=null;rawHandle=null;}
        closed=true;
    }
}
