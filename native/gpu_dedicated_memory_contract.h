#pragma once
// Dedicated capacity only. No shared-memory setting, hardware access or fallback values.
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace ymcc::dedicatedmemory {
constexpr std::uint64_t mib = 1024ull * 1024;
struct Option {
    std::string id;
    std::uint32_t raw = 0;
    std::optional<std::uint32_t> capacityMiB;
    bool removeOverride = false;
};
struct Probe {
    bool hardware = false, integrated = false, identityConfirmed = false, capacityRead = false;
    std::string provider, pnpId, driverVersion, deviceClass, bootId, adapterName, luid, registryKey, bindingSource, classificationSource, reason;
    std::uint64_t dedicatedBytes = 0, installedBytes = 0;
    std::optional<std::uint32_t> configuredRaw;
    std::vector<Option> options;
};
inline bool sameOption(const Option& a, const Option& b) {
    return a.id == b.id && a.raw == b.raw && a.capacityMiB == b.capacityMiB && a.removeOverride == b.removeOverride;
}
inline bool validOptions(const Probe& p) {
    if (p.options.size() < 2 || p.options.size() > 64) return false;
    for (std::size_t i = 0; i < p.options.size(); ++i) {
        const auto& o = p.options[i];
        if (o.capacityMiB) {
            if (*o.capacityMiB == 0 || std::uint64_t(*o.capacityMiB) * mib > p.installedBytes / 2 ||
                o.id != "mib:" + std::to_string(*o.capacityMiB) || o.removeOverride ||
                (p.provider == "intel-gmm" && o.raw != *o.capacityMiB)) return false;
        } else if (o.id != "auto" || o.raw != 0 || o.removeOverride != (p.provider == "intel-gmm")) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (o.id == p.options[j].id || o.raw == p.options[j].raw) return false;
    }
    return true;
}
inline bool offered(const Probe& p, const Option& target) {
    return std::any_of(p.options.begin(),p.options.end(),[&](const Option& o){ return sameOption(o,target); });
}
inline bool admitted(const Probe& p) {
    return p.hardware && p.integrated && p.identityConfirmed && !p.pnpId.empty() &&
        !p.driverVersion.empty() && !p.bootId.empty() && !p.luid.empty() && !p.registryKey.empty() &&
        p.capacityRead && p.installedBytes > 0 && p.installedBytes >= p.dedicatedBytes &&
        (p.dedicatedBytes > 0 || p.provider == "intel-gmm") &&
        (p.provider == "intel-gmm" || p.provider == "rog-apu" || p.provider == "rog-vram") &&
        validOptions(p) && (p.provider == "intel-gmm" || p.configuredRaw.has_value()) &&
        std::any_of(p.options.begin(),p.options.end(),[&](const Option& o) {
            return o.removeOverride ? !p.configuredRaw : p.configuredRaw == o.raw;
        });
}
inline std::string bindingFence(const Probe& p) {
    std::string value;
    const auto append = [&](const std::string& field) { value += std::to_string(field.size()) + ":" + field; };
    for (const auto* field : {&p.pnpId,&p.driverVersion,&p.deviceClass,&p.bootId,&p.luid,&p.registryKey,&p.provider}) append(*field);
    append(p.classificationSource);
    append(std::to_string(p.installedBytes));
    append(std::to_string(p.options.size()));
    for (const auto& o : p.options) {
        append(o.id); append(std::to_string(o.raw));
        append(o.capacityMiB ? std::to_string(*o.capacityMiB) : "auto"); append(o.removeOverride ? "remove" : "set");
    }
    return value;
}
inline std::string fence(const Probe& p) {
    return bindingFence(p) + "|" + (p.capacityRead ? "read" : "unread") + "|" + std::to_string(p.dedicatedBytes) +
        "|" + (p.configuredRaw ? std::to_string(*p.configuredRaw) : "absent");
}
inline std::optional<Probe> uniquePhysicalProbe(const std::vector<Probe>& rows) {
    if (rows.empty() || !admitted(rows.front())) return std::nullopt;
    Probe result = rows.front();
    for (auto row : rows) {
        if (!admitted(row) || row.options.size() != result.options.size()) return std::nullopt;
        const auto aliasLuid = row.luid; row.luid = result.luid;
        if (fence(row) != fence(result)) return std::nullopt;
        for (std::size_t i = 0; i < row.options.size(); ++i) {
            const auto& a = row.options[i]; const auto& b = result.options[i];
            if (a.id != b.id || a.raw != b.raw || a.capacityMiB != b.capacityMiB || a.removeOverride != b.removeOverride) return std::nullopt;
        }
        // Aliases are accepted only after matching the same active PCI/PnP binding and live capacity.
        result.luid = std::min(result.luid,aliasLuid);
    }
    return result;
}
inline bool configuredAs(const Probe& p, const Option& o) {
    return o.removeOverride ? !p.configuredRaw : p.configuredRaw == o.raw;
}
inline std::vector<Option> intelOptions(std::uint64_t installedBytes) {
    std::vector<Option> result{{"auto", 0, std::nullopt, true}};
    for (const std::uint32_t size : {512u,1024u,2048u,3072u,4096u,6144u,8192u,12288u,16384u})
        if (std::uint64_t(size) * mib <= installedBytes / 2)
            result.push_back({"mib:" + std::to_string(size), size, size, false});
    return result;
}
inline std::optional<std::uint32_t> decodeRogApu(std::uint32_t response) {
    if (response < 0x10000u || response > 0x101ffu) return std::nullopt;
    const auto value = response - 0x10000u;
    if (value == 0 || value == 0x100u) return 0;
    for (const auto code : {2u,3u,4u,5u,7u,8u,9u,6u}) if (value == (0x100u | code)) return value;
    return std::nullopt;
}
inline std::vector<Option> rogApuOptions(std::uint64_t installedBytes) {
    std::vector<Option> result{{"auto", 0, std::nullopt, false}};
    constexpr std::uint32_t codes[]{2,3,4,5,7,8,9,6};
    for (std::uint32_t i = 0; i < 8; ++i) {
        const auto size = (i + 1) * 1024;
        if (std::uint64_t(size) * mib <= installedBytes / 2)
            result.push_back({"mib:" + std::to_string(size), 0x100u | codes[i], size, false});
    }
    return result;
}
inline std::optional<std::vector<Option>> rogVramOptions(const std::vector<std::uint8_t>& bytes,
                                                       std::uint64_t installedBytes,
                                                       std::uint32_t& current) {
    if (bytes.size() < 10) return std::nullopt;
    const auto u16 = [&](std::size_t i) { return std::uint32_t(bytes[i]) | (std::uint32_t(bytes[i+1]) << 8); };
    const auto u32 = [&](std::size_t i) { return u16(i) | (u16(i+2) << 16); };
    const auto status = u32(0), count = status & 0xffffu;
    if ((status & 0x10000u) == 0 || (status & ~0x3ffffu) != 0 || count < 2 ||
        count > (bytes.size() - 6) / 2) return std::nullopt;
    const auto unit = (status & 0x20000u) ? 512u : 1u;
    current = u32(4);
    std::vector<Option> result{{"auto", 0, std::nullopt, false}};
    for (std::uint32_t i = 1; i < count; ++i) {
        const auto raw = u16(6 + i * 2), size = raw * unit;
        if (!raw || std::any_of(result.begin(),result.end(),[&](const Option& o){return o.raw == raw;})) return std::nullopt;
        if (std::uint64_t(size) * mib <= installedBytes / 2)
            result.push_back({"mib:" + std::to_string(size), raw, size, false});
    }
    if (std::none_of(result.begin(),result.end(),[&](const Option& o){return o.raw == current;})) return std::nullopt;
    return result;
}
struct Journal {
    std::string pnpId, driverVersion, provider, bootId, stage;
    std::optional<std::uint32_t> originalRaw;
    std::uint64_t beforeBytes = 0;
    Option target;
};
struct JournalRead { bool ok = true; std::optional<Journal> value; };
struct Ops {
    virtual ~Ops() = default;
    virtual Probe probe() = 0;
    virtual JournalRead journal() = 0;
    virtual bool save(const Journal&) = 0;
    virtual bool write(const Probe&, const Option&) = 0;
};
inline bool capacityMatches(const Probe& p, const Option& target) {
    return p.capacityRead && target.capacityMiB && p.dedicatedBytes == std::uint64_t(*target.capacityMiB) * mib;
}
inline bool configuredCapacityMismatch(const Probe& p) {
    return p.capacityRead &&
        std::any_of(p.options.begin(),p.options.end(),[&](const Option& o) {
            return o.capacityMiB && configuredAs(p,o) && !capacityMatches(p,o);
        });
}
inline bool intelConfiguredCapacityMismatch(const Probe& p) {
    return p.provider == "intel-gmm" && configuredCapacityMismatch(p);
}
struct LoaderBootIdentity { std::string environment; std::uint32_t sequence = 0; };
inline std::optional<LoaderBootIdentity> loaderBootIdentity(const std::string& value) {
    const std::string prefix = "windows-loader-v1|";
    if (!value.starts_with(prefix) || value.size() < prefix.size() + 40) return std::nullopt;
    const auto guid = value.substr(prefix.size(),38);
    if (guid.front() != '{' || guid.back() != '}' || value[prefix.size()+38] != '|') return std::nullopt;
    for (std::size_t i = 1; i < 37; ++i) {
        const char c = guid[i];
        if (i == 9 || i == 14 || i == 19 || i == 24) { if (c != '-') return std::nullopt; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return std::nullopt;
    }
    const auto start = value.data() + prefix.size() + 39, end = value.data() + value.size();
    std::uint32_t sequence = 0; const auto parsed = std::from_chars(start,end,sequence);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return LoaderBootIdentity{guid,sequence};
}
inline std::string bootComparison(const Probe& p, const Journal& j) {
    const auto current = loaderBootIdentity(p.bootId), saved = loaderBootIdentity(j.bootId);
    // A legacy GUID alone and another OS environment cannot establish a loader restart.
    if (!current || !saved || current->environment != saved->environment) return "uncomparable";
    return current->sequence == saved->sequence ? "same_boot" : "new_boot";
}
inline std::string verification(const Probe& p, const std::optional<Journal>& j) {
    // Existing external/migrated configuration is not proof of driver adoption.
    if (!j || j->pnpId != p.pnpId || j->provider != p.provider)
        return configuredCapacityMismatch(p) ? "unconfirmed" : "none";
    if (j->driverVersion != p.driverVersion) return "driver_changed";
    if (!offered(p,j->target) || j->stage != "saved" || !configuredAs(p,j->target)) return "unconfirmed";
    const auto boot = bootComparison(p,*j);
    if (boot == "uncomparable") return "unconfirmed";
    if (boot == "same_boot") return "pending_restart";
    // Exact equality only: no invented capacity tolerance can turn uncertainty into success.
    if (!j->target.capacityMiB) return "default_read";
    if (capacityMatches(p,j->target)) return "verified";
    return "not_effective";
}
struct Result { bool ok = false; std::string reason; Probe snapshot; std::optional<Journal> journal; };
inline Result read(Ops& ops) {
    Result r; r.snapshot = ops.probe();
    if (!admitted(r.snapshot)) { r.reason = r.snapshot.reason.empty() ? "无法确认当前专用显存" : r.snapshot.reason; return r; }
    const auto saved = ops.journal();
    if (!saved.ok) { r.reason = "显存设置记录无法读取"; return r; }
    r.ok = true; r.journal = saved.value; return r;
}
inline Result applyFromRead(Ops& ops, Result r, const std::string& expectedFence, const std::string& optionId, bool confirmed) {
    if (!r.ok) return r;
    r.ok = false;
    if (!confirmed) { r.reason = "需要确认保存并在重启后验证"; return r; }
    if (fence(r.snapshot) != expectedFence) { r.reason = "显卡或当前读数已变化，请重新读取后设置"; return r; }
    const auto selected = std::find_if(r.snapshot.options.begin(),r.snapshot.options.end(),[&](const Option& o){return o.id == optionId;});
    if (selected == r.snapshot.options.end()) { r.reason = "该显存档位不可用"; return r; }
    if (configuredAs(r.snapshot,*selected)) {
        const auto state = verification(r.snapshot,r.journal);
        if (state == "not_effective") {
            r.reason = "该档位已完成重启验证但未生效，请恢复默认或检查驱动支持"; return r;
        }
        const bool reapply = intelConfiguredCapacityMismatch(r.snapshot) && state != "pending_restart";
        // Preserve no-op behavior for effective targets, Auto, firmware providers and
        // already-saved same-boot targets. A confirmed Intel discrepancy takes the full write path.
        if (!reapply) { r.ok = true; return r; }
    }
    Journal j{r.snapshot.pnpId,r.snapshot.driverVersion,r.snapshot.provider,r.snapshot.bootId,"prepared",
              r.snapshot.configuredRaw,r.snapshot.dedicatedBytes,*selected};
    if (!ops.save(j)) { r.reason = "无法保存原设置，未执行显存修改"; return r; }
    const bool acknowledged = ops.write(r.snapshot,j.target);
    const auto after = ops.probe();
    if (!acknowledged || !admitted(after) || bindingFence(after) != bindingFence(r.snapshot) ||
        !offered(after,j.target) || !configuredAs(after,j.target)) {
        r.snapshot = after; r.journal = j; r.reason = "写入结果未确认，请重新读取；尚未确认生效"; return r;
    }
    j.stage = "saved";
    r.snapshot = after; r.journal = j;
    if (!ops.save(j)) { r.journal->stage = "prepared"; r.reason = "设置已写入，但记录保存失败；尚未确认生效"; return r; }
    r.ok = true; return r;
}
inline Result apply(Ops& ops, const std::string& expectedFence, const std::string& optionId, bool confirmed) {
    return applyFromRead(ops,read(ops),expectedFence,optionId,confirmed);
}
} // namespace ymcc::dedicatedmemory
