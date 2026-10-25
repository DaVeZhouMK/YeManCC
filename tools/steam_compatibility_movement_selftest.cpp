// Reuse the actual file-layer fixtures and assertions; never load a Steam DLL.
#define wmain steamCompatibilityFileLayerEntry
#include "steam_compatibility_file_selftest.cpp"
#undef wmain

namespace {
constexpr std::uint64_t oldGetter = 0xcfdc0;
constexpr std::size_t getterSize = 14;
struct MovementProof {
    std::string name, originalSha, movedSha, originalGetter, caveBefore;
    std::uint64_t cave = 0, secondCave = 0, slot = 0, referenceCount = 0;
    bool noPdata = false;
};
struct Observation {
    std::string sample, scenario, reason, recordReason, publisherReason;
    bool qualified = false, dynamicallyLocated = false;
    std::uint64_t resolved = 0;
};
std::vector<MovementProof> movementProofs;
std::vector<Observation> observations;
std::string hexBytes(const Bytes& bytes) {
    std::ostringstream text;
    for (auto byte : bytes) text << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return text.str();
}
Bytes rvaBytes(const Bytes& image, const Header& header, std::uint64_t rva, std::size_t bytes) {
    const auto at = header.rawOffset(image, rva, bytes);
    return Bytes(image.begin() + static_cast<std::ptrdiff_t>(at), image.begin() + static_cast<std::ptrdiff_t>(at + bytes));
}
std::vector<std::uint64_t> findCaves(const Bytes& bytes, const Header& header,
    const compat::PeMetadata& metadata, compat::Layout layout) {
    IMAGE_SECTION_HEADER text{}; bool found = false;
    for (unsigned i = 0; i < header.fileHeader.NumberOfSections; ++i) {
        const auto section = at<IMAGE_SECTION_HEADER>(bytes, header.sections + i * sizeof(IMAGE_SECTION_HEADER));
        if (std::memcmp(section.Name, ".text\0", 6) == 0) { require(!found, "unique text section"); text = section; found = true; }
    }
    require(found && (text.Characteristics & IMAGE_SCN_MEM_EXECUTE), "executable text section");
    const std::uint64_t end = std::uint64_t(text.VirtualAddress) + (std::min)(text.Misc.VirtualSize, text.SizeOfRawData);
    const auto low = end > 65536 ? end - 65536 : text.VirtualAddress;
    const std::array<compat::GraphSpec, 2> specs{compat::recordGraph(layout), compat::publishGraph(layout)};
    std::vector<std::uint64_t> result;
    for (std::uint64_t rva = end - getterSize; rva > low; --rva) {
        const auto raw = header.rawOffset(bytes, rva, getterSize);
        if (!std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(raw), bytes.begin() + static_cast<std::ptrdiff_t>(raw + getterSize), [](auto b) { return b == 0xcc; })) continue;
        bool occupied = false;
        for (const auto selected : result) if (rva < selected + getterSize && selected < rva + getterSize) occupied = true;
        for (const auto& function : metadata.functions) if (rva < function.end && function.begin < rva + getterSize) occupied = true;
        for (const auto& spec : specs) {
            for (const auto& range : spec.ranges) if (rva < range.refRva + range.size && range.refRva < rva + getterSize) occupied = true;
            for (const auto& fixup : spec.fixups) if (fixup.targetRva >= rva && fixup.targetRva < rva + getterSize) occupied = true;
        }
        if (!occupied) result.push_back(rva);
        if (result.size() == 2) break;
    }
    require(result.size() == 2, "two distinct 14-byte CC caves in text tail, outside pdata/contracts");
    return result;
}
std::vector<compat::GraphFixup> getterReferences(compat::Layout layout) {
    std::vector<compat::GraphFixup> fields;
    for (const auto& spec : {compat::recordGraph(layout), compat::publishGraph(layout)}) {
        for (const auto& fixup : spec.fixups) {
            if (fixup.targetRva != oldGetter) continue;
            const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto& value) { return value.fieldRva == fixup.fieldRva; });
            if (found == fields.end()) fields.push_back(fixup);
            else require(found->kind == fixup.kind && found->originEndRva == fixup.originEndRva, "duplicate reference descriptions agree");
        }
    }
    require(!fields.empty(), "qualified getter incoming reference"); return fields;
}
void rebindGetter(Bytes& bytes, const Header& header, const std::vector<compat::GraphFixup>& refs, std::uint64_t to) {
    for (const auto& field : refs) {
        const auto size = compat::fixupSize(field.kind), raw = header.rawOffset(bytes, field.fieldRva, size);
        switch (field.kind) {
        case compat::FixupKind::Absolute64:
            require(at<std::uint64_t>(bytes, raw) == header.opt.ImageBase + oldGetter, "original absolute getter binding");
            put<std::uint64_t>(bytes, raw, header.opt.ImageBase + to); break;
        case compat::FixupKind::Rva32:
            require(to <= 0xffffffffULL, "RVA32 fixture bound"); put<std::uint32_t>(bytes, raw, static_cast<std::uint32_t>(to)); break;
        case compat::FixupKind::Rva64: put<std::uint64_t>(bytes, raw, to); break;
        case compat::FixupKind::Relative32: {
            const auto delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(field.originEndRva);
            require(delta >= (std::numeric_limits<std::int32_t>::min)() && delta <= (std::numeric_limits<std::int32_t>::max)(), "rel32 fixture bound");
            put<std::int32_t>(bytes, raw, static_cast<std::int32_t>(delta)); break;
        }
        case compat::FixupKind::Relative8: {
            const auto delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(field.originEndRva);
            require(delta >= -128 && delta <= 127, "rel8 fixture bound"); put<std::int8_t>(bytes, raw, static_cast<std::int8_t>(delta)); break;
        }
        }
    }
}
compat::Qualification observeFile(const std::string& sample, const std::string& scenario, const Bytes& bytes) {
    const auto q = fileQualification(scenario, bytes, compat::ModuleKind::Client);
    std::cerr << "OBSERVED " << sample << " " << scenario << " " << q.reason << std::endl;
    observations.push_back({sample, scenario, q.reason, q.record.reason, q.publisher.reason,
        bool(q.layout), q.dynamicallyLocated, q.layout ? compat::resolvedRva(q, oldGetter) : 0});
    return q;
}
void movementLoaded(const std::string& name, const Bytes& moved, const Bytes& getter,
    const compat::Qualification& disk, std::uint64_t cave, std::uint64_t slot) {
    require(disk.layout && disk.metadata && disk.graph, "dynamic qualification before loaded test");
    const auto preferred = disk.metadata->preferredBase, size = disk.metadata->imageSize;
    for (const auto base : {preferred, preferred + 0x20000000ULL, preferred - 0x10000000ULL}) {
        auto mapped = mappedImage(moved, *disk.metadata, base);
        const auto read = readerFor(mapped);
        const auto proof = compat::qualifyLoaded(disk, compat::ModuleKind::Client, read, base, size);
        check(name + "/loaded/ASLR-" + std::to_string(base), proof.matched, proof.reason, proof.failedRva, proof.mutableRangesIgnored);
        check(name + "/loaded/actual-vtable-points-to-new-" + std::to_string(base), at<std::uint64_t>(mapped, static_cast<std::size_t>(slot)) == base + cave);
        const compat::ReadRva rawAddress = [&](std::uint64_t address, void* target, std::size_t bytes) {
            return address >= base && address - base < size && bytes <= size - (address - base) && read(address - base, target, bytes);
        };
        std::vector<std::uint64_t> actualReads;
        const auto adapted = compat::adaptProcessRead(disk, base, size, [&](std::uint64_t address, void* target, std::size_t bytes) {
            actualReads.push_back(address); return rawAddress(address, target, bytes);
        });
        Bytes observed(getterSize), oldRaw(getterSize);
        const bool adaptedOk = adapted(base + oldGetter, observed.data(), observed.size());
        check(name + "/adapt/reference-getter-reads-new-cave-" + std::to_string(base), adaptedOk && observed == getter &&
            std::find(actualReads.begin(), actualReads.end(), base + cave) != actualReads.end() &&
            std::find(actualReads.begin(), actualReads.end(), base + oldGetter) == actualReads.end(), "actualReadCount=" + std::to_string(actualReads.size()), cave);
        check(name + "/adapt/raw-old-getter-still-CC-" + std::to_string(base), rawAddress(base + oldGetter, oldRaw.data(), oldRaw.size()) &&
            std::all_of(oldRaw.begin(), oldRaw.end(), [](auto b) { return b == 0xcc; }));
        std::set<std::uint64_t> resolvedTargets;
        for (const auto& imported : disk.metadata->imports) {
            const auto target = 0x00007ff800000000ULL + imported.rva;
            put<std::uint64_t>(mapped, imported.rva, target); resolvedTargets.insert(target);
        }
        unsigned calls = 0;
        const auto resolved = compat::qualifyLoaded(disk, compat::ModuleKind::Client, read, base, size,
            [&](std::uint64_t address) { ++calls; return resolvedTargets.contains(address); });
        check(name + "/loaded/resolved-IAT-" + std::to_string(base), resolved.matched && calls > 0,
            std::string(resolved.reason) + "; validatorCalls=" + std::to_string(calls), resolved.failedRva, resolved.mutableRangesIgnored);
        mapped[static_cast<std::size_t>(cave + 3)] ^= 1;
        const auto tampered = compat::qualifyLoaded(disk, compat::ModuleKind::Client, read, base, size);
        check(name + "/loaded/new-getter-semantic-tamper-" + std::to_string(base), !tampered.matched, tampered.reason, tampered.failedRva);
    }
}
void runMovement(const std::string& name, const fs::path& source, compat::Layout layout) {
    const auto original = readFile(source); Header header(original); compat::detail::PeFile pe(original);
    require(pe.parse(), "original movement PE parse");
    const auto base = observeFile(name, "original-control", original);
    check(name + "/original-qualifies", base.layout == layout && !base.dynamicallyLocated, base.reason);
    require(base.layout == layout, "original client control must qualify");
    // Preserve and classify the initial file-layer RIP test failure. That
    // operand is an interior alignment address, followed by AND RBX,-16.
    auto alignedOperand = original;
    const std::uint64_t alignedField = 0xbeddc;
    const auto alignedRaw = header.rawOffset(original, alignedField, 8);
    require(at<std::uint32_t>(original, alignedRaw + 4) == 0xf0e38348u, "LEA operand followed by AND RBX,-16");
    const auto beforeTarget = static_cast<std::int64_t>(alignedField + 4) + at<std::int32_t>(original, alignedRaw);
    alignedOperand[alignedRaw] ^= 1;
    const auto afterTarget = static_cast<std::int64_t>(alignedField + 4) + at<std::int32_t>(alignedOperand, alignedRaw);
    check(name + "/classification/original-RIP-mutation-preserves-aligned-address", beforeTarget != afterTarget &&
        (beforeTarget & ~std::int64_t(15)) == (afterTarget & ~std::int64_t(15)), "static LEA/AND arithmetic only", alignedField);
    const auto aligned = observeFile(name, "original-single-RIP-change-allowed-by-graph", alignedOperand);
    check(name + "/classification/single-global-operand-movement-is-allowed", aligned.layout == layout && aligned.dynamicallyLocated, aligned.reason);
    const auto getter = rvaBytes(original, header, oldGetter, getterSize);
    require(getter == Bytes{0x48,0x8b,0x81,0x9a,0xe3,0xff,0xff,0x48,0x89,0x02,0x48,0x8b,0xc2,0xc3}, "exact complete 14B getter");
    bool noPdata = true;
    for (const auto& function : pe.metadata.functions) if (oldGetter < function.end && function.begin < oldGetter + getterSize) noPdata = false;
    check(name + "/getter-no-pdata-owner", noPdata);
    require(noPdata, "getter is a complete no-pdata leaf");
    const auto caves = findCaves(original, header, pe.metadata, layout);
    const auto refs = getterReferences(layout);
    const std::uint64_t expectedSlot = layout == compat::Layout::Legacy ? 0x136fc20 : 0x13701d0;
    require(std::any_of(refs.begin(), refs.end(), [&](const auto& f) { return f.fieldRva == expectedSlot && f.kind == compat::FixupKind::Absolute64; }), "actual secondary vtable +50 incoming reference");
    auto moved = original;
    const auto oldRaw = header.rawOffset(original, oldGetter, getterSize), newRaw = header.rawOffset(original, caves[0], getterSize);
    std::copy(getter.begin(), getter.end(), moved.begin() + static_cast<std::ptrdiff_t>(newRaw));
    std::fill_n(moved.begin() + static_cast<std::ptrdiff_t>(oldRaw), getterSize, static_cast<unsigned char>(0xcc));
    rebindGetter(moved, header, refs, caves[0]);
    const auto originalSha = digest(original), movedSha = digest(moved);
    check(name + "/whole-SHA-changed", originalSha != movedSha);
    check(name + "/old-14B-actually-erased", rvaBytes(moved, header, oldGetter, getterSize) == Bytes(getterSize, 0xcc));
    check(name + "/new-cave-complete-getter", rvaBytes(moved, header, caves[0], getterSize) == getter);
    writeFile(output / (name + "-moved.pe.bin"), moved);
    movementProofs.push_back({name, originalSha, movedSha, hexBytes(getter), hexBytes(rvaBytes(original, header, caves[0], getterSize)), caves[0], caves[1], expectedSlot, refs.size(), noPdata});
    const auto q = observeFile(name, "moved-positive", moved);
    check(name + "/actual-qualifyFile-dynamic", q.layout == layout && q.dynamicallyLocated && bool(q.graph), q.reason);
    check(name + "/resolved-old-coordinate-is-new-RVA", q.layout && compat::resolvedRva(q, oldGetter) == caves[0] && compat::resolvedRva(q, oldGetter) != oldGetter,
        q.record.reason, q.layout ? compat::resolvedRva(q, oldGetter) : 0);
    if (q.layout && q.graph) movementLoaded(name, moved, getter, q, caves[0], expectedSlot);
    const auto reject = [&](const std::string& scenario, const auto& edit) {
        auto bytes = moved; edit(bytes); const auto bad = observeFile(name, scenario, bytes);
        check(name + "/negative/" + scenario, !bad.layout, std::string(bad.reason) + "; graph=" + bad.record.reason);
    };
    reject("vtable-points-one-byte-inside-getter", [&](Bytes& b) { put<std::uint64_t>(b, header.rawOffset(b, expectedSlot, 8), header.opt.ImageBase + caves[0] + 1); });
    reject("vtable-still-points-erased-old-getter", [&](Bytes& b) { put<std::uint64_t>(b, header.rawOffset(b, expectedSlot, 8), header.opt.ImageBase + oldGetter); });
    reject("changed-getter-scalar-semantics", [&](Bytes& b) { b[newRaw + 3] ^= 1; });
    reject("no-copy-of-required-getter", [&](Bytes& b) { std::fill_n(b.begin() + static_cast<std::ptrdiff_t>(newRaw), getterSize, static_cast<unsigned char>(0xcc)); });
    auto duplicates = moved;
    const auto secondRaw = header.rawOffset(duplicates, caves[1], getterSize);
    std::copy(getter.begin(), getter.end(), duplicates.begin() + static_cast<std::ptrdiff_t>(secondRaw));
    const auto selected = observeFile(name, "two-copies-vtable-selects-first", duplicates);
    check(name + "/classification/two-copies-are-disambiguated-by-vtable", selected.layout == layout && selected.dynamicallyLocated && compat::resolvedRva(selected, oldGetter) == caves[0], selected.reason);
    put<std::uint64_t>(duplicates, header.rawOffset(duplicates, expectedSlot, 8), header.opt.ImageBase + oldGetter);
    const auto noRoute = observeFile(name, "two-copies-no-valid-required-route", duplicates);
    check(name + "/negative/two-copies-cannot-satisfy-required-vtable-edge", !noRoute.layout, std::string(noRoute.reason) + "; graph=" + noRoute.record.reason);
    // Leaving the original bytes is not enough to choose the old location.
    auto oldAlsoPresent = moved;
    std::copy(getter.begin(), getter.end(), oldAlsoPresent.begin() + static_cast<std::ptrdiff_t>(oldRaw));
    const auto routedNew = observeFile(name, "old-bytes-present-but-vtable-routes-new", oldAlsoPresent);
    check(name + "/classification/existing-old-pattern-cannot-steal-new-route", routedNew.layout == layout && routedNew.dynamicallyLocated && compat::resolvedRva(routedNew, oldGetter) == caves[0], routedNew.reason);
    auto neverMoved = original;
    std::copy(getter.begin(), getter.end(), neverMoved.begin() + static_cast<std::ptrdiff_t>(newRaw));
    const auto originalRoute = observeFile(name, "uncalled-copy-original-route-intact", neverMoved);
    check(name + "/classification/unused-copy-is-not-movement", originalRoute.layout == layout && !originalRoute.dynamicallyLocated && compat::resolvedRva(originalRoute, oldGetter) == oldGetter, originalRoute.reason);
    std::cerr << "FINISHED " << name << " newGetter=" << std::hex << caves[0] << " slot=" << expectedSlot << std::dec << " refs=" << refs.size() << '\n';
}
// Publisher movement uses the same complete record/publish graphs as the product.
// The new executable section is fixture storage, never loaded as a DLL.
struct PublisherProof {
    std::string sample, scenario, originalSha, movedSha;
    std::uint64_t begin, end, destination, second, getter, codeParts, pdataParts, changedFixups;
};
std::vector<PublisherProof> publisherProofs;
struct PublisherTemporaryFiles {
    std::string name;
    ~PublisherTemporaryFiles() {
        for (const auto* file : {L"fixture.bin", L"fixture-next.bin"}) {
            const auto path = output / file;
            const bool deleted = DeleteFileW(path.c_str()) != FALSE;
            const auto error = GetLastError();
            check(name + "/cleanup/" + path.filename().string(),
                (deleted || error == ERROR_FILE_NOT_FOUND) && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES,
                "Win32 DeleteFileW; error=" + std::to_string(deleted ? 0 : error));
        }
    }
};
std::vector<compat::GraphFixup> completeFixups(compat::Layout layout) {
    std::vector<compat::GraphFixup> result;
    for (const auto spec : {compat::recordGraph(layout), compat::publishGraph(layout)}) {
        for (const auto field : spec.fixups) {
            const auto found = std::find_if(result.begin(), result.end(), [&](const auto& old) { return old.fieldRva == field.fieldRva; });
            if (found == result.end()) result.push_back(field);
            else require(found->kind == field.kind && found->originEndRva == field.originEndRva && found->targetRva == field.targetRva, "merged real fixups agree");
        }
    }
    return result;
}
std::uint64_t addPublisherSection(Bytes& bytes) {
    Header h(bytes);
    const auto align = [](std::uint64_t value, std::uint64_t alignment) { require(alignment && !(alignment & (alignment - 1)), "PE alignment"); return (value + alignment - 1) & ~(alignment - 1); };
    const auto headerAt = h.sections + h.fileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
    require(headerAt + sizeof(IMAGE_SECTION_HEADER) <= h.opt.SizeOfHeaders, "space for derived executable section header");
    for (unsigned i = 0; i < h.fileHeader.NumberOfSections; ++i) {
        const auto section = at<IMAGE_SECTION_HEADER>(bytes, h.sections + i * sizeof(IMAGE_SECTION_HEADER));
        require(!section.SizeOfRawData || headerAt + sizeof(IMAGE_SECTION_HEADER) <= section.PointerToRawData, "new section header cannot overwrite existing raw data");
    }
    IMAGE_SECTION_HEADER section{};
    std::memcpy(section.Name, ".ymccmv", 7);
    section.VirtualAddress = static_cast<DWORD>(align(h.opt.SizeOfImage, h.opt.SectionAlignment));
    section.Misc.VirtualSize = 0x3000;
    section.PointerToRawData = static_cast<DWORD>(align(bytes.size(), h.opt.FileAlignment));
    section.SizeOfRawData = static_cast<DWORD>(align(section.Misc.VirtualSize, h.opt.FileAlignment));
    section.Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    bytes.resize(std::size_t(section.PointerToRawData) + section.SizeOfRawData, 0xcc);
    put(bytes, headerAt, section);
    ++h.fileHeader.NumberOfSections;
    h.opt.SizeOfImage = static_cast<DWORD>(align(std::uint64_t(section.VirtualAddress) + section.Misc.VirtualSize, h.opt.SectionAlignment));
    h.opt.SizeOfCode += section.SizeOfRawData;
    put(bytes, h.file, h.fileHeader); put(bytes, h.optional, h.opt);
    return section.VirtualAddress;
}
void writeFixtureFixup(Bytes& bytes, const Header& h, const compat::GraphFixup& field,
    std::uint64_t actualField, std::uint64_t actualEnd, std::uint64_t actualTarget) {
    const auto raw = h.rawOffset(bytes, actualField, compat::fixupSize(field.kind));
    switch (field.kind) {
    case compat::FixupKind::Relative8: case compat::FixupKind::Relative32: {
        const auto displacement = static_cast<std::int64_t>(actualTarget) - static_cast<std::int64_t>(actualEnd);
        if (field.kind == compat::FixupKind::Relative8) {
            require(displacement >= -128 && displacement <= 127, "complete movement keeps signed rel8 representable");
            put<std::int8_t>(bytes, raw, static_cast<std::int8_t>(displacement));
        } else {
            require(displacement >= (std::numeric_limits<std::int32_t>::min)() && displacement <= (std::numeric_limits<std::int32_t>::max)(), "complete movement keeps signed rel32 representable");
            put<std::int32_t>(bytes, raw, static_cast<std::int32_t>(displacement));
        }
        break;
    }
    case compat::FixupKind::Absolute64: put<std::uint64_t>(bytes, raw, h.opt.ImageBase + actualTarget); break;
    case compat::FixupKind::Rva32:
        require(actualTarget <= 0xffffffffULL, "moved RVA32 fits"); put<std::uint32_t>(bytes, raw, static_cast<std::uint32_t>(actualTarget)); break;
    case compat::FixupKind::Rva64: put<std::uint64_t>(bytes, raw, actualTarget); break;
    }
}
void sortFixturePdata(Bytes& bytes, const Header& h) {
    const auto directory = h.opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    require(directory.Size && directory.Size % sizeof(RUNTIME_FUNCTION) == 0, "real exception table");
    std::vector<RUNTIME_FUNCTION> functions;
    for (std::uint64_t rva = directory.VirtualAddress; rva < std::uint64_t(directory.VirtualAddress) + directory.Size; rva += sizeof(RUNTIME_FUNCTION))
        functions.push_back(at<RUNTIME_FUNCTION>(bytes, h.rawOffset(bytes, rva, sizeof(RUNTIME_FUNCTION))));
    std::sort(functions.begin(), functions.end(), [](const auto& a, const auto& b) { return a.BeginAddress < b.BeginAddress; });
    for (std::size_t i = 0; i < functions.size(); ++i) {
        require(!i || functions[i - 1].BeginAddress < functions[i].BeginAddress, "derived exception table has unique sorted begin addresses");
        put(bytes, h.rawOffset(bytes, directory.VirtualAddress + i * sizeof(RUNTIME_FUNCTION), sizeof(RUNTIME_FUNCTION)), functions[i]);
    }
}
void publisherLoaded(const std::string& name, const Bytes& moved, const Bytes& originalBody,
    const compat::Qualification& disk, std::uint64_t begin, std::uint64_t destination, std::uint64_t getter) {
    require(disk.layout && disk.metadata && disk.graph, "real publisher file qualification before loaded checks");
    const auto preferred = disk.metadata->preferredBase, size = disk.metadata->imageSize;
    for (const auto base : {preferred, preferred + 0x20000000ULL, preferred - 0x10000000ULL}) {
        const auto suffix = "-" + std::to_string(base);
        auto mapped = mappedImage(moved, *disk.metadata, base);
        const auto read = readerFor(mapped);
        const auto loaded = compat::qualifyLoaded(disk, compat::ModuleKind::Client, read, base, size);
        check(name + "/loaded/full-publisher" + suffix, loaded.matched, loaded.reason, loaded.failedRva, loaded.mutableRangesIgnored);
        std::vector<std::uint64_t> actualReads;
        const auto adapted = compat::adaptProcessRead(disk, base, size, [&](std::uint64_t address, void* target, std::size_t count) {
            actualReads.push_back(address);
            return address >= base && address - base < size && count <= size - (address - base) && read(address - base, target, count);
        });
        Bytes observed(originalBody.size());
        const bool ok = adapted(base + begin, observed.data(), observed.size());
        const bool touchedNew = std::find(actualReads.begin(), actualReads.end(), base + destination) != actualReads.end();
        const bool touchedOld = std::any_of(actualReads.begin(), actualReads.end(), [&](auto address) { return address >= base + begin && address < base + begin + originalBody.size(); });
        check(name + "/adapt/full-original-publisher-from-new-RVA" + suffix, ok && observed == originalBody && touchedNew && !touchedOld,
            "actualReadCount=" + std::to_string(actualReads.size()), destination);
        check(name + "/loaded/raw-old-publisher-erased" + suffix,
            std::all_of(mapped.begin() + static_cast<std::ptrdiff_t>(begin), mapped.begin() + static_cast<std::ptrdiff_t>(begin + originalBody.size()), [](auto b) { return b == 0xcc; }));
        check(name + "/loaded/actual-publisher-entry-prologue" + suffix,
            std::equal(originalBody.begin(), originalBody.begin() + 10, mapped.begin() + static_cast<std::ptrdiff_t>(destination)));
        Bytes observedGetter(getterSize);
        actualReads.clear();
        check(name + "/adapt/getter-address-propagated" + suffix, adapted(base + oldGetter, observedGetter.data(), observedGetter.size()) &&
            observedGetter == Bytes({0x48,0x8b,0x81,0x9a,0xe3,0xff,0xff,0x48,0x89,0x02,0x48,0x8b,0xc2,0xc3}) &&
            std::find(actualReads.begin(), actualReads.end(), base + getter) != actualReads.end(), "resolved getter", getter);
        std::set<std::uint64_t> resolvedTargets;
        for (const auto& imported : disk.metadata->imports) {
            const auto target = 0x00007ff800000000ULL + imported.rva;
            put<std::uint64_t>(mapped, imported.rva, target); resolvedTargets.insert(target);
        }
        unsigned validatorCalls = 0;
        const auto imports = compat::qualifyLoaded(disk, compat::ModuleKind::Client, read, base, size,
            [&](std::uint64_t address) { ++validatorCalls; return resolvedTargets.contains(address); });
        check(name + "/loaded/resolved-IAT" + suffix, imports.matched && validatorCalls > 0,
            std::string(imports.reason) + "; validatorCalls=" + std::to_string(validatorCalls), imports.failedRva, imports.mutableRangesIgnored);
        mapped[static_cast<std::size_t>(destination)] ^= 1;
        const auto tampered = compat::qualifyLoaded(disk, compat::ModuleKind::Client, read, base, size);
        check(name + "/loaded/new-publisher-tamper" + suffix, !tampered.matched, tampered.reason, tampered.failedRva);
        mapped[static_cast<std::size_t>(destination)] ^= 1;
        const auto owner = std::find_if(disk.metadata->functions.begin(), disk.metadata->functions.end(), [&](const auto& f) { return f.begin == destination; });
        require(owner != disk.metadata->functions.end(), "moved publisher has actual exception owner");
        mapped[static_cast<std::size_t>(owner->tableRva)] ^= 1;
        const auto badPdata = compat::qualifyLoaded(disk, compat::ModuleKind::Client, read, base, size);
        check(name + "/loaded/new-publisher-pdata-tamper" + suffix, !badPdata.matched, badPdata.reason, badPdata.failedRva);
    }
}
void runPublisherMovement(const std::string& sample, const fs::path& source, compat::Layout layout, bool withGetter) {
    const auto scenario = std::string(withGetter ? "getter-and-publisher" : "publisher-only");
    const auto name = sample + "/" + scenario;
    PublisherTemporaryFiles temporaryFiles{name};
    const auto original = readFile(source); Header originalHeader(original);
    compat::detail::PeFile pe(original); require(pe.parse(), "publisher original PE parse");
    const std::uint64_t begin = layout == compat::Layout::Legacy ? 0x4e27e0 : 0x4e27f0;
    const auto spec = compat::publishGraph(layout);
    std::uint64_t end = begin, codeParts = 0;
    // Adjacent complete code ranges are the split/chained publisher itself;
    // following nonadjacent callees remain at their real qualified locations.
    for (const auto& range : spec.ranges) if (range.kind == compat::RangeKind::Code && range.refRva == end) { end += range.size; ++codeParts; }
    require(codeParts > 1 && end > begin && end - begin < 0x1000, "complete contiguous publisher split ranges fit fixture page");
    std::uint64_t pdataParts = 0;
    for (const auto& function : pe.metadata.functions) if (function.begin >= begin && function.begin < end) {
        require(function.end <= end, "complete publisher includes every pdata-owned part"); ++pdataParts;
    }
    require(pdataParts > 1, "real split publisher exception owners");
    const auto body = rvaBytes(original, originalHeader, begin, static_cast<std::size_t>(end - begin));
    auto moved = original; const auto destination = addPublisherSection(moved), second = destination + 0x1000;
    Header header(moved);
    std::copy(body.begin(), body.end(), moved.begin() + static_cast<std::ptrdiff_t>(header.rawOffset(moved, destination, body.size())));
    const auto getter = withGetter ? findCaves(original, originalHeader, pe.metadata, layout)[0] : oldGetter;
    if (withGetter) {
        const auto bytes = rvaBytes(original, originalHeader, oldGetter, getterSize);
        std::copy(bytes.begin(), bytes.end(), moved.begin() + static_cast<std::ptrdiff_t>(header.rawOffset(moved, getter, getterSize)));
        std::fill_n(moved.begin() + static_cast<std::ptrdiff_t>(header.rawOffset(moved, oldGetter, getterSize)), getterSize, static_cast<unsigned char>(0xcc));
    }
    const auto translate = [&](std::uint64_t rva, bool endpoint) {
        if (rva >= begin && (rva < end || (endpoint && rva == end))) return destination + (rva - begin);
        if (withGetter && rva >= oldGetter && rva < oldGetter + getterSize) return getter + (rva - oldGetter);
        return rva;
    };
    const auto fields = completeFixups(layout);
    std::uint64_t changedFixups = 0;
    for (const auto& field : fields) {
        const auto actualField = translate(field.fieldRva, false), actualEnd = translate(field.originEndRva, true), actualTarget = translate(field.targetRva, true);
        if (field.fieldRva >= begin && field.fieldRva < end)
            require(field.kind != compat::FixupKind::Absolute64, "fixture requires explicit PE DIR64 retargeting for absolute publisher operands");
        if (actualField != field.fieldRva || actualEnd != field.originEndRva || actualTarget != field.targetRva) {
            writeFixtureFixup(moved, header, field, actualField, actualEnd, actualTarget); ++changedFixups;
        }
    }
    std::fill_n(moved.begin() + static_cast<std::ptrdiff_t>(header.rawOffset(moved, begin, body.size())), body.size(), static_cast<unsigned char>(0xcc));
    sortFixturePdata(moved, header);
    const auto originalSha = digest(original), movedSha = digest(moved);
    publisherProofs.push_back({sample, scenario, originalSha, movedSha, begin, end, destination, second, getter, codeParts, pdataParts, changedFixups});
    writeFile(output / (sample + "-" + scenario + ".pe.bin"), moved);
    compat::detail::PeFile derived(moved); const bool parsed = derived.parse();
    check(name + "/derived-PE-parses", parsed); require(parsed, "derived publisher PE must parse");
    check(name + "/whole-SHA-changed", originalSha != movedSha);
    check(name + "/old-complete-publisher-erased", rvaBytes(moved, header, begin, body.size()) == Bytes(body.size(), 0xcc));
    std::uint64_t movedOwners = 0, oldOwners = 0;
    for (const auto& function : derived.metadata.functions) {
        if (function.begin >= destination && function.end <= destination + body.size()) ++movedOwners;
        if (function.begin < end && function.end > begin) ++oldOwners;
    }
    check(name + "/all-pdata-owners-moved", movedOwners == pdataParts && oldOwners == 0, "new=" + std::to_string(movedOwners) + "; old=" + std::to_string(oldOwners));
    const auto q = observeFile(sample, scenario + "-positive", moved);
    check(name + "/actual-qualifyFile-dynamic", q.layout == layout && q.dynamicallyLocated && bool(q.graph),
        std::string(q.reason) + "; graph=" + q.record.reason, q.record.failedRva);
    check(name + "/resolved-publisher-is-new-RVA", q.layout && compat::resolvedRva(q, begin) == destination,
        q.record.reason, q.layout ? compat::resolvedRva(q, begin) : q.record.failedRva);
    check(name + "/resolved-getter-is-actual-RVA", q.layout && compat::resolvedRva(q, oldGetter) == getter,
        q.record.reason, q.layout ? compat::resolvedRva(q, oldGetter) : q.record.failedRva);
    // Do not turn unsupported product behavior into a passing fixture check.
    // Negatives still run after a positive admission failure.
    if (q.layout && q.graph) publisherLoaded(name, moved, body, q, begin, destination, getter);
    const auto reject = [&](const std::string& label, const auto& edit) {
        auto bytes = moved; edit(bytes);
        compat::detail::PeFile parser(bytes); const bool parsed = parser.parse();
        const auto bad = observeFile(sample, scenario + "/" + label, bytes);
        check(name + "/negative/" + label, parsed && !bad.layout,
            std::string(parsed ? "PE-valid; " : "PE-INVALID; ") + bad.reason + "; graph=" + bad.record.reason, bad.record.failedRva);
    };
    reject("changed-publisher-prologue", [&](Bytes& b) { b[header.rawOffset(b, destination, 1)] ^= 1; });
    for (const auto desired : {body.size() / 2, body.size() - 1}) {
        // The last four bytes are a real rel32 operand. Walk back to a literal
        // instruction byte so this negative changes code semantics, not placement.
        auto offset = desired;
        while (std::any_of(fields.begin(), fields.end(), [&](const auto& f) { return begin + offset >= f.fieldRva && begin + offset - f.fieldRva < compat::fixupSize(f.kind); })) {
            require(offset > 0, "publisher negative has a literal instruction byte"); --offset;
        }
        reject("changed-publisher-literal-" + std::to_string(offset), [&](Bytes& b) { b[header.rawOffset(b, destination + offset, 1)] ^= 1; });
    }
    reject("no-required-publisher-body", [&](Bytes& b) { std::fill_n(b.begin() + static_cast<std::ptrdiff_t>(header.rawOffset(b, destination, body.size())), body.size(), static_cast<unsigned char>(0xcc)); });
    reject("publisher-in-nonexecutable-section", [&](Bytes& b) {
        auto section = at<IMAGE_SECTION_HEADER>(b, header.sections + (header.fileHeader.NumberOfSections - 1) * sizeof(IMAGE_SECTION_HEADER));
        section.Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
        put(b, header.sections + (header.fileHeader.NumberOfSections - 1) * sizeof(IMAGE_SECTION_HEADER), section);
    });
    const auto caller = std::find_if(fields.begin(), fields.end(), [&](const auto& f) { return f.targetRva == begin && f.kind == compat::FixupKind::Relative32 && (f.fieldRva < begin || f.fieldRva >= end); });
    require(caller != fields.end(), "real incoming direct publisher caller");
    reject("caller-still-targets-erased-old-publisher", [&](Bytes& b) { writeFixtureFixup(b, header, *caller, caller->fieldRva, caller->originEndRva, begin); });
    reject("caller-targets-publisher-interior", [&](Bytes& b) { writeFixtureFixup(b, header, *caller, caller->fieldRva, caller->originEndRva, destination + 1); });
    const auto outgoing = std::find_if(fields.begin(), fields.end(), [&](const auto& f) { return f.fieldRva >= begin && f.fieldRva < end && f.kind == compat::FixupKind::Relative32 && (f.targetRva < begin || f.targetRva > end); });
    require(outgoing != fields.end(), "real outgoing RIP/call publisher operand");
    reject("outgoing-displacement-not-rebased", [&](Bytes& b) {
        const auto oldRaw = originalHeader.rawOffset(original, outgoing->fieldRva, 4), newRaw = header.rawOffset(b, destination + outgoing->fieldRva - begin, 4);
        std::copy_n(original.begin() + static_cast<std::ptrdiff_t>(oldRaw), 4, b.begin() + static_cast<std::ptrdiff_t>(newRaw));
    });
    for (const auto kind : {compat::FixupKind::Relative8, compat::FixupKind::Relative32}) {
        const auto internal = std::find_if(fields.begin(), fields.end(), [&](const auto& f) {
            return f.kind == kind && f.fieldRva >= begin && f.fieldRva < end && f.targetRva >= begin && f.targetRva < end;
        });
        require(internal != fields.end(), "real internal rel8/rel32 publisher branch");
        reject(kind == compat::FixupKind::Relative8 ? "internal-rel8-target-mismatch" : "internal-rel32-target-mismatch", [&](Bytes& b) {
            writeFixtureFixup(b, header, *internal, destination + internal->fieldRva - begin,
                destination + internal->originEndRva - begin, destination + internal->targetRva - begin + 1);
        });
    }
    reject("pdata-still-owns-erased-original", [&](Bytes& b) {
        const auto directory = originalHeader.opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        const auto raw = header.rawOffset(b, directory.VirtualAddress, directory.Size);
        std::copy_n(original.begin() + static_cast<std::ptrdiff_t>(originalHeader.rawOffset(original, directory.VirtualAddress, directory.Size)), directory.Size, b.begin() + static_cast<std::ptrdiff_t>(raw));
    });
    const auto chain = std::find_if(fields.begin(), fields.end(), [&](const auto& f) {
        if (f.kind != compat::FixupKind::Rva32 || f.targetRva != begin) return false;
        return std::any_of(spec.ranges.begin(), spec.ranges.end(), [&](const auto& r) { return r.kind == compat::RangeKind::Unwind && f.fieldRva >= r.refRva && f.fieldRva - r.refRva < r.size; });
    });
    require(chain != fields.end(), "real chained unwind reference to publisher");
    reject("unwind-chain-still-targets-old-publisher", [&](Bytes& b) { writeFixtureFixup(b, header, *chain, chain->fieldRva, 0, begin); });
    auto duplicate = moved;
    std::copy(body.begin(), body.end(), duplicate.begin() + static_cast<std::ptrdiff_t>(header.rawOffset(duplicate, second, body.size())));
    for (const auto& field : fields) if (field.fieldRva >= begin && field.fieldRva < end) {
        const auto target = field.targetRva >= begin && field.targetRva <= end ? second + field.targetRva - begin : translate(field.targetRva, true);
        writeFixtureFixup(duplicate, header, field, second + field.fieldRva - begin, second + field.originEndRva - begin, target);
    }
    const auto routed = observeFile(sample, scenario + "/duplicate-body-unique-real-route", duplicate);
    check(name + "/duplicate-body-caller-and-pdata-select-first", routed.layout == layout && routed.graph && compat::resolvedRva(routed, begin) == destination,
        routed.record.reason, routed.record.failedRva);
    writeFixtureFixup(duplicate, header, *caller, caller->fieldRva, caller->originEndRva, second);
    const auto conflict = observeFile(sample, scenario + "/duplicate-body-contradictory-caller-pdata", duplicate);
    check(name + "/negative/duplicate-body-conflicting-routes", !conflict.layout, std::string(conflict.reason) + "; graph=" + conflict.record.reason, conflict.record.failedRva);
    if (withGetter) reject("getter-route-still-targets-erased-original", [&](Bytes& b) {
        const auto slot = layout == compat::Layout::Legacy ? 0x136fc20ULL : 0x13701d0ULL;
        put<std::uint64_t>(b, header.rawOffset(b, slot, 8), header.opt.ImageBase + oldGetter);
    });
}

} // namespace
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) { std::cerr << "usage: movement OUT LEGACY_CLIENT PUBLIC_CLIENT\n"; return 2; }
    try {
        output = fs::absolute(argv[1]); fs::create_directories(output);
        runMovement("legacy-client", argv[2], compat::Layout::Legacy);
        runMovement("public-client", argv[3], compat::Layout::Public);
        for (const bool withGetter : {false, true}) {
            for (const auto layout : {compat::Layout::Legacy, compat::Layout::Public}) {
                const bool newer = layout == compat::Layout::Public;
                try { runPublisherMovement(newer ? "public-client" : "legacy-client", argv[newer ? 3 : 2], layout, withGetter); }
                catch (const std::exception& error) { check(std::string(newer ? "public-client" : "legacy-client") + (withGetter ? "/getter-and-publisher/fixture-fatal" : "/publisher-only/fixture-fatal"), false, error.what()); }
            }
        }
    } catch (const std::exception& error) { check("movement-fixture-fatal", false, error.what()); }
    const auto failed = std::count_if(cases.begin(), cases.end(), [](const auto& c) { return !c.pass; });
    std::cout << "{\n\"pass\":" << (failed ? "false" : "true") << ",\n\"checks\":" << cases.size() << ",\n\"failed\":" << failed
        << ",\n\"scope\":\"actual derived PE getter and complete publisher RVA movement; frozen product consumers; no binary execution or target access\","
        << "\n\"executableBinaryQualification\":false,\n\"finalCurrentSourceRelease\":false,\n\"movements\":[";
    bool first = true;
    for (const auto& p : movementProofs) {
        if (!first) std::cout << ','; first = false;
        std::cout << "\n{\"sample\":" << quoted(p.name) << ",\"originalSHA256\":" << quoted(p.originalSha) << ",\"movedSHA256\":" << quoted(p.movedSha)
            << ",\"oldGetterRva\":" << oldGetter << ",\"newGetterRva\":" << p.cave << ",\"secondCaveRva\":" << p.secondCave
            << ",\"vtableSlotRva\":" << p.slot << ",\"allQualifiedReferencesUpdated\":" << p.referenceCount << ",\"getterBytes\":" << quoted(p.originalGetter)
            << ",\"caveBefore\":" << quoted(p.caveBefore) << ",\"getterHasNoPdata\":" << (p.noPdata ? "true" : "false") << '}';
    }
    std::cout << "\n],\n\"publisherMovements\":["; first = true;
    for (const auto& p : publisherProofs) {
        if (!first) std::cout << ','; first = false;
        std::cout << "\n{\"sample\":" << quoted(p.sample) << ",\"scenario\":" << quoted(p.scenario)
            << ",\"originalSHA256\":" << quoted(p.originalSha) << ",\"movedSHA256\":" << quoted(p.movedSha)
            << ",\"oldPublisherRva\":" << p.begin << ",\"oldPublisherEndRva\":" << p.end
            << ",\"newPublisherRva\":" << p.destination << ",\"secondPublisherRva\":" << p.second
            << ",\"resolvedGetterRva\":" << p.getter << ",\"completeCodeParts\":" << p.codeParts
            << ",\"pdataParts\":" << p.pdataParts << ",\"changedFixups\":" << p.changedFixups << '}';
    }
    std::cout << "\n],\n\"observations\":["; first = true;
    for (const auto& o : observations) {
        if (!first) std::cout << ','; first = false;
        std::cout << "\n{\"sample\":" << quoted(o.sample) << ",\"scenario\":" << quoted(o.scenario) << ",\"qualified\":" << (o.qualified ? "true" : "false")
            << ",\"dynamicallyLocated\":" << (o.dynamicallyLocated ? "true" : "false") << ",\"resolvedGetterRva\":" << o.resolved
            << ",\"reason\":" << quoted(o.reason) << ",\"recordReason\":" << quoted(o.recordReason) << ",\"publisherReason\":" << quoted(o.publisherReason) << '}';
    }
    std::cout << "\n],\n\"cases\":["; first = true;
    for (const auto& c : cases) {
        if (!first) std::cout << ','; first = false;
        std::cout << "\n{\"name\":" << quoted(c.name) << ",\"pass\":" << (c.pass ? "true" : "false") << ",\"observed\":" << quoted(c.observed)
            << ",\"rva\":" << c.rva << ",\"mutableRangesIgnored\":" << c.mutableIgnored << '}';
    }
    std::cout << "\n]}\n";
    return failed ? 1 : 0;
}