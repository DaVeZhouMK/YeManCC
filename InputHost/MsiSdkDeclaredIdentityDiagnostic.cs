using System.Reflection;

namespace YeManInputHost.Diagnostics;

// Read only two fields of an already-created, version-checked controller object.
// SDK declarations are not a complete ownership bundle or device/Steam evidence.
internal static class MsiSdkDeclaredIdentityDiagnostic
{
    internal const int InstanceIdLimit = 256;
    internal sealed record MetadataContract(string TypeName, Guid ModuleMvid, int TypeToken,
        int IndexFieldToken, int InstanceIdFieldToken);
    private static readonly MetadataContract LockedContract = new("HIDMaestro.HMController",
        new Guid("86aed9a3-c024-4596-b02c-650082ffb852"), 0x02000004, 0x0400001A, 0x0400001B);

    internal sealed record Receipt(int Version, long CapturedAtTick, bool AssetVerified,
        bool MetadataMatched, bool IndexCaptured, int? SdkControllerIndex,
        bool InstanceIdCaptured, string? SdkSelectedInstanceId, bool InstanceIdComplete,
        bool InstanceIdTruncated, string ObservationState, string Scope)
    {
        public bool ExactOwnedIdsPinned => false;
        public bool ClassificationComplete => false;
        public bool ExactOwnedAbsenceProven => false;
        public bool PhysicalReturnVerified => false;
        public bool SteamConsumptionVerified => false;
    }

    internal static Receipt? TryCapture(object? controller, bool assetVerified,
        string verifiedMvid, long capturedAtTick)
    {
        try
        {
            // HELLO already checks the SDK file hash/size and loaded MVID. A
            // different version stays unknown; never guess a field or call a getter.
            if (!Guid.TryParse(verifiedMvid, out var mvid) || mvid != LockedContract.ModuleMvid)
                return Unknown(capturedAtTick, assetVerified, "verified-mvid-mismatch");
            return TryCaptureAgainstContract(controller, assetVerified, capturedAtTick, LockedContract);
        }
        catch { return null; }
    }

    // A data contract allows pure fixtures to verify reflection against ordinary
    // fake classes. Production calls only TryCapture with the fixed SDK contract.
    internal static Receipt? TryCaptureAgainstContract(object? controller, bool assetVerified,
        long capturedAtTick, MetadataContract contract)
    {
        try
        {
            if (!assetVerified) return Unknown(capturedAtTick, false, "asset-not-verified");
            if (controller is null) return Unknown(capturedAtTick, true, "controller-object-missing");
            var type = controller.GetType();
            if (!type.IsClass || !type.IsSealed || type.IsAbstract || type.IsGenericType ||
                type.FullName != contract.TypeName || type.Module.ModuleVersionId != contract.ModuleMvid ||
                type.MetadataToken != contract.TypeToken)
                return Unknown(capturedAtTick, true, "runtime-type-metadata-mismatch");
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            var index = type.GetField("<Index>k__BackingField", flags);
            var instanceId = type.GetField("<InstanceId>k__BackingField", flags);
            if (!Matches(index, type, typeof(int), contract.IndexFieldToken) ||
                !Matches(instanceId, type, typeof(string), contract.InstanceIdFieldToken))
                return Unknown(capturedAtTick, true, "backing-field-metadata-mismatch");
            // GetValue on these ordinary instance fields executes no SDK getter,
            // factory, disposal, report submission or device enumeration method.
            if (index!.GetValue(controller) is not int sdkIndex)
                return Unknown(capturedAtTick, true, "index-value-unavailable");
            var value = instanceId!.GetValue(controller);
            if (value is not null && value is not string)
                return Unknown(capturedAtTick, true, "instance-id-value-unavailable");
            var id = (string?)value;
            var truncated = id is { Length: > InstanceIdLimit };
            var complete = !truncated && !string.IsNullOrWhiteSpace(id);
            var state = id is null ? "sdk-null-instance-id" : complete ? "sdk-declaration-captured" : "sdk-empty-instance-id";
            if (truncated)
            {
                var length = char.IsHighSurrogate(id![InstanceIdLimit - 1]) ? InstanceIdLimit - 1 : InstanceIdLimit;
                id = id[..length]; complete = false; state = "sdk-instance-id-truncated";
            }
            // Validate only the bounded diagnostic value. An omitted suffix
            // cannot make a truncated ID complete or usable as exact identity.
            if (id is not null && !WellFormed(id))
            {
                id = null; complete = false; state = "sdk-instance-id-invalid-utf16";
            }
            return new(1, capturedAtTick, true, true, true, sdkIndex, true, id,
                complete, truncated, state, "SDK constructor declarations captured after existing create; instance ID may select main or companion; no complete ownership, incarnation, removal, physical return or Steam consumption proof");
        }
        catch { return null; }
    }

    private static bool Matches(FieldInfo? field, Type owner, Type valueType, int token) =>
        field is not null && field.DeclaringType == owner && field.FieldType == valueType &&
        field.IsPrivate && field.IsInitOnly && !field.IsStatic && !field.IsLiteral && field.MetadataToken == token;

    private static Receipt Unknown(long tick, bool verified, string state) =>
        new(1, tick, verified, false, false, null, false, null, false, false, state,
            "Optional SDK declaration diagnostic unavailable; no absence or ownership inference");

    private static bool WellFormed(string value)
    {
        for (int i = 0; i < value.Length; ++i)
        {
            if (char.IsHighSurrogate(value[i]))
            {
                if (++i >= value.Length || !char.IsLowSurrogate(value[i])) return false;
            }
            else if (char.IsLowSurrogate(value[i])) return false;
        }
        return true;
    }
}
