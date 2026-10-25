#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ymcc::steamcompat {
struct ImportSlot { std::uint32_t rva = 0; std::uint64_t originalThunk = 0; std::string library, symbol; };
struct LoadedDiagnostic {
    const char* reason = "contract-import-unchecked";
    const char* stage = "none";
    std::string library, symbol;
    std::uint64_t slotRva = 0, target = 0, allocationBase = 0, exportRva = 0, failedAddress = 0;
    std::uint64_t bytesRequested = 0, bytesRead = 0;
    DWORD state = 0, type = 0, protect = 0, win32Error = ERROR_SUCCESS;
};
// Preserve the historical function-target policy, including MEM_IMAGE ownership.
inline bool executableImportTarget(HANDLE process, std::uint64_t address) {
    if (address < 0x10000 || address > 0x7fffffffffffULL) return false;
    MEMORY_BASIC_INFORMATION value{};
    if (VirtualQueryEx(process, reinterpret_cast<const void*>(address), &value, sizeof(value)) != sizeof(value) ||
        value.State != MEM_COMMIT || value.Type != MEM_IMAGE || (value.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto protection = value.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}
namespace import_target_detail {
inline bool readable(DWORD protection) {
    if (protection & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    switch (protection & 0xff) {
    case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY: return true;
    default: return false;
    }
}
inline bool nonExecutableData(DWORD protection) {
    return !(protection & (PAGE_GUARD | PAGE_NOACCESS)) &&
        ((protection & 0xff) == PAGE_READONLY || (protection & 0xff) == PAGE_READWRITE ||
         (protection & 0xff) == PAGE_WRITECOPY);
}
inline bool sameLibrary(const std::string& a, const std::string& b) {
    if (a.empty() || a.size() != b.size() || a.size() > 255) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}
// Every parser read is bounded, read-only and confined to the target's image
// allocation. No module loading, target calls, cache or background work occurs.
struct Reader {
    HANDLE process;
    std::uint64_t base, limit = 1024 * 1024, bytesRead = 0;
    unsigned queries = 0, reads = 0;
    LoadedDiagnostic& diagnostic;
    bool fail(const char* reason, const char* stage, std::uint64_t address, DWORD error = ERROR_SUCCESS) {
        diagnostic.reason = reason; diagnostic.stage = stage;
        diagnostic.failedAddress = address; diagnostic.win32Error = error; return false;
    }
    bool contains(std::uint64_t rva, std::uint64_t bytes) const {
        return bytes && rva < limit && bytes <= limit - rva;
    }
    std::size_t piece(std::uint64_t rva, std::size_t wanted) {
        if (!contains(rva, wanted)) return 0;
        if (++queries > 1024) { fail("contract-import-target-policy", "data-read-budget", base + rva); return 0; }
        MEMORY_BASIC_INFORMATION info{};
        const auto queried = VirtualQueryEx(process, reinterpret_cast<const void*>(base + rva), &info, sizeof(info));
        if (queried != sizeof(info)) {
            const DWORD error = queried ? ERROR_BAD_LENGTH : GetLastError();
            fail("contract-import-target-query", "data-image-query", base + rva, error); return 0;
        }
        const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (info.State != MEM_COMMIT || info.Type != MEM_IMAGE ||
            reinterpret_cast<std::uintptr_t>(info.AllocationBase) != base || !readable(info.Protect) ||
            begin > base + rva || !info.RegionSize || info.RegionSize > 0x800000000000ULL - begin ||
            base + rva - begin >= info.RegionSize) {
            fail("contract-import-target-policy", "data-image-region", base + rva); return 0;
        }
        return (std::min)(wanted, static_cast<std::size_t>(info.RegionSize - (base + rva - begin)));
    }
    bool read(std::uint64_t rva, void* destination, std::size_t count) {
        if (!contains(rva, count) || count > 4096) return false;
        auto* output = static_cast<unsigned char*>(destination);
        for (std::size_t offset = 0; offset < count;) {
            const auto size = piece(rva + offset, count - offset); if (!size) return false;
            if (++reads > 1024 || size > 128 * 1024 - bytesRead)
                return fail("contract-import-target-policy", "data-read-budget", base + rva + offset);
            SIZE_T got = 0;
            const BOOL okay = ReadProcessMemory(process, reinterpret_cast<const void*>(base + rva + offset), output + offset, size, &got);
            const DWORD error = okay ? ERROR_SUCCESS : GetLastError();
            bytesRead += got;
            if (!okay || got != size) {
                diagnostic.bytesRequested = size; diagnostic.bytesRead = got;
                return fail("contract-import-target-query", "data-image-read", base + rva + offset,
                    okay ? ERROR_PARTIAL_COPY : error);
            }
            offset += size;
        }
        return true;
    }
    template<class T> bool get(std::uint64_t rva, T& value) { return read(rva, &value, sizeof(value)); }
    bool ascii(std::uint64_t rva, std::size_t maximum, std::string& result) {
        result.clear(); std::array<unsigned char, 64> buffer{};
        for (std::size_t offset = 0; offset < maximum;) {
            if (rva >= limit || offset >= limit - rva) return false;
            const auto wanted = (std::min)({buffer.size(), maximum - offset, static_cast<std::size_t>(limit - rva - offset)});
            const auto count = piece(rva + offset, wanted); if (!count || !read(rva + offset, buffer.data(), count)) return false;
            for (std::size_t i = 0; i < count; ++i) {
                if (!buffer[i]) return !result.empty();
                if (buffer[i] < 0x20 || buffer[i] > 0x7e) return false;
                result.push_back(static_cast<char>(buffer[i]));
            }
            offset += count;
        }
        return false;
    }
};
}
// Functions retain the existing executable-image policy. A non-executable
// target must instead be the exact named/ordinal data export of slot.library.
inline bool resolvedImportTarget(HANDLE process, const ImportSlot& slot, std::uint64_t target,
    LoadedDiagnostic* diagnostic = nullptr) {
    LoadedDiagnostic local; auto& detail = diagnostic ? *diagnostic : local;
    detail = {}; detail.library = slot.library; detail.symbol = slot.symbol;
    detail.slotRva = slot.rva; detail.target = target;
    if (executableImportTarget(process, target)) {
        detail.reason = "contract-import-function-matched"; detail.stage = "function-target"; return true;
    }
    const auto reject = [&](const char* reason, const char* stage) {
        detail.reason = reason; detail.stage = stage; return false;
    };
    if (target < 0x10000 || target > 0x7fffffffffffULL)
        return reject("contract-import-target-policy", "target-address");
    MEMORY_BASIC_INFORMATION info{};
    const auto queried = VirtualQueryEx(process, reinterpret_cast<const void*>(target), &info, sizeof(info));
    if (queried != sizeof(info)) {
        detail.win32Error = queried ? ERROR_BAD_LENGTH : GetLastError(); detail.failedAddress = target;
        return reject("contract-import-target-query", "target-query");
    }
    detail.state = info.State; detail.type = info.Type; detail.protect = info.Protect;
    detail.allocationBase = reinterpret_cast<std::uintptr_t>(info.AllocationBase);
    if (info.State != MEM_COMMIT || info.Type != MEM_IMAGE ||
        !import_target_detail::nonExecutableData(info.Protect) || detail.allocationBase < 0x10000 ||
        detail.allocationBase > target)
        return reject("contract-import-target-policy", "data-target-region");
    import_target_detail::Reader reader{process, detail.allocationBase, 1024 * 1024, 0, 0, 0, detail};
    const auto parse = [&]() {
        detail.stage = "data-pe-headers";
        IMAGE_DOS_HEADER dos{}; DWORD signature = 0; IMAGE_FILE_HEADER file{}; IMAGE_OPTIONAL_HEADER64 optional{};
        if (!reader.get(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) return false;
        const auto nt = static_cast<std::uint64_t>(dos.e_lfanew);
        if (nt < sizeof(dos) || nt > 1024 * 1024 - sizeof(IMAGE_NT_HEADERS64)) return false;
        if (!reader.get(nt, signature) || signature != IMAGE_NT_SIGNATURE ||
            !reader.get(nt + sizeof(signature), file) || file.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            !file.NumberOfSections || file.NumberOfSections > 96 ||
            file.SizeOfOptionalHeader < sizeof(optional) || file.SizeOfOptionalHeader > 4096 ||
            !reader.get(nt + sizeof(signature) + sizeof(file), optional) ||
            optional.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || !optional.NumberOfRvaAndSizes ||
            optional.SizeOfImage < 4096 || optional.SizeOfImage > 0x40000000 ||
            optional.SizeOfImage - 1 > 0x7fffffffffffULL - reader.base) return false;
        reader.limit = optional.SizeOfImage;
        const auto targetRva = target - reader.base;
        const auto sections = nt + sizeof(signature) + sizeof(file) + file.SizeOfOptionalHeader;
        if (!reader.contains(targetRva, 1) || !reader.contains(sections, std::uint64_t(file.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER)) ||
            optional.SizeOfHeaders > reader.limit || optional.SizeOfHeaders > 1024 * 1024 ||
            sections + std::uint64_t(file.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER) > optional.SizeOfHeaders) return false;
        const auto directory = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        IMAGE_EXPORT_DIRECTORY exports{};
        if (directory.Size < sizeof(exports) || directory.Size > 16 * 1024 * 1024 ||
            !reader.contains(directory.VirtualAddress, directory.Size) || !reader.get(directory.VirtualAddress, exports) ||
            !exports.NumberOfFunctions || exports.NumberOfFunctions > 65536 || exports.NumberOfNames > 65536 ||
            !reader.contains(exports.AddressOfFunctions, std::uint64_t(exports.NumberOfFunctions) * 4)) return false;
        detail.stage = "data-export-library"; std::string name;
        if (!reader.ascii(exports.Name, 256, name) || !import_target_detail::sameLibrary(name, slot.library)) return false;
        detail.stage = "data-export-symbol";
        std::uint64_t index = exports.NumberOfFunctions;
        if (slot.symbol.empty() || slot.symbol.size() > 511) return false;
        if (slot.symbol[0] == '#') {
            if (slot.symbol.size() < 2 || slot.symbol.size() > 6) return false;
            std::uint64_t ordinal = 0;
            for (std::size_t i = 1; i < slot.symbol.size(); ++i) {
                const auto c = slot.symbol[i]; if (c < '0' || c > '9') return false;
                ordinal = ordinal * 10 + (c - '0'); if (ordinal > 65535) return false;
            }
            if (ordinal < exports.Base) return false; index = ordinal - exports.Base;
        } else {
            if (!exports.NumberOfNames || !reader.contains(exports.AddressOfNames, std::uint64_t(exports.NumberOfNames) * 4) ||
                !reader.contains(exports.AddressOfNameOrdinals, std::uint64_t(exports.NumberOfNames) * 2)) return false;
            // The PE export name table is sorted. Bound both search and strings.
            std::uint64_t low = 0, high = exports.NumberOfNames;
            for (unsigned step = 0; low < high && step < 17; ++step) {
                const auto mid = low + (high - low) / 2; DWORD nameRva = 0; WORD ordinalIndex = 0;
                if (!reader.get(std::uint64_t(exports.AddressOfNames) + mid * 4, nameRva) || !reader.ascii(nameRva, 512, name)) return false;
                const auto order = name.compare(slot.symbol);
                if (order < 0) low = mid + 1;
                else if (order > 0) high = mid;
                else {
                    if (!reader.get(std::uint64_t(exports.AddressOfNameOrdinals) + mid * 2, ordinalIndex)) return false;
                    index = ordinalIndex; break;
                }
            }
        }
        detail.stage = "data-export-rva"; DWORD exported = 0;
        if (index >= exports.NumberOfFunctions || !reader.get(std::uint64_t(exports.AddressOfFunctions) + index * 4, exported)) return false;
        detail.exportRva = exported;
        if (!exported || exported != targetRva ||
            (exported >= directory.VirtualAddress && std::uint64_t(exported) - directory.VirtualAddress < directory.Size)) return false;
        detail.stage = "data-export-section"; unsigned matching = 0;
        for (unsigned i = 0; i < file.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER section{};
            if (!reader.get(sections + std::uint64_t(i) * sizeof(section), section)) return false;
            const auto size = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
            if (size && !reader.contains(section.VirtualAddress, size)) return false;
            if (targetRva >= section.VirtualAddress && targetRva - section.VirtualAddress < size) {
                if (!(section.Characteristics & IMAGE_SCN_MEM_READ) || (section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) return false;
                ++matching;
            }
        }
        return matching == 1;
    };
    if (!parse()) {
        if (std::string(detail.reason) == "contract-import-unchecked") detail.reason = "contract-import-data-export-mismatch";
        return false;
    }
    detail.reason = "contract-import-data-matched"; detail.stage = "data-export"; return true;
}
}
