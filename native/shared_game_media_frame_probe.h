#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace ymcc::media::frameProbe {
inline constexpr int cadenceSchema=6;
struct Cadence {
    uint32_t timescale = 0;
    uint32_t samples = 0;
    uint64_t ticks = 0;
    uint32_t shortSamples = 0;
    bool fastWindow = false;

    explicit operator bool() const { return samples >= 2 && timescale && ticks; }
    double averageFps() const { return *this ? double(samples) * timescale / double(ticks) : 0; }
    // The final sample's duration belongs to the movie span, not to the
    // presentation cadence: there is no subsequent frame at its end.
    bool exceeds30() const { return *this && fastWindow; }
};

namespace detail {
constexpr uint32_t four(char a, char b, char c, char d) {
    return (uint32_t(a)<<24) | (uint32_t(b)<<16) | (uint32_t(c)<<8) | uint32_t(d);
}
constexpr uint32_t sampleLimit = 180000;

struct Box {
    uint64_t data = 0, end = 0;
    uint32_t type = 0;
    explicit operator bool() const { return type != 0; }
};

struct EbmlElement { uint64_t data=0,end=0,id=0; bool unknown=false; };

class File {
    std::ifstream input_;
    std::array<unsigned char, 4096> page_{};
    uint64_t size_ = 0, pageBegin_ = 0;
    size_t pageSize_ = 0;
    uint32_t boxes_ = 0;

    unsigned byte(uint64_t pos) {
        if (pos >= size_) throw std::runtime_error("mp4-timing-truncated");
        if (pos < pageBegin_ || pos-pageBegin_ >= pageSize_) {
            pageBegin_ = pos;
            pageSize_ = static_cast<size_t>(size_-pos < page_.size() ? size_-pos : page_.size());
            input_.seekg(static_cast<std::streamoff>(pos));
            input_.read(reinterpret_cast<char*>(page_.data()),static_cast<std::streamsize>(pageSize_));
            if (!input_) throw std::runtime_error("mp4-timing-read-failed");
        }
        return page_[static_cast<size_t>(pos-pageBegin_)];
    }
public:
    explicit File(const std::filesystem::path& path):input_(path,std::ios::binary) {
        input_.seekg(0,std::ios::end);
        auto end=input_.tellg();
        if(end<8)throw std::runtime_error("mp4-timing-unavailable");
        size_=static_cast<uint64_t>(end);
    }
    uint64_t size() const { return size_; }
    uint32_t word(uint64_t pos,uint64_t end) {
        if(pos>end || end-pos<4)throw std::runtime_error("mp4-timing-truncated");
        return (byte(pos)<<24)|(byte(pos+1)<<16)|(byte(pos+2)<<8)|byte(pos+3);
    }
    Box box(uint64_t pos,uint64_t limit) {
        if(++boxes_>32768)throw std::runtime_error("mp4-timing-box-limit");
        auto len=uint64_t(word(pos,limit));
        const auto type=word(pos+4,limit);
        uint64_t header=8;
        if(len==1){len=(uint64_t(word(pos+8,limit))<<32)|word(pos+12,limit);header=16;}
        else if(!len)len=limit-pos;
        if(len<header || len>limit-pos)throw std::runtime_error("mp4-timing-box-invalid");
        return {pos+header,pos+len,type};
    }
    Box child(Box parent,uint32_t type) {
        Box found;
        for(auto pos=parent.data;pos<parent.end;) {
            const auto b=box(pos,parent.end);
            pos=b.end;
            if(b.type!=type)continue;
            if(found)throw std::runtime_error("mp4-timing-duplicate");
            found=b;
        }
        return found;
    }
    uint32_t field(Box b,uint64_t offset) { return word(b.data+offset,b.end); }
    EbmlElement ebml(uint64_t pos,uint64_t end) {
        if(++boxes_>1024)throw std::runtime_error("webm-timing-element-limit");
        const auto read=[&](bool id){
            if(pos>=end)throw std::runtime_error("webm-timing-truncated");
            const auto first=byte(pos++);
            unsigned flag=0x80,width=1;
            while(flag&&!(first&flag)){flag>>=1;++width;}
            if(!flag||width>(id?4u:8u)||width-1>end-pos)throw std::runtime_error("webm-timing-invalid-element");
            uint64_t value=id?first:first&(flag-1);
            for(unsigned i=1;i<width;++i)value=(value<<8)|byte(pos++);
            return std::pair{value,!id&&value==((uint64_t{1}<<(7*width))-1)};
        };
        const auto id=read(true).first;
        const auto [length,unknown]=read(false);
        if(!unknown&&length>end-pos)throw std::runtime_error("webm-timing-truncated");
        return {pos,unknown?end:pos+length,id,unknown};
    }
    template<class Visit> void ebmlChildren(EbmlElement parent,const Visit& visit,bool beforeClusters=false) {
        for(auto pos=parent.data;pos<parent.end;){
            const auto child=ebml(pos,parent.end);
            if(beforeClusters&&child.id==0x1f43b675)break;
            if(child.unknown)throw std::runtime_error("webm-timing-element-size-unknown");
            if(!visit(child))break;
            pos=child.end;
        }
    }
    uint64_t ebmlUint(EbmlElement b) {
        if(b.data==b.end||b.end-b.data>8)throw std::runtime_error("webm-timing-invalid-uint");
        uint64_t value=0;
        for(auto pos=b.data;pos<b.end;++pos)value=(value<<8)|byte(pos);
        return value;
    }
    bool ebmlString(EbmlElement b,std::string_view value) {
        if(b.end-b.data!=value.size())return false;
        for(size_t i=0;i<value.size();++i)if(byte(b.data+i)!=static_cast<unsigned char>(value[i]))return false;
        return true;
    }
};

inline void add(Cadence& c,uint32_t count,uint32_t duration) {
    if(!count || !duration || count>sampleLimit-c.samples)
        throw std::runtime_error("mp4-timing-sample-limit");
    c.samples+=count;
    c.ticks+=uint64_t(count)*duration;
}

struct PresentationWindow {
    int64_t deficit = 0;

    void append(Cadence& c,uint32_t count,int64_t gap) {
        // Largest positive suffix of (expected - actual), scaled by 30.
        // Permit one timestamp tick per window, not one tick per frame.
        const auto step=int64_t(c.timescale)-gap*30;
        if(step>30)c.shortSamples+=count;
        deficit=std::max<int64_t>(0,deficit+int64_t(count)*step);
        c.fastWindow|=deficit>30;
    }
};

inline uint32_t trackId(File& file,Box b) {
    const auto version=file.field(b,0)>>24;
    if(version>1)throw std::runtime_error("mp4-timing-version");
    return file.field(b,version==1?20:12);
}

enum class EditTimeline { invalid, unit, openEnded };

inline EditTimeline unitRateEdit(File& file,Box track) {
    const auto edts=file.child(track,four('e','d','t','s'));
    if(!edts)return EditTimeline::unit;
    const auto elst=file.child(edts,four('e','l','s','t'));
    if(!elst)return EditTimeline::invalid;
    const auto flags=file.field(elst,0),version=flags>>24,count=file.field(elst,4);
    const auto width=version?20u:12u;
    if(version>1 || (flags&0xffffffu) || count<1 || count>2 || elst.end-elst.data!=8+count*width)
        return EditTimeline::invalid;
    // A single unit-rate span and optional leading empty edit cannot speed up
    // the source cadence. For trims we conservatively inspect all samples.
    for(uint32_t i=0;i<count;++i) {
        const auto at=8+i*width;
        const auto duration=version?(uint64_t(file.field(elst,at))<<32)|file.field(elst,at+4):file.field(elst,at);
        const auto timeHi=file.field(elst,at+(version?8u:4u));
        const bool empty=timeHi==0xffffffffu&&(!version||file.field(elst,at+12)==0xffffffffu);
        if(file.field(elst,at+width-4)!=0x10000u)return EditTimeline::invalid;
        if(count==2&&!i){if(!empty||!duration)return EditTimeline::invalid;}
        else {
            if(timeHi&0x80000000u)return EditTimeline::invalid;
            // A terminal zero-duration edit is supported only for the bounded
            // contiguous fragment timeline checked below.
            if(!duration)return EditTimeline::openEnded;
        }
    }
    return EditTimeline::unit;
}

struct DisplaySample { int64_t time; uint32_t duration; };

inline int64_t sampleOffset(uint32_t version,uint32_t bits) {
    return version==1&&bits>=0x80000000u?int64_t(bits)-0x100000000ll:int64_t(bits);
}

inline void displayCadence(Cadence& c,std::vector<DisplaySample>& frames) {
    if(frames.empty())return;
    const auto earlier=[](const auto& a,const auto& b){return a.time<b.time;};
    if(!std::is_sorted(frames.begin(),frames.end(),earlier))std::sort(frames.begin(),frames.end(),earlier);
    c.shortSamples=0;
    c.fastWindow=false;
    PresentationWindow window;
    for(size_t i=1;i<frames.size();++i){
        const auto delta=frames[i].time-frames[i-1].time;
        if(delta<=0)throw std::runtime_error("mp4-timing-display-overlap");
        window.append(c,1,delta);
    }
    c.ticks=uint64_t(frames.back().time-frames.front().time)+frames.back().duration;
}

inline void sampleTable(File& file,Box media,Cadence& c) {
    const auto stbl=file.child(file.child(media,four('m','i','n','f')),four('s','t','b','l'));
    const auto stts=file.child(stbl,four('s','t','t','s'));
    auto stsz=file.child(stbl,four('s','t','s','z'));
    const auto stz2=file.child(stbl,four('s','t','z','2'));
    if(!stts || bool(stsz)==bool(stz2))throw std::runtime_error("mp4-timing-no-sample-table");
    if(!stsz)stsz=stz2;
    const auto count=file.field(stts,4);
    if(file.field(stts,0)!=0 || count>sampleLimit || stts.end-stts.data!=8+uint64_t(count)*8)
        throw std::runtime_error("mp4-timing-stts-invalid");
    PresentationWindow window;
    for(uint32_t i=0;i<count;++i) {
        const auto at=8+uint64_t(i)*8;
        const auto run=file.field(stts,at),duration=file.field(stts,at+4);
        add(c,run,duration);
        // The final duration extends the file but does not present another frame.
        window.append(c,run-(i+1==count?1u:0u),duration);
    }
    if(c.samples!=file.field(stsz,8))throw std::runtime_error("mp4-timing-stsz-mismatch");
    const auto ctts=file.child(stbl,four('c','t','t','s'));
    if(!ctts)return;
    const auto flags=file.field(ctts,0),version=flags>>24,offsetRuns=file.field(ctts,4);
    if(version>1 || (flags&0xffffffu) || offsetRuns>c.samples || ctts.end-ctts.data!=8+uint64_t(offsetRuns)*8)
        throw std::runtime_error("mp4-timing-ctts-invalid");
    uint32_t covered=0,firstOffset=0;
    bool varying=false;
    for(uint32_t i=0;i<offsetRuns;++i){
        const auto at=8+uint64_t(i)*8;
        const auto run=file.field(ctts,at),offset=file.field(ctts,at+4);
        if(!run||run>c.samples-covered)throw std::runtime_error("mp4-timing-ctts-coverage");
        covered+=run;
        if(!i)firstOffset=offset;
        else varying|=offset!=firstOffset;
    }
    if(covered!=c.samples)throw std::runtime_error("mp4-timing-ctts-coverage");
    if(!varying)return;

    // Only variable composition offsets need per-frame storage. Read each
    // table in order, avoiding page churn between stts and ctts.
    std::vector<DisplaySample> frames;
    frames.reserve(c.samples);
    int64_t time=0;
    for(uint32_t i=0;i<count;++i){
        const auto at=8+uint64_t(i)*8;
        const auto run=file.field(stts,at),duration=file.field(stts,at+4);
        for(uint32_t j=0;j<run;++j){frames.push_back({time,duration});time+=duration;}
    }
    auto next=frames.begin();
    for(uint32_t i=0;i<offsetRuns;++i){
        const auto at=8+uint64_t(i)*8;
        const auto run=file.field(ctts,at),bits=file.field(ctts,at+4);
        const auto offset=sampleOffset(version,bits);
        for(uint32_t j=0;j<run;++j,++next)next->time+=offset;
    }
    displayCadence(c,frames);
}

inline uint64_t decodeBase(File& file,Box tfdt) {
    if(!tfdt)throw std::runtime_error("mp4-timing-no-tfdt");
    const auto flags=file.field(tfdt,0),version=flags>>24;
    if(version>1 || (flags&0xffffffu) || tfdt.end-tfdt.data!=(version?12u:8u))
        throw std::runtime_error("mp4-timing-tfdt-invalid");
    const auto value=file.field(tfdt,4);
    return version?(uint64_t(value)<<32)|file.field(tfdt,8):value;
}

struct FragmentTimeline {
    uint64_t firstBase=0;
    std::vector<DisplaySample> frames;
};

inline void fragment(File& file,Box moof,uint32_t video,uint32_t trex,Cadence& c,FragmentTimeline& timeline) {
    for(auto pos=moof.data;pos<moof.end;) {
        const auto traf=file.box(pos,moof.end);
        pos=traf.end;
        if(traf.type!=four('t','r','a','f'))continue;
        const auto tfhd=file.child(traf,four('t','f','h','d'));
        if(!tfhd)throw std::runtime_error("mp4-timing-no-tfhd");
        if(file.field(tfhd,4)!=video)continue;
        const auto flags=file.field(tfhd,0);
        if(flags>>24 || flags&~0x03003bu)throw std::runtime_error("mp4-timing-tfhd-flags");
        auto duration=trex;
        const auto at=8u+(flags&1?8u:0u)+(flags&2?4u:0u);
        if(tfhd.end-tfhd.data!=at+(flags&8?4u:0u)+(flags&16?4u:0u)+(flags&32?4u:0u))
            throw std::runtime_error("mp4-timing-tfhd-invalid");
        if(flags&8)duration=file.field(tfhd,at);
        const auto base=decodeBase(file,file.child(traf,four('t','f','d','t')));
        if(!c.samples)timeline.firstBase=base;
        if(base<timeline.firstBase||base-timeline.firstBase!=c.ticks)
            throw std::runtime_error("mp4-timing-decode-discontinuity");
        for(auto runPos=traf.data;runPos<traf.end;) {
            const auto run=file.box(runPos,traf.end);
            runPos=run.end;
            if(run.type!=four('t','r','u','n'))continue;
            const auto runFlags=file.field(run,0);
            if(runFlags>>24>1 || (runFlags&0x00fff0fau) || ((runFlags&4)&&(runFlags&0x400)))
                throw std::runtime_error("mp4-timing-trun-flags");
            const auto count=file.field(run,4);
            const auto first=8u+(runFlags&1?4u:0u)+(runFlags&4?4u:0u);
            const auto step=4u*((runFlags&0x100?1u:0u)+(runFlags&0x200?1u:0u)+
                (runFlags&0x400?1u:0u)+(runFlags&0x800?1u:0u));
            if(run.end-run.data!=first+uint64_t(count)*step || count>sampleLimit-c.samples)
                throw std::runtime_error("mp4-timing-trun-invalid");
            if(count&&(flags&0x010000))throw std::runtime_error("mp4-timing-fragment-expected-empty");
            for(uint32_t i=0;i<count;++i) {
                const auto row=first+uint64_t(i)*step;
                const auto ticks=runFlags&0x100?file.field(run,row):duration;
                const auto offset=runFlags&0x800?sampleOffset(runFlags>>24,file.field(run,row+step-4)):0;
                const auto display=int64_t(c.ticks)+offset;
                add(c,1,ticks);
                timeline.frames.push_back({display,ticks});
            }
        }
    }
}
} // namespace detail

// Only an unambiguous opaque VP9 video goes to the Windows decoder probe.
// Tracks and timestamp scale are checked here; FPS is decided on decoded PTS.
inline bool opaqueVp9Webm(const std::filesystem::path& path) {
    using namespace detail;
    try {
        File file(path);
        const auto header=file.ebml(0,file.size());
        if(header.id!=0x1a45dfa3||header.unknown)return false;
        bool webm=false;
        unsigned docTypes=0;
        file.ebmlChildren(header,[&](auto part){
            if(part.id==0x4282){++docTypes;webm=file.ebmlString(part,"webm");}
            return true;
        });
        if(docTypes!=1||!webm)return false;
        auto segment=file.ebml(header.end,file.size());
        for(unsigned i=0;i<8&&!segment.unknown&&(segment.id==0xec||segment.id==0xbf);++i)
            segment=file.ebml(segment.end,file.size());
        if(segment.id!=0x18538067||segment.end!=file.size())return false;
        unsigned infos=0,tracks=0,videos=0,scales=0;
        bool opaqueVp9=false;
        uint64_t scale=1'000'000;
        file.ebmlChildren(segment,[&](auto part){
            if(part.id==0x1549a966){
                ++infos;
                file.ebmlChildren(part,[&](auto item){
                    if(item.id==0x2ad7b1){++scales;scale=file.ebmlUint(item);}
                    return true;
                });
            }else if(part.id==0x1654ae6b){
                ++tracks;
                file.ebmlChildren(part,[&](auto track){
                    if(track.id!=0xae)return true;
                    unsigned types=0,codecs=0,frames=0;
                    uint64_t type=0;
                    bool vp9=false,compatible=true;
                    file.ebmlChildren(track,[&](auto attr){
                        if(attr.id==0x83){++types;type=file.ebmlUint(attr);}
                        else if(attr.id==0x86){++codecs;vp9=file.ebmlString(attr,"V_VP9");}
                        else if(attr.id==0x55ee)compatible&=file.ebmlUint(attr)==0;
                        else if(attr.id==0x41e4)compatible=false;
                        else if(attr.id==0xe0){
                            ++frames;
                            file.ebmlChildren(attr,[&](auto video){
                                if(video.id==0x53c0)compatible&=file.ebmlUint(video)==0;
                                if(video.id==0x55b0)file.ebmlChildren(video,[&](auto color){
                                    if(color.id==0x55ba){const auto transfer=file.ebmlUint(color);compatible&=transfer!=16&&transfer!=18;}
                                    return true;
                                });
                                return true;
                            });
                        }
                        return true;
                    });
                    if(types!=1)throw std::runtime_error("webm-timing-ambiguous-track");
                    if(type==1){++videos;opaqueVp9=codecs==1&&frames==1&&vp9&&compatible;}
                    return true;
                });
            }
            return true;
        },true);
        return infos==1&&tracks==1&&videos==1&&scales<=1&&opaqueVp9&&scale>0&&scale<=1'000'000;
    }catch(const std::runtime_error&){return false;}
}

// Inspect the sample timeline, not the stream header's suggested frame rate.
// No decoding or traversal of the mdat payload is needed.
inline Cadence inspectMp4(const std::filesystem::path& path) {
    using namespace detail;
    try {
        File file(path);
        const Box root{0,file.size(),1};
        const auto moov=file.child(root,four('m','o','o','v'));
        if(!moov)return {};
        Cadence c;
        EditTimeline edit=EditTimeline::unit;
        uint32_t video=0,defaultDuration=0;
        for(auto pos=moov.data;pos<moov.end;) {
            const auto track=file.box(pos,moov.end);
            pos=track.end;
            if(track.type!=four('t','r','a','k'))continue;
            const auto media=file.child(track,four('m','d','i','a'));
            const auto handler=file.child(media,four('h','d','l','r'));
            if(!handler || file.field(handler,8)!=four('v','i','d','e'))continue;
            if(video)return {};
            edit=unitRateEdit(file,track);
            if(edit==EditTimeline::invalid)return {};
            const auto tkhd=file.child(track,four('t','k','h','d'));
            const auto mdhd=file.child(media,four('m','d','h','d'));
            video=trackId(file,tkhd);
            const auto version=file.field(mdhd,0)>>24;
            if(!video || version>1)return {};
            c.timescale=file.field(mdhd,version==1?20:12);
            if(!c.timescale)return {};
            sampleTable(file,media,c);
        }
        if(!video)return {};
        const auto mvex=file.child(moov,four('m','v','e','x'));
        bool gotDefaults=false;
        for(auto pos=mvex.data;pos<mvex.end;) {
            const auto trex=file.box(pos,mvex.end);
            pos=trex.end;
            if(trex.type!=four('t','r','e','x') || file.field(trex,4)!=video)continue;
            if(gotDefaults || file.field(trex,0)!=0)return {};
            gotDefaults=true;
            defaultDuration=file.field(trex,12);
        }
        bool payload=false;
        const bool hasTableSamples=c.samples!=0;
        if(hasTableSamples&&edit==EditTimeline::openEnded)return {};
        FragmentTimeline timeline;
        for(auto pos=root.data;pos<root.end;) {
            const auto b=file.box(pos,root.end);
            pos=b.end;
            if(b.type==four('m','d','a','t'))payload|=b.data<b.end;
            if(b.type==four('m','o','o','f')) {
                if(!gotDefaults||hasTableSamples)return {};
                fragment(file,b,video,defaultDuration,c,timeline);
            }
        }
        if(!payload||!c)return {};
        displayCadence(c,timeline.frames);
        return c;
    }catch(const std::runtime_error&){return {};}
}
} // namespace ymcc::media::frameProbe
