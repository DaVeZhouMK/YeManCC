using System;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;

// Owned by one HostSession. Call under that session's lifecycle gate, including
// the check and the associated backend submit/response. This token selects a
// reply format; it never replaces full request, sequence, or phase validation.
internal sealed class CompactFrameReplyBinding
{
    private object? _controller;
    private string _token = string.Empty;

    internal string Bind(object controller)
    {
        // A failed replacement must leave the old controller's lease revoked.
        Revoke();
        ArgumentNullException.ThrowIfNull(controller);
        Span<byte> entropy = stackalloc byte[32];
        RandomNumberGenerator.Fill(entropy);
        var token = Convert.ToHexStringLower(entropy);
        _controller = controller;
        _token = token;
        return token;
    }

    internal void Revoke()
    {
        _token = string.Empty;
        _controller = null;
    }

    internal bool Matches(object? controller, string? token) =>
        controller is not null && ReferenceEquals(_controller, controller) &&
        token is { Length: 64 } && _token.Length == 64 &&
        string.Equals(_token, token, StringComparison.Ordinal);

    internal bool TryGetToken(object? controller, out string token)
    {
        if (controller is not null && ReferenceEquals(_controller, controller) && _token.Length == 64)
        {
            token = _token;
            return true;
        }
        token = string.Empty;
        return false;
    }
}

// Only the session dispatcher decides when a successful frame may use this
// shape. A new value is serialized for every ACK; no response is retained here.
[JsonConverter(typeof(CompactFrameReplyFieldsJsonConverter))]
internal readonly record struct CompactFrameReplyFields(
    int protocolVersion, bool ok, string command, string requestId, ulong sequence,
    string frameReplyBinding, string status, bool? firstFrame, long submitElapsedUs);

internal sealed class CompactFrameReplyFieldsJsonConverter : JsonConverter<CompactFrameReplyFields>
{
    private static readonly JsonEncodedText ProtocolVersion = JsonEncodedText.Encode("protocolVersion");
    private static readonly JsonEncodedText Ok = JsonEncodedText.Encode("ok");
    private static readonly JsonEncodedText Command = JsonEncodedText.Encode("command");
    private static readonly JsonEncodedText RequestId = JsonEncodedText.Encode("requestId");
    private static readonly JsonEncodedText Sequence = JsonEncodedText.Encode("sequence");
    private static readonly JsonEncodedText Binding = JsonEncodedText.Encode("frameReplyBinding");
    private static readonly JsonEncodedText Status = JsonEncodedText.Encode("status");
    private static readonly JsonEncodedText FirstFrame = JsonEncodedText.Encode("firstFrame");
    private static readonly JsonEncodedText SubmitElapsedUs = JsonEncodedText.Encode("submitElapsedUs");
    private static readonly JsonEncodedText Version = JsonEncodedText.Encode("compactFrameReplyVersion");

    public override CompactFrameReplyFields Read(ref Utf8JsonReader reader, Type typeToConvert,
        JsonSerializerOptions options) => throw new NotSupportedException();

    public override void Write(Utf8JsonWriter writer, CompactFrameReplyFields value,
        JsonSerializerOptions options)
    {
        writer.WriteStartObject();
        writer.WriteNumber(ProtocolVersion, value.protocolVersion);
        writer.WriteBoolean(Ok, value.ok);
        writer.WriteString(Command, value.command);
        writer.WriteString(RequestId, value.requestId);
        writer.WriteNumber(Sequence, value.sequence);
        writer.WriteString(Binding, value.frameReplyBinding);
        writer.WriteString(Status, value.status);
        if (value.firstFrame.HasValue) writer.WriteBoolean(FirstFrame, value.firstFrame.Value);
        else writer.WriteNull(FirstFrame);
        writer.WriteNumber(SubmitElapsedUs, value.submitElapsedUs);
        writer.WriteNumber(Version, 1);
        writer.WriteEndObject();
    }
}
