#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace ymcc::inputHost::compactReply {
inline constexpr int kVersion = 1;
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
