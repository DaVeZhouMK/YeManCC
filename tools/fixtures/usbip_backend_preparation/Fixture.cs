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

// Inert authority ports: only private fixture files are read/written. Never
// compile the production registry/ACL provider into this fixture process.
namespace YeManCC {
    internal sealed class ModuleMaintenanceGate : IDisposable {
        internal static ModuleMaintenanceGate? TryEnter() => new();
        public void Dispose() { }
    }
    internal static partial class UsbipMaintenanceState {
        internal static bool Pending => false;
        internal static string DirectoryPath { get { FixtureBudget.AuthorityReads++; if(FixtureBudget.ThrowAuthority) throw new IOException("fixture-authority"); return FixtureBudget.Directory; } }
        internal static string? CurrentBootSessionIdentity { get { FixtureBudget.AuthorityReads++; return FixtureBudget.Boot; } }
        internal static void PrepareProtectedDirectory() {
            if(FixtureBudget.FailWrite) throw new UnauthorizedAccessException("fixture-protected-write-denied");
            System.IO.Directory.CreateDirectory(FixtureBudget.Directory);
        }
    }
}
internal static class FixtureBudget {
    internal const string BootA="windows-volatile-v2|{11111111-1111-1111-1111-111111111111}";
    internal const string BootB="windows-volatile-v2|{22222222-2222-2222-2222-222222222222}";
    internal static string Base="", Directory="";
    internal static string? Boot=BootA;
    internal static int AuthorityReads, Sequence;
    internal static bool FailWrite, ThrowAuthority;
    internal static string Marker => Path.Combine(Directory,"host-backend-attempt.json");
    internal static void Fresh() {
        Directory=Path.Combine(Base,"case-"+(++Sequence)); Boot=BootA;
        AuthorityReads=0; FailWrite=false; ThrowAuthority=false;
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
        FixtureBudget.Fresh();
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

    private static void Denied(string label, params object[] probes) {
        HMContext.Reset(probes); int creates=0;
        try { ProgramFixture.Execute(true,new(),()=>++creates,out _); Check(false,label+":budget-refusal-required"); }
        catch(YeManInputHost.UsbipBackendBudgetRejectedException ex) {
            Check(ex.Message.StartsWith("usbip-host-backend-budget-",StringComparison.Ordinal),label+":specific-budget-refusal");
        }
        catch(Exception ex) { Check(false,label+":wrong-refusal:"+ex.GetType().Name); }
        Check(creates==0 && HMContext.InstallCalls==0,label+":budget-rejection-blocks-factory-and-install");
    }
    private static void PersistentBudgetCases() {
        foreach(bool throwingProbe in new[]{false,true}) {
            FixtureBudget.Fresh();
            HMContext.Reset(throwingProbe ? new IOException("first-probe") : (object)false);
            HMContext.ThrowInstall=true; int creates=0;
            ProgramFixture.Execute(true,new(),()=>++creates,out var receipt);
            Check(creates==1 && receipt.StartsWith("backend-warning:",StringComparison.Ordinal),"first-advisory-failure-still-allows-factory");
            Check(File.Exists(FixtureBudget.Marker),throwingProbe?"probe-exception-reserves-before-factory":"missing-backend-reserves-before-install");
            string saved=File.Exists(FixtureBudget.Marker)?File.ReadAllText(FixtureBudget.Marker):"";
            // New context/prepare simulates a fresh Host session; only the file
            // remains shared, with no static per-process reservation allowance.
            Denied("second-prepare-cross-session",false,false);
            Denied("second-prepare-throwing-probe",new IOException("next-probe"));
            Check(File.Exists(FixtureBudget.Marker) && File.ReadAllText(FixtureBudget.Marker)==saved,"same-boot-denial-preserves-original-record");
            FixtureBudget.Boot=FixtureBudget.BootB;
            HMContext.Reset(false); HMContext.ThrowInstall=true; creates=0;
            ProgramFixture.Execute(true,new(),()=>++creates,out _);
            Check(creates==1 && HMContext.InstallCalls==1,"actual-new-v2-boot-renews-file-budget");
        }
        FixtureBudget.Fresh();
        System.IO.Directory.CreateDirectory(FixtureBudget.Directory);
        File.WriteAllText(FixtureBudget.Marker,"corrupt-marker");
        FixtureBudget.ThrowAuthority=true; HMContext.Reset(true); int healthyCreates=0;
        ProgramFixture.Execute(true,new(),()=>++healthyCreates,out _);
        Check(healthyCreates==1 && FixtureBudget.AuthorityReads==0 && File.ReadAllText(FixtureBudget.Marker)=="corrupt-marker","healthy-backend-zero-budget-IO-even-with-bad-marker");
        foreach(string? boot in new string?[]{null,"unknown","{33333333-3333-3333-3333-333333333333}"}) {
            FixtureBudget.Fresh(); FixtureBudget.Boot=boot;
            Denied("missing-v2-authority",false,false);
            Denied("missing-v2-authority-probe-throws",new IOException("probe"));
        }
        foreach(string record in new[]{"", "{", new string('x',4097), "{}",
            "{\"schemaVersion\":1,\"operation\":\"usbip-install\"}",
            JsonSerializer.Serialize(new {schemaVersion=1,operation="usbip-host-backend-attempt",bootSessionIdentity="unknown"}),
            JsonSerializer.Serialize(new {schemaVersion=2,operation="usbip-host-backend-attempt",bootSessionIdentity=FixtureBudget.BootA}),
            "{\"schemaVersion\":99,\"schemaVersion\":1,\"operation\":\"usbip-host-backend-attempt\",\"bootSessionIdentity\":\""+FixtureBudget.BootA+"\"}",
            "{\"schemaVersion\":1,\"operation\":\"usbip-host-backend-attempt\",\"bootSessionIdentity\":\""+FixtureBudget.BootB+"\",\"bootSessionIdentity\":\""+FixtureBudget.BootA+"\"}",
            "{\"schemaVersion\":1,\"operation\":\"usbip-host-backend-attempt\",\"bootSessionIdentity\":\""+FixtureBudget.BootA+"\",\"unknown\":{\"x\":1,\"x\":2}}"}) {
            FixtureBudget.Fresh(); FixtureBudget.Boot=FixtureBudget.BootB;
            System.IO.Directory.CreateDirectory(FixtureBudget.Directory); File.WriteAllText(FixtureBudget.Marker,record);
            Denied("corrupt-or-unknown-marker",false,false);
            Check(File.ReadAllText(FixtureBudget.Marker)==record,"invalid-marker-not-overwritten-on-new-boot");
        }
        FixtureBudget.Fresh(); FixtureBudget.FailWrite=true;
        Denied("protected-write-denied",false,false);
        Denied("protected-write-denied-probe-throws",new IOException("probe"));
        FixtureBudget.Fresh(); System.IO.Directory.CreateDirectory(FixtureBudget.Marker);
        Denied("marker-unreadable",false,false);
        // Exercise the production file overload independently of the host stub.
        FixtureBudget.Fresh(); string path=FixtureBudget.Marker; int prepared=0;
        Action prepare=()=> { prepared++; System.IO.Directory.CreateDirectory(FixtureBudget.Directory); };
        YeManInputHost.UsbipHostBackendBudget.Reserve(path,FixtureBudget.BootA,prepare);
        string original=File.ReadAllText(path);
        try { YeManInputHost.UsbipHostBackendBudget.Reserve(path,FixtureBudget.BootA,prepare); Check(false,"file-port-same-boot-rejected"); }
        catch(YeManInputHost.UsbipBackendBudgetRejectedException) { Check(prepared==1 && File.ReadAllText(path)==original,"file-port-same-boot-preserves-marker"); }
        YeManInputHost.UsbipHostBackendBudget.Reserve(path,FixtureBudget.BootB,prepare);
        using(var doc=JsonDocument.Parse(File.ReadAllText(path)))
            Check(doc.RootElement.GetProperty("bootSessionIdentity").GetString()==FixtureBudget.BootB && !doc.RootElement.TryGetProperty("complete",out _),"file-port-new-v2-only-not-product-complete");
        Check(!System.IO.Directory.EnumerateFiles(FixtureBudget.Directory,"*.tmp").Any(),"file-port-no-owned-temp-residue");
    }

    private static void CrossProcessBudgetCases() {
        FixtureBudget.Fresh(); string path=FixtureBudget.Marker;
        (int Code,int Pid,string Text) Child(string boot) {
            var start=new System.Diagnostics.ProcessStartInfo(Environment.ProcessPath!) {
                UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true };
            if(string.Equals(Path.GetFileNameWithoutExtension(Environment.ProcessPath),"dotnet",StringComparison.OrdinalIgnoreCase))
                start.ArgumentList.Add(typeof(Fixture).Assembly.Location);
            start.ArgumentList.Add("--budget-child");start.ArgumentList.Add(path);start.ArgumentList.Add(boot);
            using var child=System.Diagnostics.Process.Start(start)??throw new IOException("owned budget child did not start");
            var output=child.StandardOutput.ReadToEndAsync();var error=child.StandardError.ReadToEndAsync();
            if(!child.WaitForExit(5000)) { child.Kill(entireProcessTree:true);child.WaitForExit(2000);throw new TimeoutException("owned budget child"); }
            return(child.ExitCode,child.Id,output.GetAwaiter().GetResult()+error.GetAwaiter().GetResult());
        }
        var first=Child(FixtureBudget.BootA);Check(first.Code==0,"independent-child-first-reservation");
        if(!File.Exists(path)){Check(false,"independent-child-marker-required");return;}
        byte[] original=File.ReadAllBytes(path);
        using(var row=JsonDocument.Parse(original))Check(row.RootElement.GetProperty("hostPid").GetInt32()==first.Pid && first.Pid!=Environment.ProcessId,"marker-belongs-to-real-owned-child");
        var same=Child(FixtureBudget.BootA);
        Check(same.Code==42 && same.Text.Contains("exhausted-this-boot",StringComparison.Ordinal),"second-independent-process-cannot-refund-budget");
        Check(File.ReadAllBytes(path).SequenceEqual(original),"cross-process-refusal-preserves-first-reservation");
        var unknown=Child("unknown");Check(unknown.Code==42 && File.ReadAllBytes(path).SequenceEqual(original),"independent-process-unknown-boot-never-renews");
        var next=Child(FixtureBudget.BootB);Check(next.Code==0,"independent-process-new-boot-can-reserve");
        using(var row=JsonDocument.Parse(File.ReadAllBytes(path)))Check(row.RootElement.GetProperty("hostPid").GetInt32()==next.Pid && row.RootElement.GetProperty("bootSessionIdentity").GetString()==FixtureBudget.BootB,"new-owned-child-identity-and-declared-boot-recorded");
    }
    public static int Main(string[] args) {
        if(args.Length==3 && args[0]=="--budget-child") {
            try {
                YeManInputHost.UsbipHostBackendBudget.Reserve(args[1],args[2],()=>System.IO.Directory.CreateDirectory(Path.GetDirectoryName(args[1])!));
                Console.WriteLine("RESERVED");return 0;
            } catch(YeManInputHost.UsbipBackendBudgetRejectedException ex) { Console.WriteLine(ex.Message);return 42; }
        }
        FixtureBudget.Base=Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[0]))!,"budget-files");
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
        FixtureBudget.Fresh(); FixtureBudget.ThrowAuthority=true;
        HMContext.Reset(); var nonSd = new UsbipSetupEvidence(); int nonSdCreates=0;
        ProgramFixture.Execute(false,nonSd,()=>++nonSdCreates,out var nonSdReceipt);
        Check(nonSdCreates==1 && nonSdReceipt=="backend-ready:driver-already-installed","non-SD original ready path");
        Check(HMContext.Queries==0 && HMContext.InstallCalls==0 && nonSd.Stages.Count==0,"non-SD never touches USBIP evidence or backend");
        Check(FixtureBudget.AuthorityReads==0,"non-SD-no-budget-IO");
        foreach(bool available in new[]{false,true}) {
            FixtureBudget.Fresh();
            HMContext.Reset(available); var failure=new InvalidOperationException("factory-failed"); int attempts=0;
            if(!available) HMContext.ThrowInstall=true;
            try { ProgramFixture.Execute(true,new(),()=> { attempts++; throw failure; },out _); Check(false,"factory-failure-propagates"); }
            catch(InvalidOperationException e) { Check(ReferenceEquals(e,failure) && attempts==1,"factory-failure-retains-exact-exception-without-retry"); }
        }
        PersistentBudgetCases();
        CrossProcessBudgetCases();
        Check(startFlags.SequenceEqual(new[]{ Environment.GetEnvironmentVariable("HIDMAESTRO_DIAG"), Environment.GetEnvironmentVariable("HIDMAESTRO_TIMING") }),"no-SDK-packet-diagnostic-flags-enabled");
        Check(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HIDMaestro.Core"),"real-SDK-not-loaded");
        var result = new { pass=Failures.Count==0, checks=Checks, scenarios="original-backend-plus-host-budget-file-and-independent-process-ports", independentBudgetChildren=4, failures=Failures, actualProductionConsumersExecuted=true, realSdkLoaded=false, driverOperations=false, normalHostStarted=false, evidenceSinkBoundary="inert callback receipt; actual sink independently exercised" };
        File.WriteAllText(args[0],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine(JsonSerializer.Serialize(result)); return Failures.Count==0?0:1;
    }
}
