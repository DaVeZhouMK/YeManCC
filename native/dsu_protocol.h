#pragma once
#include "dsu_frame.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

// Fixed local HC DSU/MessageType.cs, DSUServer.BeginPacket/ReportToBuffer/Tick.
namespace ymcc::dsu::protocol {
inline constexpr std::uint16_t version=1001;
inline constexpr std::uint32_t versionMessage=0x100000,portsMessage=0x100001,dataMessage=0x100002;
inline constexpr std::size_t dataBytes=100;
inline std::uint16_t u16(const std::uint8_t* p) noexcept{return std::uint16_t(p[0]|p[1]<<8);}
inline std::uint32_t u32(const std::uint8_t* p) noexcept{return std::uint32_t(p[0])|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;}
inline void put16(std::uint8_t* p,std::uint16_t v) noexcept{p[0]=std::uint8_t(v);p[1]=std::uint8_t(v>>8);}
inline void put32(std::uint8_t* p,std::uint32_t v) noexcept{for(unsigned i=0;i<4;++i)p[i]=std::uint8_t(v>>(i*8));}
inline void put64(std::uint8_t* p,std::uint64_t v) noexcept{for(unsigned i=0;i<8;++i)p[i]=std::uint8_t(v>>(i*8));}
inline void putFloat(std::uint8_t* p,float v) noexcept{static_assert(sizeof(v)==4);std::uint32_t bits=0;std::memcpy(&bits,&v,4);put32(p,bits);}
constexpr auto table() noexcept {
    std::array<std::uint32_t,256> t{};
    for(unsigned i=0;i<256;++i){auto c=std::uint32_t(i);for(unsigned b=0;b<8;++b)c=(c&1)?(c>>1)^0xedb88320u:c>>1;t[i]=c;}
    return t;
}
inline constexpr auto crcTable=table();
inline std::uint32_t crc(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t c=0xffffffffu;
    for(std::size_t i=0;i<bytes.size();++i){const auto value=i>=8&&i<12?0:bytes[i];c=crcTable[(c^value)&255]^(c>>8);}
    return c^0xffffffffu;
}
inline void header(std::span<std::uint8_t> out,std::uint32_t server,std::uint32_t message) noexcept {
    std::fill(out.begin(),out.end(),0);out[0]='D';out[1]='S';out[2]='U';out[3]='S';
    put16(out.data()+4,version);put16(out.data()+6,std::uint16_t(out.size()-16));put32(out.data()+12,server);put32(out.data()+16,message);
}
inline void finish(std::span<std::uint8_t> out) noexcept{put32(out.data()+8,crc(out));}
inline void metadata(std::uint8_t* out,unsigned slot,bool active=true) noexcept {
    out[20]=std::uint8_t(slot);out[21]=2;out[22]=2;out[23]=1;
    std::fill(out+24,out+30,std::uint8_t(0x10+slot));out[30]=5;out[31]=active?1:0;
}
// HC uses MathF.Round (ties to even); inputs here are the actual signed XInput axes.
inline std::uint8_t axis(std::int16_t raw) noexcept {
    const float value=(float(raw)+32768.0f)*(1.0f/65535.0f)*255.0f;
    const float lower=std::floor(value),fraction=value-lower;
    return std::uint8_t(lower+(fraction>0.5f||(fraction==0.5f&&int(lower)%2)?1:0));
}
inline std::size_t encodeData(std::span<std::uint8_t> storage,unsigned slot,std::uint32_t server,
    std::uint32_t sequence,const Frame& f,Admission state) noexcept {
    if(storage.size()<dataBytes||slot>=3)return 0;
    auto out=storage.first(dataBytes);header(out,server,dataMessage);metadata(out.data(),slot);
    put32(out.data()+32,sequence);
    const bool pad=state.pad&&!f.semanticNeutral;
    const auto b=pad?f.buttons:std::uint16_t(0);
    out[36]=std::uint8_t(((b&4)?128:0)|((b&2)?64:0)|((b&8)?32:0)|((b&1)?16:0)|((b&16)?8:0)|((b&128)?4:0)|((b&64)?2:0)|((b&32)?1:0));
    out[37]=std::uint8_t(((b&0x1000)?64:0)|((b&0x2000)?32:0)|((b&0x4000)?128:0)|((b&0x8000)?16:0)|((b&512)?8:0)|((b&256)?4:0)|((pad&&f.rt==255)?2:0)|((pad&&f.lt==255)?1:0));
    out[38]=(b&0x400)?1:0;
    out[40]=axis(pad?f.lx:0);out[41]=std::uint8_t(255-axis(pad?f.ly:0));
    out[42]=axis(pad?f.rx:0);out[43]=std::uint8_t(255-axis(pad?f.ry:0));
    const std::array<std::uint16_t,10> masks={4,2,8,1,0x1000,0x2000,0x4000,0x8000,512,256};
    for(unsigned i=0;i<10;++i)out[44+i]=(b&masks[i])?255:0;
    out[54]=pad?f.rt:0;out[55]=pad?f.lt:0;
    // 56..67 are two unavailable touch points, each six bytes, already zeroed.
    put64(out.data()+68,f.timestampMs*1000u); // HC TimerManager.GetElapsedSeconds: elapsed ms * 1000.
    const bool motion=state.motion&&!f.semanticNeutral&&slot<2;
    const auto a=motion?(slot==0?f.transformed:f.raw):Axes{};
    putFloat(out.data()+76,-a.ax);putFloat(out.data()+80,-a.ay);putFloat(out.data()+84,-a.az);
    putFloat(out.data()+88,a.gx);putFloat(out.data()+92,-a.gy);putFloat(out.data()+96,-a.gz);
    finish(out);return dataBytes;
}
inline std::size_t encodeVersion(std::span<std::uint8_t> storage,std::uint32_t server) noexcept {
    if(storage.size()<24)return 0;auto out=storage.first(24);header(out,server,versionMessage);put16(out.data()+20,version);finish(out);return 24;
}
inline std::size_t encodePort(std::span<std::uint8_t> storage,unsigned slot,std::uint32_t server) noexcept {
    if(storage.size()<32||slot>=4)return 0;auto out=storage.first(32);header(out,server,portsMessage);metadata(out.data(),slot,false);finish(out);return 32;
}
struct Request {
    bool valid=false;std::uint32_t client=0,message=0;std::uint8_t flags=0,id=0,count=0;
    std::array<std::uint8_t,4> ids{};std::array<std::uint8_t,6> mac{};
};
inline Request parse(std::span<const std::uint8_t> received) noexcept {
    Request r;
    if(received.size()<20||received[0]!='D'||received[1]!='S'||received[2]!='U'||received[3]!='C'||u16(received.data()+4)>version)return r;
    const auto size=std::size_t(u16(received.data()+6))+16;
    if(size<20||size>received.size())return r;
    const auto bytes=received.first(size);if(u32(bytes.data()+8)!=crc(bytes))return r;
    r.client=u32(bytes.data()+12);r.message=u32(bytes.data()+16);
    if(r.message==portsMessage){if(size<24)return {};const auto n=u32(bytes.data()+20);if(n>4||size<24+n)return {};r.count=std::uint8_t(n);
        for(unsigned i=0;i<n;++i){r.ids[i]=bytes[24+i];if(r.ids[i]>=4)return {};}}
    if(r.message==dataMessage){if(size<28)return {};r.flags=bytes[20];r.id=bytes[21];if((r.flags&~3u)||((r.flags&1u)&&r.id>=4))return {};std::copy(bytes.begin()+22,bytes.begin()+28,r.mac.begin());}
    r.valid=true;return r;
}
} // namespace ymcc::dsu::protocol
