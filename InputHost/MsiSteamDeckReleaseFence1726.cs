using System.Text.Json.Serialization;

// Explicit native opt-in only. Evidence describes local present-PnP IDs, never
// Steam readiness. This helper has no SDK, device mutation, or background work.
internal static partial class Program
{
    private sealed class MsiSteamDeckReleaseFence1726
    {
        public const int Version = 1;
        public const string PrepareField = "msiSteamDeckReleaseFence";
        private readonly CommandEnvelope _binding;
        private bool _releaseProbeFailed;
        private IReadOnlyList<string> _ownedTargets = Array.AsReadOnly(Array.Empty<string>());
        public MsiSteamDeckReleaseEvidence1726? Evidence { get; private set; }
        public bool HasReleaseReceipt { get; private set; }
        public bool ReleaseSucceeded { get; private set; }
        public string ReleaseDetail { get; private set; } = "release-not-attempted";

        public MsiSteamDeckReleaseFence1726(CommandEnvelope binding) => _binding = binding;

        public bool Matches(CommandEnvelope envelope) =>
            SameTuple(_binding, envelope);

        public bool TryPin(VirtualEndpointObservation zeroBeforeCreate,
            VirtualEndpointObservation arrival, out string detail)
        {
            // No VID-only ownership: zero reconciliation must precede SDK create,
            // followed by exactly one started composite root and its actual HIDs.
            var roots = arrival.SteamDeckRootInstanceIds;
            var children = arrival.SteamDeckHidChildInstanceIds;
            if (!zeroBeforeCreate.IsFullyAbsent || zeroBeforeCreate.PresentInstanceIds is null ||
                !arrival.IsUniqueStartedTarget("steamdeck") || arrival.SteamDeckRootCount != 1 ||
                roots is null || roots.Count != 1 || children is null || children.Count == 0 ||
                children.Count != arrival.SteamDeckHidChildCount || arrival.PresentInstanceIds is null ||
                roots.Any(id => string.IsNullOrWhiteSpace(id) || !id.StartsWith("USB\\", StringComparison.OrdinalIgnoreCase)) ||
                children.Any(id => string.IsNullOrWhiteSpace(id) || !id.StartsWith("HID\\", StringComparison.OrdinalIgnoreCase)))
            {
                detail = "msi-steamdeck-owned-identity-not-admitted";
                return false;
            }
            var targets = roots.Concat(children).ToArray();
            var present = new HashSet<string>(arrival.PresentInstanceIds, StringComparer.OrdinalIgnoreCase);
            if (targets.Distinct(StringComparer.OrdinalIgnoreCase).Count() != targets.Length ||
                targets.Any(id => !present.Contains(id)))
            {
                detail = "msi-steamdeck-owned-identity-incomplete";
                return false;
            }
            _ownedTargets = Array.AsReadOnly(targets.OrderBy(id => id, StringComparer.OrdinalIgnoreCase).ToArray());
            Evidence = MakeEvidence(arrival, arrival, "owned-arrival-pinned");
            detail = "msi-steamdeck-owned-arrival-pinned";
            return true;
        }

        private int CountOwned(VirtualEndpointObservation observation)
        {
            if (!observation.ProbeSucceeded || observation.PresentInstanceIds is null || _ownedTargets.Count == 0)
                return -1; // Unknown is not zero/removal success.
            var present = new HashSet<string>(observation.PresentInstanceIds, StringComparer.OrdinalIgnoreCase);
            return _ownedTargets.Count(present.Contains);
        }

        public bool IsOwnedAbsent(VirtualEndpointObservation observation) => CountOwned(observation) == 0;

        public static VirtualEndpointObservation Read(Func<VirtualEndpointObservation> observe)
        {
            try { return observe(); }
            catch (Exception ex) { return FailedObservation("pnp-probe:" + ex.GetType().Name); }
        }

        private static VirtualEndpointObservation FailedObservation(string detail) =>
            new(false, 0, 0, 0, 0, 0, false, detail);

        public VirtualEndpointObservation Wait(Func<VirtualEndpointObservation> observe,
            Func<long> now, Action<int> sleep, int timeoutMs, int pollIntervalMs)
        {
            if (timeoutMs < 0 || pollIntervalMs <= 0) throw new ArgumentOutOfRangeException(nameof(timeoutMs));
            try
            {
                var started = now();
                while (true)
                {
                    var observation = Read(observe);
                    // Probe failure/unknown ownership is an explicit failure, not
                    // a transient zero. No use of a cached prepare or local ACK.
                    if (!observation.ProbeSucceeded || observation.PresentInstanceIds is null) _releaseProbeFailed = true;
                    if (!observation.ProbeSucceeded || CountOwned(observation) < 0 || IsOwnedAbsent(observation))
                        return observation;
                    var elapsed = now() - started;
                    if (elapsed >= timeoutMs) return observation;
                    sleep((int)Math.Min(pollIntervalMs, timeoutMs - Math.Max(0, elapsed)));
                }
            }
            catch (Exception ex)
            {
                _releaseProbeFailed = true;
                return FailedObservation("pnp-wait:" + ex.GetType().Name);
            }
        }

        private MsiSteamDeckReleaseEvidence1726 MakeEvidence(VirtualEndpointObservation before,
            VirtualEndpointObservation after, string detail)
        {
            var beforeCount = CountOwned(before);
            var afterCount = CountOwned(after);
            var succeeded = !_releaseProbeFailed && beforeCount >= 0 && afterCount >= 0;
            return new(Version, true, _binding.RunId, _binding.Epoch, _binding.Persona, _binding.ProfileIdentity,
                succeeded, succeeded && afterCount == 0, _ownedTargets.Count, _ownedTargets, afterCount,
                beforeCount, afterCount, before.SteamDeckRootCount, before.SteamDeckHidChildCount,
                after.SteamDeckRootCount, after.SteamDeckHidChildCount, "local-PnP", detail);
        }

        // SHUTDOWN may read present PnP after a failed RELEASE, but must not
        // overwrite its unresolved receipt or authorize absence from a local ACK.
        public void ConfirmUnresolvedRelease(VirtualEndpointObservation observation)
        {
            if (!HasReleaseReceipt || ReleaseSucceeded || Evidence is null) return;
            Evidence = Evidence with
            {
                ShutdownProbeSucceeded = observation.ProbeSucceeded && observation.PresentInstanceIds is not null,
                ShutdownRemainingOwnedTargetCount = CountOwned(observation),
                ShutdownProbeDetail = observation.Detail,
            };
        }

        public bool CompleteRelease(VirtualEndpointObservation before, VirtualEndpointObservation after,
            List<string> errors, out string detail)
        {
            Evidence = MakeEvidence(before, after, "owned-release-observed");
            if (!Evidence.AbsentVerified)
                errors.Add("msi-steamdeck-owned-absence-not-verified:" + after.Detail);
            HasReleaseReceipt = true;
            ReleaseSucceeded = errors.Count == 0;
            ReleaseDetail = detail = ReleaseSucceeded ? "local-dispose-complete" : string.Join(";", errors);
            Evidence = Evidence with { Detail = ReleaseDetail };
            return ReleaseSucceeded;
        }
    }

    private sealed record MsiSteamDeckReleaseEvidence1726(
        [property: JsonPropertyName("version")] int Version,
        [property: JsonPropertyName("required")] bool Required,
        [property: JsonPropertyName("runId")] string RunId,
        [property: JsonPropertyName("epoch")] ulong Epoch,
        [property: JsonPropertyName("persona")] string Persona,
        [property: JsonPropertyName("profileIdentity")] string ProfileIdentity,
        [property: JsonPropertyName("probeSucceeded")] bool ProbeSucceeded,
        [property: JsonPropertyName("absentVerified")] bool AbsentVerified,
        [property: JsonPropertyName("ownedTargetCount")] int OwnedTargetCount,
        [property: JsonPropertyName("ownedTargets")] IReadOnlyList<string> OwnedTargets,
        [property: JsonPropertyName("remainingOwnedTargetCount")] int RemainingOwnedTargetCount,
        [property: JsonPropertyName("beforeOwnedPresentCount")] int BeforeOwnedPresentCount,
        [property: JsonPropertyName("afterOwnedPresentCount")] int AfterOwnedPresentCount,
        [property: JsonPropertyName("beforeSteamDeckRootCount")] int BeforeSteamDeckRootCount,
        [property: JsonPropertyName("beforeSteamDeckHidChildCount")] int BeforeSteamDeckHidChildCount,
        [property: JsonPropertyName("afterSteamDeckRootCount")] int AfterSteamDeckRootCount,
        [property: JsonPropertyName("afterSteamDeckHidChildCount")] int AfterSteamDeckHidChildCount,
        [property: JsonPropertyName("scope")] string Scope,
        [property: JsonPropertyName("detail")] string Detail)
    {
        [JsonPropertyName("shutdownProbeSucceeded"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public bool? ShutdownProbeSucceeded { get; init; }
        [JsonPropertyName("shutdownRemainingOwnedTargetCount"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public int? ShutdownRemainingOwnedTargetCount { get; init; }
        [JsonPropertyName("shutdownProbeDetail"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public string? ShutdownProbeDetail { get; init; }
    }
}
