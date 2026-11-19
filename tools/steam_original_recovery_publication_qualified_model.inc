#pragma once
// Source preparation only. The parent includes its pinned final-delivery
// native/steam_compatibility_win32.h before this file. No historical source
// tree, test main, target process, or Steam API is included or invoked here.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace publication_fixture_model {
struct ModuleModel {
    ymcc::steamcompat::Qualification disk;
    std::uint64_t base = 0;
    std::vector<unsigned char> loaded;

    // Absolute actual module address; the parent's sparse object/global/detour
    // overlay is consulted FIRST. This is only its immutable module fallback.
    bool read(std::uint64_t address, void* target, std::size_t count) const {
        if (!target || !count || address < base) return false;
        const auto rva = address - base;
        if (rva >= loaded.size() || count > loaded.size() - rva) return false;
        std::memcpy(target, loaded.data() + static_cast<std::size_t>(rva), count);
        return true;
    }
};

// Pure PE mapper names stay isolated from the original fixture's Header,
// readFile, at, put, mappedImage and readerFor symbols.
namespace pe_model_detail {
using Bytes = std::vector<unsigned char>;
inline void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
inline Bytes readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    require(bool(stream), "publication model input open");
    const auto length = stream.tellg();
    require(length > 0 && length <= 256LL * 1024 * 1024, "publication model input size");
    Bytes bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(stream), "publication model input read");
    return bytes;
}
template<class T> inline T at(const Bytes& bytes, std::size_t offset) {
    require(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset,
        "publication model bounded field read");
    T result{};
    std::memcpy(&result, bytes.data() + offset, sizeof(result));
    return result;
}
template<class T> inline void put(Bytes& bytes, std::size_t offset, const T& value) {
    require(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset,
        "publication model bounded field write");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
struct Header {
    std::size_t nt = 0, file = 0, optional = 0, sections = 0;
    IMAGE_FILE_HEADER fileHeader{};
    IMAGE_OPTIONAL_HEADER64 opt{};
    explicit Header(const Bytes& bytes) {
        const auto dos = at<IMAGE_DOS_HEADER>(bytes, 0);
        require(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= 0,
            "publication model DOS header");
        nt = static_cast<std::size_t>(dos.e_lfanew);
        require(at<DWORD>(bytes, nt) == IMAGE_NT_SIGNATURE, "publication model NT signature");
        file = nt + sizeof(DWORD);
        fileHeader = at<IMAGE_FILE_HEADER>(bytes, file);
        optional = file + sizeof(IMAGE_FILE_HEADER);
        opt = at<IMAGE_OPTIONAL_HEADER64>(bytes, optional);
        require(fileHeader.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER64) &&
            opt.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC, "publication model PE64 header");
        sections = optional + fileHeader.SizeOfOptionalHeader;
    }
};
inline Bytes mappedImage(const Bytes& bytes, const ymcc::steamcompat::PeMetadata& metadata,
    std::uint64_t base) {
    Header header(bytes);
    require(metadata.imageSize && metadata.imageSize <= 0x40000000ULL,
        "publication model image size");
    Bytes mapped(static_cast<std::size_t>(metadata.imageSize));
    require(header.opt.SizeOfImage == metadata.imageSize &&
        header.opt.ImageBase == metadata.preferredBase &&
        header.opt.SizeOfHeaders <= bytes.size() && header.opt.SizeOfHeaders <= mapped.size(),
        "publication model metadata/header agreement");
    std::memcpy(mapped.data(), bytes.data(), header.opt.SizeOfHeaders);
    for (unsigned i = 0; i < header.fileHeader.NumberOfSections; ++i) {
        const auto section = at<IMAGE_SECTION_HEADER>(bytes,
            header.sections + static_cast<std::size_t>(i) * sizeof(IMAGE_SECTION_HEADER));
        if (!section.SizeOfRawData) continue;
        require(section.VirtualAddress <= mapped.size() &&
            section.SizeOfRawData <= mapped.size() - section.VirtualAddress &&
            section.PointerToRawData <= bytes.size() &&
            section.SizeOfRawData <= bytes.size() - section.PointerToRawData,
            "publication model section copy bounds");
        std::memcpy(mapped.data() + section.VirtualAddress,
            bytes.data() + section.PointerToRawData, section.SizeOfRawData);
    }
    const auto delta = base - metadata.preferredBase;
    for (const auto rva : metadata.relocations)
        put(mapped, rva, at<std::uint64_t>(mapped, rva) + delta);
    return mapped;
}
inline ymcc::steamcompat::ReadRva readerFor(const Bytes& mapped) {
    return [&mapped](std::uint64_t rva, void* target, std::size_t count) {
        if (!target || !count || rva >= mapped.size() || count > mapped.size() - rva) return false;
        std::memcpy(target, mapped.data() + static_cast<std::size_t>(rva), count);
        return true;
    };
}

struct InputsState {
    std::mutex mutex;
    std::filesystem::path client, ui;
    bool configured = false, sealed = false;
};
inline InputsState& inputsState() {
    static InputsState state;
    return state;
}
inline std::filesystem::path selectedPath(bool client) {
    auto& state = inputsState();
    std::lock_guard<std::mutex> lock(state.mutex);
    require(state.configured, "publication model needs explicit parent input paths");
    state.sealed = true;
    return client ? state.client : state.ui;
}

// These are simulated named import targets, NOT loaded OS exports or a real
// GetProcAddress/VirtualQuery proof. Every target is exactly this base+slot.rva.
inline constexpr std::uint64_t simulatedImportBase = 0x700000000000ULL;
inline ModuleModel makeModel(bool client, bool content) {
    const auto path = selectedPath(client);
    const auto kind = client ? ymcc::steamcompat::ModuleKind::Client : ymcc::steamcompat::ModuleKind::Ui;
    ModuleModel model;
    model.base = client ? 0x180000000ULL : 0x190000000ULL;
    model.disk = ymcc::steamcompat::qualifyFile(path.wstring(), kind);
    if (!model.disk.layout || !model.disk.metadata ||
        !ymcc::steamcompat::referenceBindingCurrent(model.disk))
        throw std::runtime_error(std::string("publication model disk qualification: ") + model.disk.reason);
    // Fixture input identity only: these digests never manufacture a layout,
    // graph, metadata or loaded match; both product qualifiers must still pass.
    const char* expectedSha = content ?
        (client ? "669fa08e259277fbf5a1dfe15ce88afe734e89151194304b44d92f418dcc985b" :
            "2dfb0a5d50dd90667549cc7d413c7bfb0794c9af4ebaf08bf1cf7d938a11ea93") :
        (client ? "ebf46f84606e4dc49ebd551f6621f7ca5c19927d572a49f0aa4925ce9414096c" :
            "5fdf0f7972ed7f6acb3949ec6dcd8a424e607a044a3e992bd3ffe8ebcf4d96f7");
    require(model.disk.sha256 == expectedSha, "publication model mode/input SHA mismatch");
    require(model.disk.referenceId == (content ? ymcc::steamcompat::ReferenceId::Field20261114 :
        ymcc::steamcompat::ReferenceId::Canonical), "publication model fixed/content reference mismatch");
    const auto bytes = readFile(path);
    // Same digest utility used by qualifyFile; this only binds our reopened
    // immutable mapping bytes to its proof, and grants no qualification itself.
    require(model.disk.sha256.size() == 64 &&
        ymcc::steamcompat::detail::bufferSha256(bytes) == model.disk.sha256,
        "publication model qualified/mapped input digest mismatch");
    model.loaded = mappedImage(bytes, *model.disk.metadata, model.base);
    for (const auto& slot : model.disk.metadata->imports)
        put(model.loaded, slot.rva, simulatedImportBase + slot.rva);
    const auto reader = readerFor(model.loaded);
    const auto namedImport = [](const ymcc::steamcompat::ImportSlot& slot, std::uint64_t target) {
        return target == simulatedImportBase + slot.rva;
    };
    // This is the real product qualifier over the pristine model. The parent's
    // sparse objects/globals/detours do not exist in this read surface.
    const auto proof = ymcc::steamcompat::qualifyLoaded(model.disk, kind, reader,
        model.base, model.disk.metadata->imageSize, {}, namedImport);
    if (!proof.matched)
        throw std::runtime_error(std::string("publication model loaded qualification: ") +
            proof.reason + "; rva=" + std::to_string(proof.failedRva));
    return model;
}
struct CachedModel {
    std::unique_ptr<const ModuleModel> value;
    std::exception_ptr failure;
    CachedModel(bool client, bool content) {
        try { value = std::make_unique<const ModuleModel>(makeModel(client, content)); }
        catch (...) { failure = std::current_exception(); }
    }
    const ModuleModel& get() const {
        if (failure) std::rethrow_exception(failure);
        return *value;
    }
};
} // namespace pe_model_detail

// The original parent parses --client-module/--ui-module and registers their
// source SHA pins. No working-directory/env discovery or default old tree.
// Configure exactly once before first use; inputs cannot change between cases.
inline void setPaths(const std::filesystem::path& client, const std::filesystem::path& ui) {
    auto& state = pe_model_detail::inputsState();
    std::lock_guard<std::mutex> lock(state.mutex);
    pe_model_detail::require(!state.configured && !state.sealed, "publication model input rebinding refused");
    pe_model_detail::require(!client.empty() && !ui.empty(), "publication model both input paths required");
    state.client = client;
    state.ui = ui;
    state.configured = true;
}
inline const ModuleModel& clientFor(std::string mode) {
    if (mode == "fixed") {
        static const pe_model_detail::CachedModel model(true, false);
        return model.get();
    }
    if (mode == "content") {
        static const pe_model_detail::CachedModel model(true, true);
        return model.get();
    }
    throw std::invalid_argument("publication model mode must be fixed|content");
}
inline const ModuleModel& uiFor(std::string mode) {
    if (mode == "fixed") {
        static const pe_model_detail::CachedModel model(false, false);
        return model.get();
    }
    if (mode == "content") {
        static const pe_model_detail::CachedModel model(false, true);
        return model.get();
    }
    throw std::invalid_argument("publication model mode must be fixed|content");
}
} // namespace publication_fixture_model
