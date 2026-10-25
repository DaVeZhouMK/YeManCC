#pragma once
#include "oem_gamepad_mode_contract.h"
#include <array>
#include <utility>
#include <string_view>

namespace ymcc::oem::gamepad::msi {
inline const PhysicalRoute xinputRoute{Vendor::Msi,InputProtocol::XInput,"msi.claw.mode1"};
inline const PhysicalRoute dinputRoute{Vendor::Msi,InputProtocol::DirectInput,"msi.claw.mode2"};
inline constexpr std::string_view aiEngineSha256=
    "B1C76661BC69AA7FA83BD7629A361B2702562EB620B68EDE8E7B3E322FC5D623";
inline constexpr std::string_view controlPageSha256=
    "70982566E29F799FA3077B65824EA25D9C4C17355DD6C6CC5F0E9F8959D9C2A0";
inline constexpr std::string_view sourceEvidence=
    "GP-MSI-官方手柄桌面自动切换API与固定模式边界-20261012;official-package-3.0.2605.2101";
struct InstalledBinding {
    // Supplied by a trusted in-process identity collector, NOT the IPC client.
    // Research-package hashes alone do not constitute an installed-file receipt.
    bool installedIdentityVerified=false;
    std::string aiEngineHash, controlPageHash, identityReceiptSha256;
    std::string deviceClass, board;
};
inline bool sha256Text(std::string_view s) {
    if(s.size()!=64) return false;
    for(char c:s) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) return false;
    return true;
}
inline bool hashEqual(std::string_view a,std::string_view b) {
    if(!sha256Text(a)||!sha256Text(b)) return false;
    for(size_t i=0;i<a.size();++i) {
        const auto upper=[](char c){return c>='a'&&c<='f'?char(c-'a'+'A'):c;};
        if(upper(a[i])!=upper(b[i])) return false;
    }
    return true;
}
inline bool knownBoard(const InstalledBinding& b) {
    // Exact normalized board IDs; no family-name substring or CPU inference.
    const std::array<std::pair<std::string_view,std::string_view>,5> ids{{
        {"HandheldCompanion.Devices.ClawA1M","MS-1T41"},
        {"HandheldCompanion.Devices.ClawA2VM","MS-1T42"},
        {"HandheldCompanion.Devices.ClawA2VM","MS-1T52"},
        {"HandheldCompanion.Devices.ClawCG3EM","MS-1T91"},
        {"HandheldCompanion.Devices.ClawBZ2EM","MS-1T8K"}}};
    for(const auto& id:ids) if(b.deviceClass==id.first&&b.board==id.second) return true;
    return false;
}
inline Value regSz(std::u16string_view s) {
    Value v;v.exists=true;v.type=1; // Windows REG_SZ, explicit UTF-16LE including NUL.
    for(char16_t c:s) {v.bytes.push_back(uint8_t(c&0xff));v.bytes.push_back(uint8_t(c>>8));}
    v.bytes.push_back(0);v.bytes.push_back(0);return v;
}
inline bool boundedOriginal(const Value& v) {
    if(!v.exists) return v.type==0&&v.bytes.empty();
    if(v.type!=1 || v.bytes.size()<2 || v.bytes.size()>1024 || v.bytes.size()%2 ||
       v.bytes[v.bytes.size()-2] || v.bytes.back()) return false;
    // Reject interior NULs/multi-string ambiguity. Preserve otherwise exact bytes.
    for(size_t i=0;i+2<v.bytes.size();i+=2) if(v.bytes[i]==0&&v.bytes[i+1]==0) return false;
    return true;
}
class FrontendAdapter final : public Adapter {
    InstalledBinding binding_;
public:
    explicit FrontendAdapter(InstalledBinding b):binding_(std::move(b)){}
    Capability capability() const override {
        Capability c;c.vendor=Vendor::Msi;c.evidence=std::string(sourceEvidence);
        if(!binding_.installedIdentityVerified || !knownBoard(binding_) ||
           !hashEqual(binding_.aiEngineHash,aiEngineSha256) ||
           !hashEqual(binding_.controlPageHash,controlPageSha256) ||
           !sha256Text(binding_.identityReceiptSha256)) return c;
        c.policyCapabilityVerified=true;c.firmwareModeCapabilityVerified=true;c.captureCapabilityVerified=true;c.nativeRoute=msi::xinputRoute;c.virtualRoute=msi::dinputRoute;
        c.binding=binding_.deviceClass+"|"+binding_.board+"|"+binding_.identityReceiptSha256;
        return c;
    }
    std::vector<Patch> patches(Intent) const override {
        if(!capability().policyCapabilityVerified) return {};
        // Explicit 32-bit view + native relative path. NEVER add WOW6432Node here.
        // Do not touch the User Scenario temporary ControlModeUserSet value.
        return {
            {{RegistryView::View32,L"SOFTWARE\\MSI\\MSI Center M\\Component\\User Scenario",L"AutoSwitchMode"},regSz(u"0")},
            {{RegistryView::View32,L"SOFTWARE\\MSI\\MSI Center M\\OsdEditor",L"ControlModeUserSet"},regSz(u"XInput")}
        };
    }
    bool validRecord(const Record& r) const override {
        const auto c=capability();
        if(!c.policyCapabilityVerified || r.schema!=contractVersion || r.vendor!=Vendor::Msi ||
           !validOwner(r.owner) || r.binding!=c.binding ||
           (r.intent!=Intent::NativeGamepad && r.intent!=Intent::VirtualCapture)) return false;
        const auto expected=patches(r.intent);
        if(r.entries.size()!=expected.size()) return false;
        for(size_t i=0;i<expected.size();++i) {
            const auto& e=r.entries[i];
            if(!(e.patch.address==expected[i].address) || !(e.patch.desired==expected[i].desired) ||
               !boundedOriginal(e.original) || (!r.pending && e.mayHaveWritten)) return false;
        }
        return true;
    }
};
} // namespace ymcc::oem::gamepad::msi





