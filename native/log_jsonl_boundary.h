#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace ymcc::logging {
// At stream-open only: retain an interrupted previous writer's bytes, but keep
// its unfinished tail from swallowing this writer's first complete JSON row.
// No timer, whole-file scan, truncation, parsing, or per-row filesystem probe.
inline bool openJsonLineAppend(std::ofstream& output, const std::filesystem::path& path,
                               std::uint64_t* flushedSeparatorOffset = nullptr) {
    if (flushedSeparatorOffset) *flushedSeparatorOffset = 0;
    output.clear();
    output.open(path, std::ios::binary | std::ios::app);
    if (!output) return false;
    std::ifstream previous(path, std::ios::binary | std::ios::ate);
    const auto end = previous.tellg();
    if (!previous || end < std::streampos(0)) { output.close(); return false; }
    if (end > std::streampos(0)) {
        previous.seekg(-1, std::ios::end);
        char last = 0;
        if (!previous.get(last)) { output.close(); return false; }
        if (last != '\n') {
            output.put('\n'); output.flush();
            if (output && flushedSeparatorOffset)
                *flushedSeparatorOffset = static_cast<std::uint64_t>(static_cast<std::streamoff>(end));
        }
    }
    return output.good();
}

// One-row writers can repair the tail and append through the same handle.
inline bool appendJsonLine(const std::filesystem::path& path, std::string_view line) {
    constexpr auto mode = std::ios::binary | std::ios::in | std::ios::out | std::ios::app | std::ios::ate;
    std::fstream file(path, mode);
    if (!file) {
        std::error_code error;
        if (!std::filesystem::create_directories(path.parent_path(), error) || error) return false;
        file.clear();
        file.open(path, mode);
        if (!file) return false;
    }
    const auto end = file.tellg();
    if (end < std::streampos(0)) return false;
    if (end != std::streampos(0)) {
        file.seekg(-1, std::ios::end);
        char last = 0;
        if (!file.get(last)) return false;
        file.seekp(0, std::ios::end);
        if (last != '\n') file.put('\n');
    }
    file.write(line.data(), static_cast<std::streamsize>(line.size()));
    file.put('\n');
    file.flush();
    return file.good();
}
}
