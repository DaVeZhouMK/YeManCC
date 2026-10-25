#pragma once
// Pinned MSI API_ControlMode: ProfileInfo, FunctionKeyProfile, KeyProfile,
// KeyBasicFlag, MacroKeyProfile and ProfilePacketInfo. Pure analysis/read codec.
// Different GetM12 addresses are overlapping fields, NOT independent banks.
#include <array>
#include <cstddef>
#include <cstdint>
namespace ymcc::msi::rearProfile {
constexpr uint16_t headerBytes=58,normalKeys=16,keyBytes=8,macroBytes=161;
constexpr uint16_t m1Index=headerBytes+normalKeys*keyBytes;
constexpr uint16_t m2Index=m1Index+keyBytes+macroBytes;
constexpr uint16_t m1Flag=m1Index+1,m2Flag=m2Index+1;
static_assert(m1Index==0xba&&m1Flag==0xbb&&m2Index==0x163&&m2Flag==0x164);
inline bool modernLayoutCandidate(uint16_t fw) noexcept {
    return fw==0x166||fw==0x167||fw==0x217||fw==0x219||fw==0x308||fw==0x411;
}
struct BasicKey {uint8_t index=0,flags=0,turbo=0;std::array<uint8_t,5> mapping{};};
inline BasicKey basicKey(const uint8_t* bytes) noexcept {
    BasicKey k;k.index=bytes[0];k.flags=bytes[1];k.turbo=bytes[2];
    for(unsigned i=0;i<5;++i)k.mapping[i]=bytes[i+3];return k;
}
inline bool mapOrMacroEnabled(const BasicKey& k) noexcept {return (k.flags&4)!=0;}
inline bool matchesKeyboardCandidateFields(const BasicKey& k,bool m1) noexcept {
    // This matches bytes the failed R2 candidate requested. It does NOT prove
    // which writer made them, the original user mapping, or safe restoration.
    return k.flags==4&&k.turbo==0&&k.mapping==std::array<uint8_t,5>{uint8_t(m1?0x7a:0x7d),0xff,0xff,0xff,0xff};
}
inline std::array<uint8_t,64> readCurrentBasicKey(bool m1) noexcept {
    // Official GetReadProfileCommand uses ReadProfile=4, current selector=0.
    // Caller still needs a qualified command owner, deadline and fresh ACK.
    std::array<uint8_t,64> p{};const auto offset=m1?m1Index:m2Index;
    p[0]=0x0f;p[3]=0x3c;p[4]=4;p[5]=0;p[6]=uint8_t(offset>>8);p[7]=uint8_t(offset);p[8]=8;return p;
}
struct ReadReceipt {bool valid=false;uint8_t profileIndex=0;uint16_t offset=0;BasicKey key{};};
inline ReadReceipt decodeBasicKeyAck(const uint8_t* p,size_t size,bool m1) noexcept {
    ReadReceipt r;if(!p||size!=64||p[0]!=0x0f||p[1]||p[2]||p[3]!=0x3c||p[4]!=5||p[8]!=8)return r;
    const auto offset=uint16_t((uint16_t(p[6])<<8)|p[7]);if(offset!=(m1?m1Index:m2Index))return r;
    r.valid=true;r.profileIndex=p[5];r.offset=offset;r.key=basicKey(p+9);return r;
}
} // namespace ymcc::msi::rearProfile
