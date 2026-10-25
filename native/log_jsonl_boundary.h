#pragma once
#include <filesystem>
#include <fstream>

namespace ymcc::logging {
// At stream-open only: retain an interrupted previous writer's bytes, but keep
// its unfinished tail from swallowing this writer's first complete JSON row.
// No timer, whole-file scan, truncation, parsing, or per-row filesystem probe.
inline bool openJsonLineAppend(std::ofstream& output, const std::filesystem::path& path) {
    output.clear();
    output.open(path, std::ios::binary | std::ios::app);
    if (!output) return false;
    std::ifstream previous(path, std::ios::binary | std::ios::ate);
    if (!previous || previous.tellg() < std::streampos(0)) { output.close(); return false; }
    if (previous.tellg() > std::streampos(0)) {
        previous.seekg(-1, std::ios::end);
        char last = 0;
        if (!previous.get(last)) { output.close(); return false; }
        if (last != '\n') { output.put('\n'); output.flush(); }
    }
    return output.good();
}
}
