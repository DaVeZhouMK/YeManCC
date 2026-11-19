#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#include "steam_compatibility_core.h"
#include "steam_import_target_win32.h"
#include "steam_compatibility_read_contracts.h"
#include "steam_compatibility_publish_contracts.h"
#include "steam_compatibility_read_graph.h"
#include "steam_compatibility_publish_graph.h"
#include "steam_compatibility_field_record_graph.h"
#include "steam_compatibility_field_publication_graph.h"
#include "steam_compatibility_field_ui_graph.h"
#include "steam_reference_coordinate_view.h"
#include "steam_reference_field_maps.h"
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ymcc::steamcompat {
enum class ModuleKind { Client, Ui };
inline bool validModuleKind(ModuleKind kind) noexcept { return kind==ModuleKind::Client||kind==ModuleKind::Ui; }
struct FileIdentity {
    DWORD volume = 0, indexHigh = 0, indexLow = 0, sizeHigh = 0, sizeLow = 0,
        writeHigh = 0, writeLow = 0;
    bool operator==(const FileIdentity&) const = default;
};
struct PeMetadata {
    std::uint64_t preferredBase = 0, imageSize = 0;
    std::vector<std::uint32_t> relocations;
    std::vector<SectionSpec> sections;
    std::vector<RuntimeFunction> functions;
    std::vector<ImportSlot> imports;
};
enum class ReferenceId { Canonical, Field20261114 };
struct ReferenceBinding {
    ReferenceId id = ReferenceId::Canonical;
    ModuleKind kind = ModuleKind::Client;
    std::shared_ptr<const coordinates::Map> coordinates;
    // This identifies the compiled complete migration contract, never a file
    // hash allowlist. Disk and loaded qualification still prove all F bytes.
    const char* certificate = nullptr;
};
struct Qualification {
    ReferenceId referenceId = ReferenceId::Canonical;
    ModuleKind kind = ModuleKind::Client;
    std::optional<Layout> layout;
    Evidence record, ui, publisher;
    FileIdentity identity;
    // Diagnostic digest of the exact immutable buffer whose contract was checked.
    // Counters describe this call; a stamp/identity cache hit performs no full IO.
    std::string sha256;
    std::uint64_t fileBytesRead = 0, digestBytesHashed = 0;
    std::shared_ptr<const PeMetadata> metadata;
    std::shared_ptr<const GraphResolution> graph;
    std::shared_ptr<const ReferenceBinding> reference;
    bool dynamicallyLocated = false;
    const char* reason = "compatibility-unqualified";
    DWORD win32Error = ERROR_SUCCESS;
    bool cacheHit = false;
};
inline constexpr const char fieldClientCertificate[] = "ymcc-cf-client-complete-707-dir64-v1";
inline constexpr const char fieldUiCertificate[] = "ymcc-cf-ui-complete-139-v1";
inline bool referenceBindingCurrent(const Qualification& disk) noexcept {
    try {
    if(!disk.layout||!validLayout(*disk.layout)||!disk.metadata)return false;
    if(!validModuleKind(disk.kind))return false;
    if(disk.referenceId==ReferenceId::Canonical)return !disk.reference;
    if(disk.referenceId!=ReferenceId::Field20261114||!disk.reference)return false;
    const auto& ref=*disk.reference;
    if(ref.id!=ReferenceId::Field20261114||*disk.layout!=Layout::OwnerScoped||
       !validModuleKind(ref.kind)||ref.kind!=disk.kind||!disk.graph||!disk.graph->evidence.matched||!ref.coordinates)return false;
    const auto expected=ref.kind==ModuleKind::Client?field_reference::clientMap():field_reference::uiMap();
    const auto certificate=ref.kind==ModuleKind::Client?fieldClientCertificate:fieldUiCertificate;
    return ref.coordinates==expected&&ref.certificate==certificate&&
        disk.graph->imageSize==disk.metadata->imageSize;
    }catch(...){return false;}
}
namespace detail {
inline std::uint64_t graphImageSize(const GraphSpec& specification) {
    std::uint64_t end=0;
    for(const auto& section:specification.sections)end=(std::max)(end,section.rva+(std::max)(section.virtualSize,section.rawSize));
    return (end+0xfff)&~std::uint64_t(0xfff);
}
inline Evidence checkFieldLayout(const Image& image,ModuleKind kind,bool publication=false) {
    if(kind==ModuleKind::Ui)return checkRanges(image,field_ui::fixedRanges);
    return publication?checkRanges(image,field_publication::fixedRanges):checkRanges(image,field_record::fixedRanges);
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return value && value != INVALID_HANDLE_VALUE; }
};
inline std::string bufferSha256(std::span<const unsigned char> bytes) {
    if(bytes.size()>256*1024*1024)return {};
    struct Algorithm { BCRYPT_ALG_HANDLE value=nullptr;~Algorithm(){if(value)BCryptCloseAlgorithmProvider(value,0);} } algorithm;
    struct Hash { BCRYPT_HASH_HANDLE value=nullptr;~Hash(){if(value)BCryptDestroyHash(value);} } hash;
    if(BCryptOpenAlgorithmProvider(&algorithm.value,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||
        BCryptCreateHash(algorithm.value,&hash.value,nullptr,0,nullptr,0,0)<0||
        BCryptHashData(hash.value,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)<0)return {};
    std::array<unsigned char,32> digest{};
    if(BCryptFinishHash(hash.value,digest.data(),static_cast<ULONG>(digest.size()),0)<0)return {};
    constexpr char hex[]="0123456789abcdef";std::string result(64,'0');
    for(std::size_t i=0;i<digest.size();++i){result[2*i]=hex[digest[i]>>4];result[2*i+1]=hex[digest[i]&15];}
    return result;
}
inline bool identity(HANDLE handle, FileIdentity& out) {
    BY_HANDLE_FILE_INFORMATION value{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &value) ||
        (value.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
    out = {value.dwVolumeSerialNumber, value.nFileIndexHigh, value.nFileIndexLow,
        value.nFileSizeHigh, value.nFileSizeLow, value.ftLastWriteTime.dwHighDateTime,
        value.ftLastWriteTime.dwLowDateTime};
    return true;
}
struct Section { std::uint32_t rva = 0, virtualSize = 0, raw = 0, rawSize = 0; };
class PeFile {
    const std::vector<unsigned char>& data_;
    std::vector<Section> sections_;
    std::uint32_t headers_ = 0;
    template<class T> bool raw(std::size_t offset, T& value) const {
        if (offset > data_.size() || sizeof(T) > data_.size() - offset) return false;
        std::memcpy(&value, data_.data() + offset, sizeof(T)); return true;
    }
public:
    PeMetadata metadata;
    explicit PeFile(const std::vector<unsigned char>& data) : data_(data) {}
    bool read(std::uint64_t rva, void* target, std::size_t bytes) const {
        if (!target || !bytes || rva >= metadata.imageSize || bytes > metadata.imageSize - rva) return false;
        std::uint64_t offset = 0;
        if (rva < headers_) {
            if (bytes > headers_ - rva) return false;
            offset = rva;
        } else {
            const Section* found = nullptr;
            for (const auto& section : sections_) if (rva >= section.rva && rva - section.rva < (std::max)(section.rawSize,section.virtualSize)) {
                if (found) return false; found = &section;
            }
            if (!found || bytes > (std::max)(found->rawSize,found->virtualSize) - (rva - found->rva)) return false;
            const auto relative=rva-found->rva;
            const auto initialized=relative<found->rawSize ? (std::min)(std::uint64_t(bytes),std::uint64_t(found->rawSize)-relative) : 0;
            offset=found->raw+relative;
            if(initialized&&(offset>data_.size()||initialized>data_.size()-offset))return false;
            if(initialized)std::memcpy(target,data_.data()+static_cast<std::size_t>(offset),static_cast<std::size_t>(initialized));
            if(initialized<bytes)std::memset(static_cast<unsigned char*>(target)+initialized,0,bytes-static_cast<std::size_t>(initialized));
            return true;
        }
        if (offset > data_.size() || bytes > data_.size() - offset) return false;
        std::memcpy(target, data_.data() + static_cast<std::size_t>(offset), bytes); return true;
    }
    bool parse() {
        IMAGE_DOS_HEADER dos{}; if (!raw(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) return false;
        const auto ntOffset = static_cast<std::size_t>(dos.e_lfanew);
        DWORD signature = 0; IMAGE_FILE_HEADER file{};
        if (!raw(ntOffset, signature) || signature != IMAGE_NT_SIGNATURE ||
            ntOffset > data_.size() || sizeof(signature) > data_.size() - ntOffset ||
            !raw(ntOffset + sizeof(signature), file) || file.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            !file.NumberOfSections || file.NumberOfSections > 96 ||
            file.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64)) return false;
        const auto optionalOffset = ntOffset + sizeof(signature) + sizeof(file);
        IMAGE_OPTIONAL_HEADER64 optional{};
        if (!raw(optionalOffset, optional) || optional.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            optional.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_BASERELOC ||
            optional.ImageBase < 0x10000 || optional.ImageBase > 0x7fffffffffffULL ||
            !optional.SizeOfImage || optional.SizeOfImage > 0x40000000 ||
            optional.SizeOfHeaders < optionalOffset + file.SizeOfOptionalHeader ||
            optional.SizeOfHeaders > data_.size() || optional.SizeOfHeaders > optional.SizeOfImage ||
            optional.SizeOfImage-1>0x7fffffffffffULL-optional.ImageBase) return false;
        metadata.preferredBase = optional.ImageBase; metadata.imageSize = optional.SizeOfImage; headers_ = optional.SizeOfHeaders;
        const auto sectionOffset = optionalOffset + file.SizeOfOptionalHeader;
        if (sectionOffset > headers_ || std::size_t(file.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER) > headers_ - sectionOffset) return false;
        for (unsigned i = 0; i < file.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER section{}; if (!raw(sectionOffset + i * sizeof(section), section)) return false;
            const auto extent = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
            if (!extent || section.VirtualAddress < headers_ || section.VirtualAddress >= metadata.imageSize ||
                extent > metadata.imageSize - section.VirtualAddress || section.PointerToRawData > data_.size() ||
                section.SizeOfRawData > data_.size() - section.PointerToRawData) return false;
            for (const auto& previous : sections_) {
                const auto priorExtent = (std::max)(previous.virtualSize, previous.rawSize);
                if (section.VirtualAddress < std::uint64_t(previous.rva) + priorExtent &&
                    previous.rva < std::uint64_t(section.VirtualAddress) + extent) return false;
            }
            sections_.push_back({section.VirtualAddress, section.Misc.VirtualSize, section.PointerToRawData, section.SizeOfRawData});
            SectionSpec contractSection{section.VirtualAddress, section.Misc.VirtualSize, section.SizeOfRawData, section.Characteristics, {}};
            std::memcpy(contractSection.name.data(), section.Name, contractSection.name.size());
            metadata.sections.push_back(contractSection);
        }
        const auto functions=optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if(functions.Size){
            if(functions.Size%sizeof(RUNTIME_FUNCTION)||functions.Size>8*1024*1024||
                functions.VirtualAddress>=metadata.imageSize||functions.Size>metadata.imageSize-functions.VirtualAddress)return false;
            for(std::uint32_t offset=0;offset<functions.Size;offset+=sizeof(RUNTIME_FUNCTION)){
                RUNTIME_FUNCTION value{};
                if(!read(std::uint64_t(functions.VirtualAddress)+offset,&value,sizeof(value))||
                    value.BeginAddress>=value.EndAddress||value.EndAddress>metadata.imageSize||
                    value.UnwindData>=metadata.imageSize)return false;
                metadata.functions.push_back({std::uint64_t(functions.VirtualAddress)+offset,value.BeginAddress,value.EndAddress,value.UnwindData});
            }
        }
        const auto stringAt=[&](std::uint32_t rva,std::size_t limit,std::string& out){
            out.clear();for(std::size_t i=0;i<limit;++i){char c=0;if(!read(std::uint64_t(rva)+i,&c,1))return false;
                if(!c)return !out.empty();if(static_cast<unsigned char>(c)<0x20||static_cast<unsigned char>(c)>0x7e)return false;out.push_back(c);}return false;};
        const auto imports=optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if(imports.Size){
            if(imports.VirtualAddress>=metadata.imageSize||imports.Size>metadata.imageSize-imports.VirtualAddress||imports.Size>1024*1024)return false;
            bool terminated=false;
            for(std::uint32_t i=0;i<1024&&std::uint64_t(i)*sizeof(IMAGE_IMPORT_DESCRIPTOR)<imports.Size;++i){
                IMAGE_IMPORT_DESCRIPTOR descriptor{};
                if(!read(std::uint64_t(imports.VirtualAddress)+i*sizeof(descriptor),&descriptor,sizeof(descriptor)))return false;
                if(!descriptor.Name&&!descriptor.FirstThunk&&!descriptor.OriginalFirstThunk){terminated=true;break;}
                std::string library;if(!descriptor.Name||!descriptor.FirstThunk||!stringAt(descriptor.Name,256,library))return false;
                const auto lookup=descriptor.OriginalFirstThunk?descriptor.OriginalFirstThunk:descriptor.FirstThunk;bool end=false;
                for(std::uint32_t n=0;n<65536;++n){
                    std::uint64_t thunk=0;if(!read(std::uint64_t(lookup)+std::uint64_t(n)*8,&thunk,8))return false;
                    if(!thunk){end=true;break;}std::string symbol;
                    if(thunk&IMAGE_ORDINAL_FLAG64)symbol="#"+std::to_string(thunk&0xffff);
                    else if(thunk>0xffffffffULL-2||!stringAt(static_cast<std::uint32_t>(thunk)+2,512,symbol))return false;
                    const auto slot=std::uint64_t(descriptor.FirstThunk)+std::uint64_t(n)*8;
                    if(slot>=metadata.imageSize||8>metadata.imageSize-slot||metadata.imports.size()>=65536)return false;
                    metadata.imports.push_back({static_cast<std::uint32_t>(slot),thunk,library,symbol,
                        static_cast<std::uint32_t>(std::uint64_t(lookup)+std::uint64_t(n)*8),
                        static_cast<std::uint32_t>(std::uint64_t(imports.VirtualAddress)+i*sizeof(descriptor)),
                        descriptor.OriginalFirstThunk,descriptor.FirstThunk,descriptor.Name});
                }
                if(!end)return false;
            }
            if(!terminated)return false;
            std::sort(metadata.imports.begin(),metadata.imports.end(),[](const auto&a,const auto&b){return a.rva<b.rva;});
            for(std::size_t i=1;i<metadata.imports.size();++i)if(metadata.imports[i].rva<std::uint64_t(metadata.imports[i-1].rva)+8)return false;
        }
        const auto directory = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (directory.Size) {
            if (directory.VirtualAddress >= metadata.imageSize || directory.Size > metadata.imageSize - directory.VirtualAddress ||
                directory.Size > 8 * 1024 * 1024) return false;
            std::uint32_t offset = 0;
            while (offset < directory.Size) {
                IMAGE_BASE_RELOCATION block{};
                if (directory.Size - offset < sizeof(block) || !read(std::uint64_t(directory.VirtualAddress) + offset, &block, sizeof(block)) ||
                    block.SizeOfBlock < sizeof(block) || block.SizeOfBlock > directory.Size - offset || (block.SizeOfBlock - sizeof(block)) % sizeof(WORD)) return false;
                const auto count = (block.SizeOfBlock - sizeof(block)) / sizeof(WORD);
                for (std::uint32_t i = 0; i < count; ++i) {
                    WORD item = 0; if (!read(std::uint64_t(directory.VirtualAddress) + offset + sizeof(block) + i * sizeof(item), &item, sizeof(item))) return false;
                    const auto type = item >> 12;
                    if (type == IMAGE_REL_BASED_ABSOLUTE) continue;
                    if (type != IMAGE_REL_BASED_DIR64 || metadata.relocations.size() >= 262144) return false;
                    const std::uint64_t rva = std::uint64_t(block.VirtualAddress) + (item & 0xfff);
                    if (rva >= metadata.imageSize || sizeof(std::uint64_t) > metadata.imageSize - rva) return false;
                    metadata.relocations.push_back(static_cast<std::uint32_t>(rva));
                }
                offset += block.SizeOfBlock;
            }
            std::sort(metadata.relocations.begin(), metadata.relocations.end());
            for (std::size_t i = 1; i < metadata.relocations.size(); ++i)
                if (metadata.relocations[i] < std::uint64_t(metadata.relocations[i - 1]) + 8) return false;
        }
        return true;
    }
    Image image() const { return {[this](std::uint64_t rva, void* target, std::size_t bytes) { return read(rva, target, bytes); }, metadata.preferredBase, metadata.imageSize, metadata.sections, metadata.functions, false, metadata.imports}; }
};
inline Qualification qualifyBytes(const std::vector<unsigned char>& data, ModuleKind kind) {
    Qualification result;result.kind=kind;if(!validModuleKind(kind)){result.reason="compatibility-module-kind-invalid";return result;} PeFile file(data);
    if (!file.parse()) { result.reason = "compatibility-pe-invalid"; return result; }
    const auto image = file.image(); unsigned matched = 0;
    for (const auto layout : {Layout::Legacy, Layout::Public, Layout::OwnerScoped}) {
        Evidence record, ui, publisher;
        if (kind == ModuleKind::Client) { record = checkRecordLayout(image, layout); publisher = checkPublishLayout(image, layout); }
        else ui = checkUiLayout(image, layout);
        // A complete fixed-layout proof keeps its known code coordinates.
        // New graph/import declarations are still consumed in that coordinate
        // system, including every supplemental callee and its references.
        bool ok = kind == ModuleKind::Client ? record.matched && publisher.matched : ui.matched;
        const bool graphRequired=kind==ModuleKind::Client?
            (!recordGraph(layout).imports.empty()||!publishGraph(layout).imports.empty()):!uiGraph(layout).imports.empty();
        if(ok&&graphRequired){
            GraphResolution calibrated;
            if(kind==ModuleKind::Client){const std::array<GraphSpec,2> specs{recordGraph(layout),publishGraph(layout)};
                calibrated=resolveGraphs(image,specs,true);}
            else calibrated=resolveCalibratedGraph(image,uiGraph(layout));
            if(!calibrated.evidence.matched){ok=false;
                if(kind==ModuleKind::Client){record=calibrated.evidence;publisher=calibrated.evidence;}else ui=calibrated.evidence;}
            else {result.graph=std::make_shared<const GraphResolution>(std::move(calibrated));result.dynamicallyLocated=false;}
        }
        if (ok) { ++matched; result.layout = layout; result.record = record; result.publisher = publisher; result.ui = ui; }
        else if (!matched) { result.record = record; result.publisher = publisher; result.ui = ui; }
    }
    const auto tryFieldReference=[&](bool calibrated){
        // A separately certified complete reference family is selected only
        // after the existing canonical families fail. F never synthesizes C
        // bytes for admission; both fixed and required contracts are F's own.
        GraphResolution graph;
        const auto resolve=[&](bool useFixedCoordinates){
            if(kind==ModuleKind::Client){const std::array<GraphSpec,2> specs{field_record::graph(),field_publication::graph()};return resolveGraphs(image,specs,useFixedCoordinates);}
            return useFixedCoordinates?resolveCalibratedGraph(image,field_ui::graph()):resolveGraph(image,field_ui::graph());
        };
        graph=resolve(calibrated);
        if(graph.evidence.matched){
            const auto map=kind==ModuleKind::Client?field_reference::clientMap():field_reference::uiMap();
            Image reference=image;
            reference.read=referenceImageRead(graph,image.read,image.preferredBase,graph.specification.preferredBase);
            reference.preferredBase=graph.specification.preferredBase;
            reference.imageSize=graphImageSize(graph.specification);reference.sections=graph.specification.sections;reference.functions={};
            const auto first=checkFieldLayout(reference,kind);
            const auto second=kind==ModuleKind::Client?checkFieldLayout(reference,kind,true):first;
            if(map&&map->fieldImageSize()==reference.imageSize&&first.matched&&second.matched){
                matched=1;result.layout=Layout::OwnerScoped;result.referenceId=ReferenceId::Field20261114;
                if(kind==ModuleKind::Client){result.record=first;result.publisher=second;}else result.ui=first;
                result.graph=std::make_shared<const GraphResolution>(std::move(graph));
                result.reference=std::make_shared<const ReferenceBinding>(ReferenceBinding{ReferenceId::Field20261114,kind,map,
                    kind==ModuleKind::Client?fieldClientCertificate:fieldUiCertificate});
                result.dynamicallyLocated=!calibrated;
            }else{if(kind==ModuleKind::Client){result.record=first;result.publisher=second;}else result.ui=first;}
        }else if(kind==ModuleKind::Client){result.record=graph.evidence;result.publisher=graph.evidence;}else result.ui=graph.evidence;
    };
    if(!matched)tryFieldReference(true);
    if(!matched){
        // A fast calibration miss is not a version rejection. Discover complete
        // code shapes at their actual positions and join their typed references.
        std::vector<std::uint64_t> acceptedIdentity;
        const auto resolvedIdentity=[&](const GraphResolution& graph,Layout layout){
            const bool newer=layout==Layout::Public;std::vector<std::uint64_t> reference;
            if(kind==ModuleKind::Client)reference={0xcfdc0,newer?0x181e008ULL:0x181d008ULL,
                newer?0x1362478ULL:0x1361f38ULL,newer?0x1362530ULL:0x1361ff0ULL,
                newer?0x1370030ULL:0x136fa80ULL,newer?0x1370180ULL:0x136fbd0ULL,newer?0x4e27f0ULL:0x4e27e0ULL};
            else reference={0x1196360,0x17a0f0,0xbde90,0xec3c0,newer?0x639890ULL:0x639770ULL,
                newer?0xda95c8ULL:0xda91a8ULL,newer?0xdb0cd8ULL:0xdb0888ULL,newer?0xdb0d08ULL:0xdb08b8ULL,
                newer?0xdb0720ULL:0xdb02d0ULL,newer?0x6275a0ULL:0x627480ULL};
            if(kind==ModuleKind::Ui&&layout==Layout::OwnerScoped)reference={0x11cb640,0x1817d0,0xbe4c0,0xf33e0,0x65c6d0,0xdd4e88,0xddc5d8,0xddc608,0xddbff8,0x64a530};
            if(kind==ModuleKind::Client&&layout==Layout::OwnerScoped)reference={0xcfed0,0x18546d8,0x138ff40,0x138fff8,0x139d580,0x139d6d0,0x4f8240};
            for(auto& value:reference){const auto actual=translateRva(graph,value);if(!actual)return std::vector<std::uint64_t>();value=*actual;}
            return reference;
        };
        for(const auto layout:{Layout::Legacy,Layout::Public,Layout::OwnerScoped}){
            GraphResolution graph;
            if(kind==ModuleKind::Client){const std::array<GraphSpec,2> specs{recordGraph(layout),publishGraph(layout)};graph=resolveGraphs(image,specs);}
            else graph=resolveGraph(image,uiGraph(layout));
            if(graph.evidence.matched){
                const auto identity=resolvedIdentity(graph,layout);
                if(identity.empty())continue;
                // Two calibration coordinate systems may describe the same
                // actual functions/tables. That is one candidate, not ambiguity.
                if(matched&&identity==acceptedIdentity)continue;
                acceptedIdentity=identity;
                ++matched;result.layout=layout;result.dynamicallyLocated=true;
                if(kind==ModuleKind::Client){result.record=graph.evidence;result.publisher=graph.evidence;}else result.ui=graph.evidence;
                result.graph=std::make_shared<const GraphResolution>(std::move(graph));
            }else if(!matched){if(kind==ModuleKind::Client){result.record=graph.evidence;result.publisher=graph.evidence;}else result.ui=graph.evidence;}
        }
    }
    if(!matched)tryFieldReference(false);
    if (matched != 1) { result.layout.reset(); result.graph.reset();result.reference.reset();result.reason = matched ? "compatibility-contract-ambiguous" : "compatibility-contract-unrecognized"; return result; }
    result.metadata = std::make_shared<const PeMetadata>(std::move(file.metadata)); result.reason = result.dynamicallyLocated ? "compatibility-relocated-contract-matched" : "compatibility-contract-matched"; return result;
}
}
inline Qualification qualifyFile(const std::wstring& path, ModuleKind kind, bool requireDigest = true) {
    Qualification failure;
    if(!validModuleKind(kind)){failure.reason="compatibility-module-kind-invalid";return failure;}
    // No source handle, file mapping, file buffer or thread is kept in the cache.
    detail::Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file.valid()) { failure.reason = "compatibility-file-open"; failure.win32Error = GetLastError(); return failure; }
    FileIdentity before;
    if (!detail::identity(file.value, before) || before.sizeHigh || before.sizeLow < 4096 || before.sizeLow > 256 * 1024 * 1024) {
        failure.reason = "compatibility-file-identity-or-size"; return failure;
    }
    struct CacheEntry { std::wstring path; ModuleKind kind; FileIdentity identity; Qualification value; };
    struct FlightEntry { std::wstring path; ModuleKind kind; FileIdentity identity; std::weak_ptr<std::mutex> mutex; };
    static std::mutex mutex; static std::vector<CacheEntry> cache;
    static std::vector<FlightEntry> flights;
    std::shared_ptr<std::mutex> flight;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& entry : cache) if (entry.path == path && entry.kind == kind && entry.identity == before && (!requireDigest || !entry.value.sha256.empty()) && (!entry.value.layout || referenceBindingCurrent(entry.value))) {
            auto result = entry.value; result.cacheHit = true; result.fileBytesRead = 0; result.digestBytesHashed = 0; return result;
        }
        // Only cold callers register an in-flight calculation. No file handle,
        // bytes or result is retained here; expired slots leave on the next miss.
        flights.erase(std::remove_if(flights.begin(),flights.end(),
            [](const auto& entry){return entry.mutex.expired();}),flights.end());
        for (const auto& entry : flights) if (entry.path == path && entry.kind == kind && entry.identity == before) {
            flight = entry.mutex.lock();
            if (flight) break;
        }
        if (!flight) { flight = std::make_shared<std::mutex>(); flights.push_back({path,kind,before,flight}); }
    }
    // The metadata mutex never covers disk IO or waiting. Different held file
    // identities can qualify independently, including a replacement at this path.
    std::unique_lock<std::mutex> flightLock(*flight);
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& entry : cache) if (entry.path == path && entry.kind == kind && entry.identity == before && (!requireDigest || !entry.value.sha256.empty()) && (!entry.value.layout || referenceBindingCurrent(entry.value))) {
            auto result = entry.value; result.cacheHit = true; result.fileBytesRead = 0; result.digestBytesHashed = 0; return result;
        }
    }
    std::vector<unsigned char> data(before.sizeLow); std::size_t offset = 0;
    while (offset < data.size()) {
        const auto count = static_cast<DWORD>((std::min)(std::size_t(1024 * 1024), data.size() - offset)); DWORD got = 0;
        if (!ReadFile(file.value, data.data() + offset, count, &got, nullptr) || got != count) {
            failure.reason = "compatibility-file-read"; failure.win32Error = GetLastError(); return failure;
        }
        offset += got;
    }
    FileIdentity after;
    if (!detail::identity(file.value, after) || !(before == after)) { failure.reason = "compatibility-file-changed"; return failure; }
    auto result = detail::qualifyBytes(data, kind); result.identity = before;
    result.fileBytesRead = data.size();
    // The Steam-side trial needs the full code/data proof, but no digest.
    // A later digest-requiring caller upgrades a digest-free cache entry by
    // checking and hashing the same newly-read buffer, never a different file.
    if(requireDigest){
        result.sha256 = detail::bufferSha256(data); result.digestBytesHashed = data.size();
        if(result.sha256.size()!=64){result.layout.reset();result.reason="compatibility-file-digest-unavailable";return result;}
    }
    // Stamp again after qualification+digest. The digest and contract were both
    // derived from this one buffer; no independent path reopen can mix images.
    if(!detail::identity(file.value,after)||!(before==after)){
        failure.reason="compatibility-file-changed";return failure;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& entry : cache) if (entry.path == path && entry.kind == kind) { entry = {path, kind, before, result}; return result; }
        if (cache.size() >= 8) cache.erase(cache.begin());
        cache.push_back({path, kind, before, result});
    }
    return result;
}
inline std::uint64_t resolvedRva(const Qualification& disk,std::uint64_t reference){
    if(!referenceBindingCurrent(disk))return 0;
    if(disk.reference){const auto actual=coordinates::composedAddress(*disk.reference->coordinates,*disk.graph,reference);return actual?*actual:0;}
    if(!disk.graph)return reference;const auto actual=translateRva(*disk.graph,reference);return actual?*actual:0;
}
inline std::uint64_t referenceImageSize(const Qualification& disk,std::uint64_t actualSize){
    if(!referenceBindingCurrent(disk))return 0;
    if(disk.reference)return disk.reference->coordinates->canonicalImageSize();
    return disk.graph?detail::graphImageSize(disk.graph->specification):actualSize;
}
inline ymcc::steamdiagnostic::ReadViews makeProcessReadViews(const Qualification& disk,
    std::uint64_t moduleBase,std::uint64_t moduleSize,const ReadRva& raw){
    if(!referenceBindingCurrent(disk)||!raw||moduleBase<0x10000||moduleSize!=disk.metadata->imageSize||
       moduleBase>0x7fffffffffffULL||!moduleSize||moduleSize-1>0x7fffffffffffULL-moduleBase)return {};
    if(disk.reference)return coordinates::referenceReadViews(disk.reference->coordinates,disk.graph,moduleBase,moduleSize,raw);
    if(disk.graph){
        // The canonical UI profiles compare the vtable root, while their
        // complete declared getter slot is at root+0x50. Bind just this typed
        // root from that same proved eight-byte slot, including relocated C.
        const auto root=*disk.layout==Layout::OwnerScoped?0xdd4e88ULL:
            *disk.layout==Layout::Public?0xda95c8ULL:0xda91a8ULL;
        const std::array<coordinates::Point,1> roots{{{root,root,coordinates::PointUse::TypedModulePointer,0,0x50}}};
        return coordinates::canonicalGraphReadViews(disk.graph,moduleBase,moduleSize,raw,referenceImageSize(disk,moduleSize),
            disk.kind==ModuleKind::Ui?std::span<const coordinates::Point>(roots):std::span<const coordinates::Point>{});
    }
    // A complete canonical fixed-content qualification requires no coordinate
    // translation, including unknown image hashes. Do not infer pointer intent
    // from a scalar value: only the explicit typed operation uses this identity.
    ymcc::steamdiagnostic::ReadViews result{raw,raw,{}};
    result.explicitModulePointerNormalizer=[moduleBase,moduleSize](std::uint64_t value,std::uint64_t& output){
        if(value<moduleBase||value-moduleBase>=moduleSize)return false;output=value;return true;
    };
    return result;
}
// Retain the old module-reading source API for existing movement controls.
// Object data and pointer normalization require the explicit ReadViews ports.
inline ReadRva adaptProcessRead(const Qualification& disk,std::uint64_t base,std::uint64_t size,const ReadRva& raw){
    return makeProcessReadViews(disk,base,size,raw).canonicalModule;
}
// Convert actual loaded-image reads to preferred-base PE bytes for the exact
// same complete contracts. This verifies code and relocated vtable references;
// a disk-only match never grants a mutation in the engine/driver/trial.
using NamedImportValidator = std::function<bool(const ImportSlot&, std::uint64_t)>;
inline Evidence qualifyLoaded(const Qualification& disk, ModuleKind kind, const ReadRva& loaded,
    std::uint64_t loadedBase, std::uint64_t loadedSize,
    const std::function<bool(std::uint64_t)>& importedTarget = {},
    const NamedImportValidator& namedImportedTarget = {}, LoadedDiagnostic* diagnostic = nullptr) {
    LoadedDiagnostic local; auto& detail = diagnostic ? *diagnostic : local; detail = {};
    if (!validModuleKind(kind)||disk.kind!=kind||!referenceBindingCurrent(disk) || !loaded || loadedBase < 0x10000 ||
        loadedBase>0x7fffffffffffULL||!loadedSize||loadedSize-1>0x7fffffffffffULL-loadedBase||
        loadedSize != disk.metadata->imageSize) return {false, 0, 0, 0, "compatibility-loaded-identity-invalid"};
    const auto metadata = disk.metadata; const auto delta = loadedBase - metadata->preferredBase;
    const char* failureReason = nullptr;
    const auto noteSlot = [&](const ImportSlot& slot, std::uint64_t target) {
        detail = {}; detail.library = slot.library; detail.symbol = slot.symbol;
        detail.slotRva = slot.rva; detail.target = target;
    };
    const auto reject = [&](const char* reason, const char* stage, std::uint64_t address) {
        detail.reason = reason; detail.stage = stage; detail.failedAddress = address;
        failureReason = reason; return false;
    };
    const auto finish = [&](Evidence proof) {
        if (!proof.matched && failureReason && std::strcmp(proof.reason, "contract-read-unavailable") == 0)
            proof.reason = failureReason;
        return proof;
    };
    Image normalized{[&](std::uint64_t rva, void* target, std::size_t bytes) {
        if (!bytes || rva >= loadedSize || bytes > loadedSize - rva)
            return reject("contract-loaded-range-invalid", "loaded-range", loadedBase + rva);
        const auto end = rva + bytes;
        const auto first = std::lower_bound(metadata->imports.begin(), metadata->imports.end(), rva > 7 ? rva - 7 : 0,
            [](const auto& slot, std::uint64_t address) { return slot.rva < address; });
        if (!loaded(rva, target, bytes)) {
            if (first != metadata->imports.end() && first->rva < end) {
                noteSlot(*first, 0); detail.bytesRequested = bytes;
                return reject("contract-import-slot-read", "slot-range-read", loadedBase + rva);
            }
            return reject("contract-loaded-read-unavailable", "loaded-range-read", loadedBase + rva);
        }
        auto begin = std::lower_bound(metadata->relocations.begin(), metadata->relocations.end(), rva > 7 ? rva - 7 : 0);
        for (auto it = begin; it != metadata->relocations.end() && *it < end; ++it) {
            std::uint64_t value = 0;
            if (!loaded(*it, &value, sizeof(value)))
                return reject("contract-relocation-read-unavailable", "relocation-read", loadedBase + *it);
            value -= delta;
            const auto low = (std::max)(rva, std::uint64_t(*it)), high = (std::min)(end, std::uint64_t(*it) + 8);
            std::memcpy(static_cast<unsigned char*>(target) + (low - rva), reinterpret_cast<const unsigned char*>(&value) + (low - *it), static_cast<std::size_t>(high - low));
        }
        // Preserve the address-only validator seam for existing fixtures. Product
        // callers supply the named validator so data imports prove export identity.
        for (auto it = first; it != metadata->imports.end() && it->rva < end; ++it) {
            std::uint64_t targetValue = 0; noteSlot(*it, 0);
            if (!loaded(it->rva, &targetValue, sizeof(targetValue))) {
                detail.bytesRequested = sizeof(targetValue);
                return reject("contract-import-slot-read", "slot-read", loadedBase + it->rva);
            }
            detail.target = targetValue;
            if (!targetValue) return reject("contract-import-target-policy", "zero-target", loadedBase + it->rva);
            if (namedImportedTarget || importedTarget) {
                if (targetValue == it->originalThunk)
                    return reject("contract-import-unresolved-thunk", "unresolved-thunk", loadedBase + it->rva);
                const bool accepted = namedImportedTarget ? namedImportedTarget(*it, targetValue) : importedTarget(targetValue);
                if (!accepted) {
                    if (std::strcmp(detail.reason, "contract-import-unchecked") == 0)
                        reject("contract-import-target-policy", "target-policy", targetValue);
                    else failureReason = detail.reason;
                    return false;
                }
            } else if (targetValue != it->originalThunk && (targetValue < 0x10000 || targetValue > 0x7fffffffffffULL))
                return reject("contract-import-target-policy", "target-address", targetValue);
            const auto low = (std::max)(rva, std::uint64_t(it->rva)), high = (std::min)(end, std::uint64_t(it->rva) + 8);
            std::memcpy(static_cast<unsigned char*>(target) + (low - rva), reinterpret_cast<const unsigned char*>(&it->originalThunk) + (low - it->rva), static_cast<std::size_t>(high - low));
        }
        return true;
    }, metadata->preferredBase, metadata->imageSize,metadata->sections,metadata->functions,true,metadata->imports};
    Image contract=normalized;
    if(disk.graph){contract.read=referenceImageRead(*disk.graph,normalized.read,metadata->preferredBase,disk.graph->specification.preferredBase);
        contract.preferredBase=disk.graph->specification.preferredBase;contract.imageSize=detail::graphImageSize(disk.graph->specification);
        contract.sections=disk.graph->specification.sections;contract.functions={};}
    Evidence record;
    if(kind==ModuleKind::Ui){record=finish(disk.reference?detail::checkFieldLayout(contract,kind):checkUiLayout(contract,*disk.layout));if(!record.matched)return record;}
    else {
        record=finish(disk.reference?detail::checkFieldLayout(contract,kind):checkRecordLayout(contract,*disk.layout));if(!record.matched)return record;
        const auto publisher=finish(disk.reference?detail::checkFieldLayout(contract,kind,true):checkPublishLayout(contract,*disk.layout));if(!publisher.matched)return publisher;
        record.rangesChecked+=publisher.rangesChecked;record.bytesChecked+=publisher.bytesChecked;
        record.mutableRangesIgnored+=publisher.mutableRangesIgnored;
    }
    const auto appendRequired=[&](const GraphSpec& specification,const GraphResolution* graph){
        const auto required=finish(checkLoadedRequired(specification,normalized,contract,graph));
        if(!required.matched){record=required;return false;}
        record.rangesChecked+=required.rangesChecked;record.bytesChecked+=required.bytesChecked;return true;
    };
    if(disk.graph){if(!appendRequired(disk.graph->specification,disk.graph.get()))return record;}
    else if(kind==ModuleKind::Ui){if(!appendRequired(uiGraph(*disk.layout),nullptr))return record;}
    else if(!appendRequired(recordGraph(*disk.layout),nullptr)||!appendRequired(publishGraph(*disk.layout),nullptr))return record;
    return record;
}
}
