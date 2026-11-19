#pragma once
#include "steam_reference_coordinate_map.h"
#include "steam_diagnostic_read_views.h"
#include <array>
#include <cstring>
#include <memory>

namespace ymcc::steamcompat::coordinates {
struct ViewBases {std::uint64_t canonical=0,field=0;};
inline bool fieldEncoding(const Field& row,bool canonical,const ViewBases& bases,
    std::array<unsigned char,8>& bytes){
    const auto target=canonical?row.canonicalTarget:row.fieldTarget;
    const auto origin=canonical?row.canonicalOrigin:row.fieldOrigin;
    const auto base=canonical?bases.canonical:bases.field;
    std::uint64_t value=0;
    switch(row.kind){
    case FixupKind::Relative8:case FixupKind::Relative32:{
        if(target>256ULL*1024*1024||origin>256ULL*1024*1024)return false;
        const auto delta=static_cast<std::int64_t>(target)-static_cast<std::int64_t>(origin);
        if(row.kind==FixupKind::Relative8){
            if(delta< -128||delta>127)return false;value=static_cast<std::uint8_t>(static_cast<std::int8_t>(delta));
        }else{
            if(delta<(std::numeric_limits<std::int32_t>::min)()||delta>(std::numeric_limits<std::int32_t>::max)())return false;
            value=static_cast<std::uint32_t>(static_cast<std::int32_t>(delta));
        }break;
    }
    case FixupKind::Absolute64:
        if(target>(std::numeric_limits<std::uint64_t>::max)()-base)return false;value=base+target;break;
    case FixupKind::Rva32:if(target>0xffffffffULL)return false;value=target;break;
    case FixupKind::Rva64:value=target;break;
    default:return false;
    }
    for(std::size_t i=0;i<fixupSize(row.kind);++i)bytes[i]=static_cast<unsigned char>(value>>(i*8));
    return true;
}
// fieldView must already be the disk-selected complete F contract's normalized
// F-to-actual view. It is owned by this closure. This view rewrites only exact
// certified address operands; all scalar and literal bytes stay observed.
inline ReadRva canonicalModuleView(std::shared_ptr<const Map> map,
    std::shared_ptr<const GraphResolution> graph,ReadRva fieldView,ViewBases bases){
    if(!map||!graph||!graph->evidence.matched||!fieldView)return {};
    return [map=std::move(map),graph=std::move(graph),fieldView=std::move(fieldView),bases]
        (std::uint64_t canonical,void* destination,std::size_t bytes){
        if(!destination||!bytes||canonical>=map->canonicalImageSize()||bytes>map->canonicalImageSize()-canonical)return false;
        const auto field=map->readableField(canonical,bytes);if(!field)return false;
        try{
            if(!fieldView(*field,destination,bytes))return false;
            const auto end=canonical+bytes;
            for(const auto& row:map->fields()){
                const auto width=fixupSize(row.kind);
                if(row.canonical>=end||row.canonical+width<=canonical)continue;
                std::array<unsigned char,8> observed{},expectedField{},expectedCanonical{};
                // Even a one-byte or low32 request validates the entire operand,
                // including its high32/carry. No adjacent unknown bytes are read.
                if(!map->readableField(row.canonical,width)||
                    !fieldEncoding(row,false,bases,expectedField)||!fieldEncoding(row,true,bases,expectedCanonical)||
                    !fieldView(row.field,observed.data(),width)||
                    !std::equal(observed.begin(),observed.begin()+static_cast<std::ptrdiff_t>(width),expectedField.begin()))return false;
                const auto low=(std::max)(canonical,row.canonical),high=(std::min)(end,row.canonical+width);
                std::memcpy(static_cast<unsigned char*>(destination)+(low-canonical),expectedCanonical.data()+(low-row.canonical),static_cast<std::size_t>(high-low));
            }
            return true;
        }catch(...){return false;}
    };
}
inline ReadRva absoluteCanonicalModuleView(ReadRva relative,std::uint64_t base,std::uint64_t imageSize){
    if(!relative||imageSize>(std::numeric_limits<std::uint64_t>::max)()-base)return {};
    return [relative=std::move(relative),base,imageSize](std::uint64_t address,void* destination,std::size_t bytes){
        return address>=base&&address-base<imageSize&&bytes<=imageSize-(address-base)&&relative(address-base,destination,bytes);
    };
}
inline std::optional<std::uint64_t> inverseProvedReference(const GraphResolution& graph,
    std::uint64_t actual){
    if(!graph.evidence.matched)return {};
    std::optional<std::uint64_t> reference;
    const auto accept=[&](std::uint64_t next){if(reference&&*reference!=next)return false;reference=next;return true;};
    for(const auto& point:graph.points)if(point.actual==actual&&!accept(point.reference))return {};
    for(const auto& row:graph.ranges)if(actual>=row.actual&&actual-row.actual<row.size&&
        !accept(row.reference+(actual-row.actual)))return {};
    return reference;
}
// Reader intent is explicit: object data is never routed by address range or
// numeric value. Only its module port and explicit vptr operation use mappings.
inline ymcc::steamdiagnostic::ReadViews referenceReadViews(
    std::shared_ptr<const Map> map,std::shared_ptr<const GraphResolution> graph,
    std::uint64_t actualBase,std::uint64_t actualSize,const ReadRva& rawActual){
    if(!map||!graph||!graph->evidence.matched||!rawActual||graph->imageSize!=actualSize||
        actualSize>(std::numeric_limits<std::uint64_t>::max)()-actualBase)return {};
    const ReadRva module=[rawActual,actualBase,actualSize](std::uint64_t rva,void* destination,std::size_t bytes){
        return rva<actualSize&&bytes<=actualSize-rva&&rawActual(actualBase+rva,destination,bytes);
    };
    const auto field=referenceImageRead(*graph,module,actualBase,actualBase);
    const auto canonical=canonicalModuleView(map,graph,field,{actualBase,actualBase});
    if(!canonical)return {};
    const auto absolute=absoluteCanonicalModuleView(canonical,actualBase,map->canonicalImageSize());if(!absolute)return {};
    ymcc::steamdiagnostic::ReadViews result;result.rawObject=rawActual;result.canonicalModule=absolute;
    result.explicitModulePointerNormalizer=[map=std::move(map),graph=std::move(graph),actualBase,actualSize](std::uint64_t value,std::uint64_t& output){
        const auto normalized=typedModulePointer(*map,*graph,actualBase,actualSize,value);
        if(!normalized)return false;output=*normalized;return true;
    };
    return result;
}
inline ymcc::steamdiagnostic::ReadViews canonicalGraphReadViews(
    std::shared_ptr<const GraphResolution> graph,std::uint64_t actualBase,std::uint64_t actualSize,
    const ReadRva& rawActual,std::uint64_t canonicalImageSize,std::span<const Point> typedRoots={}){
    if(!rawActual||actualSize>(std::numeric_limits<std::uint64_t>::max)()-actualBase)return {};
    if(!graph)return {rawActual,{},{}}; // Existing fixed coordinate contracts.
    if(!graph->evidence.matched||graph->imageSize!=actualSize)return {};
    const ReadRva module=[rawActual,actualBase,actualSize](std::uint64_t rva,void* destination,std::size_t bytes){
        return rva<actualSize&&bytes<=actualSize-rva&&rawActual(actualBase+rva,destination,bytes);
    };
    const auto relative=referenceImageRead(*graph,module,actualBase,actualBase);
    // referenceImageRead borrows GraphResolution. This port must own it even
    // when callers retain canonicalModule alone and destroy sibling ports.
    const ReadRva ownedRelative=[graph,relative](std::uint64_t rva,void* destination,std::size_t bytes){
        return relative(rva,destination,bytes);
    };
    const auto absolute=absoluteCanonicalModuleView(ownedRelative,actualBase,canonicalImageSize);if(!absolute)return {};
    ymcc::steamdiagnostic::ReadViews result{rawActual,absolute,{}};
    result.explicitModulePointerNormalizer=[graph=std::move(graph),actualBase,actualSize,
        typedRoots=std::vector<Point>(typedRoots.begin(),typedRoots.end())](std::uint64_t value,std::uint64_t& output){
        if(value<actualBase||value-actualBase>=actualSize)return false;
        auto reference=inverseProvedReference(*graph,value-actualBase);
        for(const auto& point:typedRoots){const auto anchored=anchoredTypedActual(*graph,point);
            if(anchored&&*anchored==value-actualBase){
                if(reference&&*reference!=point.canonical)return false;reference=point.canonical;
            }
        }
        if(!reference||*reference>(std::numeric_limits<std::uint64_t>::max)()-actualBase)return false;
        output=actualBase+*reference;return true;
    };
    return result;
}
} // namespace ymcc::steamcompat::coordinates
