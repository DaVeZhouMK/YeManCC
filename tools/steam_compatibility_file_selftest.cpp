#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#if defined(YMCC_STEAM_COLD_FILE_FLIGHT)
#include "fixtures/steam_cold_file_flight/ports.h"
#define ReadFile cold_file_ports::read
#endif
#include "../native/steam_compatibility_win32.h"
#if defined(YMCC_STEAM_COLD_FILE_FLIGHT)
#undef ReadFile
#include <future>
#endif
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace compat = ymcc::steamcompat;
using Bytes = std::vector<unsigned char>;
struct Case { std::string name; bool pass; std::string observed; std::uint64_t rva, mutableIgnored; };
struct SampleProof { std::string name, sha, timestampSha, overlaySha; std::uint64_t size, relocations; };
std::vector<Case> cases;
std::vector<SampleProof> samples;
fs::path output;

std::string quoted(const std::string& value) {
    std::ostringstream out; out << '"';
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') out << '\\' << static_cast<char>(ch);
        else if (ch < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(ch) << std::dec;
        else out << static_cast<char>(ch);
    }
    return out.str() + '"';
}
void check(std::string name, bool pass, std::string observed = {}, std::uint64_t rva = 0, std::uint64_t mutableIgnored = 0) {
    if (!pass) std::cerr << "FAIL " << name << ": " << observed << " rva=" << std::hex << rva << std::dec << '\n';
    cases.push_back({std::move(name), pass, std::move(observed), rva, mutableIgnored});
}
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Bytes readFile(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    require(bool(stream), "fixture input open");
    const auto length = stream.tellg();
    require(length > 0 && length <= 256LL * 1024 * 1024, "fixture input size");
    Bytes result(static_cast<std::size_t>(length)); stream.seekg(0);
    stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    require(bool(stream), "fixture input read"); return result;
}
void writeFile(const fs::path& file, const Bytes& data) {
    std::ofstream stream(file, std::ios::binary | std::ios::trunc); require(bool(stream), "fixture output open/retained handle");
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    require(bool(stream), "fixture output write");
}
std::string digest(const Bytes& data) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    require(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "SHA256 provider");
    struct CloseAlg { BCRYPT_ALG_HANDLE h; ~CloseAlg() { BCryptCloseAlgorithmProvider(h, 0); } } closeAlg{alg};
    DWORD length = 0, copied = 0;
    require(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &copied, 0) >= 0, "SHA256 object size");
    Bytes object(length);
    require(BCryptCreateHash(alg, &hash, object.data(), length, nullptr, 0, 0) >= 0, "SHA256 create");
    struct CloseHash { BCRYPT_HASH_HANDLE h; ~CloseHash() { BCryptDestroyHash(h); } } closeHash{hash};
    require(BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) >= 0, "SHA256 update");
    unsigned char result[32]{};
    require(BCryptFinishHash(hash, result, sizeof(result), 0) >= 0, "SHA256 finish");
    std::ostringstream text; for (auto byte : result) text << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return text.str();
}
template<class T> T at(const Bytes& data, std::size_t offset) {
    require(offset <= data.size() && sizeof(T) <= data.size() - offset, "fixture bounded field read");
    T result{}; std::memcpy(&result, data.data() + offset, sizeof(result)); return result;
}
template<class T> void put(Bytes& data, std::size_t offset, const T& value) {
    require(offset <= data.size() && sizeof(T) <= data.size() - offset, "fixture bounded field write");
    std::memcpy(data.data() + offset, &value, sizeof(value));
}
struct Header {
    std::size_t nt, file, optional, sections;
    IMAGE_FILE_HEADER fileHeader;
    IMAGE_OPTIONAL_HEADER64 opt;
    explicit Header(const Bytes& data) {
        const auto dos = at<IMAGE_DOS_HEADER>(data, 0); require(dos.e_lfanew >= 0, "fixture NT offset");
        nt = static_cast<std::size_t>(dos.e_lfanew); file = nt + sizeof(DWORD);
        fileHeader = at<IMAGE_FILE_HEADER>(data, file); optional = file + sizeof(IMAGE_FILE_HEADER);
        opt = at<IMAGE_OPTIONAL_HEADER64>(data, optional); sections = optional + fileHeader.SizeOfOptionalHeader;
    }
    std::size_t rawOffset(const Bytes& data, std::uint64_t rva, std::size_t size) const {
        if (rva < opt.SizeOfHeaders && size <= opt.SizeOfHeaders - rva) return static_cast<std::size_t>(rva);
        for (unsigned i = 0; i < fileHeader.NumberOfSections; ++i) {
            const auto section = at<IMAGE_SECTION_HEADER>(data, sections + i * sizeof(IMAGE_SECTION_HEADER));
            if (rva >= section.VirtualAddress && rva - section.VirtualAddress < section.SizeOfRawData && size <= section.SizeOfRawData - (rva - section.VirtualAddress))
                return static_cast<std::size_t>(section.PointerToRawData + (rva - section.VirtualAddress));
        }
        throw std::runtime_error("fixture RVA not file-backed");
    }
};
compat::Qualification fileQualification(const std::string& tag, const Bytes& bytes, compat::ModuleKind kind) {
    const auto active = output / L"fixture.bin";
    const auto next = output / L"fixture-next.bin";
    writeFile(next, bytes);
    require(MoveFileExW(next.c_str(), active.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE, "fixture atomic replacement/retained handle");
    const auto result = compat::qualifyFile(active.wstring(), kind);
    (void)tag; return result;
}
std::span<const compat::Range> recordRanges(compat::Layout layout) {
    using namespace compat::read_contract_detail;
    if (layout == compat::Layout::Legacy) return kRecordLegacyRanges;
    return kRecordPublicRanges;
}
std::span<const compat::Range> uiRanges(compat::Layout layout) {
    using namespace compat::read_contract_detail;
    if (layout == compat::Layout::Legacy) return kUiLegacyRanges;
    return kUiPublicRanges;
}
std::span<const compat::Range> publishRanges(compat::Layout layout) {
    using namespace compat::publish_contract_detail;
    if (layout == compat::Layout::Legacy) return legacyRanges;
    return publicRanges;
}
Bytes mappedImage(const Bytes& disk, const compat::PeMetadata& metadata, std::uint64_t base) {
    Header header(disk); Bytes mapped(static_cast<std::size_t>(metadata.imageSize));
    std::memcpy(mapped.data(), disk.data(), header.opt.SizeOfHeaders);
    for (unsigned i = 0; i < header.fileHeader.NumberOfSections; ++i) {
        const auto section = at<IMAGE_SECTION_HEADER>(disk, header.sections + i * sizeof(IMAGE_SECTION_HEADER));
        if (section.SizeOfRawData) std::memcpy(mapped.data() + section.VirtualAddress, disk.data() + section.PointerToRawData, section.SizeOfRawData);
    }
    const auto delta = base - metadata.preferredBase;
    for (const auto rva : metadata.relocations) put(mapped, rva, at<std::uint64_t>(mapped, rva) + delta);
    return mapped;
}
compat::ReadRva readerFor(const Bytes& mapped) {
    return [&mapped](std::uint64_t rva, void* target, std::size_t size) {
        if (!target || !size || rva >= mapped.size() || size > mapped.size() - rva) return false;
        std::memcpy(target, mapped.data() + rva, size); return true;
    };
}
void parserCases(const std::string& name, const Bytes& original, compat::ModuleKind kind) {
    Header h(original);
    const auto reject = [&](const std::string& label, const auto& edit) {
        auto data = original; edit(data);
        compat::detail::PeFile parser(data); const bool parsed = parser.parse();
        const auto q = fileQualification(label, data, kind);
        check(name + "/pe/" + label, !parsed && !q.layout, std::string("parse=") + (parsed ? "true; " : "false; ") + q.reason);
    };
    reject("DOS-magic", [](Bytes& d) { d[0] ^= 1; });
    reject("negative-e_lfanew", [](Bytes& d) { put<LONG>(d, offsetof(IMAGE_DOS_HEADER, e_lfanew), -1); });
    reject("huge-e_lfanew", [](Bytes& d) { put<LONG>(d, offsetof(IMAGE_DOS_HEADER, e_lfanew), 0x7fffffff); });
    reject("NT-signature", [&](Bytes& d) { put<DWORD>(d, h.nt, 0); });
    reject("x86-machine", [&](Bytes& d) { put<WORD>(d, h.file + offsetof(IMAGE_FILE_HEADER, Machine), IMAGE_FILE_MACHINE_I386); });
    reject("zero-sections", [&](Bytes& d) { put<WORD>(d, h.file + offsetof(IMAGE_FILE_HEADER, NumberOfSections), 0); });
    reject("too-many-sections", [&](Bytes& d) { put<WORD>(d, h.file + offsetof(IMAGE_FILE_HEADER, NumberOfSections), 97); });
    reject("short-optional-header", [&](Bytes& d) { put<WORD>(d, h.file + offsetof(IMAGE_FILE_HEADER, SizeOfOptionalHeader), 1); });
    reject("PE32-optional-magic", [&](Bytes& d) { put<WORD>(d, h.optional + offsetof(IMAGE_OPTIONAL_HEADER64, Magic), IMAGE_NT_OPTIONAL_HDR32_MAGIC); });
    reject("zero-image-size", [&](Bytes& d) { put<DWORD>(d, h.optional + offsetof(IMAGE_OPTIONAL_HEADER64, SizeOfImage), 0); });
    reject("image-over-cap", [&](Bytes& d) { put<DWORD>(d, h.optional + offsetof(IMAGE_OPTIONAL_HEADER64, SizeOfImage), 0xffffffff); });
    reject("invalid-preferred-base", [&](Bytes& d) { put<ULONGLONG>(d, h.optional + offsetof(IMAGE_OPTIONAL_HEADER64, ImageBase), 0xffff); });
    reject("section-virtual-overflow", [&](Bytes& d) { put<DWORD>(d, h.sections + offsetof(IMAGE_SECTION_HEADER, VirtualAddress), 0xfffffff0); });
    reject("section-extent-overflow", [&](Bytes& d) { put<DWORD>(d, h.sections + offsetof(IMAGE_SECTION_HEADER, Misc), 0xffffffff); });
    reject("section-raw-overflow", [&](Bytes& d) { put<DWORD>(d, h.sections + offsetof(IMAGE_SECTION_HEADER, PointerToRawData), 0xfffffff0); });
    reject("overlapping-virtual-sections", [&](Bytes& d) {
        require(h.fileHeader.NumberOfSections > 1, "fixture two sections");
        const auto first = at<IMAGE_SECTION_HEADER>(d, h.sections);
        put<DWORD>(d, h.sections + sizeof(IMAGE_SECTION_HEADER) + offsetof(IMAGE_SECTION_HEADER, VirtualAddress), first.VirtualAddress);
    });
    reject("truncated-header", [](Bytes& d) { d.resize(80); });
    reject("truncated-section-data", [&](Bytes& d) {
        const auto first = at<IMAGE_SECTION_HEADER>(d, h.sections);
        d.resize(static_cast<std::size_t>(first.PointerToRawData) + first.SizeOfRawData - 1);
    });
    const auto directory = h.opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    require(directory.Size >= sizeof(IMAGE_BASE_RELOCATION) + sizeof(WORD), "sample relocation directory");
    const auto reloc = h.rawOffset(original, directory.VirtualAddress, sizeof(IMAGE_BASE_RELOCATION) + sizeof(WORD));
    reject("relocation-block-too-short", [&](Bytes& d) { put<DWORD>(d, reloc + offsetof(IMAGE_BASE_RELOCATION, SizeOfBlock), 6); });
    reject("relocation-block-odd-size", [&](Bytes& d) { put<DWORD>(d, reloc + offsetof(IMAGE_BASE_RELOCATION, SizeOfBlock), 9); });
    reject("relocation-block-outside-directory", [&](Bytes& d) { put<DWORD>(d, reloc + offsetof(IMAGE_BASE_RELOCATION, SizeOfBlock), 0xfffffffe); });
    reject("unsupported-relocation-type", [&](Bytes& d) { put<WORD>(d, reloc + sizeof(IMAGE_BASE_RELOCATION), static_cast<WORD>(0x3000)); });
    reject("relocation-target-overflow", [&](Bytes& d) {
        put<DWORD>(d, reloc + offsetof(IMAGE_BASE_RELOCATION, VirtualAddress), 0xfffff000);
        put<WORD>(d, reloc + sizeof(IMAGE_BASE_RELOCATION), static_cast<WORD>(0xafff));
    });
    reject("duplicate-overlapping-DIR64", [&](Bytes& d) {
        const auto block = at<IMAGE_BASE_RELOCATION>(d, reloc);
        require(block.SizeOfBlock >= sizeof(block) + 2 * sizeof(WORD), "fixture relocation entries");
        put<WORD>(d, reloc + sizeof(block), static_cast<WORD>(0xa100));
        put<WORD>(d, reloc + sizeof(block) + sizeof(WORD), static_cast<WORD>(0xa104));
    });
}
void contractMutations(const std::string& name, const Bytes& original, compat::ModuleKind kind, compat::Layout layout) {
    Header header(original);
    auto ranges = kind == compat::ModuleKind::Client ? recordRanges(layout) : uiRanges(layout);
    const auto mutate = [&](const std::string& label, std::uint64_t rva) {
        auto data = original; data[header.rawOffset(data, rva, 1)] ^= 1;
        const auto q = fileQualification(label, data, kind);
        check(name + "/contract/" + label, !q.layout, q.reason, rva);
    };
    const auto longest = *std::max_element(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) { return a.size < b.size; });
    mutate("complete-callee-first-byte", longest.rva);
    mutate("complete-callee-middle-byte", longest.rva + longest.size / 2);
    mutate("complete-callee-last-byte", longest.rva + longest.size - 1);
    const auto scanCode = [&](const std::string& label, const auto& predicate, std::size_t displacement) {
        for (const auto& range : ranges) {
            if (range.size < 20 || range.size > 10000) continue;
            const auto raw = header.rawOffset(original, range.rva, range.size);
            for (std::size_t j = 0; j + 8 < range.size; ++j) {
                if (predicate(original.data() + raw + j)) { mutate(label, range.rva + j + displacement); return; }
            }
        }
        throw std::runtime_error("no instruction fixture for " + label);
    };
    scanCode("rel32-call-displacement", [](const unsigned char* p) { return p[0] == 0xe8; }, 1);
    if (kind == compat::ModuleKind::Client) {
        // A single writable-global operand may legitimately relocate. Corrupt
        // one of multiple references to the SAME global to require a conflict.
        const auto graph = compat::recordGraph(layout);
        const auto& firstCode = ranges.front();
        const auto repeated = std::find_if(graph.fixups.begin(), graph.fixups.end(), [&](const auto& field) {
            return field.kind == compat::FixupKind::Relative32 && field.fieldRva >= firstCode.rva &&
                field.fieldRva - firstCode.rva < firstCode.size &&
                std::any_of(graph.fixups.begin(), graph.fixups.end(), [&](const auto& other) {
                    return other.fieldRva != field.fieldRva && other.targetRva == field.targetRva;
                });
        });
        require(repeated != graph.fixups.end(), "repeated-global RIP reference fixture");
        mutate("RIP-memory-displacement", repeated->fieldRva);
    } else {
        scanCode("RIP-memory-displacement", [](const unsigned char* p) { return p[0] == 0x48 && (p[1] == 0x8b || p[1] == 0x8d) && (p[2] & 0xc7) == 5; }, 3);
    }
    std::span<const compat::read_contract_detail::PointerOrigin> pointers;
    if (kind == compat::ModuleKind::Client) pointers = layout == compat::Layout::Legacy ? std::span(compat::read_contract_detail::kRecordLegacyPointers) : std::span(compat::read_contract_detail::kRecordPublicPointers);
    else pointers = layout == compat::Layout::Legacy ? std::span(compat::read_contract_detail::kUiLegacyPointers) : std::span(compat::read_contract_detail::kUiPublicPointers);
    require(!pointers.empty(), "vtable pointer contracts");
    mutate("vtable-slot-or-COL-pointer", ranges[pointers.back().rangeIndex].rva);
    if (kind == compat::ModuleKind::Client) {
        const auto publisher = publishRanges(layout);
        const auto complete = *std::max_element(publisher.begin(), publisher.end(), [](const auto& a, const auto& b) { return a.size < b.size; });
        mutate("publisher-complete-callee-first", complete.rva);
        mutate("publisher-complete-callee-middle", complete.rva + complete.size / 2);
        mutate("publisher-complete-callee-last", complete.rva + complete.size - 1);
    }
}
void loadedCases(const std::string& name, const Bytes& original, compat::ModuleKind kind, const compat::Qualification& disk) {
    require(bool(disk.metadata) && bool(disk.layout), "qualified disk prerequisite");
    const auto size = disk.metadata->imageSize, preferred = disk.metadata->preferredBase;
    const auto ranges = kind == compat::ModuleKind::Client ? recordRanges(*disk.layout) : uiRanges(*disk.layout);
    for (const auto base : {preferred, preferred + 0x20000000ULL, preferred - 0x10000000ULL}) {
        auto mapped = mappedImage(original, *disk.metadata, base);
        const auto read = readerFor(mapped);
        const auto proof = compat::qualifyLoaded(disk, kind, read, base, size);
        check(name + "/loaded/ASLR-" + std::to_string(base), proof.matched, proof.reason, proof.failedRva, proof.mutableRangesIgnored);
        const auto key = ranges.front().rva;
        mapped[static_cast<std::size_t>(key)] ^= 1;
        const auto bad = compat::qualifyLoaded(disk, kind, read, base, size);
        check(name + "/loaded/code-tamper-" + std::to_string(base), !bad.matched, bad.reason, bad.failedRva);
        mapped[static_cast<std::size_t>(key)] ^= 1;
    }
    const auto base = preferred + 0x20000000ULL;
    auto mapped = mappedImage(original, *disk.metadata, base);
    auto read = readerFor(mapped);
    std::span<const compat::read_contract_detail::PointerOrigin> pointers;
    if (kind == compat::ModuleKind::Client) pointers = *disk.layout == compat::Layout::Legacy ? std::span(compat::read_contract_detail::kRecordLegacyPointers) : std::span(compat::read_contract_detail::kRecordPublicPointers);
    else pointers = *disk.layout == compat::Layout::Legacy ? std::span(compat::read_contract_detail::kUiLegacyPointers) : std::span(compat::read_contract_detail::kUiPublicPointers);
    const auto pointerRva = ranges[pointers.back().rangeIndex].rva;
    mapped[static_cast<std::size_t>(pointerRva)] ^= 1;
    const auto pointerProof = compat::qualifyLoaded(disk, kind, read, base, size);
    check(name + "/loaded/relocated-vtable-tamper", !pointerProof.matched, pointerProof.reason, pointerProof.failedRva);
    mapped[static_cast<std::size_t>(pointerRva)] ^= 1;
    auto missing = disk; missing.metadata.reset();
    check(name + "/loaded/missing-metadata", !compat::qualifyLoaded(missing, kind, read, base, size).matched);
    auto unqualified = disk; unqualified.layout.reset();
    check(name + "/loaded/unqualified-disk", !compat::qualifyLoaded(unqualified, kind, read, base, size).matched);
    check(name + "/loaded/wrong-image-size", !compat::qualifyLoaded(disk, kind, read, base, size - 1).matched);
    check(name + "/loaded/low-base", !compat::qualifyLoaded(disk, kind, read, 0xffff, size).matched);
    check(name + "/loaded/no-reader", !compat::qualifyLoaded(disk, kind, {}, base, size).matched);
    check(name + "/loaded/read-failure", !compat::qualifyLoaded(disk, kind, [](auto, void*, auto) { return false; }, base, size).matched);
    const auto thrown = compat::qualifyLoaded(disk, kind, [](auto, void*, auto) -> bool { throw std::runtime_error("injected reader exception"); }, base, size);
    check(name + "/loaded/reader-exception", !thrown.matched && std::string(thrown.reason) == "contract-read-exception", thrown.reason);
    const auto overflowBase = (std::numeric_limits<std::uint64_t>::max)() - size + 2;
    const auto impossible = mappedImage(original, *disk.metadata, overflowBase);
    const auto overflowProof = compat::qualifyLoaded(disk, kind, readerFor(impossible), overflowBase, size);
    check(name + "/loaded/base-plus-size-overflow", !overflowProof.matched, overflowProof.reason, overflowProof.failedRva);
    // Loaded writable globals are runtime state, not file initializers.
    // Count the exact ignored descriptor ranges and change their bytes.
    std::uint64_t expectedIgnored = 0;
    std::set<std::uint64_t> mutableAddresses;
    const auto collectMutable = [&](std::span<const compat::Range> contract) {
        for (const auto& range : contract) {
            for (const auto& section : disk.metadata->sections) {
                const auto extent = (std::max)(section.virtualSize, section.rawSize);
                if ((section.characteristics & IMAGE_SCN_MEM_WRITE) && range.rva >= section.rva &&
                    range.rva - section.rva < extent && range.size <= extent - (range.rva - section.rva)) {
                    ++expectedIgnored; mutableAddresses.insert(range.rva); break;
                }
            }
        }
    };
    collectMutable(ranges);
    if (kind == compat::ModuleKind::Client) collectMutable(publishRanges(*disk.layout));
    for (const auto rva : mutableAddresses) mapped[static_cast<std::size_t>(rva)] ^= 0x5a;
    const auto mutableProof = compat::qualifyLoaded(disk, kind, read, base, size);
    check(name + "/loaded/live-mutable-globals-reported-not-file-compared",
        mutableProof.matched && expectedIgnored > 0 && mutableProof.mutableRangesIgnored == expectedIgnored,
        std::string(mutableProof.reason) + "; expectedIgnored=" + std::to_string(expectedIgnored),
        mutableProof.failedRva, mutableProof.mutableRangesIgnored);
    mapped[static_cast<std::size_t>(ranges.front().rva)] ^= 1;
    const auto readonlyTamper = compat::qualifyLoaded(disk, kind, read, base, size);
    check(name + "/loaded/mutable-skip-does-not-hide-readonly-tamper", !readonlyTamper.matched, readonlyTamper.reason, readonlyTamper.failedRva);
    mapped = mappedImage(original, *disk.metadata, base);
    // This callback seam models the production executable-image validator.
    // No DLL is loaded and none of these synthetic external addresses is called.
    std::set<std::uint64_t> executableTargets;
    for (const auto& slot : disk.metadata->imports) {
        const auto target = 0x00007ff800000000ULL + slot.rva;
        put<std::uint64_t>(mapped, slot.rva, target); executableTargets.insert(target);
    }
    Header h(original); const auto iat = h.opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    std::uint64_t testedSlot = 0;
    for (const auto& range : ranges) {
        if (range.size == 8 && range.rva >= iat.VirtualAddress && range.rva - iat.VirtualAddress < iat.Size) {
            testedSlot = range.rva; break;
        }
    }
    require(testedSlot != 0 && !executableTargets.empty(), "contract-overlapping import fixture");
    const auto imports = compat::qualifyLoaded(disk, kind, read, base, size);
    check(name + "/loaded/normal-loader-resolved-imports", imports.matched, imports.reason, imports.failedRva, imports.mutableRangesIgnored);
    unsigned validationCalls = 0;
    const auto allowExecutable = [&](std::uint64_t address) {
        ++validationCalls; return executableTargets.contains(address);
    };
    const auto validated = compat::qualifyLoaded(disk, kind, read, base, size, allowExecutable);
    check(name + "/loaded/resolved-imports-executable-validator-accepted", validated.matched && validationCalls > 0,
        std::string(validated.reason) + "; validatorCalls=" + std::to_string(validationCalls), validated.failedRva, validated.mutableRangesIgnored);
    unsigned nonExecCalls = 0;
    const auto nonExec = compat::qualifyLoaded(disk, kind, read, base, size, [&](std::uint64_t) { ++nonExecCalls; return false; });
    check(name + "/loaded/non-executable-validator-rejected", !nonExec.matched && nonExecCalls > 0, nonExec.reason, nonExec.failedRva);
    const auto validatorThrows = compat::qualifyLoaded(disk, kind, read, base, size, [](std::uint64_t) -> bool { throw std::runtime_error("validator exception"); });
    check(name + "/loaded/import-validator-exception-rejected", !validatorThrows.matched && std::string(validatorThrows.reason) == "contract-read-exception", validatorThrows.reason, validatorThrows.failedRva);
    const auto savedTarget = at<std::uint64_t>(mapped, static_cast<std::size_t>(testedSlot));
    for (const std::uint64_t bad : {0ULL, 1ULL, 0xffffULL, 0xffffffffffffffffULL}) {
        put<std::uint64_t>(mapped, static_cast<std::size_t>(testedSlot), bad);
        const auto badDefault = compat::qualifyLoaded(disk, kind, read, base, size);
        check(name + "/loaded/bad-import-default-rejected-" + std::to_string(bad), !badDefault.matched, badDefault.reason, badDefault.failedRva);
        const auto badValidated = compat::qualifyLoaded(disk, kind, read, base, size, allowExecutable);
        check(name + "/loaded/bad-import-validator-rejected-" + std::to_string(bad), !badValidated.matched, badValidated.reason, badValidated.failedRva);
    }
    put<std::uint64_t>(mapped, static_cast<std::size_t>(testedSlot), savedTarget);
    auto unbound = mappedImage(original, *disk.metadata, base);
    const auto unresolved = compat::qualifyLoaded(disk, kind, readerFor(unbound), base, size, allowExecutable);
    check(name + "/loaded/unresolved-file-thunk-with-production-validator-rejected", !unresolved.matched, unresolved.reason, unresolved.failedRva);
    const auto badKind = compat::qualifyLoaded(disk, static_cast<compat::ModuleKind>(99), read, base, size, allowExecutable);
    check(name + "/loaded/invalid-module-kind", !badKind.matched, badKind.reason);

}
void checkQualificationDigest(const std::string& name, const compat::Qualification& q,
    const std::string& expected, std::uint64_t bytes, bool expectedHit) {
    check(name + "/digest-exact-content-SHA256", q.sha256 == expected, "sha256=" + q.sha256);
    check(name + "/digest-this-call-byte-counts", q.cacheHit == expectedHit &&
        q.fileBytesRead == (expectedHit ? 0 : bytes) && q.digestBytesHashed == (expectedHit ? 0 : bytes),
        "cacheHit=" + std::to_string(q.cacheHit) + "; fileBytesRead=" + std::to_string(q.fileBytesRead) +
        "; digestBytesHashed=" + std::to_string(q.digestBytesHashed));
}
void cacheCases(const std::string& name, const Bytes& original, compat::ModuleKind kind) {
    const auto file = output / (name + "-cache.bin"), next = output / (name + "-next.bin");
    writeFile(file, original);
    const auto first = compat::qualifyFile(file.wstring(), kind);
    const auto second = compat::qualifyFile(file.wstring(), kind);
    check(name + "/cache/cold-qualified", bool(first.layout) && !first.cacheHit, first.reason);
    check(name + "/cache/same-identity-hit", bool(second.layout) && second.cacheHit && first.identity == second.identity, second.reason);
    const auto expectedDigest = digest(original);
    checkQualificationDigest(name + "/cache/cold", first, expectedDigest, original.size(), false);
    checkQualificationDigest(name + "/cache/hot", second, expectedDigest, original.size(), true);
    const HANDLE exclusive = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(name + "/cache/exclusive-open-no-retained-source-handle", exclusive != INVALID_HANDLE_VALUE, "win32=" + std::to_string(GetLastError()));
    if (exclusive != INVALID_HANDLE_VALUE) CloseHandle(exclusive);
    std::atomic<unsigned> failures{0}, hits{0};
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 8; ++t) threads.emplace_back([&] {
        for (unsigned i = 0; i < 16; ++i) {
            const auto q = compat::qualifyFile(file.wstring(), kind);
            if (!q.layout || q.layout != first.layout || !(q.identity == first.identity) || !q.metadata || q.sha256 != expectedDigest || q.fileBytesRead || q.digestBytesHashed) ++failures;
            if (q.cacheHit) ++hits;
        }
    });
    for (auto& thread : threads) thread.join();
    check(name + "/cache/concurrent-8x16", failures == 0 && hits == 128, "failures=" + std::to_string(failures.load()) + "; hits=" + std::to_string(hits.load()));
    auto corrupt = original; Header h(corrupt);
    const auto range = kind == compat::ModuleKind::Client ? recordRanges(*first.layout).front() : uiRanges(*first.layout).front();
    corrupt[h.rawOffset(corrupt, range.rva, 1)] ^= 1;
    writeFile(next, corrupt);
    const bool replaced = MoveFileExW(next.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    check(name + "/cache/replace-after-qualification-no-retained-handle", replaced, "win32=" + std::to_string(GetLastError()));
    if (replaced) {
        const auto changed = compat::qualifyFile(file.wstring(), kind);
        check(name + "/cache/replacement-identity-invalidates", !changed.layout && !changed.cacheHit && !(changed.identity == first.identity), changed.reason);
        checkQualificationDigest(name + "/cache/replaced-rejected-content", changed, digest(corrupt), corrupt.size(), false);
        const auto repeated = compat::qualifyFile(file.wstring(), kind);
        checkQualificationDigest(name + "/cache/replaced-negative-hit", repeated, digest(corrupt), corrupt.size(), true);
        check(name + "/cache/negative-result-stable", !repeated.layout && repeated.cacheHit, repeated.reason);
    }
    const bool deleted = DeleteFileW(file.c_str()) != FALSE;
    check(name + "/cache/delete-after-qualified-objects-still-alive", deleted, "win32=" + std::to_string(GetLastError()));
    writeFile(file, original);
    const auto recreated = compat::qualifyFile(file.wstring(), kind);
    check(name + "/cache/deleted-then-recreated-not-stale", bool(recreated.layout) && !recreated.cacheHit && !(recreated.identity == first.identity), recreated.reason);
    checkQualificationDigest(name + "/cache/recreated", recreated, expectedDigest, original.size(), false);
    check(name + "/cache/old-returned-digest-not-rebound", first.sha256 == expectedDigest && second.sha256 == expectedDigest);
    require(DeleteFileW(file.c_str()) != FALSE, "fixture final deletion");
    const auto absent = compat::qualifyFile(file.wstring(), kind);
    check(name + "/cache/deleted-path-not-cache-hit", !absent.layout);
    check(name + "/cache/deleted-path-no-stale-digest", absent.sha256.empty() && !absent.cacheHit && !absent.fileBytesRead && !absent.digestBytesHashed);
}
void runSample(const std::string& name, const fs::path& source, compat::ModuleKind kind, compat::Layout expected) {
    const auto original = readFile(source); Header header(original);
    compat::detail::PeFile parser(original);
    require(parser.parse(), "original PE parse");
    const auto disk = fileQualification(name, original, kind);
    if (kind == compat::ModuleKind::Client) {
        const auto record = compat::checkRecordLayout(parser.image(), expected);
        const auto publisher = compat::checkPublishLayout(parser.image(), expected);
        check(name + "/original-record-domain", record.matched, record.reason, record.failedRva);
        check(name + "/original-publisher-domain", publisher.matched, publisher.reason, publisher.failedRva);
    }
    check(name + "/original-file-qualified", disk.layout == expected && bool(disk.metadata), disk.reason);

    auto timestamp = original;
    put<DWORD>(timestamp, header.file + offsetof(IMAGE_FILE_HEADER, TimeDateStamp), header.fileHeader.TimeDateStamp ^ 0x5a5a5a5a);
    put<WORD>(timestamp, header.optional + offsetof(IMAGE_OPTIONAL_HEADER64, MajorImageVersion), 0x7b7b);
    put<WORD>(timestamp, header.optional + offsetof(IMAGE_OPTIONAL_HEADER64, MinorImageVersion), 0x6c6c);
    const auto changed = fileQualification(name, timestamp, kind);
    const auto originalSha = digest(original), timestampSha = digest(timestamp);
    check(name + "/unknown-whole-SHA-timestamp-version-allowed", originalSha != timestampSha && changed.layout == expected, changed.reason);
    auto overlay = original;
    constexpr unsigned char marker[]{0x59, 0x4d, 0x43, 0x43, 0x2d, 0x55, 0x4e, 0x4b, 0x4e, 0x4f, 0x57, 0x4e, 0x2d, 0x48, 0x41, 0x53, 0x48};
    overlay.insert(overlay.end(), std::begin(marker), std::end(marker));
    const auto over = fileQualification(name, overlay, kind); const auto overlaySha = digest(overlay);
    check(name + "/unknown-whole-SHA-overlay-allowed", originalSha != overlaySha && timestampSha != overlaySha && over.layout == expected, over.reason);
    checkQualificationDigest(name + "/original-file", disk, originalSha, original.size(), false);
    checkQualificationDigest(name + "/timestamp-version-file", changed, timestampSha, timestamp.size(), false);
    checkQualificationDigest(name + "/overlay-file", over, overlaySha, overlay.size(), false);
    samples.push_back({name, originalSha, timestampSha, overlaySha, static_cast<std::uint64_t>(original.size()), static_cast<std::uint64_t>(parser.metadata.relocations.size())});
    if (!disk.layout || !disk.metadata) {
        parserCases(name, original, kind);
        std::cerr << "BLOCKED " << name << " contract/loaded/positive-cache need original-file qualification; parser negatives executed\n";
        return;
    }
    check(name + "/metadata/sections-functions-imports-collected",
        !disk.metadata->sections.empty() && !disk.metadata->functions.empty() && !disk.metadata->imports.empty(),
        "sections=" + std::to_string(disk.metadata->sections.size()) + "; functions=" + std::to_string(disk.metadata->functions.size()) + "; imports=" + std::to_string(disk.metadata->imports.size()));
    const auto invalidBytes = compat::detail::qualifyBytes(original, static_cast<compat::ModuleKind>(99));
    check(name + "/invalid-module-kind-bytes", !invalidBytes.layout, invalidBytes.reason);
    contractMutations(name, original, kind, expected);
    parserCases(name, original, kind);
    loadedCases(name, original, kind, disk);
    cacheCases(name, original, kind);
    check(name + "/wrong-module-kind-rejected", !fileQualification(name, original, kind == compat::ModuleKind::Client ? compat::ModuleKind::Ui : compat::ModuleKind::Client).layout);
    if (kind == compat::ModuleKind::Ui) {
        const auto invalid = fileQualification(name, original, static_cast<compat::ModuleKind>(99));
        check(name + "/invalid-module-kind-rejected", !invalid.layout, invalid.reason);
    }
    std::cerr << "FINISHED " << name << " totalCases=" << cases.size() << '\n';
}
void coreCases() {
    unsigned char expected[]{1,2,3,4};
    const compat::Range good{0x20, expected, sizeof(expected)};
    compat::Image image{[&](std::uint64_t rva, void* p, std::size_t bytes) {
        if (rva < 0x20 || rva - 0x20 > sizeof(expected) || bytes > sizeof(expected) - (rva - 0x20)) return false;
        std::memcpy(p, expected + (rva - 0x20), bytes); return true;
    }, 0x138000000ULL, 0x1000};
    check("core/actual-range-matched", compat::checkRanges(image, std::span(&good, 1)).matched);
    check("core/empty-contract-rejected", !compat::checkRanges(image, {}).matched);
    auto bad = good; bad.rva = (std::numeric_limits<std::uint64_t>::max)();
    check("core/range-RVA-overflow", !compat::checkRanges(image, std::span(&bad, 1)).matched);
    bad = good; bad.size = (std::numeric_limits<std::size_t>::max)();
    check("core/range-size-overflow", !compat::checkRanges(image, std::span(&bad, 1)).matched);
    bad = good; bad.bytes = nullptr;
    check("core/null-contract-bytes", !compat::checkRanges(image, std::span(&bad, 1)).matched);
    auto throwing = image; throwing.read = [](auto, void*, auto) -> bool { throw 1; };
    check("core/reader-exception-rejected", !compat::checkRanges(throwing, std::span(&good, 1)).matched);
    check("core/invalid-layout", !compat::checkRecordLayout(image, static_cast<compat::Layout>(99)).matched && !compat::checkUiLayout(image, static_cast<compat::Layout>(99)).matched && !compat::checkPublishLayout(image, static_cast<compat::Layout>(99)).matched);
    check("file/nonexistent", !compat::qualifyFile((output / L"not-existing.dll").wstring(), compat::ModuleKind::Client).layout);
    check("file/directory", !compat::qualifyFile(output.wstring(), compat::ModuleKind::Client).layout);
    check("file/empty-path", !compat::qualifyFile(L"", compat::ModuleKind::Client).layout);
    const auto codeAddress = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&coreCases));
    check("import-validator/own-test-image-code-accepted", compat::executableImportTarget(GetCurrentProcess(), codeAddress));
    check("import-validator/own-test-image-data-nonexec-rejected", !compat::executableImportTarget(GetCurrentProcess(), static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&cases))));
    check("import-validator/own-stack-private-memory-rejected", !compat::executableImportTarget(GetCurrentProcess(), static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(expected))));
    check("import-validator/zero-and-noncanonical-rejected", !compat::executableImportTarget(GetCurrentProcess(), 0) && !compat::executableImportTarget(GetCurrentProcess(), 0xffffffffffffffffULL));
}
void printQualificationEvidence(const compat::Evidence& evidence) {
    std::cout << "{\"matched\":" << (evidence.matched ? "true" : "false")
        << ",\"reason\":" << quoted(evidence.reason) << ",\"failedRva\":" << evidence.failedRva
        << ",\"rangesChecked\":" << evidence.rangesChecked << ",\"bytesChecked\":" << evidence.bytesChecked << '}';
}
void printQualificationObservation(const char* kind, const compat::Qualification& observed) {
    std::cout << "{\"kind\":" << quoted(kind) << ",\"qualified\":" << (observed.layout ? "true" : "false")
        << ",\"reason\":" << quoted(observed.reason) << ",\"sha256\":" << quoted(observed.sha256)
        << ",\"dynamicallyLocated\":" << (observed.dynamicallyLocated ? "true" : "false")
        << ",\"failureCoordinate\":\"reference-graph-RVA\",\"record\":";
    printQualificationEvidence(observed.record); std::cout << ",\"publisher\":";
    printQualificationEvidence(observed.publisher); std::cout << ",\"ui\":";
    printQualificationEvidence(observed.ui); std::cout << '}';
}
int qualifyOnly(const wchar_t* clientPath, const wchar_t* uiPath) {
    const auto client = compat::qualifyFile(clientPath, compat::ModuleKind::Client);
    const auto ui = compat::qualifyFile(uiPath, compat::ModuleKind::Ui);
    std::cout << "{\"mode\":\"qualify-only\",\"diagnosticOnly\":true,\"fullSelftestExecuted\":false,"
        << "\"targetProcessAccess\":false,\"targetCalls\":0,\"targetWrites\":0,\"observations\":[";
    printQualificationObservation("client", client); std::cout << ',';
    printQualificationObservation("ui", ui); std::cout << "]}\n";
    return client.layout && ui.layout ? 0 : 3;
}
void sourceFileCases() {
    constexpr char reference[]="C:\\buildworker\\steam_rel_client_hotfix_win64\\build\\src\\public\\tier1\\utlmemory.h";
    constexpr std::string_view suffix="\\build\\src\\public\\tier1\\utlmemory.h";
    const std::string oldPath(reinterpret_cast<const char*>(reference));
    const std::string newPath="C:\\buildworker\\steam_rel_client_win64"+std::string(suffix);
    struct Outcome { bool accepted=false, point=false, interior=false, endpoint=false, same=false, other=false; std::size_t boundSize=0; };
    const auto probe=[&](const std::string& value,std::uint32_t flags=0x40000040u,bool terminated=true,
        bool readable=true,std::string_view tail="\\build\\src\\public\\tier1\\utlmemory.h",bool referenceReadable=true,bool pathFixup=false) {
        Bytes memory(0x1000,0);
        for(const std::size_t at:{std::size_t(0x300),std::size_t(0x600)}){
            if(!terminated)std::fill_n(memory.begin()+static_cast<std::ptrdiff_t>(at),512,static_cast<unsigned char>('x'));
            require(value.size()<512,"source-file fixture path bound");std::memcpy(memory.data()+at,value.data(),value.size());
            if(terminated)memory[at+value.size()]=0;
        }
        std::array<compat::SectionSpec,1> referenceSections{{{0x100,0xf00,0xf00,referenceReadable?0x40000040u:0x00000040u,{}}}};
        auto actualSections=referenceSections;actualSections[0].characteristics=flags;
        const std::array<compat::GraphRange,1> ranges{{{0x180,reinterpret_cast<const unsigned char*>(reference),sizeof(reference),compat::RangeKind::SourceFile,tail}}};
        const std::array<compat::GraphFixup,1> fields{{{0x185,0x189,0x300,compat::FixupKind::Relative32}}};
        const std::span<const compat::GraphFixup> fixups=pathFixup?std::span<const compat::GraphFixup>(fields):std::span<const compat::GraphFixup>();
        compat::Image image{[&](std::uint64_t rva,void* target,std::size_t bytes){if(!readable||rva>=memory.size()||bytes>memory.size()-rva)return false;std::memcpy(target,memory.data()+static_cast<std::size_t>(rva),bytes);return true;},0x180000000,static_cast<std::uint64_t>(memory.size()),actualSections,{},false};
        const compat::GraphSpec spec{ranges,fixups,referenceSections,0x180000000};
        compat::graph_detail::Resolver resolver(image,spec);Outcome result;
        result.accepted=resolver.target(0x180,0x300);
        if(result.accepted){
            result.point=compat::translateRva(resolver.result,0x180)==0x300;
            result.interior=compat::translateRva(resolver.result,0x181).has_value();
            result.endpoint=compat::translateRva(resolver.result,0x180+sizeof(reference),true).has_value();
            result.same=resolver.target(0x180,0x300);result.other=resolver.target(0x180,0x600);
            result.boundSize=resolver.result.ranges.at(0).size;
        }
        return result;
    };
    check("source-file/old-build-prefix-accepted",probe(oldPath).accepted);
    check("source-file/changed-build-prefix-accepted",probe(newPath).accepted);
    check("source-file/moved-root-accepted",probe("D:\\different_root\\toolchain"+std::string(suffix)).accepted);
    std::string forward=newPath;std::replace(forward.begin(),forward.end(),'\\','/');
    check("source-file/slash-normalization-accepted",probe(forward).accepted);
    check("source-file/wrong-source-tail-rejected",!probe(newPath+".other").accepted);
    check("source-file/relative-path-rejected",!probe("buildworker"+std::string(suffix)).accepted);
    check("source-file/traversal-rejected",!probe("C:\\buildworker\\..\\other"+std::string(suffix)).accepted);
    check("source-file/writable-target-rejected",!probe(newPath,0xc0000040u).accepted);
    check("source-file/executable-target-rejected",!probe(newPath,0x60000020u).accepted);
    check("source-file/non-readable-reference-rejected",!probe(newPath,0x40000040u,true,true,suffix,false).accepted);
    check("source-file/unterminated-target-rejected",!probe(newPath,0x40000040u,false).accepted);
    check("source-file/unreadable-target-rejected",!probe(newPath,0x40000040u,true,false).accepted);
    check("source-file/empty-tail-rejected",!probe(newPath,0x40000040u,true,true,{}).accepted);
    const auto bound=probe(newPath);
    check("source-file/point-only-no-interior-or-endpoint",bound.accepted&&bound.point&&!bound.interior&&!bound.endpoint&&bound.boundSize==1);
    check("source-file/repeated-target-conflict-rejected",bound.same&&!bound.other);
    check("source-file/reference-path-tail-mismatch-rejected",!probe(newPath,0x40000040u,true,true,"\\other.h").accepted);
    check("source-file/fixup-masking-in-path-rejected",!probe(newPath,0x40000040u,true,true,suffix,true,true).accepted);
}
int sourceFileOnly(const wchar_t* clientPath,const wchar_t* uiPath,const wchar_t* publicClientPath,const wchar_t* publicUiPath) {
    sourceFileCases();
    const auto client=compat::qualifyFile(clientPath,compat::ModuleKind::Client),ui=compat::qualifyFile(uiPath,compat::ModuleKind::Ui);
    const auto controlClient=compat::qualifyFile(publicClientPath,compat::ModuleKind::Client),controlUi=compat::qualifyFile(publicUiPath,compat::ModuleKind::Ui);
    const auto failed=std::count_if(cases.begin(),cases.end(),[](const auto& c){return !c.pass;});
    std::cout << "{\"mode\":\"source-file-only\",\"fullSelftestExecuted\":false,\"targetProcessAccess\":false,\"boundaryPassed\":" << (failed?"false":"true") << ",\"cases\":[";
    bool first=true;for(const auto& item:cases){if(!first)std::cout << ',';first=false;std::cout << "{\"name\":" << quoted(item.name) << ",\"passed\":" << (item.pass?"true":"false") << '}';}
    std::cout << "],\"current\":[";printQualificationObservation("client",client);std::cout << ',';printQualificationObservation("ui",ui);
    std::cout << "],\"originalControl\":[";printQualificationObservation("client",controlClient);std::cout << ',';printQualificationObservation("ui",controlUi);std::cout << "]}\n";
    return failed||!controlClient.layout||!controlUi.layout?1:client.layout&&ui.layout?0:3;
}
struct ImportGraphProductProof {
    bool graphKept=false, fixedFlag=false, loadedAccepted=false, namedTargetRejected=false,
        codeMutationRejected=false, pdataMutationRejected=false, unwindMutationRejected=false, sourceMutationRejected=false, sourceInteriorDenied=false;
    std::uint64_t namedCalls=0, rejectedNamedCalls=0;
    std::string loadedReason, rejectedReason;
    std::uint64_t loadedFailedRva=0;
};
ImportGraphProductProof importGraphProductProof(const compat::Qualification& disk,const wchar_t* path,compat::ModuleKind kind) {
    ImportGraphProductProof result;result.graphKept=bool(disk.graph);result.fixedFlag=!disk.dynamicallyLocated;
    if(!disk.layout||!disk.metadata||!disk.graph)return result;
    const auto bytes=readFile(fs::path(path));if(digest(bytes)!=disk.sha256)return result;
    compat::detail::PeFile file(bytes);if(!file.parse())return result;
    const auto metadata=disk.metadata;constexpr std::uint64_t fakeBase=0x700000000000ULL;
    std::optional<std::uint64_t> corrupt;
    const compat::ReadRva loaded=[&](std::uint64_t rva,void* target,std::size_t count){
        if(!file.read(rva,target,count))return false;
        const auto end=rva+count;
        for(const auto& slot:metadata->imports)if(slot.rva<end&&std::uint64_t(slot.rva)+8>rva){
            const auto value=fakeBase+slot.rva,low=(std::max)(rva,std::uint64_t(slot.rva)),high=(std::min)(end,std::uint64_t(slot.rva)+8);
            std::memcpy(static_cast<unsigned char*>(target)+(low-rva),reinterpret_cast<const unsigned char*>(&value)+(low-slot.rva),static_cast<std::size_t>(high-low));
        }
        if(corrupt&&*corrupt>=rva&&*corrupt-rva<count)static_cast<unsigned char*>(target)[*corrupt-rva]^=1;
        return true;
    };
    const auto accept=[&](const compat::ImportSlot& slot,std::uint64_t value){++result.namedCalls;return value==fakeBase+slot.rva;};
    const auto deny=[&](const compat::ImportSlot&,std::uint64_t){++result.rejectedNamedCalls;return false;};
    const auto healthy=compat::qualifyLoaded(disk,kind,loaded,metadata->preferredBase,metadata->imageSize,{},accept);
    result.loadedAccepted=healthy.matched;result.loadedReason=healthy.reason;result.loadedFailedRva=healthy.failedRva;
    const auto rejected=compat::qualifyLoaded(disk,kind,loaded,metadata->preferredBase,metadata->imageSize,{},deny);
    result.namedTargetRejected=!rejected.matched&&std::string(rejected.reason)=="contract-import-target-policy"&&result.rejectedNamedCalls>0;result.rejectedReason=rejected.reason;
    const auto mutation=[&](compat::RangeKind rangeKind){
        const auto& ranges=disk.graph->specification.ranges;
        const auto found=std::find_if(ranges.begin(),ranges.end(),[&](const auto& range){return range.loadedRequired&&range.kind==rangeKind;});
        if(found==ranges.end())return false;
        const auto actual=compat::translateRva(*disk.graph,found->refRva);if(!actual)return false;
        corrupt=*actual+(rangeKind==compat::RangeKind::SourceFile?found->size-5:(std::min)(std::size_t(4),found->size-1));
        const auto proof=compat::qualifyLoaded(disk,kind,loaded,metadata->preferredBase,metadata->imageSize,{},accept);corrupt.reset();
        return !proof.matched;
    };
    if(kind==compat::ModuleKind::Client){result.codeMutationRejected=mutation(compat::RangeKind::Code);
        result.pdataMutationRejected=mutation(compat::RangeKind::Pdata);result.unwindMutationRejected=mutation(compat::RangeKind::Unwind);result.sourceMutationRejected=mutation(compat::RangeKind::SourceFile);
        const auto source=std::find_if(disk.graph->specification.ranges.begin(),disk.graph->specification.ranges.end(),[](const auto& r){return r.loadedRequired&&r.kind==compat::RangeKind::SourceFile;});
        result.sourceInteriorDenied=source!=disk.graph->specification.ranges.end()&&!compat::translateRva(*disk.graph,source->refRva+1);
    }
    return result;
}

void importGraphCases(const std::array<ImportGraphProductProof,2>& productProof,bool focused=false) {
    enum class Fault { None, DllCase, WrongDll, WrongSymbol, DuplicateSlot, MissingFacts,
        TruncatedName, Descriptor, DivergentLookup, Writable, Executable, PartialName,
        LoadedImage, WrongReference, SectionConflict, FixupOverlap, ImportConflict, Calibrated, CalibratedWrong, ModeledHint, PartialHint };
    struct Observation { bool matched=false, names=false, slotPartial=false, hintDenied=false,
        prefixDenied=false, loadedAccepted=false, loadedMutationDenied=false, avoidsAmbiguousScan=false, unrelatedReadonly=false; };
    const auto probe=[](Fault fault) {
        constexpr std::uint64_t base=0x180000000ULL,refCode=0x110,actualCode=0x140,
            refSlot=0x300,refLookup=0x3a0,refName=0x500,refDll=0x560;
        constexpr char library[]="tier0_s64.dll",symbol[]="Plat_IsInDebugSession";
        Bytes memory(0x2000,0),referenceCode{
            0x48,0x83,0xec,0x28,0x90,0x90,0x90,0x90,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
        const auto put=[&](std::uint64_t rva,const auto& value){std::memcpy(memory.data()+static_cast<std::size_t>(rva),&value,sizeof(value));};
        const auto refDisplacement=static_cast<std::int32_t>(refSlot-(refCode+14));
        std::memcpy(referenceCode.data()+10,&refDisplacement,4);
        std::memcpy(memory.data()+actualCode,referenceCode.data(),referenceCode.size());
        const auto actualDisplacement=static_cast<std::int32_t>(0x760-(actualCode+14));put(actualCode+10,actualDisplacement);
        const std::uint64_t nameValue=0x900;put(0x760,nameValue);put(0x840,nameValue);
        const std::uint32_t lookupBase=0x800,dllName=0x980,iatBase=0x720;
        put(0x680,lookupBase);put(0x68c,dllName);put(0x690,iatBase);
        const std::uint16_t hint=405;put(0x900,hint);
        std::memcpy(memory.data()+0x902,symbol,sizeof(symbol));
        std::memcpy(memory.data()+0x980,library,sizeof(library));
        std::vector<compat::ImportSlot> facts{{0x760,nameValue,library,symbol,0x840,0x680,0x800,0x720,0x980}};
        std::array<compat::SectionSpec,2> referenceSections{{
            {0x100,0x100,0x100,0x60000020u,{}},{0x200,0x1d00,0x1d00,0x40000040u,{}}}};
        auto actualSections=referenceSections;
        std::array<compat::GraphImport,1> imports{{{refSlot,refLookup,refName,0x240,0x380,0x2e0,refDll,library,symbol}}};
        const std::array<std::uint32_t,3> descriptor{{0x380,static_cast<std::uint32_t>(refDll),0x2e0}};
        std::vector<compat::GraphRange> ranges{
            {refCode,referenceCode.data(),referenceCode.size(),compat::RangeKind::Code,{},true},
            {refSlot,reinterpret_cast<const unsigned char*>(&refName),8,compat::RangeKind::Data,{},true},
            {refLookup,reinterpret_cast<const unsigned char*>(&refName),8,compat::RangeKind::Data},
            {0x240,reinterpret_cast<const unsigned char*>(&descriptor[0]),4,compat::RangeKind::Data},
            {0x24c,reinterpret_cast<const unsigned char*>(&descriptor[1]),4,compat::RangeKind::Data},
            {0x250,reinterpret_cast<const unsigned char*>(&descriptor[2]),4,compat::RangeKind::Data},
            {refName+2,reinterpret_cast<const unsigned char*>(symbol),sizeof(symbol),compat::RangeKind::Data},
            {refDll,reinterpret_cast<const unsigned char*>(library),sizeof(library),compat::RangeKind::Data}};
        const std::uint16_t referenceHint=1;
        if(fault==Fault::ModeledHint||fault==Fault::PartialHint)ranges.push_back({refName,reinterpret_cast<const unsigned char*>(&referenceHint),2,compat::RangeKind::Data});
        std::vector<compat::GraphFixup> fields{
            {refCode+10,refCode+14,refSlot,compat::FixupKind::Relative32},
            {refSlot,0,refName,compat::FixupKind::Rva64},
            {refLookup,0,refName,compat::FixupKind::Rva64},
            {0x240,0,0x380,compat::FixupKind::Rva32},
            {0x24c,0,refDll,compat::FixupKind::Rva32},
            {0x250,0,0x2e0,compat::FixupKind::Rva32}};
        if(fault==Fault::DllCase){facts[0].library="TIER0_S64.DLL";std::memcpy(memory.data()+0x980,"TIER0_S64.DLL",sizeof(library));}
        if(fault==Fault::WrongDll)facts[0].library="other_s64.dll";
        if(fault==Fault::WrongSymbol)facts[0].symbol="Plat_IsOtherSession";
        if(fault==Fault::DuplicateSlot)facts.push_back(facts[0]);
        if(fault==Fault::MissingFacts)facts.clear();
        if(fault==Fault::TruncatedName)memory[0x902+sizeof(symbol)-1]='x';
        if(fault==Fault::Descriptor){const std::uint32_t bad=0x990;put(0x68c,bad);}
        if(fault==Fault::DivergentLookup){const std::uint64_t bad=0x920;put(0x840,bad);}
        if(fault==Fault::Writable)actualSections[1].characteristics=0xc0000040u;
        if(fault==Fault::Executable)actualSections[1].characteristics=0x60000020u;
        if(fault==Fault::WrongReference)imports[0].symbol="Plat_IsOtherSession";
        if(fault==Fault::FixupOverlap)fields.push_back({refCode+12,refCode+16,refSlot,compat::FixupKind::Relative32});
        if(fault==Fault::Calibrated||fault==Fault::CalibratedWrong){
            const std::uint64_t oldName=refName;const std::uint32_t oldLookup=0x380,oldDll=0x560,oldIat=0x2e0;
            std::memcpy(memory.data()+refCode,referenceCode.data(),referenceCode.size());
            std::memcpy(memory.data()+actualCode,referenceCode.data(),referenceCode.size());
            const auto duplicateDisplacement=static_cast<std::int32_t>(refSlot-(actualCode+14));put(actualCode+10,duplicateDisplacement);
            put(refSlot,oldName);put(refLookup,oldName);put(0x240,oldLookup);put(0x24c,oldDll);put(0x250,oldIat);
            std::memcpy(memory.data()+refName+2,symbol,sizeof(symbol));std::memcpy(memory.data()+refDll,library,sizeof(library));
            facts={{static_cast<std::uint32_t>(refSlot),refName,library,symbol,static_cast<std::uint32_t>(refLookup),0x240,0x380,0x2e0,0x560}};
            if(fault==Fault::CalibratedWrong)facts[0].symbol="Plat_IsOtherSession";
        }
        const auto read=[&](std::uint64_t rva,void* target,std::size_t count){
            if(rva>=memory.size()||count>memory.size()-rva)return false;
            if(fault==Fault::PartialName&&rva<=0x902&&count>0x902-rva)return false;
            if(fault==Fault::PartialHint&&rva<=0x900&&count>0x900-rva)return false;
            std::memcpy(target,memory.data()+static_cast<std::size_t>(rva),count);return true;};
        compat::Image image{read,base,static_cast<std::uint64_t>(memory.size()),actualSections,{},fault==Fault::LoadedImage,facts};
        const compat::GraphSpec spec{ranges,fields,referenceSections,base,imports};
        compat::GraphResolution graph;
        if(fault==Fault::SectionConflict){auto otherSections=referenceSections;otherSections[1].virtualSize+=8;
            const compat::GraphSpec other{ranges,fields,otherSections,base,imports};
            const std::array<compat::GraphSpec,2> specs{{spec,other}};graph=compat::resolveGraphs(image,specs);
        }else if(fault==Fault::ImportConflict){auto otherImports=imports;otherImports[0].symbol="Plat_IsOtherSession";
            const compat::GraphSpec other{ranges,fields,referenceSections,base,otherImports};
            const std::array<compat::GraphSpec,2> specs{{spec,other}};graph=compat::resolveGraphs(image,specs);
        }else if(fault==Fault::Calibrated||fault==Fault::CalibratedWrong)graph=compat::resolveCalibratedGraph(image,spec);
        else graph=compat::resolveGraph(image,spec);
        Observation result;result.matched=graph.evidence.matched;if(!result.matched)return result;
        const auto normalized=compat::referenceImageRead(graph,read,base,base);
        std::array<char,sizeof(library)> observed{};std::uint16_t partial=0;
        result.names=normalized(refDll,observed.data(),observed.size())&&std::memcmp(observed.data(),library,sizeof(library))==0;
        result.slotPartial=normalized(refSlot+1,&partial,2)&&partial==static_cast<std::uint16_t>((refName>>8)&0xffff);
        result.hintDenied=!compat::translateRva(graph,refName+1);
        if(fault==Fault::ModeledHint){std::uint16_t observedHint=0;result.names=result.names&&normalized(refName,&observedHint,2)&&observedHint==referenceHint;}
        result.prefixDenied=!compat::translateRva(graph,refSlot-8);
        result.unrelatedReadonly=compat::translateRva(graph,0x1500)==std::optional<std::uint64_t>(0x1500);
        compat::Image contract=image;contract.read=normalized;contract.sections=referenceSections;
        result.loadedAccepted=compat::checkLoadedRequired(spec,image,contract,&graph).matched;
        if(fault==Fault::Calibrated){compat::graph_detail::Resolver scanner(image,spec);
            const auto candidates=scanner.loadCode()?scanner.scan(0):std::vector<std::uint64_t>();
            result.avoidsAmbiguousScan=candidates==std::vector<std::uint64_t>{refCode,actualCode}&&!compat::resolveGraph(image,spec).evidence.matched;}
        const auto boundCode=compat::translateRva(graph,refCode);if(boundCode)memory[static_cast<std::size_t>(*boundCode+4)]^=1;
        result.loadedMutationDenied=!compat::checkLoadedRequired(spec,image,contract,&graph).matched;
        return result;
    };
    if(focused){
        const auto good=probe(Fault::None),calibrated=probe(Fault::Calibrated);
        const std::array<const char*,3> names{{"import-graph/calibrated-coordinates-still-check-import-identity","import-graph/calibrated-known-coordinates-avoid-ambiguous-scan","import-graph/loaded-required-code-mutation-rejected"}};
        check(names[0],calibrated.matched&&calibrated.loadedAccepted&&!probe(Fault::CalibratedWrong).matched&&productProof[0].graphKept&&productProof[1].graphKept&&productProof[0].fixedFlag&&productProof[1].fixedFlag);
        check(names[1],calibrated.matched&&calibrated.avoidsAmbiguousScan&&productProof[0].loadedAccepted&&productProof[1].loadedAccepted&&productProof[0].namedTargetRejected&&productProof[1].namedTargetRejected);
        check(names[2],good.matched&&good.loadedAccepted&&good.loadedMutationDenied&&productProof[0].loadedAccepted&&productProof[0].codeMutationRejected&&productProof[0].pdataMutationRejected&&productProof[0].unwindMutationRejected&&productProof[0].sourceMutationRejected&&productProof[0].sourceInteriorDenied);
        std::cerr << "focused calibrated=" << calibrated.matched << " loaded=" << calibrated.loadedAccepted << " ambiguity=" << calibrated.avoidsAmbiguousScan << '\n';
        for(std::size_t i=0;i<productProof.size();++i){const auto& p=productProof[i];
            std::cerr << "product[" << i << "] graph=" << p.graphKept << " fixed=" << p.fixedFlag << " loaded=" << p.loadedAccepted << " reason=" << p.loadedReason << " failedRva=" << p.loadedFailedRva << " deny=" << p.namedTargetRejected << " denyReason=" << p.rejectedReason << " namedCalls=" << p.namedCalls << " deniedCalls=" << p.rejectedNamedCalls << '\n';}
        return;
    }
    const auto good=probe(Fault::None),caseFold=probe(Fault::DllCase),calibrated=probe(Fault::Calibrated);
    check("import-graph/calibrated-coordinates-still-check-import-identity",calibrated.matched&&calibrated.loadedAccepted&&!probe(Fault::CalibratedWrong).matched&&productProof[0].graphKept&&productProof[1].graphKept&&productProof[0].fixedFlag&&productProof[1].fixedFlag);
    check("import-graph/calibrated-known-coordinates-avoid-ambiguous-scan",calibrated.matched&&calibrated.avoidsAmbiguousScan&&productProof[0].loadedAccepted&&productProof[1].loadedAccepted&&productProof[0].namedTargetRejected&&productProof[1].namedTargetRejected);
    check("import-graph/moved-slot-and-reordered-lookup-accepted",good.matched);
    check("import-graph/dll-ascii-case-accepted",caseFold.matched&&caseFold.names);
    check("import-graph/wrong-dll-rejected",!probe(Fault::WrongDll).matched);
    check("import-graph/wrong-symbol-rejected",!probe(Fault::WrongSymbol).matched);
    check("import-graph/duplicate-actual-slot-rejected",!probe(Fault::DuplicateSlot).matched);
    check("import-graph/missing-facts-rejected",!probe(Fault::MissingFacts).matched);
    check("import-graph/truncated-name-rejected",!probe(Fault::TruncatedName).matched);
    check("import-graph/descriptor-mismatch-rejected",!probe(Fault::Descriptor).matched);
    check("import-graph/lookup-iat-divergence-rejected",!probe(Fault::DivergentLookup).matched);
    check("import-graph/writable-target-rejected",!probe(Fault::Writable).matched);
    check("import-graph/executable-target-rejected",!probe(Fault::Executable).matched);
    check("import-graph/partial-name-read-rejected",!probe(Fault::PartialName).matched);
    check("import-graph/loaded-image-resolver-rejected",!probe(Fault::LoadedImage).matched);
    check("import-graph/reference-identity-mismatch-rejected",!probe(Fault::WrongReference).matched);
    const auto modeledHint=probe(Fault::ModeledHint);
    check("import-graph/unmodeled-hint-byte-not-mapped",good.matched&&good.hintDenied&&modeledHint.matched&&modeledHint.names&&!modeledHint.hintDenied&&!probe(Fault::PartialHint).matched);
    check("import-graph/unmodeled-table-prefix-not-mapped",good.matched&&good.prefixDenied&&good.unrelatedReadonly);
    check("import-graph/dll-canonical-read",good.matched&&good.names&&caseFold.names);
    check("import-graph/partial-fixup-read-normalized",good.matched&&good.slotPartial);
    check("import-graph/reference-sections-conflict-rejected",!probe(Fault::SectionConflict).matched);
    check("import-graph/partly-overlapping-fixup-rejected",!probe(Fault::FixupOverlap).matched);
    check("import-graph/import-declaration-conflict-rejected",!probe(Fault::ImportConflict).matched);
    check("import-graph/loaded-required-code-accepted",good.matched&&good.loadedAccepted);
    check("import-graph/loaded-required-code-mutation-rejected",good.matched&&good.loadedAccepted&&good.loadedMutationDenied&&productProof[0].loadedAccepted&&productProof[0].codeMutationRejected&&productProof[0].pdataMutationRejected&&productProof[0].unwindMutationRejected&&productProof[0].sourceMutationRejected&&productProof[0].sourceInteriorDenied);
    const auto additional=[](compat::RangeKind kind,bool changed){
        constexpr std::uint64_t base=0x180000000ULL;Bytes memory(0x1000,0);
        const std::array<unsigned char,12> original{{1,2,3,4,5,6,7,8,9,10,11,12}};
        std::memcpy(memory.data()+0x300,original.data(),original.size());if(changed)memory[0x305]^=1;
        const std::array<compat::SectionSpec,1> sections{{{0x100,0xf00,0xf00,0x40000040u,{}}}};
        const std::array<compat::GraphRange,1> ranges{{{0x300,original.data(),original.size(),kind,{},true}}};
        const compat::GraphSpec spec{ranges,{},sections,base};
        const compat::Image image{[&](std::uint64_t rva,void* target,std::size_t bytes){if(rva>=memory.size()||bytes>memory.size()-rva)return false;std::memcpy(target,memory.data()+static_cast<std::size_t>(rva),bytes);return true;},base,static_cast<std::uint64_t>(memory.size()),sections,{},true};
        return compat::checkLoadedRequired(spec,image,image).matched;
    };
    check("import-graph/loaded-required-pdata-bytes-checked",additional(compat::RangeKind::Pdata,false)&&!additional(compat::RangeKind::Pdata,true));
    check("import-graph/loaded-required-unwind-bytes-checked",additional(compat::RangeKind::Unwind,false)&&!additional(compat::RangeKind::Unwind,true));
    const auto source=[](bool wrong){
        constexpr char reference[]="C:\\buildworker\\steam_rel_client_hotfix_win64\\build\\src\\tier1\\utlmemory.cpp";
        const std::string current=wrong?"D:\\worker\\build\\src\\tier1\\other.cpp":"D:\\worker\\build\\src\\tier1\\utlmemory.cpp";
        constexpr std::uint64_t base=0x180000000ULL;Bytes memory(0x1000,0);std::memcpy(memory.data()+0x300,current.c_str(),current.size()+1);
        const std::array<compat::SectionSpec,1> sections{{{0x100,0xf00,0xf00,0x40000040u,{}}}};
        const std::array<compat::GraphRange,1> ranges{{{0x300,reinterpret_cast<const unsigned char*>(reference),sizeof(reference),compat::RangeKind::SourceFile,"\\build\\src\\tier1\\utlmemory.cpp",true}}};
        const compat::GraphSpec spec{ranges,{},sections,base};
        const compat::Image image{[&](std::uint64_t rva,void* target,std::size_t bytes){if(rva>=memory.size()||bytes>memory.size()-rva)return false;std::memcpy(target,memory.data()+static_cast<std::size_t>(rva),bytes);return true;},base,static_cast<std::uint64_t>(memory.size()),sections,{},true};
        return compat::checkLoadedRequired(spec,image,image).matched;
    };
    check("import-graph/loaded-source-file-new-prefix-accepted",source(false));
    check("import-graph/loaded-source-file-wrong-tail-rejected",!source(true));
}
int importGraphOnly(const wchar_t* clientPath,const wchar_t* uiPath,const wchar_t* publicClientPath,const wchar_t* publicUiPath,bool focused=false) {
    const auto client=compat::qualifyFile(clientPath,compat::ModuleKind::Client),ui=compat::qualifyFile(uiPath,compat::ModuleKind::Ui);
    const auto controlClient=compat::qualifyFile(publicClientPath,compat::ModuleKind::Client),controlUi=compat::qualifyFile(publicUiPath,compat::ModuleKind::Ui);
    const std::array<ImportGraphProductProof,2> productProof{{importGraphProductProof(controlClient,publicClientPath,compat::ModuleKind::Client),importGraphProductProof(controlUi,publicUiPath,compat::ModuleKind::Ui)}};
    importGraphCases(productProof,focused);
    const auto failed=std::count_if(cases.begin(),cases.end(),[](const auto& c){return !c.pass;});
    std::cout << "{\"mode\":\"import-graph-only\",\"fullSelftestExecuted\":false,\"targetProcessAccess\":false,\"boundaryPassed\":" << (failed?"false":"true") << ",\"cases\":[";
    bool first=true;for(const auto& item:cases){if(!first)std::cout << ',';first=false;std::cout << "{\"name\":" << quoted(item.name) << ",\"passed\":" << (item.pass?"true":"false") << '}';}
    std::cout << "],\"current\":[";printQualificationObservation("client",client);std::cout << ',';printQualificationObservation("ui",ui);
    std::cout << "],\"originalControl\":[";printQualificationObservation("client",controlClient);std::cout << ',';printQualificationObservation("ui",controlUi);std::cout << "]}\n";
    return failed||!controlClient.layout||!controlUi.layout?1:client.layout&&ui.layout?0:3;
}


#if defined(YMCC_STEAM_SESSION_CACHE_PEER)
extern "C" bool steamSessionCachePeer(const wchar_t*,const char*,bool*,unsigned long long*,unsigned long long*);
int sessionCacheOnly(const wchar_t* destination,const wchar_t* clientPath) {
    output=fs::absolute(destination);fs::create_directories(output);
    const auto original=readFile(clientPath);const auto active=output/L"prepared-client.bin",next=output/L"next-client.bin";
    writeFile(active,original);
    const auto first=compat::qualifyFile(active.wstring(),compat::ModuleKind::Client);
    check("session-cache/cold-source-facts",first.layout.has_value()&&!first.cacheHit&&first.fileBytesRead==original.size()&&first.digestBytesHashed==original.size(),first.reason);
    bool same=false;unsigned long long bytes=~0ULL,hashed=~0ULL;
    const bool shared=steamSessionCachePeer(active.c_str(),first.sha256.c_str(),&same,&bytes,&hashed);
    check("session-cache/actual-cross-tu-reuse",shared&&same&&bytes==0&&hashed==0,"session preparation and engine TU must share disk fact cache");
    if(!shared||!same||bytes||hashed)return 1;
    const auto again=compat::qualifyFile(active.wstring(),compat::ModuleKind::Client);
    check("session-cache/same-key-no-content-read",again.cacheHit&&again.fileBytesRead==0&&again.digestBytesHashed==0&&again.identity==first.identity);
    auto replacement=original;Header h(replacement);
    const auto stampOffset=h.file+offsetof(IMAGE_FILE_HEADER,TimeDateStamp);
    put<DWORD>(replacement,stampOffset,at<DWORD>(replacement,stampOffset)^0x01010101);
    writeFile(next,replacement);
    require(MoveFileExW(next.c_str(),active.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"owned fixture replacement");
    const bool oldMatched=steamSessionCachePeer(active.c_str(),first.sha256.c_str(),&same,&bytes,&hashed);
    check("session-cache/new-file-identity-not-reused",!oldMatched&&!same&&bytes==replacement.size()&&hashed==replacement.size());
    const auto changed=compat::qualifyFile(active.wstring(),compat::ModuleKind::Client);
    check("session-cache/replacement-qualified-and-shared",changed.layout.has_value()&&changed.cacheHit&&!(changed.identity==first.identity)&&changed.sha256!=first.sha256);
    const auto mapped=mappedImage(replacement,*changed.metadata,0x180000000ULL);
    const auto loaded=compat::qualifyLoaded(changed,compat::ModuleKind::Client,readerFor(mapped),0x180000000ULL,changed.metadata->imageSize);
    check("session-cache/loaded-still-qualified",loaded.matched,loaded.reason);
    auto tampered=mapped;
    const auto& range=compat::field_record::fixedRanges[0];
    const auto actual=compat::resolvedRva(changed,range.rva);
    require(actual<tampered.size(),"loaded mutation RVA");tampered[static_cast<std::size_t>(actual)]^=1;
    const auto rejected=compat::qualifyLoaded(changed,compat::ModuleKind::Client,readerFor(tampered),0x180000000ULL,changed.metadata->imageSize);
    check("session-cache/cached-disk-does-not-bypass-loaded-guard",!rejected.matched,rejected.reason);
    const auto failed=std::count_if(cases.begin(),cases.end(),[](const auto& item){return !item.pass;});
    std::cout<<"{\"mode\":\"session-cache-cross-tu-only\",\"checks\":"<<cases.size()<<",\"failed\":"<<failed<<",\"cases\":[";
    bool sep=false;for(const auto& item:cases){if(sep)std::cout<<',';sep=true;std::cout<<"{\"name\":"<<quoted(item.name)<<",\"passed\":"<<(item.pass?"true":"false")<<'}';}
    std::cout<<"],\"realSteamAccess\":false,\"healthReferenceProduced\":false,\"fullDefaultSelftestExecuted\":false}\n";
    return failed?1:0;
}
#endif

#if defined(YMCC_STEAM_COLD_FILE_FLIGHT)
#include "fixtures/steam_cold_file_flight/cases.inc"
#endif

int wmain(int argc, wchar_t** argv) {
#if defined(YMCC_STEAM_COLD_FILE_FLIGHT)
    if(argc==4&&std::wstring(argv[1])==L"--cold-single-flight-from-replacement-only"){try{return coldSingleFlightOnly(argv[2],argv[3],true);}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
    if(argc==4&&std::wstring(argv[1])==L"--cold-single-flight-only"){try{return coldSingleFlightOnly(argv[2],argv[3]);}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
#endif
#if defined(YMCC_STEAM_SESSION_CACHE_PEER)
    if(argc==4&&std::wstring(argv[1])==L"--session-cache-cross-tu-only"){try{return sessionCacheOnly(argv[2],argv[3]);}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
#endif
    if (argc >= 2 && std::wstring(argv[1]) == L"--import-graph-review-only") {
        if (argc != 6) { std::cerr << "usage: selftest --import-graph-review-only CLIENT UI PUBLIC_CLIENT PUBLIC_UI\n"; return 2; }
        try { return importGraphOnly(argv[2],argv[3],argv[4],argv[5],true); }
        catch(const std::exception& error){std::cerr << error.what() << '\n';return 1;}
    }
    if (argc >= 2 && std::wstring(argv[1]) == L"--import-graph-only") {
        if (argc != 6) { std::cerr << "usage: selftest --import-graph-only CLIENT UI PUBLIC_CLIENT PUBLIC_UI\n"; return 2; }
        try { return importGraphOnly(argv[2],argv[3],argv[4],argv[5]); }
        catch (const std::exception& error) { std::cerr << "import graph observation failed: " << error.what() << '\n'; return 1; }
    }
    if (argc >= 2 && std::wstring(argv[1]) == L"--source-file-only") {
        if (argc != 6) { std::cerr << "usage: selftest --source-file-only CLIENT UI PUBLIC_CLIENT PUBLIC_UI\n"; return 2; }
        try { return sourceFileOnly(argv[2],argv[3],argv[4],argv[5]); }
        catch (const std::exception& error) { std::cerr << "source file observation failed: " << error.what() << '\n'; return 1; }
    }
    if (argc >= 2 && std::wstring(argv[1]) == L"--qualify-only") {
        if (argc != 4) { std::cerr << "usage: selftest --qualify-only CLIENT UI\n"; return 2; }
        try { return qualifyOnly(argv[2], argv[3]); }
        catch (const std::exception& error) { std::cerr << "qualification observation failed: " << error.what() << '\n'; return 1; }
    }
    if (argc != 6) { std::cerr << "usage: selftest OUT LEGACY_CLIENT PUBLIC_CLIENT LEGACY_UI PUBLIC_UI\n"; return 2; }
    try {
        output = fs::absolute(argv[1]); fs::create_directories(output);
        coreCases();
        runSample("legacy-client", argv[2], compat::ModuleKind::Client, compat::Layout::Legacy);
        runSample("public-client", argv[3], compat::ModuleKind::Client, compat::Layout::Public);
        runSample("legacy-ui", argv[4], compat::ModuleKind::Ui, compat::Layout::Legacy);
        runSample("public-ui", argv[5], compat::ModuleKind::Ui, compat::Layout::Public);
    } catch (const std::exception& error) { check("fixture-fatal-exception", false, error.what()); }
    const auto failed = std::count_if(cases.begin(), cases.end(), [](const auto& c) { return !c.pass; });
    std::cout << "{\n  \"pass\":" << (failed ? "false" : "true") << ",\n  \"checks\":" << cases.size() << ",\n  \"failed\":" << failed
        << ",\n  \"scope\":\"real qualifyFile/PE parser/qualifyLoaded; self-process/file fixtures only; no target process, target API invocation or device\","
        << "\n  \"addressMigrationQualified\":false,\n  \"hashPolicyProof\":\"Different SHA256 from timestamp/version and overlay changes; preserves existing fixed-RVA critical contracts. Not evidence of arbitrary address relocation support.\",\n  \"samples\":[";
    bool first = true;
    for (const auto& sample : samples) {
        if (!first) std::cout << ','; first = false;
        std::cout << "\n    {\"name\":" << quoted(sample.name) << ",\"originalSHA256\":" << quoted(sample.sha)
            << ",\"timestampVersionSHA256\":" << quoted(sample.timestampSha) << ",\"overlaySHA256\":" << quoted(sample.overlaySha)
            << ",\"bytes\":" << sample.size << ",\"DIR64relocations\":" << sample.relocations << '}';
    }
    std::cout << "\n  ],\n  \"cases\":["; first = true;
    for (const auto& c : cases) {
        if (!first) std::cout << ','; first = false;
        std::cout << "\n    {\"name\":" << quoted(c.name) << ",\"pass\":" << (c.pass ? "true" : "false")
            << ",\"observed\":" << quoted(c.observed) << ",\"rva\":" << c.rva << ",\"mutableRangesIgnored\":" << c.mutableIgnored << '}';
    }
    std::cout << "\n  ]\n}\n";
    return failed ? 1 : 0;
}