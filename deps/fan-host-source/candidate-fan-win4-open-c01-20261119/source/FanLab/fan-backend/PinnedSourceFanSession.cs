// Fixed-source fan owner, compiled into the existing backend DLL. No threads or
// default device factory. A caller supplies its current authorization predicate
// and an exact transport; declaration lookup never opens hardware.
using System;
using System.IO;
namespace YeManFanBackend.Capabilities;

public interface IPinnedFanTransport : IDisposable
{
    string EndpointIdentity { get; }
    bool IsReady { get; }
    // The session captures this exact resource before any external open can fail.
    void Initialize() { }
    // Finish a retained transaction without dropping the terminal needed for OEM return.
    void PrepareRetirement() { }
    // A failed read must throw; zero is valid data and cannot encode failure.
    byte ReadByte(ushort register);
    // A failed/short/unknown write must throw. Return is transport evidence only.
    void WriteByte(ushort register, byte value);
}
public enum PinnedFanPhase { Reserved, Opening, Ready, Active, Retiring, Closed }
public sealed record PinnedFanWriteReceipt(string DeviceId, long Generation, byte RegisterValue,string SourceCommit)
{ public bool PhysicalEffectVerified => false; }

public sealed class PinnedSourceFanSession : IDisposable
{
    private readonly object stateGate = new();
    private readonly Func<bool> ordinaryPermit;
    private readonly Func<IPinnedFanTransport> openTransport;
    private PinnedFanPhase phase = PinnedFanPhase.Reserved;
    private IPinnedFanTransport? transport;
    private string? endpoint;
    private int inFlight;
    private bool controlAttempted, automaticObserved;
    private string? lastFailure;
    internal PinnedSourceFanSession(PinnedFanDescriptor descriptor,long generation,string contextToken,
        Func<bool> permit,Func<IPinnedFanTransport> factory)
    { Descriptor=descriptor;Generation=generation;ContextToken=contextToken;ordinaryPermit=permit;openTransport=factory; }
    public PinnedFanDescriptor Descriptor {get;}
    public long Generation {get;}
    public string ContextToken {get;}
    public PinnedFanPhase Phase {get{lock(stateGate)return phase;}}
    public bool PendingIo {get{lock(stateGate)return inFlight!=0;}}
    public bool ControlWasAttempted {get{lock(stateGate)return controlAttempted;}}
    public bool AutomaticReturnObserved {get{lock(stateGate)return automaticObserved;}}
    public string? LastFailure {get{lock(stateGate)return lastFailure;}}
    public string? Endpoint {get{lock(stateGate)return endpoint;}}

    public byte ScalePercent(double percent)
    {
        if(!double.IsFinite(percent)||percent<0||percent>100)
            throw new ArgumentOutOfRangeException(nameof(percent),"Fan percentage must be finite and within0..100");
        return checked((byte)Math.Round(percent*(Descriptor.Maximum-Descriptor.Minimum)/100d+Descriptor.Minimum,
            MidpointRounding.ToEven));
    }
    private void Begin(bool opening)
    {
        if(!ordinaryPermit()) { Revoke();throw new InvalidOperationException("PINNED_FAN_CONTEXT_REVOKED"); }
        lock(stateGate)
        {
            if(!FanCapabilityCatalog.OwnsPinnedSource(this))throw new InvalidOperationException("PINNED_FAN_OWNER_STALE");
            if(inFlight!=0)throw new InvalidOperationException("PINNED_FAN_IO_PENDING");
            if(opening?phase!=PinnedFanPhase.Reserved:phase is not (PinnedFanPhase.Ready or PinnedFanPhase.Active))
                throw new InvalidOperationException("PINNED_FAN_PHASE_REJECTED");
            inFlight=1;if(opening)phase=PinnedFanPhase.Opening;
        }
    }
    private void End(bool failed)
    {
        lock(stateGate){inFlight=0;if(failed&&phase!=PinnedFanPhase.Closed)phase=PinnedFanPhase.Retiring;}
    }
    private void Guard()
    {
        if(!ordinaryPermit()) { Revoke();throw new InvalidOperationException("PINNED_FAN_CONTEXT_REVOKED"); }
        lock(stateGate)
        {
            if(phase is not (PinnedFanPhase.Opening or PinnedFanPhase.Ready or PinnedFanPhase.Active)||inFlight!=1||
                !FanCapabilityCatalog.OwnsPinnedSource(this))throw new InvalidOperationException("PINNED_FAN_OWNER_REVOKED");
        }
    }
    private void GuardTarget(IPinnedFanTransport target)
    {
        if(!FanCapabilityCatalog.OwnsPinnedSource(this)||!ReferenceEquals(target,transport)||
            !string.Equals(target.EndpointIdentity,endpoint,StringComparison.Ordinal))
            throw new InvalidOperationException("PINNED_FAN_ENDPOINT_CHANGED");
        if(!target.IsReady)throw new IOException("PINNED_FAN_TRANSPORT_NOT_READY");
    }
    private IPinnedFanTransport Target() => transport??throw new InvalidOperationException("PINNED_FAN_TRANSPORT_MISSING");
    private void Failure(Exception error){lock(stateGate)lastFailure=error.GetType().Name+": "+error.Message;}

    public void Open()
    {
        Begin(true);bool failed=true;
        try
        {
            var opened=openTransport()??throw new IOException("PINNED_FAN_TRANSPORT_MISSING");
            // Capture the returned exact transport even if stop arrived while
            // the factory was pending. Its cleanup debt must never disappear.
            lock(stateGate){transport=opened;endpoint=opened.EndpointIdentity;}
            if(string.IsNullOrEmpty(endpoint))throw new IOException("PINNED_FAN_ENDPOINT_MISSING");
            Guard();opened.Initialize();Guard();GuardTarget(opened);
            // A manual bit is firmware state, not an exclusive owner receipt.
            // Known automatic/manual modes are read-only admission; catalog ownership and the
            // first issued control write/readback still establish this session.
            var mode=opened.ReadByte(Descriptor.ControlRegister);Guard();
            if(mode!=Descriptor.ControlOff&&mode!=Descriptor.ControlOn)
                throw new InvalidOperationException($"PINNED_FAN_INVALID_CONTROL_MODE: 0x{mode:X2}");
            lock(stateGate){if(phase!=PinnedFanPhase.Opening)throw new InvalidOperationException("PINNED_FAN_OWNER_REVOKED");phase=PinnedFanPhase.Ready;}
            failed=false;
        }
        catch(Exception error){Failure(error);throw;}
        finally{End(failed);}
    }
    public PinnedFanWriteReceipt ApplyPercent(double percent,Func<bool>? currentOperation=null)
    {
        var value=ScalePercent(percent);Begin(false);bool failed=true;
        void StepGuard()
        {
            Guard();
            if(currentOperation is not null&&!currentOperation())
            {Revoke();throw new InvalidOperationException("PINNED_FAN_OPERATION_SUPERSEDED");}
        }
        try
        {
            var target=Target();StepGuard();GuardTarget(target);
            var mode=target.ReadByte(Descriptor.ControlRegister);StepGuard();
            bool first;lock(stateGate)first=!controlAttempted;
            if(first)
            {
                if(mode!=Descriptor.ControlOff&&mode!=Descriptor.ControlOn)
                    throw new InvalidOperationException($"PINNED_FAN_INVALID_CONTROL_MODE: 0x{mode:X2}");
                StepGuard();GuardTarget(target);
                // Only the imminent call incurs return debt. A prior guard refusal
                // must not clear a manual mode this session never wrote.
                lock(stateGate){controlAttempted=true;automaticObserved=false;}
                target.WriteByte(Descriptor.ControlRegister,Descriptor.ControlOn);StepGuard();
                if(target.ReadByte(Descriptor.ControlRegister)!=Descriptor.ControlOn)
                    throw new IOException("PINNED_FAN_CONTROL_NOT_OBSERVED");
                StepGuard();
            }
            else if(mode!=Descriptor.ControlOn)throw new InvalidOperationException($"PINNED_FAN_OWNERSHIP_LOST: register=0x{Descriptor.ControlRegister:X4}, observed=0x{mode:X2}, expected=0x{Descriptor.ControlOn:X2}");
            StepGuard();GuardTarget(target);target.WriteByte(Descriptor.DutyRegister,value);StepGuard();
            lock(stateGate){if(phase is not (PinnedFanPhase.Ready or PinnedFanPhase.Active))throw new InvalidOperationException("PINNED_FAN_OWNER_REVOKED");phase=PinnedFanPhase.Active;}
            failed=false;
            return new PinnedFanWriteReceipt(Descriptor.DeviceId,Generation,value,Descriptor.SourceCommit);
        }
        catch(Exception error){Failure(error);throw;}
        finally{End(failed);}
    }
    public byte? ReadDutyRegister()
    {
        Begin(false);bool failed=true;
        try{var target=Target();Guard();GuardTarget(target);var value=target.ReadByte(Descriptor.DutyRegister);Guard();failed=false;return value;}
        catch(Exception error){Failure(error);throw;}
        finally{End(failed);}
    }
    public float? ReadTemperature()
    {
        if(Descriptor.TemperatureRegister is not ushort register)return null;
        Begin(false);bool failed=true;
        try{var target=Target();Guard();GuardTarget(target);var value=target.ReadByte(register);Guard();failed=false;return value is >0 and <=110?value:null;}
        catch(Exception error){Failure(error);throw;}
        finally{End(failed);}
    }
    // Non-blocking announcement. It never waits on a driver/COM call and
    // prevents every subsequent ordinary step. The issued call retains debt.
    public void Revoke(){lock(stateGate){if(phase!=PinnedFanPhase.Closed)phase=PinnedFanPhase.Retiring;}}
    public bool TryStop()
    {
        lock(stateGate)
        {
            if(phase==PinnedFanPhase.Closed)return true;
            phase=PinnedFanPhase.Retiring;
            if(inFlight!=0)return false;
            if(!FanCapabilityCatalog.OwnsPinnedSource(this))return false;
            inFlight=1;
        }
        try
        {
            var target=transport;
            if(target is not null)
            {
                target.PrepareRetirement();
                if(controlAttempted&&!automaticObserved)
                {
                    GuardTarget(target);var mode=target.ReadByte(Descriptor.ControlRegister);
                    if(mode!=Descriptor.ControlOff)
                    {
                        if(mode!=Descriptor.ControlOn)throw new InvalidOperationException($"PINNED_FAN_FOREIGN_RETURN_MODE: register=0x{Descriptor.ControlRegister:X4}, observed=0x{mode:X2}, manual=0x{Descriptor.ControlOn:X2}, automatic=0x{Descriptor.ControlOff:X2}");
                        GuardTarget(target);target.WriteByte(Descriptor.ControlRegister,Descriptor.ControlOff);
                        GuardTarget(target);mode=target.ReadByte(Descriptor.ControlRegister);
                    }
                    if(mode!=Descriptor.ControlOff)throw new IOException("PINNED_FAN_OEM_RETURN_NOT_OBSERVED");
                    lock(stateGate)automaticObserved=true;
                }
                // An exact retained transport remains owned if Dispose throws
                // or still reports an open handle. No new lease may pass it.
                if(!ReferenceEquals(target,transport)||target.EndpointIdentity!=endpoint)
                    throw new InvalidOperationException("PINNED_FAN_ENDPOINT_CHANGED");
                target.Dispose();
                if(target.IsReady)throw new IOException("PINNED_FAN_TRANSPORT_CLOSE_NOT_OBSERVED");
            }
            lock(stateGate)transport=null;
            FanCapabilityCatalog.ReleasePinnedSource(this);
            lock(stateGate)phase=PinnedFanPhase.Closed;
            return true;
        }
        catch(Exception error){Failure(error);return false;}
        finally{lock(stateGate)inFlight=0;}
    }
    public void Dispose()
    {if(!TryStop())throw new InvalidOperationException("PINNED_FAN_RETURN_PENDING: "+LastFailure);}
}