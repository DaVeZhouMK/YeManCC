internal static class UsbipUpgradeSelfTest
{
    private sealed class Fake : UsbipUpgrade.Runtime
    {
        internal UsbipUpgrade.Inventory Inventory = new(new Version("0.9.8.1"), new Version("0.9.7.7"), true, true, false);
        internal readonly List<string> Calls = new();
        internal readonly List<UsbipUpgrade.Snapshot> States = new();
        internal readonly List<string> InstallMessages = new();
        internal Action<string>? InstallCallback;
        internal UsbipUpgrade.Inventory? InventoryAfterVerify, VerifyInput;
        internal Action<UsbipUpgrade.Snapshot>? OnPublish;
        internal Action<UsbipUpgrade.Inventory>? OnVerify, OnInspect;
        internal bool InstallReturned, FailProgressPublish;
        internal int ProgressCallbacks, ProgressPublishFailures;
        public bool Administrator { get; set; } = true;
        public bool OwnerAlive { get; set; } = true;
        public bool RestartRequired { get; set; }
        public bool RebootPending { get; set; }
        internal bool FailInspect;
        internal bool Safe = true, Attempted, IsRemoved = true, Verified = true, FailInstall, ExitAfterRemoval, Resume, FailPublish;
        internal int UninstallExit;
        public UsbipUpgrade.Inventory Inspect() {
            Calls.Add("inspect");
            if (FailInspect) throw new UnauthorizedAccessException("Access to the path is denied");
            OnInspect?.Invoke(Inventory);
            return Inventory;
        }
        public bool SafeToChange(bool existingTransport, out string reason) { Calls.Add(existingTransport ? "safety" : "install-safety"); reason = "busy fixture"; return Safe; }
        public bool ShouldResumeInstall(UsbipUpgrade.Inventory inventory) => Resume;
        internal Action<UsbipUpgrade.Inventory>? OnConfirmCurrent;
        public void ConfirmCurrent(UsbipUpgrade.Inventory inventory) => OnConfirmCurrent?.Invoke(inventory);
        public bool AlreadyAttempted(UsbipUpgrade.Inventory i) => Attempted;
        public void RecordAttempt(UsbipUpgrade.Inventory i) { Calls.Add("journal"); Attempted = true; }
        public int Uninstall() { Calls.Add("uninstall"); return UninstallExit; }
        public bool Removed() { Calls.Add("removed"); if (ExitAfterRemoval) OwnerAlive = false; return IsRemoved; }
        public void Install(Action<string> progress) {
            Calls.Add("install"); InstallCallback = progress;
            foreach (string message in InstallMessages) { ++ProgressCallbacks; progress(message); }
            if (FailInstall) throw new IOException("fixture installer failure");
            InstallReturned = true;
        }
        public bool Verify(UsbipUpgrade.Inventory i) {
            Calls.Add("verify"); VerifyInput = i;
            if (Verified) Inventory = InventoryAfterVerify ?? (Inventory with {
                Installed = Inventory.Target, Present = true, Verified = true, DriverCurrent = true });
            OnVerify?.Invoke(Inventory);
            return Verified;
        }
        public void Publish(UsbipUpgrade.Snapshot state) {
            if (FailPublish) throw new IOException("fixture status sink unavailable");
            if (FailProgressPublish && state.phase == "installing" && InstallCallback is not null && !InstallReturned) {
                ++ProgressPublishFailures;
                throw new IOException("fixture progress sink unavailable during SDK installation");
            }
            States.Add(state); OnPublish?.Invoke(state);
        }
    }
    // A declared OS/installer fault model. The production Execute, continuation
    // and attempt/boot predicates make decisions; this port only supplies effects
    // and preserves records across application sessions and simulated reboots.
    private sealed class BusModel : UsbipUpgrade.Runtime
    {
        internal const string Core = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        internal readonly Version Target = new("0.9.8.1");
        internal UsbipUpgrade.Inventory Inventory;
        internal readonly List<UsbipUpgrade.Snapshot> States = new();
        internal readonly List<(int Boot, string Operation)> SdkCalls = new();
        internal readonly Dictionary<string, int> Steps = new();
        internal string? Fault, Attempt, Pending, Previous;
        internal string Effect = "complete";
        internal bool Busy, Residue, BackendAvailable;
        internal int Boot = 1, Session = 1, UninstallExit, Confirmations;
        private UsbipUpgrade.Inventory? planned;
        public bool Administrator { get; set; } = true;
        public bool OwnerAlive { get; set; } = true;
        public bool RestartRequired => true; // actual approved Windows uninstaller
        internal string BootSession => YeManCC.UsbipMaintenanceState.BootSessionPrefix + $"{{00000000-0000-4000-8000-{Boot:D12}}}";
        private static readonly Guid Loader = new("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
        internal BusModel(bool legacy = false) => Inventory = legacy
            ? new(Target, new("0.9.7.7"), true, true, false)
            : new(Target, null, false, true, false);
        private void Step(string label)
        {
            int n = Steps.TryGetValue(label, out int previous) ? previous + 1 : 1;
            Steps[label] = n;
            if (Fault == label + "#" + n) { Fault = null; throw new IOException("injected " + label); }
        }
        private string Record(string operation, bool mutationStarted) {
            var text = System.Text.Json.JsonSerializer.Serialize(new {
                schemaVersion = 3, operation, session = "model-session-" + Session, coreSha256 = Core,
                installedVersion = planned?.Installed?.ToString() ?? "absent", targetVersion = Target.ToString(),
                bootIdentifier = Loader.ToString("D"), bootSequence = 7, bootSessionIdentity = BootSession,
                uptimeMs = 100, mutationStarted });
            // The actual WriteAttempt producer has no operation field. Do not
            // invent a third terminal operation only in the fault model.
            if (operation == "attempt") { var node = System.Text.Json.Nodes.JsonNode.Parse(text)!.AsObject(); node.Remove("operation"); return node.ToJsonString(); }
            return text;
        }
        public bool RebootPending {
            get {
                if (Pending is null) return false;
                try { using var doc = System.Text.Json.JsonDocument.Parse(Pending);
                    return YeManCC.UsbipMaintenanceState.RecordRequiresReboot(doc.RootElement, Loader, 7, 100, BootSession);
                } catch { return true; }
            }
        }
        public UsbipUpgrade.Inventory Inspect() { Step("inspect"); return Inventory; }
        public bool SafeToChange(bool existingTransport, out string reason) {
            Step("safety"); reason = "model active consumer"; return !Busy && !RebootPending;
        }
        public bool ShouldResumeInstall(UsbipUpgrade.Inventory inventory) {
            if (Previous is null) return false;
            try { using var doc = System.Text.Json.JsonDocument.Parse(Previous);
                return UsbipUpgrade.WindowsRuntime.ContinuationMatches(doc.RootElement, Core, Target, Loader, 7, BootSession);
            } catch { return false; }
        }
        public bool AlreadyAttempted(UsbipUpgrade.Inventory inventory) {
            Step("attempt-read"); if (Attempt is null) return false;
            try { using var doc = System.Text.Json.JsonDocument.Parse(Attempt);
                return UsbipUpgrade.WindowsRuntime.AttemptStillBlocks(doc.RootElement, Core, inventory.Installed, Target, Loader, 7, BootSession);
            } catch { return true; }
        }
        public void RecordAttempt(UsbipUpgrade.Inventory inventory) {
            Step("journal-before"); planned = inventory; Attempt = Record("attempt", false); Step("journal-after");
        }
        private void BeginTerminal(string operation) {
            Step(operation + "-record-before"); Attempt = Record(operation, true); Step(operation + "-record-after");
            Step(operation + "-pending-before"); Pending = Record(operation, true); Step(operation + "-pending-after");
            Step(operation + "-sdk-before"); SdkCalls.Add((Boot, operation));
        }
        public int Uninstall() {
            BeginTerminal("usbip-uninstall");
            if (Effect != "unchanged") { Inventory = new(Target, null, false, true, false); BackendAvailable = false; Residue = true; }
            Step("uninstall-sdk-after"); return UninstallExit;
        }
        public bool Removed() { Step("absence"); return !Inventory.Present && !Residue && !BackendAvailable; }
        public void Install(Action<string> progress) {
            BeginTerminal("usbip-install");
            Inventory = Effect switch {
                "complete" => new(Target, Target, true, true, true),
                "product-only" => new(Target, Target, true, false, false, "service missing"),
                "service-only" => new(Target, null, true, false, true, "product missing"),
                "wrong-driver" => new(Target, Target, true, true, false),
                "legacy-product" => new(Target, new("0.9.7.7"), true, true, true),
                "newer-product" => new(Target, new("0.9.9.0"), true, true, false),
                _ => new(Target, null, false, true, false)
            };
            BackendAvailable = Effect is "complete" or "service-only" or "legacy-product";
            progress("model SDK terminal returned"); Step("install-sdk-after");
        }
        public bool Verify(UsbipUpgrade.Inventory inventory) {
            Step("verify-before"); bool answer = Inventory.DriverCurrent && BackendAvailable;
            Step("verify-after"); return answer;
        }
        public void ConfirmCurrent(UsbipUpgrade.Inventory inventory) {
            Step("confirm-before"); ++Confirmations; Pending = null; Step("confirm-after");
        }
        public void Publish(UsbipUpgrade.Snapshot snapshot) { Step("publish:" + snapshot.phase); States.Add(snapshot); }
        internal void Reopen() { ++Session; OwnerAlive = true; }
        internal void RestartWindows(bool removeTransientResidue = true) {
            ++Boot; Reopen();
            // Explicit environment arrangement after Windows restart. Real file
            // retirement/locking is covered by the existing file-transition port.
            if (!RebootPending) { Previous = Pending ?? Previous; Pending = null; }
            if (removeTransientResidue) Residue = false;
        }
    }
    private static int RunStateModel(Action<bool, string> Need)
    {
        int scenarios = 0;
        UsbipUpgrade.Snapshot Run(BusModel model, bool prepare = true) {
            int start = model.States.Count;
            var result = UsbipUpgrade.Execute(model, prepare);
            var period = model.States.Skip(start).ToArray();
            Need(!period.Any(s => s.phase == "complete") || !period.Any(s => s.phase == "failed"), "model has one consistent completion outcome per invocation");
            Need(model.SdkCalls.GroupBy(c => c.Boot).All(g => g.Count() <= 1), "interrupted same-target attempt never repeats managed SDK terminals within one boot");
            return result;
        }
        // Inventory facts cross privilege, consumer, owner, prepare and pending
        // conditions; expected base verdicts are data, not a second executor.
        var inventories = new (UsbipUpgrade.Inventory Inventory, string Kind, string Base)[] {
            (new(new("0.9.8.1"), null, false, true, false), "absent", "not-installed"),
            (new(new("0.9.8.1"), new("0.9.7.7"), true, true, false), "legacy", "update-found"),
            (new(new("0.9.8.1"), new("0.9.7.7"), true, true, true), "legacy", "update-found"),
            (new(new("0.9.8.1"), new("0.9.8.1"), true, true, true), "stable", "current"),
            (new(new("0.9.8.1"), new("0.9.9.0"), true, true, false), "stable", "newer-preserved"),
            (new(new("0.9.8.1"), new("0.9.8.1"), true, true, false), "stable", "unverified"),
            (new(new("0.9.8.1"), null, true, false, true, "partial registration"), "stable", "unverified"),
            (new(new("0.9.8.1"), null, true, true, false), "stable", "unverified")
        };
        foreach (var row in inventories)
        foreach (bool prepare in new[] { false, true })
        foreach (bool admin in new[] { false, true })
        foreach (bool busy in new[] { false, true })
        foreach (bool owner in new[] { false, true })
        foreach (bool pending in new[] { false, true }) {
            ++scenarios;
            var model = new BusModel { Inventory = row.Inventory, Administrator = admin, Busy = busy, OwnerAlive = owner };
            if (pending) model.Pending = "{broken persisted transaction";
            string expected = pending ? "reboot-required" : row.Base;
            bool eligible = prepare && row.Kind != "stable" && !pending;
            if (eligible) expected = !admin ? "admin-required" : !owner ? "failed" : busy ? "busy" : row.Kind == "legacy" ? "reboot-required" : "complete";
            Need(Run(model, prepare).phase == expected, "inventory matrix: " + row.Kind + "/" + prepare + "/" + admin + "/" + busy + "/" + owner + "/" + pending);
            bool mutates = eligible && admin && owner && !busy;
            Need(model.SdkCalls.Count == (mutates ? 1 : 0), "matrix preserves unrelated/same/newer/unknown states and only admitted preparation mutates");
            Need(prepare || (model.Attempt is null && model.Confirmations == 0), "check-only never journals or confirms maintenance");
        }
        foreach (bool legacy in new[] { false, true }) {
            string op = legacy ? "usbip-uninstall" : "usbip-install";
            var faults = new List<string> { "inspect#1", "publish:checking#1", "safety#1", "attempt-read#1", "journal-before#1", "journal-after#1",
                op + "-record-before#1", op + "-record-after#1", op + "-pending-before#1", op + "-pending-after#1", op + "-sdk-before#1",
                (legacy ? "uninstall-sdk-after#1" : "install-sdk-after#1") };
            if (legacy) faults.Add("publish:removing#1");
            else faults.AddRange(new[] { "absence#1", "publish:installing#1", "publish:installing#2", "publish:verifying#1", "verify-before#1", "verify-after#1", "inspect#2", "confirm-before#1", "confirm-after#1", "publish:complete#1" });
            foreach (string fault in faults) {
                ++scenarios; var model = new BusModel(legacy) { Fault = fault };
                Run(model); Need(model.Fault is null, "declared fault was actually reached: " + fault);
                model.Reopen(); Run(model); model.Reopen(); Run(model);
                model.RestartWindows(); Run(model); model.Reopen(); Run(model);
                // An early failure can leave the old product untouched. Its next
                // admitted removal still requires one further real restart.
                if (model.RebootPending) { model.RestartWindows(); Run(model); }
                Need(model.Inventory.Installed == model.Target && model.Inventory.DriverCurrent && model.BackendAvailable && !model.RebootPending,
                    "fault removed: bounded restart sequence reaches inspected backend-ready product: " + legacy + "/" + fault);
            }
        }
        foreach (string effect in new[] { "none", "product-only", "service-only", "wrong-driver", "legacy-product", "newer-product" }) {
            ++scenarios; var model = new BusModel { Effect = effect };
            Need(Run(model).phase != "complete" && model.Confirmations == 0 && model.RebootPending, "partial SDK effect never clears the install transaction: " + effect);
            model.Reopen(); Need(Run(model).phase == "reboot-required" && model.SdkCalls.Count == 1, "partial SDK result cannot loop on reopen: " + effect);
            model.RestartWindows(); model.Effect = "complete";
            var observedAfterBoot = model.Inventory;
            var afterBoot = Run(model);
            Need(afterBoot.phase != "complete" || (model.Inventory.Installed == model.Target && model.Inventory.DriverCurrent), "post-reboot partial state cannot become fabricated success: " + effect);
            if (!observedAfterBoot.Verified || observedAfterBoot.Installed == model.Target || observedAfterBoot.Installed > model.Target)
                Need(model.SdkCalls.Count == 1, "unknown/same/newer post-reboot partial install is preserved: " + effect);
            // Explicitly arrange that Windows completed removal, rather than
            // pretending an unknown persistent partial state repaired itself.
            if (afterBoot.phase == "reboot-required") model.RestartWindows();
            model.Inventory = new(model.Target, null, false, true, false); model.BackendAvailable = false; model.Residue = false;
            model.RestartWindows();
            Need(Run(model).phase == "complete" && model.Inventory.Installed == model.Target && !model.RebootPending, "confirmed clean environment resumes once and installs: " + effect);
        }
        foreach (int exit in new[] { -1, 1, 5, 1603, 1641, 3010 })
        foreach (bool disappeared in new[] { false, true }) {
            ++scenarios; var model = new BusModel(true) { UninstallExit = exit, Effect = disappeared ? "complete" : "unchanged" };
            Need(Run(model).phase == "failed" && model.RebootPending, "nonzero uninstall retains its transaction even when registry disappeared");
            model.Reopen(); Need(Run(model).phase == "reboot-required" && model.SdkCalls.Count == 1, "uncertain removal never starts replacement in same boot");
            model.RestartWindows(); model.UninstallExit = 0; model.Effect = "complete"; Run(model);
            if (model.RebootPending) { model.RestartWindows(); Run(model); }
            Need(model.Inventory.Installed == model.Target && !model.RebootPending, "uncertain uninstall has a bounded reboot/rescan recovery path");
        }
        { ++scenarios; var model = new BusModel(true); Need(Run(model).phase == "reboot-required", "removal precedes replacement");
            model.RestartWindows(removeTransientResidue: false); Need(Run(model).phase == "unverified" && model.SdkCalls.Count == 1, "post-reboot DriverStore residue blocks an overlapping install");
            model.Residue = false; model.Reopen(); Need(Run(model).phase == "complete" && model.SdkCalls.Count == 2, "once exact absence is proved, residual refusal has not consumed the new boot's install budget"); }
        return scenarios;
    }
    private static int RunAdapterStateTests(Action<bool, string> Need)
    {
        int scenarios = 0;
        var target = new Version("0.9.8.1");
        var planned = new UsbipUpgrade.Inventory(target, null, false, true, false);
        foreach (var item in new (string Name, UsbipUpgrade.Inventory Actual, bool Owner, bool Safe, bool ExitDuringSafety, bool? Expected)[] {
            ("still-clean", planned, true, true, false, true),
            ("other-installer-finished", new(target,target,true,true,true),true,true,false,false),
            ("old-product-appeared",new(target,new("0.9.7.7"),true,true,false),true,true,false,null),
            ("old-product-current-UDE",new(target,new("0.9.7.7"),true,true,true),true,true,false,null),
            ("newer-product-appeared",new(target,new("0.9.9.0"),true,true,false),true,true,false,null),
            ("current-wrong-driver",new(target,target,true,true,false),true,true,false,null),
            ("partial-registration",new(target,null,true,false,true),true,true,false,null),
            ("unknown-absent",new(target,null,false,false,false),true,true,false,null),
            ("SDK-target-changed",new(new("0.9.9.0"),null,false,true,false),true,true,false,null),
            ("owner-left",planned,false,true,false,null),
            ("consumer-appeared",planned,true,false,false,null),
            ("owner-left-in-safety",planned,true,true,true,null) }) {
            ++scenarios; bool owner = item.Owner; int observations = 0, safety = 0; bool? result = null; Exception? error = null;
            try { result = UsbipUpgrade.WindowsRuntime.RecheckInstall(planned, () => { ++observations; return item.Actual; }, () => owner,
                () => { ++safety; if (item.ExitDuringSafety) owner = false; return (item.Safe, "fixture consumer active"); }); }
            catch (Exception ex) { error = ex; }
            Need(item.Expected is null ? error is not null : error is null && result == item.Expected, "actual install preflight: " + item.Name);
            Need(observations == (item.Owner ? 1 : 0), "preflight does not reuse initial inventory or inspect after known owner exit");
            if (item.Name == "other-installer-finished") Need(safety == 0, "already-current preflight skips duplicate installer and consumer probes");
        }
        string boot = YeManCC.UsbipMaintenanceState.BootSessionPrefix + "{11111111-1111-4111-8111-111111111111}";
        string otherBoot = YeManCC.UsbipMaintenanceState.BootSessionPrefix + "{22222222-2222-4222-8222-222222222222}";
        string folder = Path.Combine(Path.GetTempPath(), "YMCC-usbip-owned-confirm-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder);
        string file = Path.Combine(folder, "pending.json"), unrelated = Path.Combine(folder, "unrelated.keep");
        byte[] sentinel = [0x12, 0x34, 0x56]; File.WriteAllBytes(unrelated, sentinel);
        Dictionary<string, object?> ValidRecord() => new() { ["schemaVersion"]=3, ["operation"]="usbip-install", ["session"]="current",
            ["coreSha256"]=BusModel.Core, ["targetVersion"]=target.ToString(), ["bootSessionIdentity"]=boot };
        bool Complete(string? currentBoot = null) => UsbipUpgrade.WindowsRuntime.CompleteOwnedInstallRecord(file, BusModel.Core, target, "current", currentBoot ?? boot);
        try {
            ++scenarios; Need(!Complete(), "no active install record is not manufactured by current-state confirmation");
            foreach (string variant in new[] { "matching", "hash-case", "foreign-session", "foreign-core", "foreign-target", "foreign-boot", "uninstall", "unknown-operation", "schema", "null-boot", "nonstring-boot", "missing-core", "bad-json", "oversize", "current-boot-unknown" }) {
                ++scenarios; var row = ValidRecord();
                switch (variant) {
                    case "hash-case": row["coreSha256"] = BusModel.Core.ToUpperInvariant(); break;
                    case "foreign-session": row["session"] = "other"; break;
                    case "foreign-core": row["coreSha256"] = new string('b',64); break;
                    case "foreign-target": row["targetVersion"] = "0.9.9.0"; break;
                    case "foreign-boot": row["bootSessionIdentity"] = otherBoot; break;
                    case "uninstall": row["operation"] = "usbip-uninstall"; break;
                    case "unknown-operation": row["operation"] = "unknown"; break;
                    case "schema": row["schemaVersion"] = 2; break;
                    case "null-boot": row["bootSessionIdentity"] = null; break;
                    case "nonstring-boot": row["bootSessionIdentity"] = 123; break;
                    case "missing-core": row.Remove("coreSha256"); break;
                    case "oversize": row["padding"] = new string('x',5000); break;
                }
                string json = variant == "bad-json" ? "{bad" : System.Text.Json.JsonSerializer.Serialize(row);
                byte[] original = System.Text.Encoding.UTF8.GetPreamble().Concat(System.Text.Encoding.UTF8.GetBytes(json)).ToArray();
                File.WriteAllBytes(file, original); bool completed = false; Exception? error = null;
                try { completed = Complete(variant == "current-boot-unknown" ? "invalid" : boot); } catch (Exception ex) { error = ex; }
                bool expected = variant is "matching" or "hash-case";
                Need(expected ? completed && error is null && !File.Exists(file) : !completed && error is not null && File.ReadAllBytes(file).SequenceEqual(original),
                    "actual owned completion preserves foreign/unknown records: " + variant);
                Need(File.ReadAllBytes(unrelated).SequenceEqual(sentinel), "owned completion never consumes unrelated files");
            }
            ++scenarios; var bytes = System.Text.Encoding.UTF8.GetBytes(System.Text.Json.JsonSerializer.Serialize(ValidRecord())); File.WriteAllBytes(file, bytes);
            bool rejected = false;
            using (var held = new FileStream(file,FileMode.Open,FileAccess.ReadWrite,FileShare.None)) {
                try { Complete(); } catch (IOException) { rejected = true; }
            }
            Need(rejected && File.ReadAllBytes(file).SequenceEqual(bytes), "locked install record cannot be silently completed");
            Need(Complete() && !File.Exists(file), "release of owned record lock permits exact completion");
        } finally {
            File.Delete(file); File.Delete(unrelated);
            // Only archives whose path is authenticated by their own exact-byte
            // digest are fixture-owned. Never recursive-delete a recovered root.
            foreach (string archived in Directory.EnumerateFiles(folder)) {
                string prefix = file + ".owned-completed-";
                if (!archived.StartsWith(prefix, StringComparison.Ordinal) || !archived.EndsWith(".json", StringComparison.Ordinal)) continue;
                byte[] content = File.ReadAllBytes(archived); string hash = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(content)).ToLowerInvariant();
                Need(archived == prefix + hash + ".json", "owned completion archive has exact-byte digest"); File.Delete(archived);
            }
            Directory.Delete(folder, recursive:false);
        }
        return scenarios;
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
            ("0.9.8.1", true, false, "current"), ("0.9.7.7", true, false, "update-found"), ("0.9.9.0", false, false, "newer-preserved"),
            ("0.9.7.7", false, false, "update-found"), ("0.9.8.1", false, false, "unverified"),
            ("0.9.8.1", true, true, "reboot-required") }) {
            var f = new Fake { Inventory = new(new("0.9.8.1"), new(installed), true, true, current), RebootPending = reboot };
            var observed = UsbipUpgrade.Observe(f);
            Need(observed.phase == expected, "read-only refresh keeps current inventory and mandatory reboot separate");
            Need(f.Calls.SequenceEqual(new[] { "inspect" }) && f.States.Count == 0 && !f.Attempted,
                "page refresh never publishes a startup stage, journals, uninstalls, installs or confirms a transaction");
        }
        // Old registration must not disappear behind HIDM's current raw UDE.
        foreach (bool prepare in new[] { false, true }) {
            var f = new Fake { Inventory = new(new("0.9.8.1"), new("0.9.7.7"), true, true, true), RestartRequired = true };
            var observed = UsbipUpgrade.Observe(f);
            Need(observed.phase == "update-found" && f.Calls.SequenceEqual(new[] { "inspect" }) && !f.Attempted,
                "readonly recognizes old product despite current UDE and never mutates");
            f.Calls.Clear();
            var started = Run(f, prepare);
            Need(started.phase == (prepare ? "reboot-required" : "update-found"), "old registered product reaches removal instead of false current");
            using var state = System.Text.Json.JsonDocument.Parse(System.Text.Json.JsonSerializer.Serialize(started));
            Need(state.RootElement.TryGetProperty("udeDiskMatchConfirmed", out var evidence) && evidence.ValueKind == System.Text.Json.JsonValueKind.True,
                "current UDE disk evidence never hides a legacy product");
            Need(!f.Calls.Contains("install") && !f.Calls.Contains("verify"), "legacy official uninstall requires reboot before SDK installation");
            if (prepare) {
                Need(f.Calls.SequenceEqual(new[] { "inspect", "safety", "journal", "uninstall" }), "legacy mixed-version removal keeps original admission order");
                Run(f);Need(f.Calls.Count(c => c == "uninstall") == 1, "same-boot mixed product cannot loop uninstalls");
            } else Need(f.Calls.SequenceEqual(new[] { "inspect" }) && !f.Attempted, "check-only mixed product never uninstalls");
        }
        foreach (bool admin in new[] { false, true }) {
            var f = new Fake { Inventory = new(new("0.9.8.1"), new("0.9.7.7"), true, true, true), Administrator = admin, Safe = false };
            Need(Run(f).phase == (admin ? "busy" : "admin-required"), "mixed-version removal still requires privilege and idle admission");
            Need(!f.Attempted && !f.Calls.Contains("uninstall"), "busy/nonadmin legacy product remains untouched");
        }
        const string targetHash = "abd4fc43dce40e027e4acc884cbfe92433e026206f6565c5ee5284a39441169a";
        const string oldHash = "51db440065393e588a6b2585508c50eb3e1510b7b06d9afa6c5bde583751ea7d";
        Need(UsbipUpgrade.WindowsRuntime.LegacyProductMatchesDriver(new("0.9.7.7"), new("0.9.8.1"), targetHash, targetHash), "reviewed old product accepts pinned current UDE for uninstall admission");
        Need(UsbipUpgrade.WindowsRuntime.LegacyProductMatchesDriver(new("0.9.7.7"), new("0.9.8.1"), oldHash, targetHash), "exact reviewed legacy UDE remains eligible");
        Need(!UsbipUpgrade.WindowsRuntime.LegacyProductMatchesDriver(new("0.9.7.7"), new("0.9.8.1"), new string('a',64), targetHash), "unknown UDE is not automatically removed");
        Need(!UsbipUpgrade.WindowsRuntime.LegacyProductMatchesDriver(new("0.9.6.0"), new("0.9.8.1"), targetHash, targetHash), "unreviewed old product is not authorized by current UDE alone");
        Need(!UsbipUpgrade.WindowsRuntime.LegacyProductMatchesDriver(new("0.9.8.1"), new("0.9.8.1"), targetHash, targetHash), "same product is not uninstalled");
        Need(!UsbipUpgrade.WindowsRuntime.LegacyProductMatchesDriver(new("0.9.9.0"), new("0.9.8.1"), targetHash, targetHash), "newer product is not uninstalled");
        foreach (bool prepare in new[] { false, true }) {
            var f = new Fake { Inventory = new(new("0.9.8.1"), new("0.9.8.1"), true, true, false) };
            var started = Run(f, prepare);
            Need(started.phase == "unverified" && UsbipUpgrade.Observe(f).phase == "unverified", "product label alone cannot confirm current UDE during startup or refresh");
            using var state = System.Text.Json.JsonDocument.Parse(System.Text.Json.JsonSerializer.Serialize(started));
            Need(state.RootElement.TryGetProperty("udeDiskMatchConfirmed", out var evidence) && evidence.ValueKind == System.Text.Json.JsonValueKind.False,
                "unconfirmed UDE is not reported as matched");
            Need(f.Calls.SequenceEqual(new[] { "inspect", "inspect" }) && !f.Attempted, "same-version unconfirmed driver remains preserved without repair");
        }
        foreach (string version in new[] { "0.9.8.1", "0.9.9.0", "1.0.0.0" }) {
            var f = new Fake { Inventory = new(new("0.9.8.1"), new(version), true, true, false) };
            Need(Run(f).phase == (version == "0.9.8.1" ? "unverified" : "newer-preserved"), "same/newer preserved");
            Need(f.Calls.SequenceEqual(new[] { "inspect" }), "same/newer never uninstalls or enters device safety");
        }
        foreach (var inventory in new[] { new UsbipUpgrade.Inventory(new("0.9.8.1"), null, false, false, false),
            new(new("0.9.8.1"), new("0.9.7.7"), true, false, false),
            new(new("0.9.8.1"), null, true, true, false) }) {
            var f = new Fake { Inventory = inventory };
            Need(Run(f).phase == "unverified", "unknown inventory never authorizes first installation or removal");
            Need(f.Calls.SequenceEqual(new[] { "inspect" }) && !f.Attempted, "unknown state never journals, uninstalls, installs or verifies");
        }
        Fake Absent(bool resume = false) => new() { Resume = resume,
            Inventory = new(new("0.9.8.1"), null, false, true, false) };
        { var f = Absent();
            Need(UsbipUpgrade.Observe(f).phase == "not-installed", "verified absence remains not-installed during read-only observation");
            Need(f.Calls.SequenceEqual(new[] { "inspect" }) && f.States.Count == 0 && !f.Attempted, "observing verified absence never mutates or publishes startup progress");
            f.Calls.Clear();
            Need(Run(f, false).phase == "not-installed", "check-only verified absence never starts first installation");
            Need(f.Calls.SequenceEqual(new[] { "inspect" }) && !f.Attempted && f.States.Last().phase == "not-installed", "check-only absence never checks removal, journals or invokes the SDK");
        }
        { var f = new Fake(); Need(Run(f, false).phase == "update-found", "check only reports new version"); Need(!f.Attempted, "check does not journal or mutate"); }
        { var f = new Fake { Administrator = false }; Need(Run(f).phase == "admin-required", "administrator guidance"); Need(!f.Attempted, "no UAC or write without admin"); }
        { var f = new Fake { Safe = false }; Need(Run(f).phase == "busy", "live imports/consumer postpones"); Need(!f.Attempted, "busy leaves original untouched"); }
        { var f = new Fake { OwnerAlive = false }; Need(Run(f).phase == "failed", "owner exit stops maintenance"); Need(!f.Attempted, "owner exit never journals uninstall"); }
        { var f = new Fake { Attempted = true }; Need(Run(f).phase == "failed", "interrupted old attempt retained"); Need(!f.Calls.Contains("uninstall"), "no repeated uninstall"); }
        { var f = new Fake(); Need(Run(f).phase == "complete", "upgrade reaches observed endpoint");
            Need(f.Calls.SequenceEqual(new[] { "inspect", "safety", "journal", "uninstall", "removed", "install", "verify", "inspect" }), "journal -> uninstall -> observed absence -> SDK install -> verification -> actual inventory");
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
        // Extend the original consumer for verified absence and reboot resume;
        // these share admission rules but first installation needs no Resume.
        foreach (bool resume in new[] { false, true }) {
            string route = resume ? "resume" : "first install";
            var f = Absent(resume); f.RestartRequired = resume;
            int confirmations = 0; f.OnConfirmCurrent = _ => ++confirmations;
            var result = Run(f);
            Need(result.phase == "complete" && confirmations == 1, route + ": verified absence installs and confirms the inspected bundled version");
            Need(f.Calls.SequenceEqual(new[] { "inspect", "install-safety", "removed", "journal", "install", "verify", "inspect" }),
                route + ": absence -> idle admission -> residual check -> journal -> SDK -> Verify -> actual inventory");
            Need(f.VerifyInput is { Installed: null, DriverCurrent: false }, route + ": verification cannot inherit an advertised installed version or old driver match");
            Need(result.installedVersion == "0.9.8.1" && result.udeDiskMatchConfirmed && f.States.Last() == result,
                route + ": completion reports the inspected target and confirmed driver");
            Need(f.States.Select(s => s.phase).SequenceEqual(new[] { "checking", "installing", "verifying", "complete" }),
                route + ": installation has truthful progress and a final state");
        }
        foreach (bool resume in new[] { false, true }) {
            string route = resume ? "resume" : "first install";
            foreach (string blocker in new[] { "admin", "busy", "owner", "reboot", "attempted", "residual", "owner-after-removal" }) {
                var f = Absent(resume);
                string expected;
                switch (blocker) {
                    case "admin": f.Administrator = false; expected = "admin-required"; break;
                    case "busy": f.Safe = false; expected = "busy"; break;
                    case "owner": f.OwnerAlive = false; expected = "failed"; break;
                    case "reboot": f.RebootPending = true; expected = "reboot-required"; break;
                    case "attempted": f.Attempted = true; expected = "failed"; break;
                    case "residual": f.IsRemoved = false; expected = "unverified"; break;
                    default: f.ExitAfterRemoval = true; expected = "failed"; break;
                }
                bool attemptedBefore = f.Attempted;
                var result = Run(f);
                Need(result.phase == expected && f.States.Last() == result, route + ": " + blocker + " stops at the required endpoint");
                Need(!f.Calls.Contains("journal") && !f.Calls.Contains("uninstall") && !f.Calls.Contains("install") && !f.Calls.Contains("verify") && f.Attempted == attemptedBefore,
                    route + ": " + blocker + " never records a new attempt or mutates devices");
                if (blocker is "residual" or "owner-after-removal")
                    Need(f.Calls.SequenceEqual(new[] { "inspect", "install-safety", "removed" }), route + ": removal observation precedes the final admission decision");
            }
            { var f = Absent(resume); f.FailInstall = true;
                var failed = Run(f);
                Need(failed.phase == "failed" && f.States.Last() == failed && f.Attempted, route + ": installer exception leaves a journal and terminal failure");
                Need(Run(f).phase == "failed" && f.Calls.Count(c => c == "install") == 1 && f.Calls.Count(c => c == "journal") == 1,
                    route + ": the same boot cannot repeat a failed SDK installation");
                Need(!f.Calls.Contains("uninstall") && !f.Calls.Contains("verify"), route + ": failed install is never mistaken for successful verification");
            }
            { var f = Absent(resume); f.Verified = false;
                var result = Run(f);
                Need(result.phase == "reboot-required" && f.States.Last() == result, route + ": SDK return without verified readiness requires reboot");
                Need(f.Calls.Count(c => c == "verify") == 1 && f.Calls.Count(c => c == "inspect") == 1 && !f.States.Any(s => s.phase == "complete"),
                    route + ": Verify is mandatory and a false result cannot complete");
                Need(result.installedVersion == "" && !result.udeDiskMatchConfirmed, route + ": failed verification never fabricates an installed target");
            }
        }
        { var f = Absent(true);
            Need(UsbipUpgrade.Observe(f).phase == "not-installed" && f.States.Count == 0, "read-only absence never consumes an available continuation");
            f.Calls.Clear();
            Need(Run(f, false).phase == "update-found" && !f.Attempted && f.Calls.SequenceEqual(new[] { "inspect" }),
                "check-only reports a known continuation without resuming installation");
        }
        // Owner exit at the last observable boundaries must still stop the
        // SDK call or completion acknowledgement on both installation routes.
        foreach (bool resume in new[] { false, true }) {
            foreach (string boundary in new[] { "installing-publish", "verify", "final-inspect" }) {
                var f = Absent(resume);
                int confirmations = 0; f.OnConfirmCurrent = _ => ++confirmations;
                if (boundary == "installing-publish")
                    f.OnPublish = state => { if (state.phase == "installing") f.OwnerAlive = false; };
                else if (boundary == "verify")
                    f.OnVerify = _ => f.OwnerAlive = false;
                else
                    f.OnInspect = _ => { if (f.Calls.Count(c => c == "inspect") == 2) f.OwnerAlive = false; };
                var result = Run(f);
                Need(!f.OwnerAlive && result.phase == "failed" && f.States.Last() == result && !f.States.Any(s => s.phase == "complete"),
                    (resume ? "resume" : "first install") + ": owner exit during " + boundary + " leaves a terminal failure");
                Need(confirmations == 0, "owner exit before the final acknowledgement never calls ConfirmCurrent");
                if (boundary == "installing-publish")
                    Need(!f.Calls.Contains("install") && !f.Calls.Contains("verify"), "owner exit in installing Publish stops before the SDK installation call");
                else {
                    Need(f.InstallReturned && f.Calls.Count(c => c == "verify") == 1 && f.Inventory is { Installed: not null, Present: true, Verified: true, DriverCurrent: true },
                        "successful SDK installation and Verify cannot override owner exit before completion");
                    if (boundary == "final-inspect")
                        Need(f.Calls.Count(c => c == "inspect") == 2, "owner exit is injected during the actual post-Verify inventory inspection");
                }
            }
        }
        // A successful UDE/backend probe is not a product registration result.
        foreach (bool resume in new[] { false, true }) {
            foreach (var actual in new[] {
                new UsbipUpgrade.Inventory(new("0.9.8.1"), new("0.9.7.7"), true, true, true),
                new(new("0.9.8.1"), null, false, true, true),
                new(new("0.9.8.1"), new("0.9.8.1"), false, true, true),
                new(new("0.9.8.1"), new("0.9.8.1"), true, false, true, "fixture inventory remains unknown"),
                new(new("0.9.8.1"), new("0.9.8.1"), true, true, false) }) {
                var f = Absent(resume); f.InventoryAfterVerify = actual;
                int confirmations = 0; f.OnConfirmCurrent = _ => ++confirmations;
                var result = Run(f);
                Need(result.phase == "unverified" && f.States.Last() == result && confirmations == 0 && !f.States.Any(s => s.phase == "complete"),
                    "backend true with old/absent/unknown registration or changed driver never completes");
                Need(f.Calls.SequenceEqual(new[] { "inspect", "install-safety", "removed", "journal", "install", "verify", "inspect" }),
                    "successful Verify must be followed by actual inventory inspection on both installation routes");
                Need(result.installedVersion == (actual.Installed?.ToString() ?? "") && result.udeDiskMatchConfirmed == actual.DriverCurrent,
                    "post-install failure reports actual registration and driver evidence without synthesizing target version");
            }
        }
        const string progressPrefix = "HIDM 安装进度：";
        void SeedProgress(Fake f) {
            f.InstallMessages.AddRange(new[] { "", " \t ", " stage-00 ", "stage-00", new string('x', 400) + "a", new string('x', 400) + "b" });
            for (int i = 1; i <= 40; ++i) {
                f.InstallMessages.Add("stage-" + i.ToString("D2"));
                f.InstallMessages.Add("stage-" + i.ToString("D2"));
            }
        }
        foreach (bool resume in new[] { false, true }) {
            foreach (string outcome in new[] { "complete", "install-failed", "verify-failed" }) {
                var f = Absent(resume); SeedProgress(f);
                f.FailInstall = outcome == "install-failed"; f.Verified = outcome != "verify-failed";
                var result = Run(f);
                var installing = f.States.Where(s => s.phase == "installing").ToArray();
                var progress = installing.Skip(1).ToArray();
                Need(progress.Length == 16 && f.ProgressCallbacks == f.InstallMessages.Count,
                    "both routes forward at most sixteen messages without stopping SDK callbacks after the limit");
                Need(progress.All(s => s.detail.StartsWith(progressPrefix, StringComparison.Ordinal) && s.detail.Length <= progressPrefix.Length + 240 && !string.IsNullOrWhiteSpace(s.detail[progressPrefix.Length..])),
                    "each forwarded SDK detail has a bounded nonblank body");
                Need(progress.Select(s => s.detail).Distinct().Count() == progress.Length && progress[0].detail == progressPrefix + "stage-00" && progress[1].detail == progressPrefix + new string('x', 240),
                    "trimmed duplicates and identical truncated messages consume no extra progress slots");
                Need(f.States.Select(s => s.sequence).SequenceEqual(Enumerable.Range(1, f.States.Count)),
                    "SDK progress and terminal states have one monotonic sequence");
                string terminal = outcome == "install-failed" ? "failed" : outcome == "verify-failed" ? "reboot-required" : "complete";
                Need(result.phase == terminal && f.States.Last() == result, "successful and failed SDK progress always ends in the corresponding terminal state");
                Need(f.Calls.Count(c => c == "verify") == (f.FailInstall ? 0 : 1) && f.InstallReturned == !f.FailInstall,
                    "SDK progress never bypasses Verify or disguises an install exception");

            }
            // Leave capacity available so closure is tested independently of
            // the sixteen-message limit, on both normal and exceptional exit.
            foreach (bool failInstall in new[] { false, true }) {
                var f = Absent(resume); f.FailInstall = failInstall;
                f.InstallMessages.Add("one progress message");
                var result = Run(f);
                Need(f.States.Count(s => s.phase == "installing") == 2 && f.InstallCallback is not null,
                    "late-callback fixture has forwarded progress and unused capacity");
                int beforeLateCallback = f.States.Count;
                f.InstallCallback!("late progress after terminal state");
                Need(f.States.Count == beforeLateCallback && f.States.Last() == result && result.phase == (failInstall ? "failed" : "complete"),
                    "late SDK callback cannot revive a completed or failed installation while capacity remains");
            }
            { var f = Absent(resume); SeedProgress(f); f.FailProgressPublish = true;
                var result = Run(f);
                Need(f.ProgressPublishFailures > 0 && f.ProgressPublishFailures <= 16 && f.ProgressCallbacks == f.InstallMessages.Count,
                    "throwing progress Publish is exercised and remains bounded while SDK callbacks continue");
                Need(f.InstallReturned && result.phase == "complete" && f.States.Last() == result && f.Calls.Count(c => c == "verify") == 1 && f.Calls.Count(c => c == "inspect") == 2,
                    "progress sink failure cannot interrupt an already started SDK install, verification or final inspection");
            }
        }
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
        // Seed only the previous-boot input boundary; the native pending ->
        // previous-boot transition still owns real boot authority and ACLs.
        // Every archive below is performed by the same production method that
        // WindowsRuntime.ConfirmCurrent uses, through the original Execute.
        int archiveCheckStart = checks;
        string archiveRoot = Path.Combine(Path.GetTempPath(), "YMCC-usbip-completion-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(archiveRoot);
        var ownedFolders = new List<string>();
        try {
            foreach (var item in new (string Name, string? SavedV2, string? CurrentV2, string Core, string Target, bool OmitV2, bool Prepare, bool Expected)[] {
                ("new-v2", volatileBootA, volatileBootB, "core", "0.9.8.1", false, true, true),
                ("same-v2", volatileBootA, volatileBootA, "core", "0.9.8.1", false, true, false),
                ("missing-current-v2", volatileBootA, null, "core", "0.9.8.1", false, true, false),
                ("null-record-v2", null, volatileBootB, "core", "0.9.8.1", false, true, false),
                ("absent-v2-repeated-loader", null, volatileBootB, "core", "0.9.8.1", true, true, false),
                ("malformed-v2", "invalid", volatileBootB, "core", "0.9.8.1", false, true, false),
                ("wrong-core", volatileBootA, volatileBootB, "other-core", "0.9.8.1", false, true, false),
                ("wrong-target", volatileBootA, volatileBootB, "core", "0.9.9.0", false, true, false),
                ("check-only", volatileBootA, volatileBootB, "core", "0.9.8.1", false, false, false),
                ("malformed-json", volatileBootA, volatileBootB, "core", "0.9.8.1", false, true, false) }) {
                string folder = Path.Combine(archiveRoot, item.Name);
                Directory.CreateDirectory(folder); ownedFolders.Add(folder);
                string pending = Path.Combine(folder, "pending.json");
                string previous = pending + ".previous-boot", completed = previous + ".completed";
                var record = new Dictionary<string, object?> { ["schemaVersion"] = 3, ["operation"] = "usbip-uninstall",
                    ["coreSha256"] = new string(item.Core == "core" ? 'a' : 'b',64), ["targetVersion"] = item.Target,
                    ["bootIdentifier"] = previousBoot.ToString("D"), ["bootSequence"] = 8, ["uptimeMs"] = 999999 };
                if (!item.OmitV2) record["bootSessionIdentity"] = item.SavedV2;
                string json = item.Name == "malformed-json" ? "{broken" : System.Text.Json.JsonSerializer.Serialize(record);
                byte[] original = new byte[] { 0xef, 0xbb, 0xbf }.Concat(System.Text.Encoding.UTF8.GetBytes("  " + json + "\r\n")).ToArray();
                byte[] sentinel = [0xde, 0xad, 0xbe, 0xef];
                File.WriteAllBytes(previous, original); File.WriteAllBytes(completed, sentinel);
                int confirmations = 0; bool archived = false;
                var f = new Fake { Inventory = new(new("0.9.8.1"), new("0.9.8.1"), true, true, true) };
                // Same/missing V2 must not borrow a changed legacy loader tuple.
                bool changedLoader = item.Name is "same-v2" or "missing-current-v2";
                f.OnConfirmCurrent = inventory => {
                    ++confirmations;
                    if (item.Expected && File.Exists(completed) && File.Exists(previous)) {
                        Need(!UsbipUpgrade.WindowsRuntime.CompletePreviousBootRecord(pending,new string('a',64),inventory.Target,
                            changedLoader?nextBoot:previousBoot,changedLoader?9u:8u,item.CurrentV2) && File.ReadAllBytes(completed).SequenceEqual(sentinel),
                            "archive unknown completed destination is preserved before explicit fixture release");
                        File.Delete(completed);
                    }
                    archived = UsbipUpgrade.WindowsRuntime.CompletePreviousBootRecord(pending, new string('a',64), inventory.Target,
                        changedLoader ? nextBoot : previousBoot, changedLoader ? 9u : 8u, item.CurrentV2);
                };
                Need(Run(f, item.Prepare).phase == "current", "archive " + item.Name + ": diagnostic completion keeps current driver verdict");
                Need(confirmations == (item.Prepare ? 1 : 0), "archive " + item.Name + ": exact startup ConfirmCurrent consumer");
                Need(archived == item.Expected, "archive " + item.Name + ": production archive admission");
                Need(item.Expected
                    ? !File.Exists(previous) && File.ReadAllBytes(completed).SequenceEqual(original)
                    : File.ReadAllBytes(previous).SequenceEqual(original) && File.ReadAllBytes(completed).SequenceEqual(sentinel),
                    "archive " + item.Name + ": original and existing completed bytes preserved on refusal");
                Need(!File.Exists(pending) && Directory.GetFiles(folder).Length == (item.Expected ? 1 : 2),
                    "archive " + item.Name + ": no active pending rewrite or extra files");
                if (item.Expected) {
                    Need(Run(f).phase == "current" && confirmations == 2 && !archived,
                        "archive completed: duplicate confirmation is a no-op");
                    Need(File.ReadAllBytes(completed).SequenceEqual(original) && Directory.GetFiles(folder).Length == 1,
                        "archive completed: repeat preserves BOM, whitespace and line endings");
                }
            }
        } finally {
            // Only delete the exact fixture-owned names; never recursively
            // enumerate or remove an external directory on failure.
            foreach (string folder in ownedFolders) {
                string pending = Path.Combine(folder, "pending.json");
                foreach (string file in new[] { pending, pending + ".previous-boot", pending + ".previous-boot.completed" }) File.Delete(file);
                Directory.Delete(folder);
            }
            Directory.Delete(archiveRoot);
        }
        int fileArchiveChecks = checks - archiveCheckStart;
        int stateModelScenarios = RunStateModel(Need);
        int adapterStateScenarios = RunAdapterStateTests(Need);
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new { status = "PASS", checks, fileArchiveChecks, stateModelScenarios, adapterStateScenarios, hardwareIo = false, actualConsumer = "UsbipUpgrade.Execute", physicalUpgradeVerified = false }));
        return 0;
    }
}
