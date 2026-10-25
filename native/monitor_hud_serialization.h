#pragma once
// Frame body is already serialized/bounded by the native carrier. Reuse it
// instead of copying its JSON tree and serializing that body a second time.
#include <string>
#include <string_view>
namespace ymcc::hudserialization {
inline std::string desktopFrameEvent(std::string_view body) {
    constexpr std::string_view prefix = "{\"data\":";
    constexpr std::string_view suffix = ",\"event\":\"desktopHud.frame\"}";
    std::string text; text.reserve(prefix.size() + body.size() + suffix.size());
    text.append(prefix); text.append(body); text.append(suffix);
    return text;
}
} // namespace ymcc::hudserialization
