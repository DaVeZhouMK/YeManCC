using System.Threading;
using HIDMaestro;
// Test-only OS ports: expose the moment the first real 500ms wait returns false.
// Opening a gate then cannot convert that first result into a false PASS.
internal static class TerminalTestPorts
{
    internal static bool ReleaseOutputAfterTimeout,ReleaseClockAfterTimeout,ThrowHandle,CaptureIdentityFailure;
    internal static int OutputTimeouts,ClockTimeouts,LockTimeouts;
    internal static bool ThrowInitialization;
    internal static int StageCreates,StageDisposes;
    internal static IDisposable? CostLifetime;
    internal static Action? BeforeStageDispose;
    internal static Action? AfterLockTimeout,BeforeWrite;
    internal static void Reset(){ReleaseOutputAfterTimeout=ReleaseClockAfterTimeout=ThrowHandle=CaptureIdentityFailure=false;OutputTimeouts=ClockTimeouts=LockTimeouts=0;AfterLockTimeout=BeforeWrite=null;ThrowInitialization=false;StageCreates=StageDisposes=0;CostLifetime=null;BeforeStageDispose=null;}
    internal static bool Join(object thread,int milliseconds)
    {
        bool ok=((Thread)thread).Join(milliseconds);
        if(!ok){OutputTimeouts++;if(ReleaseOutputAfterTimeout){ReleaseOutputAfterTimeout=false;OwnedWorkers.All.Single(w=>ReferenceEquals(w.Output,thread)).Gate.Set();}}
        return ok;
    }
    internal static bool HasTerminated(object thread)
    {
        bool ok=((Thread)thread).Join(0);
        if(!ok){ClockTimeouts++;if(ReleaseClockAfterTimeout){ReleaseClockAfterTimeout=false;OwnedWorkers.All.Single(w=>ReferenceEquals(w.Clock.Thread,thread)).Clock.ExternalGate.Set();}}
        return ok;
    }
    internal static bool TryEnter(object gate,int milliseconds)
    {
        bool ok=Monitor.TryEnter(gate,milliseconds);if(!ok){LockTimeouts++;AfterLockTimeout?.Invoke();}return ok;
    }
}
