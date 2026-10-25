using System.Text.Json;
using YeManInputHost.Diagnostics;

namespace YeManInputHost.Diagnostics {
    // The evidence sink itself is exercised by usbip_setup_evidence_selftest.mjs.
    internal sealed class UsbipSetupEvidence {
        internal readonly List<string> Stages = new();
        internal readonly List<string> Progress = new();
        internal void Stage(string phase, string detail = "", bool? available = null) => Stages.Add(phase + ":" + detail + ":" + available);
        internal void Report(string message) => Progress.Add(message);
    }
}
internal sealed class HMContext {
    internal static readonly List<string> Events = new();
    internal static Queue<object> Availability = new();
    internal static bool ThrowInstall, CallbackPresent;
    internal static int InstallCalls, Queries;
    internal bool IsDriverInstalled = true;
    internal static bool IsUsbipBackendAvailable {
        get { Queries++; Events.Add("probe"); var item = Availability.Dequeue(); if (item is Exception e) throw e; return (bool)item; }
    }
    internal static void InstallUsbipBackend(Action<string>? progress) {
        InstallCalls++; Events.Add("install"); CallbackPresent = progress is not null;
        progress?.Invoke("official progress\n\u5b89\u88c5");
        if (ThrowInstall) throw new IOException("fixture-install-failed");
    }
    internal void InstallDriver() => throw new InvalidOperationException("unadmitted-umdf-install");
    internal static void Reset(params object[] observations) {
        Availability = new(observations); Events.Clear(); InstallCalls=0; Queries=0; ThrowInstall=false; CallbackPresent=false;
    }
}
internal static partial class ProgramFixture {
    private static string StandaloneTestAutoInstallMarkerPath => Path.Combine(Path.GetTempPath(), "ymcc-usbip-fixture-no-marker-" + Environment.ProcessId);
}
internal static class Fixture {
    private static int Checks;
    private static readonly List<string> Failures = new();
    private static void Check(bool condition, string label) { Checks++; if (!condition) Failures.Add(label); }
    private static void Case(string label, object[] probes, bool throws, string receiptPrefix, int installs, bool diagnostic) {
        HMContext.Reset(probes); HMContext.ThrowInstall=throws;
        var sink = diagnostic ? new UsbipSetupEvidence() : null;
        int creates=0;
        int result=ProgramFixture.Execute(true,sink,() => { creates++; HMContext.Events.Add("create"); return 42; },out var receipt);
        Check(result==42 && creates==1,label+":factory-called-once-even-when-backend-warning");
        Check(receipt.StartsWith(receiptPrefix,StringComparison.Ordinal),label+":original-backend-receipt-retained");
        Check(HMContext.InstallCalls==installs,label+":no-extra-install-or-retry");
        Check(HMContext.Events.Last()=="create",label+":factory-runs-after-backend-attempt");
        Check(HMContext.CallbackPresent==(diagnostic && installs>0),label+":progress-callback-reaches-official-install-only");
        if(diagnostic) {
            Check(sink!.Stages.Count>0 && sink.Stages[0].StartsWith("backend-probe-begin:"),label+":probe-start-flushed-before-work");
            Check(sink.Progress.Count==installs,label+":official-progress-retained");
        }
        Check(HMContext.Availability.Count==0,label+":exact-probe-count");
    }
    public static int Main(string[] args) {
        var startFlags = new[]{ Environment.GetEnvironmentVariable("HIDMAESTRO_DIAG"), Environment.GetEnvironmentVariable("HIDMAESTRO_TIMING") };
        foreach(bool diagnostic in new[]{false,true}) {
            string suffix=diagnostic?"-evidence":"-legacy-null-sink";
            Case("known-available"+suffix,new object[]{true},false,"backend-ready:usbip-backend-available",0,diagnostic);
            Case("absent-install-ready"+suffix,new object[]{false,true},false,"backend-ready:usbip-backend-installed-by-sdk",1,diagnostic);
            Case("absent-install-not-ready"+suffix,new object[]{false,false},false,"backend-warning:usbip-backend-install-failed",1,diagnostic);
            Case("install-throws"+suffix,new object[]{false},true,"backend-warning:usbip-backend-install-failed:IOException:fixture-install-failed",1,diagnostic);
            Case("probe-throws"+suffix,new object[]{new IOException("fixture-probe")},false,"backend-warning:backend-check-exception:IOException",0,diagnostic);
            Case("post-install-probe-throws"+suffix,new object[]{false,new IOException("fixture-post-probe")},false,"backend-warning:usbip-backend-install-failed:IOException:fixture-post-probe",1,diagnostic);
        }
        HMContext.Reset(); var nonSd = new UsbipSetupEvidence(); int nonSdCreates=0;
        ProgramFixture.Execute(false,nonSd,()=>++nonSdCreates,out var nonSdReceipt);
        Check(nonSdCreates==1 && nonSdReceipt=="backend-ready:driver-already-installed","non-SD original ready path");
        Check(HMContext.Queries==0 && HMContext.InstallCalls==0 && nonSd.Stages.Count==0,"non-SD never touches USBIP evidence or backend");
        foreach(bool available in new[]{false,true}) {
            HMContext.Reset(available); var failure=new InvalidOperationException("factory-failed"); int attempts=0;
            if(!available) HMContext.ThrowInstall=true;
            try { ProgramFixture.Execute(true,new(),()=> { attempts++; throw failure; },out _); Check(false,"factory-failure-propagates"); }
            catch(InvalidOperationException e) { Check(ReferenceEquals(e,failure) && attempts==1,"factory-failure-retains-exact-exception-without-retry"); }
        }
        Check(startFlags.SequenceEqual(new[]{ Environment.GetEnvironmentVariable("HIDMAESTRO_DIAG"), Environment.GetEnvironmentVariable("HIDMAESTRO_TIMING") }),"no-SDK-packet-diagnostic-flags-enabled");
        Check(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HIDMaestro.Core"),"real-SDK-not-loaded");
        var result = new { pass=Failures.Count==0, checks=Checks, scenarios=15, failures=Failures, actualProductionConsumersExecuted=true, realSdkLoaded=false, driverOperations=false, normalHostStarted=false, evidenceSinkBoundary="inert callback receipt; actual sink independently exercised" };
        File.WriteAllText(args[0],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine(JsonSerializer.Serialize(result)); return Failures.Count==0?0:1;
    }
}
