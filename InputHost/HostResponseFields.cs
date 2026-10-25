using System;
using System.Text.Json;
using System.Text.Json.Serialization;

[JsonConverter(typeof(HostResponseFieldsJsonConverter))]
internal readonly record struct HostResponseFields(
    int protocolVersion,
    bool ok,
    string command,
    string requestId,
    ulong sequence,
    string hostInstanceId,
    string runId,
    ulong epoch,
    ulong powerGeneration,
    string targetId,
    string ownerIdentity,
    string persona,
    string profileIdentity,
    string startupNonce,
    ulong configRevision,
    string configHash,
    string status,
    string detail,
    bool assetVerified,
    string lockedCoreSha256,
    string lockedCoreMvid,
    bool virtualEndpointReconciled,
    bool targetPnpAdmitted,
    int targetPnpRootCount,
    int targetPnpPrimaryRootCount,
    int targetPnpXboxCompanionRootCount,
    int targetPnpHidChildCount,
    bool targetPnpAllStarted,
    string targetPnpDetail,
    bool? firstFrame,
    string? releaseDisposition,
    long submitElapsedUs);

internal sealed class HostResponseFieldsJsonConverter : JsonConverter<HostResponseFields>
{
    private static readonly JsonEncodedText n_protocolVersion = JsonEncodedText.Encode("protocolVersion");
    private static readonly JsonEncodedText n_ok = JsonEncodedText.Encode("ok");
    private static readonly JsonEncodedText n_command = JsonEncodedText.Encode("command");
    private static readonly JsonEncodedText n_requestId = JsonEncodedText.Encode("requestId");
    private static readonly JsonEncodedText n_sequence = JsonEncodedText.Encode("sequence");
    private static readonly JsonEncodedText n_hostInstanceId = JsonEncodedText.Encode("hostInstanceId");
    private static readonly JsonEncodedText n_runId = JsonEncodedText.Encode("runId");
    private static readonly JsonEncodedText n_epoch = JsonEncodedText.Encode("epoch");
    private static readonly JsonEncodedText n_powerGeneration = JsonEncodedText.Encode("powerGeneration");
    private static readonly JsonEncodedText n_targetId = JsonEncodedText.Encode("targetId");
    private static readonly JsonEncodedText n_ownerIdentity = JsonEncodedText.Encode("ownerIdentity");
    private static readonly JsonEncodedText n_persona = JsonEncodedText.Encode("persona");
    private static readonly JsonEncodedText n_profileIdentity = JsonEncodedText.Encode("profileIdentity");
    private static readonly JsonEncodedText n_startupNonce = JsonEncodedText.Encode("startupNonce");
    private static readonly JsonEncodedText n_configRevision = JsonEncodedText.Encode("configRevision");
    private static readonly JsonEncodedText n_configHash = JsonEncodedText.Encode("configHash");
    private static readonly JsonEncodedText n_status = JsonEncodedText.Encode("status");
    private static readonly JsonEncodedText n_detail = JsonEncodedText.Encode("detail");
    private static readonly JsonEncodedText n_assetVerified = JsonEncodedText.Encode("assetVerified");
    private static readonly JsonEncodedText n_lockedCoreSha256 = JsonEncodedText.Encode("lockedCoreSha256");
    private static readonly JsonEncodedText n_lockedCoreMvid = JsonEncodedText.Encode("lockedCoreMvid");
    private static readonly JsonEncodedText n_virtualEndpointReconciled = JsonEncodedText.Encode("virtualEndpointReconciled");
    private static readonly JsonEncodedText n_targetPnpAdmitted = JsonEncodedText.Encode("targetPnpAdmitted");
    private static readonly JsonEncodedText n_targetPnpRootCount = JsonEncodedText.Encode("targetPnpRootCount");
    private static readonly JsonEncodedText n_targetPnpPrimaryRootCount = JsonEncodedText.Encode("targetPnpPrimaryRootCount");
    private static readonly JsonEncodedText n_targetPnpXboxCompanionRootCount = JsonEncodedText.Encode("targetPnpXboxCompanionRootCount");
    private static readonly JsonEncodedText n_targetPnpHidChildCount = JsonEncodedText.Encode("targetPnpHidChildCount");
    private static readonly JsonEncodedText n_targetPnpAllStarted = JsonEncodedText.Encode("targetPnpAllStarted");
    private static readonly JsonEncodedText n_targetPnpDetail = JsonEncodedText.Encode("targetPnpDetail");
    private static readonly JsonEncodedText n_firstFrame = JsonEncodedText.Encode("firstFrame");
    private static readonly JsonEncodedText n_releaseDisposition = JsonEncodedText.Encode("releaseDisposition");
    private static readonly JsonEncodedText n_submitElapsedUs = JsonEncodedText.Encode("submitElapsedUs");
    public HostResponseFieldsJsonConverter() {}
    public override HostResponseFields Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) => throw new NotSupportedException();
    public override void Write(Utf8JsonWriter writer, HostResponseFields value, JsonSerializerOptions options)
    {
        writer.WriteStartObject();
        writer.WriteNumber(n_protocolVersion, value.protocolVersion);
        writer.WriteBoolean(n_ok, value.ok);
        writer.WriteString(n_command, value.command);
        writer.WriteString(n_requestId, value.requestId);
        writer.WriteNumber(n_sequence, value.sequence);
        writer.WriteString(n_hostInstanceId, value.hostInstanceId);
        writer.WriteString(n_runId, value.runId);
        writer.WriteNumber(n_epoch, value.epoch);
        writer.WriteNumber(n_powerGeneration, value.powerGeneration);
        writer.WriteString(n_targetId, value.targetId);
        writer.WriteString(n_ownerIdentity, value.ownerIdentity);
        writer.WriteString(n_persona, value.persona);
        writer.WriteString(n_profileIdentity, value.profileIdentity);
        writer.WriteString(n_startupNonce, value.startupNonce);
        writer.WriteNumber(n_configRevision, value.configRevision);
        writer.WriteString(n_configHash, value.configHash);
        writer.WriteString(n_status, value.status);
        writer.WriteString(n_detail, value.detail);
        writer.WriteBoolean(n_assetVerified, value.assetVerified);
        writer.WriteString(n_lockedCoreSha256, value.lockedCoreSha256);
        writer.WriteString(n_lockedCoreMvid, value.lockedCoreMvid);
        writer.WriteBoolean(n_virtualEndpointReconciled, value.virtualEndpointReconciled);
        writer.WriteBoolean(n_targetPnpAdmitted, value.targetPnpAdmitted);
        writer.WriteNumber(n_targetPnpRootCount, value.targetPnpRootCount);
        writer.WriteNumber(n_targetPnpPrimaryRootCount, value.targetPnpPrimaryRootCount);
        writer.WriteNumber(n_targetPnpXboxCompanionRootCount, value.targetPnpXboxCompanionRootCount);
        writer.WriteNumber(n_targetPnpHidChildCount, value.targetPnpHidChildCount);
        writer.WriteBoolean(n_targetPnpAllStarted, value.targetPnpAllStarted);
        writer.WriteString(n_targetPnpDetail, value.targetPnpDetail);
        if (value.firstFrame.HasValue) writer.WriteBoolean(n_firstFrame, value.firstFrame.Value);
        else writer.WriteNull(n_firstFrame);
        writer.WriteString(n_releaseDisposition, value.releaseDisposition);
        writer.WriteNumber(n_submitElapsedUs, value.submitElapsedUs);
        writer.WriteEndObject();
    }
}
