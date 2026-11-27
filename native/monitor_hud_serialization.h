#pragma once
// Frame body is already serialized/bounded by the native carrier. Reuse it
// instead of copying its JSON tree and serializing that body a second time.
#include <string>
#include <string_view>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
namespace ymcc::hudserialization {
inline std::string desktopFrameEvent(std::string_view body) {
    constexpr std::string_view prefix = "{\"data\":";
    constexpr std::string_view suffix = ",\"event\":\"desktopHud.frame\"}";
    std::string text; text.reserve(prefix.size() + body.size() + suffix.size());
    text.append(prefix); text.append(body); text.append(suffix);
    return text;
}

template<class Json>
class DesktopLayoutKey {
    struct FlatShape {
        bool crosshair;
        std::string layout;
        double scale;
        int gap;
        Json fields;
    };
    std::optional<FlatShape> flat_;
    std::string serialized_;
    static bool ascii(std::string_view text) noexcept {
        return std::all_of(text.begin(), text.end(), [](unsigned char ch) { return ch < 128; });
    }
    static std::string serialize(bool crosshair, const std::string& layout, double scale,
                                 int gap, const Json& fields) {
        return Json{{"crosshair", crosshair}, {"layout", layout}, {"scale", scale},
                    {"gap", gap}, {"fields", fields}}.dump();
    }
public:
    bool update(bool crosshair, const Json& appearance) {
        auto layout = appearance.value("layout", std::string{});
        const auto scale = appearance.value("scale", 1.0);
        const auto gap = appearance.value("gap", 8);
        static const Json emptyFields = Json::object();
        const auto it = appearance.find("fields");
        const auto& fields = it == appearance.end() ? emptyFields : *it;
        const bool sameScalars = flat_ && flat_->crosshair == crosshair && flat_->layout == layout &&
            flat_->gap == gap && flat_->scale == scale && std::signbit(flat_->scale) == std::signbit(scale);
        if (sameScalars && flat_->fields == fields) return false;
        // Normal settings are an ASCII string and an object of boolean flags.
        // Arbitrary JSON stays on the serialized comparison path.
        bool flat = fields.is_object() && std::isfinite(scale) && (sameScalars || ascii(layout));
        if (flat) for (auto flag = fields.begin(); flag != fields.end(); ++flag) {
            if (!flag->is_boolean() || !ascii(flag.key())) { flat = false; break; }
        }
        if (flat && flat_) {
            // Finite doubles round-trip through JSON, and the sign check
            // above distinguishes both spellings of zero.
            flat_.emplace(FlatShape{crosshair, std::move(layout), scale, gap, fields});
            return true;
        }
        const auto serialized = serialize(crosshair, layout, scale, gap, fields);
        const bool changed = flat_
            ? serialize(flat_->crosshair, flat_->layout, flat_->scale, flat_->gap, flat_->fields) != serialized
            : serialized_ != serialized;
        if (flat) {
            flat_.emplace(FlatShape{crosshair, std::move(layout), scale, gap, fields});
            serialized_.clear();
        } else {
            serialized_ = serialized;
            flat_.reset();
        }
        return changed;
    }
};
} // namespace ymcc::hudserialization
