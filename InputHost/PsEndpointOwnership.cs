using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace YeManInputHost.Diagnostics;

// One owner per HostSession, under its existing lifecycle lock. Only the caller's
// explicit create/query invokes Capture; serialization and frame submission do not.
internal sealed class PsEndpointOwnership
{
    internal const int Version = 1;
    internal const string Persona = "dualsense-edge";
    internal const int MaximumNodes = 128, MaximumDepth = 16, MaximumIdLength = 256;
    internal const int MaximumEvidenceBytes = 8192;
    internal const int Success = 0, NoSuchDevnode = 0x0D;
    private const uint Started = 0x00000008;
    private const string Source = "sdk-instance-id-and-cm-subtree";

    internal interface ICmReader
    {
        int Locate(string instanceId, out uint node);
        int GetChild(uint node, out uint child);
        int GetSibling(uint node, out uint sibling);
        int GetDeviceId(uint node, out string instanceId);
        int GetStatus(uint node, out uint status, out uint problem);
    }

    internal sealed record SdkContract(string TypeName, Guid Mvid);
    private static readonly SdkContract ApprovedSdk = new("HIDMaestro.HMController",
        new Guid("86aed9a3-c024-4596-b02c-650082ffb852"));
    internal sealed record Binding(string HostInstanceId, string RunId, ulong Epoch,
        string TargetId, string Persona);
    internal sealed record Evidence(
        [property: JsonPropertyName("schemaVersion")] int SchemaVersion,
        [property: JsonPropertyName("persona")] string Persona,
        [property: JsonPropertyName("hostInstanceId")] string HostInstanceId,
        [property: JsonPropertyName("runId")] string RunId,
        [property: JsonPropertyName("epoch")] ulong Epoch,
        [property: JsonPropertyName("targetId")] string TargetId,
        [property: JsonPropertyName("bindingGeneration")] ulong BindingGeneration,
        [property: JsonPropertyName("source")] string Source,
        [property: JsonPropertyName("sdkRootInstanceId")] string SdkRootInstanceId,
        [property: JsonPropertyName("hidInstanceIds")] IReadOnlyList<string> HidInstanceIds,
        [property: JsonPropertyName("complete")] bool Complete,
        [property: JsonPropertyName("reason")] string Reason);

    private readonly SdkContract _sdk;
    private Binding? _binding;
    private object? _controller;
    private ulong _generation;
    private bool _active, _exhausted;
    private Evidence? _evidence;
    internal PsEndpointOwnership() : this(ApprovedSdk) { }
    // Data-only metadata seam for SDK-free fixtures; shipping uses ApprovedSdk.
    internal PsEndpointOwnership(SdkContract sdk) => _sdk = sdk;
    internal ulong BindingGeneration => _generation;

    // Call immediately before the real SDK factory, not before backend preparation.
    internal void BeginCreate(Binding binding)
    {
        _active = false; _controller = null; _binding = binding;
        if (_generation == ulong.MaxValue) _exhausted = true;
        else ++_generation;
        _active = !_exhausted;
        _evidence = Incomplete(_exhausted ? "binding-generation-exhausted" : "sdk-create-pending");
    }

    internal void Created(object? controller)
    {
        if (!_active) return;
        _controller = controller;
        _evidence = Incomplete(controller is null ? "sdk-controller-unavailable" : "pnp-capture-pending");
    }

    // Release invalidates before any barrier, disposal or cleanup can fail.
    internal void Invalidate(string reason)
    {
        if (_binding is null) return;
        _active = false; _controller = null;
        _evidence = Incomplete(reason);
    }

    internal Evidence? ForBinding(Binding binding) => _binding == binding && _generation > 0 ? _evidence : null;

    private Evidence? Incomplete(string reason, string root = "") => _binding is null ? null :
        new(Version, Persona, _binding.HostInstanceId, _binding.RunId, _binding.Epoch,
            _binding.TargetId, _generation, Source, root, Array.Empty<string>(), false, reason);

    internal Evidence? Capture(object? controller, bool assetVerified, ICmReader cm)
    {
        // A failed/partial read replaces the previous complete observation. It
        // cannot publish partial IDs, nor borrow a pin from a released object.
        _evidence = Incomplete("ownership-read-incomplete");
        string root = "";
        Evidence? Fail(string reason) => _evidence = Incomplete(reason, root);
        try
        {
            if (!_active || _exhausted) return Fail(_exhausted ? "binding-generation-exhausted" : "binding-invalidated");
            if (_binding is null || _binding.Persona != Persona || _binding.HostInstanceId.Length == 0 ||
                _binding.RunId.Length == 0 || _binding.Epoch == 0 || _binding.TargetId.Length == 0)
                return Fail("binding-unavailable");
            if (controller is null || !ReferenceEquals(controller, _controller)) return Fail("current-sdk-controller-unavailable");
            if (!assetVerified) return Fail("sdk-asset-unverified");
            var type = controller.GetType();
            if (!type.IsClass || !type.IsSealed || type.IsGenericType || type.FullName != _sdk.TypeName ||
                type.Module.ModuleVersionId != _sdk.Mvid) return Fail("sdk-type-metadata-mismatch");
            // Exact approved internal, nonvirtual, parameterless property. No
            // alternate field names or null-to-global-enumeration fallback.
            var property = type.GetProperty("InstanceId", BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly);
            var getter = property?.GetGetMethod(nonPublic: true);
            if (property?.PropertyType != typeof(string) || property.DeclaringType != type ||
                property.GetIndexParameters().Length != 0 || property.GetSetMethod(true) is not null ||
                getter is null || !getter.IsAssembly || getter.IsStatic || getter.IsVirtual)
                return Fail("sdk-instance-id-property-unavailable");
            var declared = property.GetValue(controller) as string;
            // Stock USBip controllers deliberately have null InstanceId. They
            // remain usable targets, but this contract cannot prove their PnP owner.
            if (string.IsNullOrEmpty(declared)) return Fail("sdk-instance-id-unavailable");
            if (!ValidId(declared)) return Fail("sdk-instance-id-invalid");
            root = declared;
            if (cm.Locate(root, out var rootNode) != Success) return Fail("cm-root-unavailable");
            var pending = new Stack<(uint Node, int Depth)>();
            var nodes = new HashSet<uint>();
            var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var hids = new List<string>();
            pending.Push((rootNode, 0));
            while (pending.Count != 0)
            {
                var (node, depth) = pending.Pop();
                if (depth > MaximumDepth) return Fail("cm-depth-limit");
                if (nodes.Count >= MaximumNodes) return Fail("cm-node-limit");
                if (!nodes.Add(node)) return Fail("cm-cycle-or-duplicate-node");
                if (cm.GetDeviceId(node, out var id) != Success || !ValidId(id)) return Fail("cm-device-id-unavailable");
                if (!ids.Add(id)) return Fail("cm-duplicate-instance-id");
                if (depth == 0 && !StringComparer.OrdinalIgnoreCase.Equals(id, root)) return Fail("cm-root-identity-mismatch");
                if (cm.GetStatus(node, out var status, out var problem) != Success) return Fail("cm-status-unavailable");
                if ((status & Started) == 0 || problem != 0) return Fail("cm-node-not-started");
                if (depth != 0 && id.StartsWith("HID\\", StringComparison.OrdinalIgnoreCase)) hids.Add(id);
                // Only a node reached as this root's child/descendant may have
                // its sibling followed. Never enumerate the root's siblings.
                if (depth != 0)
                {
                    var siblingResult = cm.GetSibling(node, out var sibling);
                    if (siblingResult == Success) pending.Push((sibling, depth));
                    else if (siblingResult != NoSuchDevnode) return Fail("cm-sibling-read-failed");
                }
                var childResult = cm.GetChild(node, out var child);
                if (childResult == Success)
                {
                    if (depth == MaximumDepth) return Fail("cm-depth-limit");
                    pending.Push((child, depth + 1));
                }
                else if (childResult != NoSuchDevnode) return Fail("cm-child-read-failed");
            }
            if (hids.Count == 0) return Fail("hid-descendant-unavailable");
            hids.Sort(StringComparer.OrdinalIgnoreCase);
            var complete = new Evidence(Version, Persona, _binding.HostInstanceId, _binding.RunId,
                _binding.Epoch, _binding.TargetId, _generation, Source, root, hids.AsReadOnly(), true, "owned-subtree-complete");
            if (Encoding.UTF8.GetByteCount(JsonSerializer.Serialize(complete)) > MaximumEvidenceBytes)
                return Fail("ownership-evidence-limit");
            return _evidence = complete;
        }
        catch { return Fail("ownership-read-exception"); }
    }

    private static bool ValidId(string? id)
    {
        if (string.IsNullOrWhiteSpace(id) || id.Length > MaximumIdLength || id != id.Trim() || !id.Contains('\\')) return false;
        foreach (var c in id) if (char.IsControl(c) || char.IsSurrogate(c)) return false;
        return true;
    }

    // Serialization is observational: no SDK/CM access and no generation changes.
    internal string Append(string response, Binding binding, int maximumBytes)
    {
        var evidence = ForBinding(binding);
        if (evidence is null || response.Length == 0 || response[^1] != '}') return response;
        try
        {
            string Add(Evidence value) => response[..^1] + ",\"psEndpointOwnership\":" + JsonSerializer.Serialize(value) + "}";
            var result = Add(evidence);
            if (Encoding.UTF8.GetByteCount(result) <= maximumBytes) return result;
            // Preserve the original command result when only ownership evidence
            // cannot fit, without silently publishing a truncated complete pin.
            result = Add(evidence with { Complete = false, HidInstanceIds = Array.Empty<string>(), Reason = "ownership-response-limit" });
            return Encoding.UTF8.GetByteCount(result) <= maximumBytes ? result : response;
        }
        catch { return response; }
    }
}

