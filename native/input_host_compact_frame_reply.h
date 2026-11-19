#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace ymcc::inputHost::compactReply {
inline constexpr int kVersion = 1;
inline constexpr int kRequestVersion = 1;
inline constexpr const char* kRequestTupleFields[] = {
    "runId", "epoch", "powerGeneration", "targetId", "ownerIdentity",
    "persona", "profileIdentity", "configRevision", "configHash"
};
inline constexpr std::size_t kMaxReplyBytes = 16 * 1024;
inline bool validToken(std::string_view token) noexcept {
    if (token.size() != 64) return false;
    for (const char c : token)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline bool revokesBinding(std::string_view command) noexcept {
    return command == "HELLO" || command == "PREPARE_TARGET" || command == "RECREATE_TARGET" ||
        command == "QUIESCE" || command == "RELEASE_TARGET" || command == "SHUTDOWN";
}
inline bool legacyIdentityField(std::string_view key) noexcept {
    return key == "runId" || key == "epoch" || key == "powerGeneration" ||
        key == "targetId" || key == "ownerIdentity" || key == "persona" ||
        key == "profileIdentity" || key == "startupNonce" || key == "configRevision" ||
        key == "configHash" || key == "assetVerified" || key == "lockedCoreSha256" ||
        key == "lockedCoreMvid" || key == "hostInstanceId";
}
// The original DOM fallback keeps its legacy duplicate-key semantics. Only the
// new compact shape rejects duplicate admission keys and mixed identity forms.
struct DomKeys {
    // Request advertisement duplicates must not disable legacy reply binding.
    bool requestVersionSeen = false;
    bool requestVersionDuplicate = false;
    unsigned seen = 0;
    bool duplicate = false;
    bool legacyIdentity = false;
    bool wireValid = false;
    void observeWire(std::string_view wire) noexcept {
        // nlohmann treats a raw NUL as EOF. The compact protocol must validate
        // the complete pipe message before inheriting any omitted identity.
        wireValid = !wire.empty() && wire.size() <= kMaxReplyBytes && wire.find('\0') == std::string_view::npos;
    }
    void key(std::string_view name) noexcept {
        if (name == "compactFrameRequestVersion") {
            requestVersionDuplicate |= requestVersionSeen;
            requestVersionSeen = true;
        }
        unsigned bit = 0;
        if (name == "compactFrameReplyVersion") bit = 1u;
        else if (name == "frameReplyBinding") bit = 2u;
        else if (name == "protocolVersion") bit = 4u;
        else if (name == "command") bit = 8u;
        else if (name == "requestId") bit = 16u;
        else if (name == "sequence") bit = 32u;
        else if (name == "ok") bit = 64u;
        else if (name == "status") bit = 128u;
        else if (name == "firstFrame") bit = 256u;
        else if (name == "submitElapsedUs") bit = 512u;
        if (bit) { duplicate |= (seen & bit) != 0; seen |= bit; }
        legacyIdentity |= legacyIdentityField(name);
    }
};
inline bool requestEnabled(std::string_view token, bool advertised) noexcept {
    return advertised && validToken(token);
}
// Call only after the original full PREPARE/RECREATE identity/status admission.
template<class Json>
bool advertisesRequest(const Json& reply, const DomKeys& keys) {
    if (!keys.wireValid || !keys.requestVersionSeen || keys.requestVersionDuplicate) return false;
    const auto version = reply.find("compactFrameRequestVersion");
    return version != reply.end() && version->is_number_unsigned() &&
        version->template get<std::uint64_t>() == kRequestVersion;
}
// Diagnostic-only expansion, before the sole binding append. Mixed tuple forms
// cannot mask a serializer error; no request bytes or production state change.
template<class Json>
bool expandRequestForParity(Json& request, const Json& full) {
    if (!request.is_object() || !full.is_object()) return false;
    const auto version = request.find("compactFrameRequestVersion");
    if (version == request.end() || !version->is_number_unsigned() ||
        version->template get<std::uint64_t>() != kRequestVersion) return false;
    for (const char* field : kRequestTupleFields)
        if (request.contains(field) || !full.contains(field)) return false;
    request.erase("compactFrameRequestVersion");
    for (const char* field : kRequestTupleFields) request[field] = full.at(field);
    return true;
}
template<class Json>
bool mayExpand(const Json& reply, const std::string* expected, const DomKeys& keys) {
    if (!expected || !validToken(*expected) || !keys.wireValid || keys.duplicate || keys.legacyIdentity ||
        (keys.seen & 1023u) != 1023u) return false;
    const auto version = reply.find("compactFrameReplyVersion"), binding = reply.find("frameReplyBinding");
    const auto protocol = reply.find("protocolVersion"), sequence = reply.find("sequence");
    const auto first = reply.find("firstFrame"), elapsed = reply.find("submitElapsedUs");
    const auto ok = reply.find("ok"), command = reply.find("command"), status = reply.find("status");
    return protocol != reply.end() && protocol->is_number_unsigned() && protocol->template get<std::uint64_t>() == 1 &&
        sequence != reply.end() && sequence->is_number_unsigned() && sequence->template get<std::uint64_t>() != 0 &&
        first != reply.end() && first->is_boolean() && elapsed != reply.end() && elapsed->is_number_unsigned() &&
        version != reply.end() && version->is_number_unsigned() && version->template get<std::uint64_t>() == kVersion &&
        binding != reply.end() && binding->is_string() && binding->template get_ref<const std::string&>() == *expected &&
        ok != reply.end() && ok->is_boolean() && ok->template get<bool>() &&
        command != reply.end() && command->is_string() && command->template get_ref<const std::string&>() == "SUBMIT_FRAME" &&
        status != reply.end() && status->is_string() && status->template get_ref<const std::string&>() == "frame-accepted";
}
} // namespace ymcc::inputHost::compactReply
