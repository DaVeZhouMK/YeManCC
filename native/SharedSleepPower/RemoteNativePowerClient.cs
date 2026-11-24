using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Threading;
using Microsoft.Win32.SafeHandles;

namespace YeManCC.SharedSleepPower;

public readonly record struct RemotePowerOfferV1(string PipeName, RemotePowerDescriptorV1 Binding);
// ASCII header: "1 <pipe-stem> <256 hex digits>". All uint64 fields remain
// exact original 128-byte ABI values; decoding is never an admission receipt.
public static class RemotePowerOfferHeaderV1
{
    public const string HeaderName="X-YMCC-Shared-Power";
    public static bool TryParse(string? text,out RemotePowerOfferV1 offer)
    {
        offer=default;
        if(text is null||text.Length is <319 or >419||!text.StartsWith("1 ",StringComparison.Ordinal))return false;
        int end=text.IndexOf(' ',2);
        if(end<0||text.Length-end-1!=256)return false;
        string stem=text[2..end];if(!RemoteNativePowerScope.ValidStem(stem))return false;
        try
        {
            var bytes=Convert.FromHexString(text[(end+1)..]);
            if(bytes.Length!=128||Marshal.SizeOf<RemotePowerDescriptorV1>()!=128)return false;
            var binding=MemoryMarshal.Read<RemotePowerDescriptorV1>(bytes);
            if(!RemoteNativePowerScope.ValidDescriptor(binding))return false;
            offer=new(stem,binding);return true;
        }
        catch(FormatException){return false;}
    }
}

public enum RemoteSettlementDelivery { Unknown=0, Acknowledged=1, Rejected=2 }

internal enum RemoteWireCommand : uint { Bind=1, Read=2, Current=3, Cancel=4, Settle=5, Acknowledge=6 }
[StructLayout(LayoutKind.Sequential,Pack=8)]
internal struct RemoteWireRequestV1
{
    internal uint Size,Version;
    internal RemoteWireCommand Command;
    internal RemotePowerPurpose Purpose;
    internal RemotePowerKey128 Scope,Request;
    internal RemotePowerSettlementV1 Settlement;
}
[StructLayout(LayoutKind.Sequential,Pack=8)]
internal struct RemoteWireResponseV1
{
    internal uint Size,Version,Accepted;
    internal RemoteWireCommand Command;
    internal RemotePowerDescriptorV1 Binding;
    internal SharedPowerSnapshotV1 Current;
}

internal sealed class RemoteProcessWait(SafeWaitHandle handle) : WaitHandle
{
    internal RemoteProcessWait Initialize() {SafeWaitHandle=handle;return this;}
}

// Cleanup ownership only. This carries no power permission and never retries a
// remote terminal delivery. The original serial caller retains it until all
// local import resources have actually returned.
public sealed class RemoteNativePowerImportOwner : IDisposable
{
    private readonly object gate=new();
    internal SafeWaitHandle? RawProvider,RawNative;
    internal RemoteProcessWait? Provider,NativeEvent;
    internal ManualResetEvent? Stop,Revoked;
    internal RemoteNativePowerScope? Candidate;
    public bool LocalReturnComplete
    { get { lock(gate)return Candidate is null&&RawProvider is null&&RawNative is null&&
        Provider is null&&NativeEvent is null&&Stop is null&&Revoked is null; } }
    private static void Close<T>(ref T? owner,ref Exception? failure) where T:class,IDisposable
    {
        if(owner is null)return;
        try{owner.Dispose();owner=null;}catch(Exception error){failure??=error;}
    }
    public void Dispose()
    {
        lock(gate)
        {
            Exception? failure=null;
            if(Candidate is not null)
            {
                try{Candidate.Dispose();if(Candidate.LocalReturnComplete)Candidate=null;}
                catch(Exception error){failure=error;}
            }
            // Once a candidate owns a reader its events belong only to it.
            // Partial construction never starts a reader, so independent raw
            // and wrapper resources can each return without skipping siblings.
            Close(ref NativeEvent,ref failure);Close(ref Provider,ref failure);
            Close(ref Stop,ref failure);Close(ref Revoked,ref failure);
            Close(ref RawNative,ref failure);Close(ref RawProvider,ref failure);
            if(failure is not null)throw new IOException("Original unpublished shared-power local return incomplete",failure);
        }
    }
}

public sealed class RemoteNativePowerLocalReturnException(RemoteNativePowerImportOwner owner,string message)
    : IOException(message)
{ public RemoteNativePowerImportOwner LocalOwner { get; }=owner; }

public sealed class RemoteNativePowerScope : IRemoteNativePowerScope
{
    private readonly object gate=new();
    private readonly RemotePowerOfferV1 offer;
    private readonly RemoteProcessWait providerExited;
    private readonly RemoteProcessWait nativeRevoked;
    private readonly ManualResetEvent revoked,stop;
    private Thread? mirror;
    private int cancelled,disposed;
    private bool returnedPrepared,returnedDeliveryAttempted;
    private RemotePowerSettlementOutcome preparedOutcome;
    private RemotePowerSettlementFlags preparedFlags;
    private RemoteNativePowerScope(RemotePowerOfferV1 value,RemoteProcessWait provider,RemoteProcessWait native,
        ManualResetEvent localRevoked,ManualResetEvent localStop)
    {offer=value;providerExited=provider;nativeRevoked=native;revoked=localRevoked;stop=localStop;}
    public RemotePowerDescriptorV1 Binding=>offer.Binding;
    public WaitHandle Revoked=>revoked;
    public WaitHandle ProviderExited=>providerExited;
    // Local import resources only. This never certifies SDK/device/occupancy
    // return, Native power or remote settlement delivery.
    public bool LocalReturnComplete { get { lock(gate)return disposed==2; } }

    // Parent identity comes from the original launcher/held parent owner, never
    // from the received offer. Path alone cannot select one of several Native
    // instances installed at the same path. Original TryImport verifies kernel
    // PID/birth/session, same user, image and pipe after this local-owner check.
    public static bool TryImportFromParent(RemotePowerOfferV1 value,RemotePowerProcessV1 trustedNativeParent,
        string trustedNativeImagePath,RemotePowerPurpose requiredPurpose,
        out RemoteNativePowerScope? scope,out string reason)
    {
        scope=null;reason="remote-power-parent-identity";
        if(trustedNativeParent.Pid==0||trustedNativeParent.Birth100ns==0||!SameProcess(value.Binding.Provider,trustedNativeParent))return false;
        if(value.Binding.Purpose!=requiredPurpose){reason="remote-power-parent-purpose";return false;}
        return TryImport(value,trustedNativeImagePath,out scope,out reason);
    }

    // Explicit cleanup handoff for the original Host serial owner. A rejected
    // import can return a cleanup-only owner; it can never be used as a scope.
    public static bool TryImportFromParent(RemotePowerOfferV1 value,RemotePowerProcessV1 trustedNativeParent,
        string trustedNativeImagePath,RemotePowerPurpose requiredPurpose,
        out RemoteNativePowerScope? scope,out string reason,out RemoteNativePowerImportOwner? failedLocalOwner)
    {
        scope=null;failedLocalOwner=null;reason="remote-power-parent-identity";
        if(trustedNativeParent.Pid==0||trustedNativeParent.Birth100ns==0||!SameProcess(value.Binding.Provider,trustedNativeParent))return false;
        if(value.Binding.Purpose!=requiredPurpose){reason="remote-power-parent-purpose";return false;}
        return TryImportOwned(value,trustedNativeImagePath,out scope,out reason,out failedLocalOwner);
    }

    // trustedNativeImagePath is a local installation/source pin supplied by the
    // caller, never a path copied from the received offer. The exact pipe server
    // PID is additionally verified by the kernel on every exchange.
    public static bool TryImport(RemotePowerOfferV1 value,string trustedNativeImagePath,
        out RemoteNativePowerScope? scope,out string reason)
    {
        bool accepted=TryImportOwned(value,trustedNativeImagePath,out scope,out reason,out var failedLocalOwner);
        if(failedLocalOwner is not null)
            throw new RemoteNativePowerLocalReturnException(failedLocalOwner,reason);
        return accepted;
    }
    private static bool TryImportOwned(RemotePowerOfferV1 value,string trustedNativeImagePath,
        out RemoteNativePowerScope? scope,out string reason,out RemoteNativePowerImportOwner? failedLocalOwner)
    {
        scope=null;failedLocalOwner=null;reason="remote-power-import-unavailable";
        var localOwner=new RemoteNativePowerImportOwner();
        RemoteProcessWait? provider=null,native=null,failedInputProvider=null;RemoteNativePowerScope? candidate=null;
        bool authenticatedInputBind=false;
        try
        {
            if(string.IsNullOrWhiteSpace(trustedNativeImagePath)||!Path.IsPathFullyQualified(trustedNativeImagePath)||
               Marshal.SizeOf<RemotePowerDescriptorV1>()!=128||Marshal.SizeOf<SharedPowerSnapshotV1>()!=48||
               Marshal.SizeOf<RemotePowerSettlementV1>()!=80||Marshal.SizeOf<RemoteWireRequestV1>()!=128||
               Marshal.SizeOf<RemoteWireResponseV1>()!=192||!ValidStem(value.PipeName)||!ValidDescriptor(value.Binding))
            {reason="remote-power-binding-shape";return false;}
            var self=Native.OpenHeldProcess((uint)Environment.ProcessId);
            using(self)
            {
                if(self.IsInvalid||!Native.Identity(self,out var selfIdentity)||!SameProcess(selfIdentity,value.Binding.Consumer))
                {reason="remote-power-consumer-identity";return false;}
                var handle=Native.OpenHeldProcess(value.Binding.Provider.Pid);localOwner.RawProvider=handle;
                if(handle.IsInvalid){reason="remote-power-provider-open";return false;}
                provider=new RemoteProcessWait(handle).Initialize();localOwner.Provider=provider;localOwner.RawProvider=null;
                if(!Native.Identity(handle,out var identity)||!SameProcess(identity,value.Binding.Provider)||identity.Session!=selfIdentity.Session)
                {reason="remote-power-provider-identity";return false;}
                if(!Native.SameUser(self,handle)){reason="remote-power-provider-user";return false;}
                if(!string.Equals(Path.GetFullPath(Native.Image(handle)),Path.GetFullPath(trustedNativeImagePath),StringComparison.OrdinalIgnoreCase))
                {reason="remote-power-provider-image";return false;}
            }
            if(!Exchange(value,provider,RemoteWireCommand.Bind,default,out var response,out string transportReason)){reason="remote-power-bind-"+transportReason;return false;}
            if(response.Accepted!=1||!SameDescriptor(value.Binding,response.Binding)){reason="remote-power-bind-rejected";return false;}
            // No caller has received this scope or entered an input effect.
            // Only this exact authenticated successful Bind permits command-only
            // settlement on a later local setup failure. Uncertain Bind stays debt.
            if(value.Binding.Purpose is RemotePowerPurpose.InputLifecycle or RemotePowerPurpose.InputFrame)
            { authenticatedInputBind=true; failedInputProvider=provider; }
            string eventName="Local\\"+value.PipeName+"."+value.Binding.Scope.Low+"."+value.Binding.Scope.High+".Cancel";
            var eventHandle=Native.OpenEvent(0x00100000,false,eventName);localOwner.RawNative=eventHandle;
            if(eventHandle.IsInvalid){reason="remote-power-event-open";return false;}
            native=new RemoteProcessWait(eventHandle).Initialize();localOwner.NativeEvent=native;localOwner.RawNative=null;
            localOwner.Revoked=new ManualResetEvent(false);localOwner.Stop=new ManualResetEvent(false);
            candidate=new RemoteNativePowerScope(value,provider,native,localOwner.Revoked,localOwner.Stop);
            localOwner.Candidate=candidate;localOwner.Provider=null;localOwner.NativeEvent=null;localOwner.Revoked=null;localOwner.Stop=null;
            provider=null;native=null;
            if(candidate.nativeRevoked.WaitOne(0)||candidate.providerExited.WaitOne(0)||
               !candidate.Current(value.Binding.Purpose)){reason="remote-power-initial-current";return false;}
            candidate.mirror=new Thread(candidate.MirrorCancellation){IsBackground=true,Name="YMCC shared power IPC cancellation"};
            candidate.mirror.Start();scope=candidate;localOwner.Candidate=null;candidate=null;reason="";return true;
        }
        catch(Exception error) when(error is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception or System.Threading.ThreadStateException or OutOfMemoryException)
        {reason="remote-power-import-"+error.GetType().Name;return false;}
        finally
        {
            // This still owns the unpublished candidate. Stop and join its local
            // cancellation reader before reporting zero command borrowers. There
            // is no SDK/endpoint/occupancy release assertion in flags10.
            if(scope is null && authenticatedInputBind && failedInputProvider is not null)
            {
                RemoteSettlementDelivery delivery=RemoteSettlementDelivery.Unknown;
                try
                {
                    candidate?.Withdraw();
                    if(candidate?.mirror is { IsAlive:true } mirror&&!mirror.Join(1000))
                        throw new IOException("Unpublished original cancellation reader remains owned");
                    var receipt=new RemotePowerSettlementV1 {Size=80,Version=1,Scope=value.Binding.Scope,
                        Request=value.Binding.Request,Provider=value.Binding.Provider,Consumer=value.Binding.Consumer,
                        Outcome=RemotePowerSettlementOutcome.InputOperationReturned,
                        Flags=RemotePowerSettlementFlags.InputOperationFinished};
                    if(Exchange(value,failedInputProvider,RemoteWireCommand.Settle,receipt,out var response,out _))
                        delivery=response.Accepted==1?RemoteSettlementDelivery.Acknowledged:RemoteSettlementDelivery.Rejected;
                }
                catch(Exception error) when(error is IOException or UnauthorizedAccessException or ArgumentException or
                    InvalidOperationException or System.ComponentModel.Win32Exception or OutOfMemoryException)
                { /* One actual attempt only. Unknown delivery never refunds or replays it. */ }
                reason+=";input-command-settlement="+delivery.ToString();
            }
            // Independent ownership returns even when another local disposal
            // fails. No cleanup exception may silently skip the remaining owner.
            try { localOwner.Dispose(); }
            catch(Exception error)
            {
                // This exact preallocated owner reaches the caller even when
                // cleanup fails. No thread or global pending-resource queue.
                reason+=";local-return="+error.GetType().Name;
            }
            if(!localOwner.LocalReturnComplete)failedLocalOwner=localOwner;
        }
    }
    private void MirrorCancellation()
    {
        try{WaitHandle.WaitAny([stop,nativeRevoked,providerExited]);}
        catch(ObjectDisposedException){}
        finally{Interlocked.Exchange(ref cancelled,1);revoked.Set();}
    }
    private void Withdraw()
    {Interlocked.Exchange(ref cancelled,1);revoked.Set();stop.Set();}
    private bool LocalCurrent()=>Volatile.Read(ref disposed)==0&&Volatile.Read(ref cancelled)==0&&
        !providerExited.WaitOne(0)&&!nativeRevoked.WaitOne(0);
    public bool TryReadCurrent(out SharedPowerSnapshotV1 value)
    {
        value=default;lock(gate)
        {
            if(!LocalCurrent())return false;
            if(Exchange(offer,providerExited,RemoteWireCommand.Read,default,out var response,out _)&&response.Accepted==1&&
               SameDescriptor(offer.Binding,response.Binding)&&Known(response.Current)&&LocalCurrent())
            {value=response.Current;return true;}
            Withdraw();return false;
        }
    }
    public bool Current(RemotePowerPurpose requiredPurpose)
    {
        if(requiredPurpose!=offer.Binding.Purpose)return false;
        lock(gate)
        {
            if(!LocalCurrent())return false;
            if(Exchange(offer,providerExited,RemoteWireCommand.Current,default,out var response,out _)&&response.Accepted==1&&
               SameDescriptor(offer.Binding,response.Binding)&&SamePower(offer.Binding.Captured,response.Current)&&
               (response.Current.Flags&4)==0&&LocalCurrent())return true;
            Withdraw();return false;
        }
    }
    // Input effect path: original authenticated import already proved the
    // Native tuple/purpose. Every boundary checks the original kernel event;
    // no per-frame IPC, recapture, epoch or new power fact is introduced.
    public bool InputCurrentLocal(RemotePowerPurpose requiredPurpose)
    {
        if(requiredPurpose is not (RemotePowerPurpose.InputLifecycle or RemotePowerPurpose.InputFrame)||
           requiredPurpose!=offer.Binding.Purpose)return false;
        lock(gate){return LocalCurrent();}
    }
    private static bool Terminal(RemotePowerSettlementOutcome outcome)=>
        outcome is RemotePowerSettlementOutcome.Returned or RemotePowerSettlementOutcome.InputOperationReturned;
    public void CancelLocal()
    {
        Interlocked.Exchange(ref cancelled,1);
        lock(gate){if(disposed==0){Withdraw();Exchange(offer,providerExited,RemoteWireCommand.Cancel,default,out _,out _);}}
    }
    public bool TryCreateSettlement(RemotePowerSettlementOutcome outcome,RemotePowerSettlementFlags flags,
        out RemotePowerSettlementV1 receipt)
    {
        receipt=default;lock(gate)
        {
            if(disposed!=0||returnedPrepared||(flags&~RemotePowerSettlementFlags.AllReturned)!=0||
               outcome is not (RemotePowerSettlementOutcome.Unknown or RemotePowerSettlementOutcome.Returned or RemotePowerSettlementOutcome.RetainedResources or RemotePowerSettlementOutcome.InputOperationReturned)||
               (outcome==RemotePowerSettlementOutcome.Returned&&flags!=RemotePowerSettlementFlags.AllReturned)||
               (outcome==RemotePowerSettlementOutcome.InputOperationReturned&&
                (offer.Binding.Purpose is not (RemotePowerPurpose.InputLifecycle or RemotePowerPurpose.InputFrame)||
                 flags!=RemotePowerSettlementFlags.InputOperationFinished)))return false;
            receipt=new(){Size=80,Version=1,Scope=offer.Binding.Scope,Request=offer.Binding.Request,
                Provider=offer.Binding.Provider,Consumer=offer.Binding.Consumer,Outcome=outcome,Flags=flags};
            if(Terminal(outcome)){preparedOutcome=outcome;preparedFlags=flags;returnedPrepared=true;Withdraw();}
            return true;
        }
    }
    // One exchange only. A lost response is Unknown; it never retries a return
    // or invents a resource receipt. The original consumer retains the raw ACK.
    public RemoteSettlementDelivery SendSettlement(RemotePowerSettlementV1 receipt)
    {
        lock(gate)
        {
            if(disposed!=0||receipt.Size!=80||receipt.Version!=1||!SameKey(receipt.Scope,offer.Binding.Scope)||
               !SameKey(receipt.Request,offer.Binding.Request)||!SameProcess(receipt.Provider,offer.Binding.Provider)||
               !SameProcess(receipt.Consumer,offer.Binding.Consumer))return RemoteSettlementDelivery.Rejected;
            if(Terminal(receipt.Outcome))
            {
                bool correctFlags=receipt.Outcome==RemotePowerSettlementOutcome.Returned
                    ? receipt.Flags==RemotePowerSettlementFlags.AllReturned
                    : offer.Binding.Purpose is (RemotePowerPurpose.InputLifecycle or RemotePowerPurpose.InputFrame)&&
                      receipt.Flags==RemotePowerSettlementFlags.InputOperationFinished;
                if(!returnedPrepared||returnedDeliveryAttempted||!correctFlags||
                   receipt.Outcome!=preparedOutcome||receipt.Flags!=preparedFlags)
                    return RemoteSettlementDelivery.Rejected;
                // Reserve the one terminal delivery before any IO. Unknown,
                // rejection, cancellation and exceptions never refund it.
                returnedDeliveryAttempted=true;
            }
            else if(returnedPrepared)return RemoteSettlementDelivery.Rejected;
            if(!Exchange(offer,providerExited,RemoteWireCommand.Settle,receipt,out var response,out _))return RemoteSettlementDelivery.Unknown;
            return response.Accepted==1?RemoteSettlementDelivery.Acknowledged:RemoteSettlementDelivery.Rejected;
        }
    }
    private static void CloseLocal(IDisposable owner,ref Exception? failure)
    { try{owner.Dispose();}catch(Exception error){failure??=error;} }
    public void Dispose()
    {
        lock(gate)
        {
            if(disposed==2)return;
            Volatile.Write(ref disposed,1);Interlocked.Exchange(ref cancelled,1);
            Exception? failure=null;bool stopped=false;
            try
            {
                if(!stop.SafeWaitHandle.IsClosed)stopped=stop.Set();
                else stopped=mirror is null||!mirror.IsAlive;
            }
            catch(Exception error){failure=error;}
            try { if(!revoked.SafeWaitHandle.IsClosed)revoked.Set(); }
            catch(Exception error){failure??=error;}
            if(mirror is {IsAlive:true})
            {
                // Never close an event/process handle still borrowed by the
                // cancellation reader. One bounded join; the next original
                // serial cleanup owns any continuation, without a new worker.
                if(!stopped||!mirror.Join(1000))
                    throw new IOException("Original shared-power cancellation reader remains owned",failure);
            }
            mirror=null;
            CloseLocal(nativeRevoked,ref failure);CloseLocal(providerExited,ref failure);
            CloseLocal(stop,ref failure);CloseLocal(revoked,ref failure);
            if(failure is not null)throw new IOException("Original shared-power local return incomplete",failure);
            Volatile.Write(ref disposed,2);
        }
    }
    private static bool Exchange(RemotePowerOfferV1 value,RemoteProcessWait provider,RemoteWireCommand command,
        RemotePowerSettlementV1 settlement,out RemoteWireResponseV1 response,out string failure)
    {
        response=default;failure="provider-exited";
        try
        {
            if(provider.WaitOne(0))return false;
            long deadline=Environment.TickCount64+1000;
            failure="connect";using var pipe=Native.OpenPipe(value.PipeName,deadline);
            if(pipe.IsInvalid)return false;
            failure="server-identity";
            if(!Native.GetNamedPipeServerProcessId(pipe,out uint server)||server!=value.Binding.Provider.Pid||
               !Native.Identity(provider.SafeWaitHandle,out var identity)||!SameProcess(identity,value.Binding.Provider)||provider.WaitOne(0))return false;
            var request=new RemoteWireRequestV1{Size=128,Version=1,Command=command,Purpose=value.Binding.Purpose,
                Scope=value.Binding.Scope,Request=value.Binding.Request,Settlement=settlement};
            byte[] buffer=new byte[128];MemoryMarshal.Write(buffer,request);
            failure="write";if(!Native.Transfer(pipe,provider.SafeWaitHandle,buffer,true,deadline))return false;
            failure="read";byte[] result=new byte[192];if(!Native.Transfer(pipe,provider.SafeWaitHandle,result,false,deadline))return false;
            failure="reply";response=MemoryMarshal.Read<RemoteWireResponseV1>(result);
            if(response.Size!=192||response.Version!=1||response.Command!=command||response.Accepted>1||
               !Native.GetNamedPipeServerProcessId(pipe,out server)||server!=value.Binding.Provider.Pid||provider.WaitOne(0))return false;
            failure="response-ack";request.Command=RemoteWireCommand.Acknowledge;
            MemoryMarshal.Write(buffer,request);return Native.Transfer(pipe,provider.SafeWaitHandle,buffer,true,deadline);
        }
        catch(Exception error) when(error is IOException or OperationCanceledException or UnauthorizedAccessException or InvalidOperationException)
        {failure+="-"+error.GetType().Name+"-"+error.HResult;return false;}
    }
    private static bool Known(SharedPowerSnapshotV1 value)=>value.Size==48&&value.Version==1&&value.Generation!=0&&value.Phase<=3&&(value.Flags&1)!=0;
    internal static bool ValidDescriptor(RemotePowerDescriptorV1 v)=>v.Size==128&&v.Version==1&&v.Reserved==0&&
        v.Purpose is RemotePowerPurpose.Observation or RemotePowerPurpose.FanResume or RemotePowerPurpose.FanWrite or RemotePowerPurpose.InputLifecycle or RemotePowerPurpose.InputFrame&&
        (v.Scope.Low!=0||v.Scope.High!=0)&&(v.Request.Low!=0||v.Request.High!=0)&&v.Provider.Pid!=0&&v.Provider.Birth100ns!=0&&
        v.Consumer.Pid!=0&&v.Consumer.Birth100ns!=0&&Known(v.Captured)&&(v.Captured.Flags&4)==0;
    internal static bool ValidStem(string value)
    {
        if(string.IsNullOrEmpty(value)||value.Length is <60 or >160||!value.StartsWith("YeManCC.SharedPower.v1.",StringComparison.Ordinal))return false;
        foreach(char c in value)if(c!='.'&&!char.IsAsciiLetterOrDigit(c))return false;return true;
    }
    private static bool SameKey(RemotePowerKey128 a,RemotePowerKey128 b)=>a.Low==b.Low&&a.High==b.High;
    private static bool SameProcess(RemotePowerProcessV1 a,RemotePowerProcessV1 b)=>a.Pid==b.Pid&&a.Session==b.Session&&a.Birth100ns==b.Birth100ns;
    private static bool SamePower(SharedPowerSnapshotV1 a,SharedPowerSnapshotV1 b)=>Known(a)&&Known(b)&&a.Generation==b.Generation&&
        a.SuspendSerial==b.SuspendSerial&&a.ObservedNotificationSequence==b.ObservedNotificationSequence&&a.Phase==b.Phase&&a.Flags==b.Flags&&b.PublicationRevision>=a.PublicationRevision;
    private static bool SameDescriptor(RemotePowerDescriptorV1 a,RemotePowerDescriptorV1 b)=>a.Size==b.Size&&a.Version==b.Version&&
        a.Reserved==b.Reserved&&a.Purpose==b.Purpose&&SameKey(a.Scope,b.Scope)&&SameKey(a.Request,b.Request)&&
        SameProcess(a.Provider,b.Provider)&&SameProcess(a.Consumer,b.Consumer)&&SamePower(a.Captured,b.Captured)&&a.Captured.PublicationRevision==b.Captured.PublicationRevision;

    private static class Native
    {
        internal static SafeWaitHandle OpenHeldProcess(uint pid)=>OpenProcess(0x00101000,false,pid);
        [DllImport("kernel32.dll",SetLastError=true)]private static extern SafeWaitHandle OpenProcess(uint access,[MarshalAs(UnmanagedType.Bool)]bool inherit,uint pid);
        [DllImport("kernel32.dll",SetLastError=true,CharSet=CharSet.Unicode)]internal static extern SafeWaitHandle OpenEvent(uint access,[MarshalAs(UnmanagedType.Bool)]bool inherit,string name);
        [DllImport("kernel32.dll",SetLastError=true)]internal static extern bool GetNamedPipeServerProcessId(SafeFileHandle pipe,out uint pid);
        [DllImport("kernel32.dll",SetLastError=true)]private static extern uint GetProcessId(SafeWaitHandle process);
        [DllImport("kernel32.dll",SetLastError=true)]private static extern bool ProcessIdToSessionId(uint pid,out uint session);
        [DllImport("kernel32.dll",SetLastError=true)]private static extern bool GetProcessTimes(SafeWaitHandle process,out long birth,out long exit,out long kernel,out long user);
        [DllImport("kernel32.dll",SetLastError=true)]private static extern uint WaitForSingleObject(SafeWaitHandle handle,uint milliseconds);
        [DllImport("kernel32.dll",SetLastError=true,CharSet=CharSet.Unicode)]private static extern bool QueryFullProcessImageName(SafeWaitHandle process,uint flags,System.Text.StringBuilder text,ref uint length);
        [DllImport("advapi32.dll",SetLastError=true)]private static extern bool OpenProcessToken(SafeWaitHandle process,uint access,out SafeAccessTokenHandle token);
        [StructLayout(LayoutKind.Sequential)]private struct IoOperation
        {internal UIntPtr Internal,InternalHigh;internal uint Offset,OffsetHigh;internal IntPtr Event;}
        [DllImport("kernel32.dll",SetLastError=true,CharSet=CharSet.Unicode)]private static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
        [DllImport("kernel32.dll",SetLastError=true,CharSet=CharSet.Unicode)]private static extern bool WaitNamedPipe(string name,uint milliseconds);
        [DllImport("kernel32.dll",SetLastError=true,CharSet=CharSet.Unicode)]private static extern SafeWaitHandle CreateEvent(IntPtr security,bool manual,bool initial,string? name);
        [DllImport("kernel32.dll",SetLastError=true)]private static extern bool WriteFile(SafeFileHandle file,IntPtr bytes,uint count,out uint transferred,IntPtr overlapped);
        [DllImport("kernel32.dll",SetLastError=true)]private static extern bool ReadFile(SafeFileHandle file,IntPtr bytes,uint count,out uint transferred,IntPtr overlapped);
        [DllImport("kernel32.dll",SetLastError=true)]private static extern bool GetOverlappedResult(SafeFileHandle file,IntPtr operation,out uint transferred,bool wait);
        [DllImport("kernel32.dll",SetLastError=true)]private static extern bool CancelIoEx(SafeFileHandle file,IntPtr operation);
        [DllImport("kernel32.dll",SetLastError=true)]private static extern uint WaitForMultipleObjects(uint count,IntPtr[] handles,bool all,uint milliseconds);
        internal static SafeFileHandle OpenPipe(string stem,long deadline)
        {
            string name="\\\\.\\pipe\\"+stem;
            for(int attempt=0;attempt<3;attempt++)
            {
                long remaining=deadline-Environment.TickCount64;
                if(remaining<=0||!WaitNamedPipe(name,(uint)remaining))return new(IntPtr.Zero,true);
                var pipe=CreateFile(name,0xC0000000,0,IntPtr.Zero,3,0x40000000|0x00100000|0x00010000,IntPtr.Zero);
                if(!pipe.IsInvalid)return pipe;
                int error=Marshal.GetLastWin32Error();pipe.Dispose();if(error!=231)break;
                // Only connection acquisition races can continue within the
                // original deadline. No request bytes have been sent yet.
            }
            return new(IntPtr.Zero,true);
        }
        internal static bool Transfer(SafeFileHandle file,SafeWaitHandle provider,byte[] bytes,bool write,long deadline)
        {
            using var done=CreateEvent(IntPtr.Zero,true,false,null);if(done.IsInvalid)return false;
            IntPtr data=Marshal.AllocHGlobal(bytes.Length),operation=IntPtr.Zero;
            try
            {
                operation=Marshal.AllocHGlobal(Marshal.SizeOf<IoOperation>());
                Marshal.StructureToPtr(new IoOperation{Event=done.DangerousGetHandle()},operation,false);
                if(write)Marshal.Copy(bytes,0,data,bytes.Length);
                bool initiated=write?WriteFile(file,data,(uint)bytes.Length,out uint count,operation):ReadFile(file,data,(uint)bytes.Length,out count,operation);
                int error=initiated?0:Marshal.GetLastWin32Error();
                if(!initiated)
                {
                    if(error!=997)return false;
                    long remaining=deadline-Environment.TickCount64;
                    uint wait=remaining>0?WaitForMultipleObjects(2,[provider.DangerousGetHandle(),done.DangerousGetHandle()],false,(uint)remaining):258;
                    if(wait!=1)
                    {
                        CancelIoEx(file,operation);GetOverlappedResult(file,operation,out _,true);
                        return false;
                    }
                }
                // Buffers and OVERLAPPED remain owned until actual kernel
                // completion, including the cancellation/timeout path above.
                if(!GetOverlappedResult(file,operation,out count,false)||count!=bytes.Length)return false;
                if(!write)Marshal.Copy(data,bytes,0,bytes.Length);
                return true;
            }
            finally{if(operation!=IntPtr.Zero)Marshal.FreeHGlobal(operation);Marshal.FreeHGlobal(data);}
        }
        internal static bool Identity(SafeWaitHandle process,out RemotePowerProcessV1 value)
        {
            value=default;uint pid=GetProcessId(process);
            if(process.IsInvalid||pid==0||WaitForSingleObject(process,0)!=258||!ProcessIdToSessionId(pid,out uint session)||
               !GetProcessTimes(process,out long birth,out _,out _,out _)||birth<=0||WaitForSingleObject(process,0)!=258)return false;
            value=new(){Pid=pid,Session=session,Birth100ns=(ulong)birth};return true;
        }
        internal static string Image(SafeWaitHandle process)
        {uint length=32768;var text=new System.Text.StringBuilder((int)length);return QueryFullProcessImageName(process,0,text,ref length)?text.ToString():"";}
        internal static bool SameUser(SafeWaitHandle a,SafeWaitHandle b)
        {
            if(!OpenProcessToken(a,8,out var first))return false;
            using(first){if(!OpenProcessToken(b,8,out var second))return false;
                using(second)using(var x=new WindowsIdentity(first.DangerousGetHandle()))using(var y=new WindowsIdentity(second.DangerousGetHandle()))
                    return x.User is not null&&x.User==y.User;
            }
        }
    }
}
