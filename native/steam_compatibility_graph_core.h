#pragma once
#include "steam_compatibility_core.h"
#include <optional>
#include <memory>
#include <map>
#include <limits>
#include <string>
#include <vector>
namespace ymcc::steamcompat {
enum class RangeKind { Code, Data, Pdata, Unwind };
enum class FixupKind { Relative32, Absolute64, Rva32, Rva64, Relative8 };
struct GraphRange {
    std::uint64_t refRva = 0;
    const unsigned char* bytes = nullptr;
    std::size_t size = 0;
    RangeKind kind = RangeKind::Data;
};
struct GraphFixup {
    std::uint64_t fieldRva = 0, originEndRva = 0, targetRva = 0;
    FixupKind kind = FixupKind::Relative32;
};
struct GraphSpec {
    std::span<const GraphRange> ranges;
    std::span<const GraphFixup> fixups;
    std::span<const SectionSpec> sections;
    std::uint64_t preferredBase = 0;
};
inline std::size_t fixupSize(FixupKind kind) noexcept {
    return kind==FixupKind::Relative8?1:(kind == FixupKind::Absolute64 || kind == FixupKind::Rva64 ? 8 : 4);
}
struct BoundRange { std::uint64_t reference = 0, actual = 0; std::size_t size = 0; RangeKind kind = RangeKind::Data; };
struct BoundPoint { std::uint64_t reference = 0, actual = 0; };
struct GraphStorage { std::vector<GraphRange> ranges; std::vector<GraphFixup> fixups; std::vector<SectionSpec> sections; };
struct GraphResolution {
    std::shared_ptr<const GraphStorage> storage;
    Evidence evidence;
    GraphSpec specification;
    std::vector<BoundRange> ranges;
    std::vector<BoundPoint> points;
    std::vector<SectionSpec> actualSections;
    std::uint64_t actualPreferredBase = 0, imageSize = 0;
    std::uint64_t candidatesExamined = 0;
};
// Filled by the bounded whole-pattern/reference-constraint resolver, never a
// first-string hit. An unresolved/ambiguous required code node stays unavailable.
inline GraphResolution resolveGraph(const Image&, const GraphSpec&);
inline std::optional<std::uint64_t> translateRva(const GraphResolution&, std::uint64_t, bool endpoint = false);
inline ReadRva referenceImageRead(const GraphResolution&, const ReadRva& actualRead, std::uint64_t actualBase,
    std::uint64_t referenceBase);
}

namespace ymcc::steamcompat {
namespace graph_detail {
inline std::optional<std::uint64_t> sectionTranslate(const GraphResolution& result, std::uint64_t rva, bool endpoint=false) {
    const SectionSpec* reference=nullptr;
    for(const auto& section:result.specification.sections){const auto extent=(std::max)(section.virtualSize,section.rawSize);
        if(rva>=section.rva&&(rva-section.rva<extent||(endpoint&&rva-section.rva==extent))){
            if(reference)return {};reference=&section;
        }
    }
    if(!reference){
        std::uint64_t first=0x1000;for(const auto& section:result.specification.sections)first=(std::min)(first,section.rva);
        return rva<first?std::optional<std::uint64_t>(rva):std::nullopt;
    }
    const SectionSpec* actual=nullptr;
    for(const auto& section:result.actualSections)if(section.name==reference->name&&
        ((section.characteristics^reference->characteristics)&0xe0000020u)==0){if(actual)return {};actual=&section;}
    if(!actual)return {};
    const auto offset=rva-reference->rva,extent=(std::max)(actual->virtualSize,actual->rawSize);
    if(offset>extent||(!endpoint&&offset==extent)||actual->rva>result.imageSize||offset>result.imageSize-actual->rva)return {};
    return actual->rva+offset;
}
// A reference-derived location must keep its PE storage class. In particular,
// a writable global cannot be rebound into executable code merely because a
// masked RIP operand points there; full patterns still verify the contents.
inline const SectionSpec* containingSection(std::span<const SectionSpec> sections,
    std::uint64_t rva,std::size_t bytes){
    const SectionSpec* found=nullptr;
    for(const auto& section:sections){const auto extent=(std::max)(section.virtualSize,section.rawSize);
        if(rva>=section.rva&&rva-section.rva<extent&&bytes<=extent-(rva-section.rva)){
            if(found)return nullptr;found=&section;
        }
    }
    return found;
}
inline bool typedLocation(const GraphResolution& result,std::uint64_t reference,
    std::uint64_t actual,std::size_t bytes){
    if(!bytes||actual>=result.imageSize||bytes>result.imageSize-actual)return false;
    const auto* source=containingSection(result.specification.sections,reference,bytes);
    if(!source){
        std::uint64_t firstReference=0x1000,firstActual=0x1000;
        for(const auto& section:result.specification.sections)firstReference=(std::min)(firstReference,section.rva);
        for(const auto& section:result.actualSections)firstActual=(std::min)(firstActual,section.rva);
        return reference==actual&&reference<firstReference&&bytes<=firstReference-reference&&
            actual<firstActual&&bytes<=firstActual-actual;
    }
    const auto* destination=containingSection(result.actualSections,actual,bytes);
    return destination&&((source->characteristics^destination->characteristics)&0xe0000000u)==0;
}
inline bool fieldValue(const Image& image,const GraphResolution& result,const BoundRange& source,const GraphFixup& field,
    std::uint64_t& target) {
    const auto size=fixupSize(field.kind);
    if(field.fieldRva<source.reference||field.fieldRva-source.reference>source.size||size>source.size-(field.fieldRva-source.reference))return false;
    const auto actual=source.actual+(field.fieldRva-source.reference);std::uint64_t value=0;
    if(!image.read(actual,&value,size))return false;
    switch(field.kind){
    case FixupKind::Relative8:case FixupKind::Relative32:{
        if(field.originEndRva<source.reference||field.originEndRva-source.reference>source.size)return false;
        const auto end=source.actual+(field.originEndRva-source.reference);
        const auto displacement=field.kind==FixupKind::Relative8 ? static_cast<std::int32_t>(static_cast<std::int8_t>(static_cast<std::uint8_t>(value))) : static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
        if(displacement<0){const auto distance=static_cast<std::uint64_t>(-static_cast<std::int64_t>(displacement));if(distance>end)return false;target=end-distance;}
        else {if(std::uint64_t(displacement)>result.imageSize-end)return false;target=end+static_cast<std::uint64_t>(displacement);}break;
    }
    case FixupKind::Absolute64:if(value<image.preferredBase)return false;target=value-image.preferredBase;break;
    case FixupKind::Rva32:case FixupKind::Rva64:target=value;break;
    }
    return target<=result.imageSize;
}
inline bool maskedEqual(const Image& image,const GraphSpec& specification,const GraphRange& range,std::uint64_t actual,
    std::vector<unsigned char>* maskOut=nullptr) {
    if(!range.bytes||!range.size||range.size>1024*1024||actual>=image.imageSize||range.size>image.imageSize-actual)return false;
    std::vector<unsigned char> bytes(range.size),mask(range.size,1);
    if(!image.read(actual,bytes.data(),bytes.size()))return false;
    for(const auto& field:specification.fixups){const auto size=fixupSize(field.kind);
        if(field.fieldRva>=range.refRva&&field.fieldRva-range.refRva<range.size){
            if(size>range.size-(field.fieldRva-range.refRva))return false;
            std::fill_n(mask.begin()+static_cast<std::ptrdiff_t>(field.fieldRva-range.refRva),size,static_cast<unsigned char>(0));
        }
    }
    for(std::size_t i=0;i<range.size;++i)if(mask[i]&&bytes[i]!=range.bytes[i])return false;
    if(maskOut)*maskOut=std::move(mask);return true;
}
struct Buffer { SectionSpec section;std::vector<unsigned char> bytes; };
struct Resolver {
    const Image& image;GraphResolution result;std::vector<std::optional<std::uint64_t>> bindings;
    std::vector<Buffer> executable;bool changed=false;const char* failure=nullptr;
    explicit Resolver(const Image& value,const GraphSpec& specification):image(value){
        result.specification=specification;result.actualPreferredBase=image.preferredBase;result.imageSize=image.imageSize;
        result.actualSections.assign(image.sections.begin(),image.sections.end());bindings.resize(specification.ranges.size());
    }
    bool bind(std::size_t index,std::uint64_t actual){
        const auto& range=result.specification.ranges[index];
        if(bindings[index])return *bindings[index]==actual;
        if(!typedLocation(result,range.refRva,actual,range.size)||!maskedEqual(image,result.specification,range,actual))return false;
        for(const auto& bound:result.ranges){
            const auto low=(std::max)(bound.reference,range.refRva),high=(std::min)(bound.reference+bound.size,range.refRva+range.size);
            if(low<high&&bound.actual+(low-bound.reference)!=actual+(low-range.refRva))return false;
        }
        for(const auto& point:result.points)if(point.reference>=range.refRva&&point.reference-range.refRva<range.size&&
            point.actual!=actual+(point.reference-range.refRva))return false;
        bindings[index]=actual;result.ranges.push_back({range.refRva,actual,range.size,range.kind});changed=true;return true;
    }
    bool bindPoint(std::uint64_t reference,std::uint64_t actual){
        if(actual>=image.imageSize)return false;
        for(const auto& point:result.points)if(point.reference==reference)return point.actual==actual;
        for(const auto& range:result.ranges)if(reference>=range.reference&&reference-range.reference<range.size)
            return range.actual+(reference-range.reference)==actual;
        result.points.push_back({reference,actual});changed=true;return true;
    }
    bool target(std::uint64_t reference,std::uint64_t actual){
        bool contained=false;
        for(std::size_t i=0;i<result.specification.ranges.size();++i){const auto& range=result.specification.ranges[i];
            if(reference>=range.refRva&&reference-range.refRva<range.size){
                contained=true;const auto offset=reference-range.refRva;if(actual<offset||!bind(i,actual-offset))return false;
            }
        }
        if(contained)return true;
        // A vtable's COL pointer is the eight bytes immediately before its
        // address. A verified constructor reference can locate that complete
        // typed predecessor even when the table moved within the section.
        for(std::size_t i=0;i<result.specification.ranges.size();++i){const auto& range=result.specification.ranges[i];
            if(range.kind==RangeKind::Data&&reference==range.refRva+range.size&&actual>=range.size){
                if(!bind(i,actual-range.size))return false;return bindPoint(reference,actual);
            }
        }
        for(const auto& range:result.ranges)if(reference==range.reference+range.size)return actual==range.actual+range.size;
        for(const auto& point:result.points)if(point.reference==reference)return point.actual==actual;
        // Writable singleton/TLS/global locations can be derived by a verified
        // operand; repeated uses of the same reference must bind identically.
        // Opaque code/read-only targets retain their section-relative contract.
        bool writable=false;
        for(const auto& section:result.specification.sections)if(reference>=section.rva&&reference-section.rva<(std::max)(section.virtualSize,section.rawSize))
            writable=(section.characteristics&0x80000000u)!=0;
        if(writable&&!typedLocation(result,reference,actual,1))return false;
        if(!writable){const auto expected=sectionTranslate(result,reference,true);if(!expected||*expected!=actual)return false;}
        return bindPoint(reference,actual);
    }
    bool propagate(){
        const auto& spec=result.specification;
        for(std::size_t i=0;i<spec.ranges.size();++i)if(bindings[i]){
            const auto& range=spec.ranges[i];const BoundRange source{range.refRva,*bindings[i],range.size,range.kind};
            for(const auto& field:spec.fixups)if(field.fieldRva>=range.refRva&&field.fieldRva-range.refRva<range.size){
                std::uint64_t actual=0;if(!fieldValue(image,result,source,field,actual)||!target(field.targetRva,actual))return false;
            }
        }
        // Exception table positions can change independently of their code.
        // The uniquely resolved BeginAddress selects its real pdata entry.
        for(std::size_t i=0;i<spec.ranges.size();++i)if(!bindings[i]&&spec.ranges[i].kind==RangeKind::Pdata&&spec.ranges[i].size==12){
            std::uint32_t referenceBegin=0;std::memcpy(&referenceBegin,spec.ranges[i].bytes,4);
            const auto begin=translateRva(result,referenceBegin);if(!begin)continue;
            const RuntimeFunction* found=nullptr;
            for(const auto& function:image.functions)if(function.begin==*begin){if(found)return false;found=&function;}
            if(found&&!bind(i,found->tableRva))return false;
        }
        return true;
    }
    bool loadCode(){
        std::uint64_t total=0;
        for(const auto& section:image.sections)if(section.characteristics&0x20000000u){
            if(!section.rawSize||section.rawSize>64*1024*1024||total>128*1024*1024-section.rawSize)return false;
            Buffer buffer{section,std::vector<unsigned char>(static_cast<std::size_t>(section.rawSize))};
            if(!image.read(section.rva,buffer.bytes.data(),buffer.bytes.size()))return false;
            total+=section.rawSize;executable.push_back(std::move(buffer));
        }
        return !executable.empty();
    }
    std::vector<std::uint64_t> scan(std::size_t index){
        const auto& range=result.specification.ranges[index];std::vector<unsigned char> mask(range.size,1);
        for(const auto& field:result.specification.fixups)if(field.fieldRva>=range.refRva&&field.fieldRva-range.refRva<range.size){
            const auto size=fixupSize(field.kind);if(size>range.size-(field.fieldRva-range.refRva))return {};
            std::fill_n(mask.begin()+static_cast<std::ptrdiff_t>(field.fieldRva-range.refRva),size,static_cast<unsigned char>(0));
        }
        std::size_t best=0,bestAt=0,run=0;
        for(std::size_t i=0;i<mask.size();++i){run=mask[i]?run+1:0;if(run>best){best=(std::min)(run,std::size_t(32));bestAt=i+1-best;}}
        if(best<8)return {};
        std::vector<std::uint64_t> matches;
        for(const auto& buffer:executable){
            const auto search=std::boyer_moore_horspool_searcher(range.bytes+bestAt,range.bytes+bestAt+best);
            auto cursor=buffer.bytes.begin();
            while(cursor!=buffer.bytes.end()){
                const auto found=std::search(cursor,buffer.bytes.end(),search);if(found==buffer.bytes.end())break;
                const auto offset=static_cast<std::size_t>(found-buffer.bytes.begin());cursor=found+1;
                if(offset<bestAt)continue;const auto actual=buffer.section.rva+offset-bestAt;++result.candidatesExamined;
                if(result.candidatesExamined>65536){failure="graph-candidate-budget";return {};}
                if(maskedEqual(image,result.specification,range,actual)){
                    matches.push_back(actual);if(matches.size()>8)return matches;
                }
            }
        }
        return matches;
    }
    GraphResolution run(){
        const auto& spec=result.specification;
        if(!image.read||image.sections.empty()||spec.sections.empty()||spec.ranges.empty()||spec.ranges.size()>2048||spec.fixups.size()>32768){
            result.evidence.reason="graph-input-invalid";return std::move(result);
        }
        std::uint64_t bytes=0;
        for(const auto& range:spec.ranges){
            if(!range.bytes||!range.size||range.size>1024*1024||bytes>2*1024*1024-range.size){result.evidence.reason="graph-range-budget";return std::move(result);}bytes+=range.size;
        }
        if(!loadCode()){result.evidence.reason="graph-code-sections-unavailable";return std::move(result);}
        std::vector<std::size_t> order;for(std::size_t i=0;i<spec.ranges.size();++i)if(spec.ranges[i].kind==RangeKind::Code)order.push_back(i);
        std::sort(order.begin(),order.end(),[&](auto a,auto b){return spec.ranges[a].size>spec.ranges[b].size;});
        for(const auto index:order){if(bindings[index])continue;const auto candidates=scan(index);if(failure){result.evidence.reason=failure;return std::move(result);}
            if(candidates.size()==1&&!bind(index,candidates.front())){result.evidence.reason="graph-code-binding-conflict";return std::move(result);}
        }
        for(std::size_t round=0;round<spec.ranges.size()+2;++round){
            changed=false;if(!propagate()){result.evidence.reason="graph-reference-contract-incompatible";return std::move(result);}
            if(changed)continue;
            bool fallback=false;
            for(std::size_t i=0;i<spec.ranges.size();++i)if(!bindings[i]&&spec.ranges[i].kind!=RangeKind::Code){
                const auto actual=sectionTranslate(result,spec.ranges[i].refRva);
                if(actual&&bind(i,*actual)){fallback=true;break;}
            }
            if(!fallback)break;
        }
        for(std::size_t i=0;i<bindings.size();++i)if(!bindings[i]){
            result.evidence.failedRva=spec.ranges[i].refRva;
            result.evidence.reason=spec.ranges[i].kind==RangeKind::Code?"graph-code-unresolved-or-ambiguous":"graph-data-unresolved";return std::move(result);
        }
        changed=false;if(!propagate()){result.evidence.reason="graph-final-reference-mismatch";return std::move(result);}
        result.evidence={true,static_cast<std::uint64_t>(spec.ranges.size()),bytes,0,"graph-complete-pattern-and-references-matched"};return std::move(result);
    }
};
}
inline std::optional<std::uint64_t> translateRva(const GraphResolution& result,std::uint64_t rva,bool endpoint){
    for(const auto& point:result.points)if(point.reference==rva)return point.actual;
    std::optional<std::uint64_t> value;
    for(const auto& range:result.ranges)if(rva>=range.reference&&(rva-range.reference<range.size||(endpoint&&rva-range.reference==range.size))){
        const auto next=range.actual+(rva-range.reference);if(value&&*value!=next)return {};value=next;
    }
    return value?value:graph_detail::sectionTranslate(result,rva,endpoint);
}
inline GraphResolution resolveGraph(const Image& image,const GraphSpec& specification){
    try{return graph_detail::Resolver(image,specification).run();}
    catch(...){GraphResolution result;result.specification=specification;result.evidence.reason="graph-resolution-exception";return result;}
}
inline GraphResolution resolveGraphs(const Image& image,std::span<const GraphSpec> graphs){
    if(graphs.empty()){GraphResolution result;result.evidence.reason="graph-empty";return result;}
    auto storage=std::make_shared<GraphStorage>();storage->sections.assign(graphs[0].sections.begin(),graphs[0].sections.end());
    const auto preferred=graphs[0].preferredBase;
    for(const auto& graph:graphs){
        if(graph.preferredBase!=preferred){GraphResolution result;result.evidence.reason="graph-reference-base-mismatch";return result;}
        storage->ranges.insert(storage->ranges.end(),graph.ranges.begin(),graph.ranges.end());
        for(const auto& field:graph.fixups){
            bool duplicate=false;for(const auto& old:storage->fixups)if(old.fieldRva==field.fieldRva){
                if(old.originEndRva!=field.originEndRva||old.targetRva!=field.targetRva||old.kind!=field.kind){GraphResolution result;result.evidence.reason="graph-field-contract-conflict";return result;}duplicate=true;break;
            }
            if(!duplicate)storage->fixups.push_back(field);
        }
    }
    const GraphSpec combined{storage->ranges,storage->fixups,storage->sections,preferred};
    auto result=resolveGraph(image,combined);result.storage=std::move(storage);return result;
}
inline ReadRva referenceImageRead(const GraphResolution& result,const ReadRva& actualRead,std::uint64_t actualBase,
    std::uint64_t referenceBase){
    return [&result,actualRead,actualBase,referenceBase](std::uint64_t rva,void* target,std::size_t bytes){
        if(!result.evidence.matched||!actualRead||!bytes)return false;
        const auto actual=translateRva(result,rva),last=translateRva(result,rva+bytes-1);
        if(!actual||!last||*last<*actual||*last-*actual!=bytes-1||!actualRead(*actual,target,bytes))return false;
        for(const auto& field:result.specification.fixups){const auto size=fixupSize(field.kind);
            if(field.fieldRva>=rva+bytes||field.fieldRva+size<=rva)continue;
            const auto at=translateRva(result,field.fieldRva),origin=translateRva(result,field.originEndRva,true),to=translateRva(result,field.targetRva,true);
            if(!at||!to||((field.kind==FixupKind::Relative32||field.kind==FixupKind::Relative8)&&!origin))return false;
            std::uint64_t observed=0,expectedActual=0,reference=0;if(!actualRead(*at,&observed,size))return false;
            switch(field.kind){
            case FixupKind::Relative8:case FixupKind::Relative32:{
                const auto delta=static_cast<std::int64_t>(*to)-static_cast<std::int64_t>(*origin);
                const auto wanted=static_cast<std::int64_t>(field.targetRva)-static_cast<std::int64_t>(field.originEndRva);
                const auto low=field.kind==FixupKind::Relative8?-128LL:static_cast<long long>((std::numeric_limits<std::int32_t>::min)());
                const auto high=field.kind==FixupKind::Relative8?127LL:static_cast<long long>((std::numeric_limits<std::int32_t>::max)());
                if(delta<low||delta>high||wanted<low||wanted>high)return false;
                if(field.kind==FixupKind::Relative8){expectedActual=static_cast<std::uint8_t>(static_cast<std::int8_t>(delta));reference=static_cast<std::uint8_t>(static_cast<std::int8_t>(wanted));}
                else {expectedActual=static_cast<std::uint32_t>(static_cast<std::int32_t>(delta));reference=static_cast<std::uint32_t>(static_cast<std::int32_t>(wanted));}break;
            }
            case FixupKind::Absolute64:expectedActual=actualBase+*to;reference=referenceBase+field.targetRva;break;
            case FixupKind::Rva32:case FixupKind::Rva64:expectedActual=*to;reference=field.targetRva;break;
            }
            if(observed!=expectedActual)return false;
            const auto low=(std::max)(rva,field.fieldRva),high=(std::min)(rva+bytes,field.fieldRva+size);
            std::memcpy(static_cast<unsigned char*>(target)+(low-rva),reinterpret_cast<const unsigned char*>(&reference)+(low-field.fieldRva),static_cast<std::size_t>(high-low));
        }
        return true;
    };
}
}
