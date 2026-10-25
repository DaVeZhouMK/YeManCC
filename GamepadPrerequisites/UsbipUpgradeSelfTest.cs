internal static class UsbipUpgradeSelfTest
{
    private sealed class Fake : UsbipUpgrade.Runtime
    {
        internal UsbipUpgrade.Inventory Inventory = new(new Version("0.9.8.1"), new Version("0.9.7.7"), true, true, false);
        internal readonly List<string> Calls = new();
        internal readonly List<UsbipUpgrade.Snapshot> States = new();
        public bool Administrator { get; set; } = true;
        public bool OwnerAlive { get; set; } = true;
        public bool RestartRequired { get; set; }
        public bool RebootPending { get; set; }
        internal bool FailInspect;
        internal bool Safe = true, Attempted, IsRemoved = true, Verified = true, FailInstall, ExitAfterRemoval, Resume, FailPublish;
        internal int UninstallExit;
        public UsbipUpgrade.Inventory Inspect() { Calls.Add("inspect"); if(FailInspect)throw new UnauthorizedAccessException("Access to the path is denied");return Inventory; }
        public bool SafeToChange(bool existingTransport, out string reason) { Calls.Add(existingTransport ? "safety" : "install-safety"); reason = "busy fixture"; return Safe; }
        public bool ShouldResumeInstall(UsbipUpgrade.Inventory inventory) => Resume;
        public void ConfirmCurrent(UsbipUpgrade.Inventory inventory) { }
        public bool AlreadyAttempted(UsbipUpgrade.Inventory i) => Attempted;
        public void RecordAttempt(UsbipUpgrade.Inventory i) { Calls.Add("journal"); Attempted = true; }
        public int Uninstall() { Calls.Add("uninstall"); return UninstallExit; }
        public bool Removed() { Calls.Add("removed"); if (ExitAfterRemoval) OwnerAlive = false; return IsRemoved; }
        public void Install(Action<string> progress) { Calls.Add("install"); if (FailInstall) throw new IOException("fixture installer failure"); }
        public bool Verify(UsbipUpgrade.Inventory i) { Calls.Add("verify"); return Verified; }
        public void Publish(UsbipUpgrade.Snapshot state) { if (FailPublish) throw new IOException("fixture status sink unavailable"); States.Add(state); }
    }
    internal static int WaitBudget()
    {
        var si = new System.Diagnostics.ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true };
        if (string.Equals(Path.GetFileNameWithoutExtension(Environment.ProcessPath),"dotnet",StringComparison.OrdinalIgnoreCase)) si.ArgumentList.Add(typeof(UsbipUpgradeSelfTest).Assembly.Location);
        si.ArgumentList.Add("--selftest-usbip-owned-wait-child");
        using var child = System.Diagnostics.Process.Start(si) ?? throw new IOException("owned test child did not start");
        try {
            var ready = child.StandardOutput.ReadLineAsync();
            if (!ready.Wait(3000) || ready.Result!="READY") throw new Exception("owned child readiness");
            var clock=System.Diagnostics.Stopwatch.StartNew();bool timedOut=false;
            try { UsbipUpgrade.WindowsRuntime.WaitForUninstall(child, 40); } catch(TimeoutException) { timedOut=true; }
            if (!timedOut || clock.ElapsedMilliseconds>2000 || child.HasExited) throw new Exception("uninstall observer must time out without killing its process");
            child.StandardInput.WriteLine("release");child.StandardInput.Flush();
            if (!child.WaitForExit(3000)) throw new Exception("owned child cleanup did not finish");
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new {status="PASS",checks=3,boundedTimeout=true,systemInstallerKilled=false,ownedChildExited=true,hardwareIo=false}));return 0;
        } finally { if (!child.HasExited) { child.Kill(true);child.WaitForExit(2000); } }
    }
    internal static int Run()
    {
        int checks = 0;
        void Need(bool ok, string name) { if (!ok) throw new Exception(name); ++checks; }
        UsbipUpgrade.Snapshot Run(Fake f, bool prepare = true) => UsbipUpgrade.Execute(f, prepare);
        {var f = new Fake { FailInspect = true };var observed = UsbipUpgrade.Observe(f);Need(observed.phase=="unverified" && observed.detail.Contains("Access to the path is denied"), "read-only ACL error is unknown current state, not another failed upgrade");Need(f.Calls.SequenceEqual(new[]{"inspect"}) && f.States.Count==0 && !f.Attempted,"read-only failure never changes maintenance state");}
        foreach (var (installed, current, reboot, expected) in new[] {
            ("0.9.8.1", true, false, "current"), ("0.9.9.0", false, false, "newer-preserved"),
            ("0.9.7.7", false, false, "update-found"), ("0.9.8.1", false, false, "unverified"),
            ("0.9.8.1", true, true, "reboot-required") }) {
            var f = new Fake { Inventory = new(new("0.9.8.1"), new(installed), true, true, current), RebootPending = reboot };
            var observed = UsbipUpgrade.Observe(f);
            Need(observed.phase == expected, "read-only refresh keeps current inventory and mandatory reboot separate");
            Need(f.Calls.SequenceEqual(new[] { "inspect" }) && f.States.Count == 0 && !f.Attempted,
                "page refresh never publishes a startup stage, journals, uninstalls, installs or confirms a transaction");
        }
        foreach (string version in new[] { "0.9.8.1", "0.9.9.0", "1.0.0.0" }) {
            var f = new Fake { Inventory = new(new("0.9.8.1"), new(version), true, true, false) };
            Need(Run(f).phase == (version == "0.9.8.1" ? "current" : "newer-preserved"), "same/newer preserved");
            Need(f.Calls.SequenceEqual(new[] { "inspect" }), "same/newer never uninstalls or enters device safety");
        }
        foreach (var inventory in new[] { new UsbipUpgrade.Inventory(new("0.9.8.1"), null, false, true, false),
            new(new("0.9.8.1"), new("0.9.7.7"), true, false, false),
            new(new("0.9.8.1"), null, true, true, false) }) {
            var f = new Fake { Inventory = inventory }; Run(f);
            Need(!f.Calls.Contains("uninstall") && !f.Calls.Contains("install"), "missing/unknown never triggers uninstall");
        }
        { var f = new Fake(); Need(Run(f, false).phase == "update-found", "check only reports new version"); Need(!f.Attempted, "check does not journal or mutate"); }
        { var f = new Fake { Administrator = false }; Need(Run(f).phase == "admin-required", "administrator guidance"); Need(!f.Attempted, "no UAC or write without admin"); }
        { var f = new Fake { Safe = false }; Need(Run(f).phase == "busy", "live imports/consumer postpones"); Need(!f.Attempted, "busy leaves original untouched"); }
        { var f = new Fake { OwnerAlive = false }; Need(Run(f).phase == "failed", "owner exit stops maintenance"); Need(!f.Attempted, "owner exit never journals uninstall"); }
        { var f = new Fake { Attempted = true }; Need(Run(f).phase == "failed", "interrupted old attempt retained"); Need(!f.Calls.Contains("uninstall"), "no repeated uninstall"); }
        { var f = new Fake(); Need(Run(f).phase == "complete", "upgrade reaches observed endpoint");
            Need(f.Calls.SequenceEqual(new[] { "inspect", "safety", "journal", "uninstall", "removed", "install", "verify" }), "journal -> uninstall -> observed absence -> SDK install -> verification");
            Need(f.States.Select(s => s.phase).SequenceEqual(new[] { "checking", "update-found", "removing", "installing", "verifying", "complete" }), "truthful progress boundaries");
            Need(f.States.Select(s => s.sequence).SequenceEqual(Enumerable.Range(1, f.States.Count)), "monotonic status");
            Need(f.States.Single(s => s.phase == "installing").installedVersion == "", "removed version is not advertised as installed");
            Need(f.States.Last().installedVersion == "0.9.8.1", "final verified version is the actual target");
            Run(f); Need(f.Calls.Count(c => c == "uninstall") == 1, "second startup cannot replay uninstall"); }
        foreach (int code in new[] { 5, 1603, 3010, 1641, -1 }) {
            var f = new Fake { UninstallExit = code }; var result = Run(f);
            Need(result.phase == "failed", "all nonzero uninstaller exits are failures");
            Need(!f.Calls.Contains("install"), "uninstall failure/reboot never installs concurrently");
        }
        { var f = new Fake { RebootPending = true, Inventory = new(new("0.9.8.1"), new("0.9.8.1"), true, true, true) }; Need(Run(f).phase == "reboot-required", "same installed version cannot erase mandatory reboot"); Need(!f.Calls.Contains("uninstall"), "reboot fence never repeats upgrade"); }
        { var f = new Fake { RestartRequired = true }; Need(Run(f).phase == "reboot-required", "official script reboot requirement survives verified installation"); Need(!f.Calls.Contains("install") && !f.Calls.Contains("verify"), "required reboot stops before replacement installation"); }
        { var f = new Fake { IsRemoved = false }; Need(Run(f).phase == "reboot-required", "pending PnP removal needs reboot"); Need(!f.Calls.Contains("install"), "cannot install before absence"); }
        { var f = new Fake { ExitAfterRemoval = true }; Need(Run(f).phase == "failed", "owner exits after uninstall"); Need(!f.Calls.Contains("install"), "owner exit does not start SDK"); }
        { var f = new Fake { FailInstall = true }; Need(Run(f).phase == "failed", "install exception shown"); Need(!f.Calls.Contains("verify"), "exception not success"); }
        { var f = new Fake { Verified = false }; Need(Run(f).phase == "reboot-required", "SDK return is not actual readiness"); }
        foreach (string text in new[] { "cmd /c unins.exe", @"C:\path\unins000.exe", "\"C:\\path\\unins000.exe\" /unexpected", "\"bad" })
            Need(UsbipUpgrade.WindowsRuntime.UninstallerPath(text) == "", "no shell or registry arguments accepted");
        Need(UsbipUpgrade.WindowsRuntime.UninstallerPath("\"C:\\path with spaces\\unins000.exe\"") == @"C:\path with spaces\unins000.exe", "quoted exact path preserved");
        Need(YeManCC.UsbipMaintenanceState.RequiresReboot(1000, 1000), "same boot remains fenced");
        Need(YeManCC.UsbipMaintenanceState.RequiresReboot(1000, 999999), "later uptime cannot erase interrupted state");
        Need(!YeManCC.UsbipMaintenanceState.RequiresReboot(1000, 10), "strictly lower uptime proves next boot");
        Need(YeManCC.UsbipMaintenanceState.RequiresReboot(-1, 10), "invalid boot state stays fenced");
        var previousBoot = Guid.NewGuid(); var nextBoot = Guid.NewGuid();
        Need(YeManCC.UsbipMaintenanceState.RequiresReboot(previousBoot, previousBoot), "same actual Windows boot remains fenced");
        Need(!YeManCC.UsbipMaintenanceState.RequiresReboot(previousBoot, nextBoot), "new actual Windows boot clears fence even with longer uptime");
        Need(YeManCC.UsbipMaintenanceState.RequiresReboot((Guid?)null, nextBoot), "missing boot record stays unverified");
        Need(YeManCC.UsbipMaintenanceState.RequiresReboot(previousBoot, (Guid?)null), "failed boot query cannot clear fence");
        Need(YeManCC.UsbipMaintenanceState.RequiresReboot(Guid.Empty, nextBoot), "empty boot identity rejected");

        using (var attempt = System.Text.Json.JsonDocument.Parse(System.Text.Json.JsonSerializer.Serialize(new { coreSha256="core",installedVersion="0.9.7.7",targetVersion="0.9.8.1",mutationStarted=true,bootIdentifier=previousBoot.ToString("D") }))) {
            Need(UsbipUpgrade.WindowsRuntime.AttemptStillBlocks(attempt.RootElement,"core",new("0.9.7.7"),new("0.9.8.1"),previousBoot), "reopening app cannot renew uninstall budget");
            Need(!UsbipUpgrade.WindowsRuntime.AttemptStillBlocks(attempt.RootElement,"core",new("0.9.7.7"),new("0.9.8.1"),nextBoot), "verified reboot permits one fresh recovery attempt");
            Need(UsbipUpgrade.WindowsRuntime.AttemptStillBlocks(attempt.RootElement,"core",new("0.9.7.7"),new("0.9.8.1"),null), "unknown boot cannot renew retry budget");
        }
        { var f = new Fake { Resume = true, RestartRequired = true, Inventory = new(new("0.9.8.1"), null, false, true, false) };
            Need(Run(f).phase == "complete", "verified new boot continues HIDM installation automatically");
            Need(!f.Calls.Contains("uninstall") && f.Calls.Contains("install-safety") && f.Calls.Contains("verify"), "continuation never uninstalls an absent product");
            Need(f.States.Last().installedVersion == "0.9.8.1", "continuation reports verified target version");
        }
        { var f = new Fake { Resume = true, FailInstall = true, Inventory = new(new("0.9.8.1"), null, false, true, false) };
            Need(Run(f).phase == "failed", "continuation installer failure is actionable"); Run(f);
            Need(f.Calls.Count(c=>c=="install") == 1, "same-boot continuation failure cannot loop installs");
        }
        foreach (bool admin in new[]{true,false}) {
            var f = new Fake { Resume = true, Administrator = admin, Safe = false, Inventory = new(new("0.9.8.1"), null, false, true, false) };
            Need(Run(f).phase == (admin ? "busy" : "admin-required"), "continuation respects permissions and active consumers");
            Need(!f.Calls.Contains("install") && !f.Calls.Contains("uninstall"), "blocked continuation performs no device mutation");
        }
        { var f = new Fake { Resume = true, Inventory = new(new("0.9.8.1"), null, false, true, false) }; Need(Run(f,false).phase == "update-found" && !f.Attempted, "check-only never resumes installation"); }
        { var f = new Fake { Resume = true, IsRemoved = false, Inventory = new(new("0.9.8.1"), null, false, true, false) }; Need(Run(f).phase == "unverified", "post-reboot orphan packages do not start an installer"); Need(!f.Calls.Contains("install") && !f.Attempted, "no destructive retry for post-reboot unknown state"); }
        { var f = new Fake { FailPublish = true }; Need(Run(f).phase == "failed", "broken progress sink returns a bounded failure"); Need(!f.Calls.Contains("uninstall"), "unobservable initial state never authorizes uninstall"); }
        using (var continuation = System.Text.Json.JsonDocument.Parse(System.Text.Json.JsonSerializer.Serialize(new { operation="usbip-uninstall",coreSha256="core",targetVersion="0.9.8.1",bootIdentifier=previousBoot.ToString("D") }))) {
            Need(UsbipUpgrade.WindowsRuntime.ContinuationMatches(continuation.RootElement,"core",new("0.9.8.1"),nextBoot), "exact target and actual new boot admit continuation");
            Need(!UsbipUpgrade.WindowsRuntime.ContinuationMatches(continuation.RootElement,"core",new("0.9.8.1"),previousBoot), "same boot cannot consume continuation");
            Need(!UsbipUpgrade.WindowsRuntime.ContinuationMatches(continuation.RootElement,"other-core",new("0.9.8.1"),nextBoot), "changed SDK cannot replay old continuation");
            Need(!UsbipUpgrade.WindowsRuntime.ContinuationMatches(continuation.RootElement,"core",new("0.9.9.0"),nextBoot), "changed target cannot replay old continuation");
            Need(!UsbipUpgrade.WindowsRuntime.ContinuationMatches(continuation.RootElement,"core",new("0.9.8.1"),null), "unknown boot cannot resume installation");
        }
        Need(YeManCC.UsbipMaintenanceState.RequiresReboot(previousBoot,previousBoot,8,8),"same loader identity remains fenced");
        Need(!YeManCC.UsbipMaintenanceState.RequiresReboot(previousBoot,previousBoot,8,9),"new loader sequence proves restart even with same GUID");
        Need(YeManCC.UsbipMaintenanceState.RequiresReboot(previousBoot,nextBoot,8,null),"unknown loader sequence cannot clear new fence");
        Need(YeManCC.UsbipMaintenanceState.CurrentBootSequence is not null,"real read-only loader sequence is available");
        Need(YeManCC.UsbipMaintenanceState.RequiresLoaderSequence(System.Runtime.InteropServices.Architecture.X64,System.Runtime.InteropServices.Architecture.X64),"reviewed x64 layout requires sequence before mutation");
        Need(!YeManCC.UsbipMaintenanceState.RequiresLoaderSequence(System.Runtime.InteropServices.Architecture.X64,System.Runtime.InteropServices.Architecture.Arm64),"emulated x64 on ARM retains original boot evidence path");
        using (var loaderRecord=System.Text.Json.JsonDocument.Parse(System.Text.Json.JsonSerializer.Serialize(new {operation="usbip-uninstall",coreSha256="core",installedVersion="0.9.7.7",targetVersion="0.9.8.1",mutationStarted=true,bootIdentifier=previousBoot.ToString("D"),bootSequence=8}))) {
            var record=loaderRecord.RootElement;
            Need(YeManCC.UsbipMaintenanceState.RecordRequiresReboot(record,previousBoot,8,999999),"long same boot uptime stays fenced");
            Need(!YeManCC.UsbipMaintenanceState.RecordRequiresReboot(record,previousBoot,9,999999),"longer uptime after real sequence change recovers");
            Need(UsbipUpgrade.WindowsRuntime.ContinuationMatches(record,"core",new("0.9.8.1"),previousBoot,9),"matching continuation uses composite boot evidence");
            Need(!UsbipUpgrade.WindowsRuntime.ContinuationMatches(record,"core",new("0.9.8.1"),previousBoot,8),"same boot cannot consume continuation");
            Need(!UsbipUpgrade.WindowsRuntime.AttemptStillBlocks(record,"core",new("0.9.7.7"),new("0.9.8.1"),previousBoot,9),"attempt budget renews only on proved new loader identity");
            Need(UsbipUpgrade.WindowsRuntime.AttemptStillBlocks(record,"core",new("0.9.7.7"),new("0.9.8.1"),previousBoot,8),"same boot attempt cannot repeat");
        }
        // Same canonical suite, new boot-handoff boundaries; no extra fixture.
        var volatileBootA = "windows-volatile-v2|" + previousBoot.ToString("B").ToUpperInvariant();
        var volatileBootB = "windows-volatile-v2|" + nextBoot.ToString("B").ToUpperInvariant();
        using (var sessionRecord = System.Text.Json.JsonDocument.Parse(System.Text.Json.JsonSerializer.Serialize(new {
            operation="usbip-uninstall", coreSha256="core", installedVersion="0.9.7.7", targetVersion="0.9.8.1", mutationStarted=true,
            bootIdentifier=previousBoot.ToString("D"), bootSequence=8, bootSessionIdentity=volatileBootA }))) {
            var record = sessionRecord.RootElement;
            Need(YeManCC.UsbipMaintenanceState.RecordRequiresReboot(record,previousBoot,8,999999,volatileBootA), "same volatile session keeps uninstall fence despite app restart");
            Need(!YeManCC.UsbipMaintenanceState.RecordRequiresReboot(record,previousBoot,8,999999,volatileBootB), "different volatile session resumes even when loader tuple repeats");
            Need(YeManCC.UsbipMaintenanceState.RecordRequiresReboot(record,nextBoot,9,1,null), "missing current volatile authority cannot borrow different loader tuple");
            Need(UsbipUpgrade.WindowsRuntime.ContinuationMatches(record,"core",new("0.9.8.1"),previousBoot,8,volatileBootB), "post-uninstall matching continuation follows shared boot authority");
            Need(!UsbipUpgrade.WindowsRuntime.ContinuationMatches(record,"other",new("0.9.8.1"),previousBoot,8,volatileBootB), "new boot does not forgive wrong SDK ownership");
            Need(!UsbipUpgrade.WindowsRuntime.AttemptStillBlocks(record,"core",new("0.9.7.7"),new("0.9.8.1"),previousBoot,8,volatileBootB), "same version pair may recover once on proved new volatile boot");
            Need(UsbipUpgrade.WindowsRuntime.AttemptStillBlocks(record,"core",new("0.9.7.7"),new("0.9.8.1"),nextBoot,9,volatileBootA), "new loader tuple cannot refund same volatile boot attempt");
        }
        using (var malformed = System.Text.Json.JsonDocument.Parse("{\"bootSessionIdentity\":\"invalid\",\"uptimeMs\":999999}"))
            Need(YeManCC.UsbipMaintenanceState.RecordRequiresReboot(malformed.RootElement,nextBoot,9,1,volatileBootB), "malformed V2 authority stays fenced without legacy downgrade");
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new { status = "PASS", checks, hardwareIo = false, actualConsumer = "UsbipUpgrade.Execute", physicalUpgradeVerified = false }));
        return 0;
    }
}
