#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace ymcc::logexport {

inline std::string_view trimManifestLine(std::string_view line) noexcept {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    return line;
}

inline std::string_view lastFanRuntimeSample(std::string_view text) noexcept {
    for (std::size_t end = text.size(); end > 0;) {
        const auto separator = text.rfind('\n', end - 1);
        const auto start = separator == std::string_view::npos ? 0 : separator + 1;
        const auto line = trimManifestLine(text.substr(start, end - start));
        if (line.find("\"eventName\":\"host.runtime-sample\"") != std::string_view::npos) return line;
        if (start == 0) break;
        end = separator;
    }
    return {};
}

class FanHcSessionMatches {
    static constexpr std::size_t keep = 40;
    static constexpr std::size_t clip = 240;
    std::array<std::string, keep> recent_;
    std::size_t count_ = 0;

public:
    void scan(std::string_view text) {
        static constexpr std::string_view markers[] = {
            "OpenLibSys initialization", "RogAllyReadLoopStopRequested", "RogAllyReadLoopExit", "RogAllyCloseObserved",
        };
        while (!text.empty()) {
            const auto separator = text.find('\n');
            const auto line = trimManifestLine(text.substr(0, separator));
            for (const auto marker : markers) {
                if (line.find(marker) == std::string_view::npos) continue;
                auto& hit = recent_[count_++ % keep];
                hit.assign(line.substr(0, clip));
                if (line.size() > clip) hit.append("...(clipped)");
                break;
            }
            if (separator == std::string_view::npos) break;
            text.remove_prefix(separator + 1);
        }
    }

    std::size_t count() const noexcept { return count_; }

    void append(std::string& out) const {
        if (count_ > keep) out += "(showing last " + std::to_string(keep) + " of " + std::to_string(count_) + ")\n";
        for (auto i = count_ > keep ? count_ - keep : 0; i < count_; ++i) {
            out += recent_[i % keep];
            out += '\n';
        }
    }
};

} // namespace ymcc::logexport
