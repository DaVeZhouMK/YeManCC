using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;

// Reads the distinct native SuperJoy handoff. An event/heartbeat alone never
// grants admission: exact parent process birth, model, path and all token fields
// must agree with an Active, stable record. No HC or HID endpoint is opened.
internal sealed class SuperJoyOwnerLease : IDisposable
{
    internal const uint Magic=0x534A4C31,Version=2;
    internal const int RecordSize=1344,SequenceOffset=8,ParentPidOffset=16,StateOffset=20,ParentBirthOffset=24,
        GenerationOffset=32,TopologyOffset=40,AdmissionOffset=48,HeartbeatOffset=56,ChildPidOffset=64,ChildBirthOffset=72,
        TokenOffset=80,TokenLength=128,ModelOffset=208,ModelLength=96,PathOffset=304,PathBytes=1040;
    internal const long HeartbeatToleranceMs=2000;
    private static readonly ulong SelfBirth=ReadSelfBirth();
    private static ulong ReadSelfBirth(){using var self=Process.GetCurrentProcess();return (ulong)self.StartTime.ToUniversalTime().ToFileTimeUtc();}
    private readonly object gate=new();
    private readonly MemoryMappedFile file;
    private readonly MemoryMappedViewAccessor view;
    private readonly EventWaitHandle revoked;
    private readonly Process parent;
    private readonly CancellationTokenSource cancellation=new();
    private readonly CancellationToken token;
    private RegisteredWaitHandle? registration;
    private readonly Task monitor;
    private bool disposed;
    private string? revocationReason;
    internal SuperJoyIdentity Identity{get;}
    internal string AdmissionToken{get;}
    private readonly ulong[] parts;
    internal CancellationToken Token=>token;
    internal string? RevocationReason{get{lock(gate)return revocationReason;}}
    internal bool IsCurrent{get{lock(gate)return !disposed&&!token.IsCancellationRequested&&RecordCurrent(view,Identity,AdmissionToken,parts)&&!parent.HasExited;}}
    private SuperJoyOwnerLease(SuperJoyIdentity identity,string admission,ulong[] fields,MemoryMappedFile file,
        MemoryMappedViewAccessor view,EventWaitHandle revoked,Process parent)
    {
        Identity=identity;AdmissionToken=admission;parts=fields;this.file=file;this.view=view;this.revoked=revoked;this.parent=parent;
        token=cancellation.Token;
        registration=ThreadPool.RegisterWaitForSingleObject(revoked,(_,_)=>Revoke("native-owner-revoked"),null,Timeout.Infinite,true);
        monitor=Task.Run(MonitorAsync);
    }
    internal static string MappingName(int pid,ulong birth)=>@"Local\YeManCC-SuperJoy-LightingOwner-v2-"+pid.ToString(CultureInfo.InvariantCulture)+"-"+birth.ToString(CultureInfo.InvariantCulture);
    internal static string EventName(string admission)=>@"Local\YeManCC-SuperJoy-LightingRevoked-v2-"+admission.Replace(':','-');
    internal static ulong[] ParseToken(string admission)
    {
        if(string.IsNullOrWhiteSpace(admission)||admission.Length>=TokenLength)throw new ArgumentException("SUPERJOY_OWNER_TOKEN_FORMAT");
        var text=admission.Split(':');if(text.Length!=5)throw new ArgumentException("SUPERJOY_OWNER_TOKEN_FORMAT");
        var parts=new ulong[5];
        for(var i=0;i<parts.Length;i++)
            if(!ulong.TryParse(text[i],NumberStyles.None,CultureInfo.InvariantCulture,out parts[i])||parts[i]==0||
                parts[i].ToString(CultureInfo.InvariantCulture)!=text[i])throw new ArgumentException("SUPERJOY_OWNER_TOKEN_FORMAT");
        if(parts[0]>int.MaxValue||parts[1]>long.MaxValue||parts[2]>long.MaxValue)throw new ArgumentException("SUPERJOY_OWNER_TOKEN_RANGE");
        return parts;
    }
    internal static SuperJoyOwnerLease Acquire(SuperJoyIdentity identity,string admission,CancellationToken cancellationToken=default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if(!identity.MatchesProtocol && !ApexLightingConsumer.MatchesOwnerIdentity(identity))throw new ArgumentException("SUPERJOY_OWNER_IDENTITY_REQUIRED");
        var parts=ParseToken(admission);
        if((ulong)identity.Generation!=parts[2])throw new ArgumentException("SUPERJOY_OWNER_GENERATION_MISMATCH");
        if(!OperatingSystem.IsWindows())throw new PlatformNotSupportedException("SuperJoy native handoff requires Windows");
        MemoryMappedFile? file=null;MemoryMappedViewAccessor? view=null;EventWaitHandle? revoked=null;Process? parent=null;
        try
        {
            file=MemoryMappedFile.OpenExisting(MappingName((int)parts[0],parts[1]),MemoryMappedFileRights.Read);
            view=file.CreateViewAccessor(0,RecordSize,MemoryMappedFileAccess.Read);
            if(!RecordCurrent(view,identity,admission,parts))throw new IOException("SUPERJOY_OWNER_NOT_ACTIVE");
            parent=Process.GetProcessById((int)parts[0]);
            _ = parent.SafeHandle; // retain this exact process object across PID reuse
            if(parent.HasExited||(ulong)parent.StartTime.ToUniversalTime().ToFileTimeUtc()!=parts[1])
                throw new IOException("SUPERJOY_PARENT_IDENTITY_MISMATCH");
            revoked=EventWaitHandle.OpenExisting(EventName(admission));
            cancellationToken.ThrowIfCancellationRequested();
            if(revoked.WaitOne(0)||!RecordCurrent(view,identity,admission,parts)||parent.HasExited)
                throw new IOException("SUPERJOY_OWNER_CHANGED_DURING_JOIN");
            var result=new SuperJoyOwnerLease(identity,admission,parts,file,view,revoked,parent);
            try{if(!result.IsCurrent)throw new IOException("SUPERJOY_OWNER_CHANGED_AFTER_JOIN");}
            catch{result.Dispose();throw;}
            file=null;view=null;revoked=null;parent=null;return result;
        }
        finally{parent?.Dispose();revoked?.Dispose();view?.Dispose();file?.Dispose();}
    }
    internal void EnsureCurrent()
    {
        if(IsCurrent)return;
        Revoke("native-owner-record-or-parent-retired");
        throw new OperationCanceledException("SUPERJOY_OWNER_NO_LONGER_CURRENT",token);
    }
    private void Revoke(string reason)
    {
        lock(gate)
        {
            if(disposed)return;
            revocationReason??=reason;cancellation.Cancel();
        }
    }
    private async Task MonitorAsync()
    {
        try
        {
            while(!token.IsCancellationRequested)
            {
                if(!IsCurrent){Revoke("native-owner-record-or-parent-retired");return;}
                await Task.Delay(25,token).ConfigureAwait(false);
            }
        }
        catch(OperationCanceledException)when(token.IsCancellationRequested){ }
        catch(Exception error)when(error is IOException or InvalidOperationException or System.ComponentModel.Win32Exception)
        {Revoke("native-owner-monitor-failed");}
    }
    private static bool RecordCurrent(MemoryMappedViewAccessor view,SuperJoyIdentity identity,string admission,ulong[] parts)
    {
        for(var attempt=0;attempt<8;attempt++)
        {
            var before=view.ReadUInt64(SequenceOffset);if((before&1)!=0){Thread.SpinWait(4);continue;}
            Thread.MemoryBarrier();
            var magic=view.ReadUInt32(0);var version=view.ReadUInt32(4);var pid=view.ReadUInt32(ParentPidOffset);var state=view.ReadUInt32(StateOffset);
            var birth=view.ReadUInt64(ParentBirthOffset);var generation=view.ReadUInt64(GenerationOffset);var topology=view.ReadUInt64(TopologyOffset);
            var number=view.ReadUInt64(AdmissionOffset);var heartbeat=view.ReadInt64(HeartbeatOffset);
            var childPid=view.ReadUInt32(ChildPidOffset);var childBirth=view.ReadUInt64(ChildBirthOffset);
            var tokenBytes=new byte[TokenLength];var modelBytes=new byte[ModelLength];var pathBytes=new byte[PathBytes];
            view.ReadArray(TokenOffset,tokenBytes,0,tokenBytes.Length);view.ReadArray(ModelOffset,modelBytes,0,modelBytes.Length);view.ReadArray(PathOffset,pathBytes,0,pathBytes.Length);
            Thread.MemoryBarrier();var after=view.ReadUInt64(SequenceOffset);
            if(before!=after||(after&1)!=0){Thread.SpinWait(4);continue;}
            if(magic!=Magic||version!=Version||state!=2||pid!=parts[0]||birth!=parts[1]||generation!=parts[2]||topology!=parts[3]||number!=parts[4]||
                childPid!=(uint)Environment.ProcessId||childBirth!=SelfBirth)return false;
            if(Ascii(tokenBytes)!=admission||Ascii(modelBytes)!=identity.ModelClass||!string.Equals(Wide(pathBytes),identity.DevicePath,StringComparison.OrdinalIgnoreCase))return false;
            var age=unchecked(Environment.TickCount64-heartbeat);return age>=0&&age<=HeartbeatToleranceMs;
        }
        return false;
    }
    private static string? Ascii(byte[] bytes)
    {
        var end=Array.IndexOf(bytes,(byte)0);if(end<0)return null;
        for(var i=0;i<end;i++)if(bytes[i]>127)return null;
        return Encoding.ASCII.GetString(bytes,0,end);
    }
    private static string? Wide(byte[] bytes)
    {
        var end=0;while(end+1<bytes.Length&&(bytes[end]!=0||bytes[end+1]!=0))end+=2;
        return end+1<bytes.Length?Encoding.Unicode.GetString(bytes,0,end):null;
    }
    public void Dispose()
    {
        lock(gate)
        {
            if(disposed)return;disposed=true;revocationReason??="lease-disposed";cancellation.Cancel();
            registration?.Unregister(null);registration=null;
            revoked.Dispose();view.Dispose();file.Dispose();parent.Dispose();
        }
        // Cancellation may inline a consumer continuation on the monitor itself.
        // Never join that task synchronously. Closed resources are protected by
        // disposed+gate; dispose the CTS only after monitoring and Cancel unwind.
        _=monitor.ContinueWith(_=>{lock(gate)cancellation.Dispose();},CancellationToken.None,
            TaskContinuationOptions.None,TaskScheduler.Default);
    }
}
