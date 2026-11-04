using System.Text.RegularExpressions;

namespace YeManFanHost;

internal sealed record MsiWmiMetadataProbe(bool Available, bool Missing, string Detail);

/** HC and CTW use the instance named by the device's WmiPath. HC's generic
 * PNP0C14 restart loop can choose DSARDEV first, which does not establish that
 * instance. Keep the same registration/restart operation, bind its exact device,
 * and require the WMI object to exist before HC enters its actual Open override. */
internal static class MsiWmiRegistrationOrchestrator
{
    internal static string ExactDeviceId(string wmiPath, IReadOnlyList<string> devices)
    {
        var match = Regex.Match(wmiPath,
            @"^MSI_ACPI\.InstanceName='(ACPI\\PNP0C14\\[A-Za-z0-9&._-]+)_[0-9]+'$",
            RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);
        if (!match.Success) throw new InvalidOperationException("unsupported MSI WMI instance contract; no device restart permitted");
        var expected = match.Groups[1].Value;
        var matches = devices.Where(id => string.Equals(id, expected, StringComparison.OrdinalIgnoreCase))
            .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        if (matches.Length != 1)
            throw new InvalidOperationException("MSI WMI instance's exact PNP device not found; no broad restart permitted: " + expected);
        return matches[0];
    }

    internal static string EnsureAvailable(string wmiPath, Func<MsiWmiMetadataProbe> probe,
        Func<IReadOnlyList<string>> enumerateDevices, Func<bool> ensureOriginalRegistration,
        Func<string, bool> restartExact, Func<bool> stillCurrent, Func<long> monotonicMs,
        Action<int> wait, Action<string, object> record, int settleBudgetMs = 5000)
    {
        void Current()
        {
            if (!stillCurrent()) throw new InvalidOperationException("MSI WMI initialization superseded by sleep or operation timeout");
        }
        Current();
        var before = probe();
        if (before.Available) return "existing-msi-wmi-instance-available";
        record("missing-or-unavailable", new { wmiPath, before.Missing, before.Detail });
        if (!before.Missing)
            throw new InvalidOperationException("MSI WMI metadata access failed; restart is not justified: " + before.Detail);
        var exact = ExactDeviceId(wmiPath, enumerateDevices());
        Current();
        if (!ensureOriginalRegistration())
            throw new InvalidOperationException("HC MSI WmiAcpi registration did not succeed; no device restarted");
        Current();
        record("restart-exact-begin", new { exactDeviceId = exact, wmiPath, attemptLimit = 1 });
        if (!restartExact(exact))
            throw new InvalidOperationException("MSI exact device restart failed or requires a reboot: " + exact);
        Current();
        var deadline = monotonicMs() + Math.Max(0, settleBudgetMs);
        var observed = before;
        while (true)
        {
            Current();
            observed = probe();
            if (observed.Available)
            {
                record("instance-available-after-exact-restart", new { exactDeviceId = exact, wmiPath,
                    fanWriteSuccessClaimed = false, oemOwnershipClaimed = false });
                return "msi-wmi-instance-restored-after-exact-restart";
            }
            if (!observed.Missing)
                throw new InvalidOperationException("MSI WMI metadata failed after exact restart: " + observed.Detail);
            var remaining = deadline - monotonicMs();
            if (remaining <= 0) break;
            wait((int)Math.Min(250, remaining));
        }
        record("instance-still-missing", new { exactDeviceId = exact, wmiPath, observed.Detail, settleBudgetMs });
        throw new InvalidOperationException("MSI WMI class/instance still missing after one exact restart: " + observed.Detail);
    }

    internal static IReadOnlyList<string> RunSelfTest()
    {
        const string path = @"MSI_ACPI.InstanceName='ACPI\PNP0C14\0_0'";
        string[] devices = [@"ACPI\PNP0C14\DSARDEV", @"ACPI\PNP0C14\0", @"ACPI\PNP0C14\TESTDEV"];
        var checks = new List<string>();
        void Assert(bool condition, string label)
        {
            if (!condition) throw new InvalidOperationException("fan950: " + label);
            checks.Add("fan950-" + label);
        }
        void Refuse(Action action, string label)
        {
            bool refused = false;
            try { action(); } catch (InvalidOperationException) { refused = true; }
            Assert(refused, label);
        }
        Assert(ExactDeviceId(path, devices) == devices[1], "exact-pnp-zero-selected-instead-of-first-dsardev");
        Assert(ExactDeviceId(path, devices.Reverse().ToArray()) == devices[1], "enumeration-order-does-not-select-another-device");
        Refuse(() => ExactDeviceId(path, [devices[0], devices[2]]), "no-known-device-no-fallback-restart");
        Refuse(() => ExactDeviceId("arbitrary path", devices), "unsupported-instance-contract-refused");
        int reads = 0, restarts = 0, registrations = 0, enumerations = 0;
        long tick = 0;
        bool available = true, missing = false, current = true, registrationOk = true, restartOk = true;
        bool becomesAvailable = false, cancelOnRestart = false;
        string Run() => EnsureAvailable(path,
            () => { reads++; return new(available, missing, available ? "bound" : missing ? "InvalidClass" : "AccessDenied"); },
            () => { enumerations++; return devices; },
            () => { registrations++; return registrationOk; },
            id => { restarts++; if (id != devices[1]) throw new InvalidOperationException("wrong device restarted");
                if (becomesAvailable) { available = true; missing = false; }
                if (cancelOnRestart) current = false; return restartOk; },
            () => current, () => tick, milliseconds => tick += milliseconds, (_, _) => { }, 1000);
        Assert(Run() == "existing-msi-wmi-instance-available" && reads == 1 && restarts == 0
            && registrations == 0 && enumerations == 0, "working-provider-zero-registry-or-pnp-work");
        available = false; missing = true; becomesAvailable = true; reads = 0;
        Assert(Run() == "msi-wmi-instance-restored-after-exact-restart" && restarts == 1
            && registrations == 1 && reads == 2, "missing-class-one-exact-restart-then-real-metadata-proof");
        available = false; missing = false; reads = 0; restarts = registrations = enumerations = 0;
        Refuse(() => Run(), "access-denied-not-treated-as-missing-class");
        Assert(restarts == 0 && registrations == 0 && enumerations == 0, "metadata-access-failure-zero-device-restarts");
        missing = true; registrationOk = false;
        Refuse(() => Run(), "registry-failure-cannot-advance-to-restart");
        Assert(restarts == 0, "failed-registration-zero-pnp-calls");
        registrationOk = true; restartOk = false;
        Refuse(() => Run(), "restart-return-failure-not-ready");
        available = false; missing = true; restartOk = true; becomesAvailable = false;
        restarts = 0; tick = 0;
        Refuse(() => Run(), "class-still-missing-has-finite-settle-budget");
        Assert(tick == 1000 && restarts == 1, "no-repeated-restart-or-unbounded-missing-class-loop");
        current = false; restarts = registrations = reads = 0;
        Refuse(() => Run(), "superseded-init-cannot-run-device-or-open");
        Assert(restarts == 0 && registrations == 0 && reads == 0, "cancelled-initialization-does-no-io");
        current = true; available = false; missing = true; becomesAvailable = true; cancelOnRestart = true;
        restarts = reads = 0;
        Refuse(() => Run(), "sleep-or-timeout-during-pnp-return-prevents-late-open");
        Assert(restarts == 1 && reads == 1, "late-restart-result-not-consumed-as-write-admission");
        return checks;
    }
}
