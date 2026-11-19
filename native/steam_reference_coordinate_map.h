#pragma once
#include "steam_compatibility_graph_core.h"
#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace ymcc::steamcompat::coordinates {
// Structural translation only. This type is not a module qualification, a
// process/account/power lease or permission to call/read an opaque target.
struct Domain {
    std::uint64_t canonical=0,field=0;
    std::size_t canonicalSize=0,fieldSize=0;
    RangeKind kind=RangeKind::Data;
    bool readable=true;
};
enum class PointUse { AddressOnly, ReaderStorage, TypedModulePointer, CallableEntry };
struct Point {
    std::uint64_t canonical=0,field=0;
    PointUse use=PointUse::AddressOnly;
    std::size_t readerBytes=0;
    // Only an explicit typed root may use its declared complete eight-byte
    // module slot to bind the root. This grants no readable root/body/entry.
    std::uint64_t bindingSlotOffset=0;
};
struct Field {
    std::uint64_t canonical=0,field=0,canonicalOrigin=0,fieldOrigin=0,
        canonicalTarget=0,fieldTarget=0;
    FixupKind kind=FixupKind::Relative32;
    bool operator==(const Field&)const=default;
};
struct Definition {
    std::uint64_t canonicalImageSize=0,fieldImageSize=0;
    std::span<const Domain> domains;
    std::span<const Point> points;
    std::span<const Field> fields;
};
class Map {
    std::uint64_t canonicalSize_=0,fieldSize_=0;
    std::vector<Domain> domains_;
    std::vector<Point> points_;
    std::vector<Point> inversePoints_;
    std::vector<Field> fields_;
    static bool bounded(std::uint64_t start,std::size_t bytes,std::uint64_t size){
        return bytes&&start<size&&bytes<=size-start;
    }
    std::optional<std::uint64_t> address(std::uint64_t value,bool inverse,bool endpoint)const{
        std::optional<std::uint64_t> found;
        const auto accept=[&](std::uint64_t next){if(found&&*found!=next)return false;found=next;return true;};
        // A SourceFile is a suffix-checked complete string, not a translation
        // of the old build-directory bytes. Its interior stays unmapped even
        // if a separate point happens to use the same numeric offset.
        for(const auto& row:domains_)if(row.kind==RangeKind::SourceFile){
            const auto start=inverse?row.field:row.canonical;
            const auto size=inverse?row.fieldSize:row.canonicalSize;
            if(value>start&&value-start<=size)return {};
        }
        const auto& ordered=inverse?inversePoints_:points_;
        auto point=std::lower_bound(ordered.begin(),ordered.end(),value,[&](const auto& row,std::uint64_t v){return (inverse?row.field:row.canonical)<v;});
        for(;point!=ordered.end()&&(inverse?point->field:point->canonical)==value;++point){
            if(!accept(inverse?point->canonical:point->field))return {};
        }
        for(const auto& row:domains_){
            const auto start=inverse?row.field:row.canonical;
            const auto size=inverse?row.fieldSize:row.canonicalSize;
            if(value<start)continue;
            const auto offset=value-start;
            if(row.kind==RangeKind::SourceFile){if(offset)continue;}
            else if(offset>size||(!endpoint&&offset==size))continue;
            const auto target=inverse?row.canonical:row.field;
            if(offset>(std::numeric_limits<std::uint64_t>::max)()-target||!accept(target+offset))return {};
        }
        return found;
    }
public:
    std::uint64_t canonicalImageSize()const noexcept{return canonicalSize_;}
    std::uint64_t fieldImageSize()const noexcept{return fieldSize_;}
    std::span<const Field> fields()const noexcept{return fields_;}
    std::span<const Domain> domains()const noexcept{return domains_;}
    std::span<const Point> points()const noexcept{return points_;}
    std::optional<std::uint64_t> toField(std::uint64_t value,bool endpoint=false)const{return address(value,false,endpoint);}
    std::optional<std::uint64_t> toCanonical(std::uint64_t value,bool endpoint=false)const{return address(value,true,endpoint);}
    std::optional<std::uint64_t> readableField(std::uint64_t value,std::size_t bytes)const{
        if(!bounded(value,bytes,canonicalSize_))return {};
        std::optional<std::uint64_t> found;
        const auto accept=[&](std::uint64_t next){if(found&&*found!=next)return false;found=next;return true;};
        for(const auto& row:domains_){
            if(row.kind==RangeKind::SourceFile||!row.readable||value<row.canonical)continue;
            const auto offset=value-row.canonical;
            // One complete proved interval must cover the read. Equal mapped
            // endpoints across a gap or two adjacent domains are insufficient.
            if(offset<row.canonicalSize&&bytes<=row.canonicalSize-offset&&
                !accept(row.field+offset))return {};
        }
        auto point=std::lower_bound(points_.begin(),points_.end(),value,[](const auto& row,std::uint64_t v){return row.canonical<v;});
        for(;point!=points_.end()&&point->canonical==value;++point)if(point->use==PointUse::ReaderStorage&&
            bytes<=point->readerBytes&&!accept(point->field))return {};
        return found;
    }
    bool typedPointerRoot(std::uint64_t canonical,std::uint64_t field)const noexcept{
        return std::any_of(points_.begin(),points_.end(),[&](const auto& row){
            return row.use==PointUse::TypedModulePointer&&row.canonical==canonical&&row.field==field;
        });
    }
    bool callableRoot(std::uint64_t canonical)const noexcept{
        return std::any_of(points_.begin(),points_.end(),[&](const auto& row){
            return row.use==PointUse::CallableEntry&&row.canonical==canonical;
        });
    }
    static std::optional<Map> make(const Definition& definition){
        if(definition.canonicalImageSize<4096||definition.fieldImageSize<4096||
            definition.canonicalImageSize>256ULL*1024*1024||definition.fieldImageSize>256ULL*1024*1024||
            definition.domains.empty()||definition.domains.size()>4096||definition.points.size()>65536||definition.fields.size()>32768)return {};
        Map out;out.canonicalSize_=definition.canonicalImageSize;out.fieldSize_=definition.fieldImageSize;
        out.domains_.assign(definition.domains.begin(),definition.domains.end());
        out.points_.assign(definition.points.begin(),definition.points.end());
        std::sort(out.points_.begin(),out.points_.end(),[](const auto& a,const auto& b){return a.canonical<b.canonical;});
        out.inversePoints_=out.points_;
        std::sort(out.inversePoints_.begin(),out.inversePoints_.end(),[](const auto& a,const auto& b){return a.field<b.field;});
        out.fields_.assign(definition.fields.begin(),definition.fields.end());
        for(const auto& row:out.domains_){
            if(!bounded(row.canonical,row.canonicalSize,out.canonicalSize_)||!bounded(row.field,row.fieldSize,out.fieldSize_))return {};
            if(row.kind!=RangeKind::SourceFile&&row.canonicalSize!=row.fieldSize)return {};
            if(row.kind==RangeKind::SourceFile&&row.readable)return {};
            if(static_cast<unsigned>(row.kind)>static_cast<unsigned>(RangeKind::SourceFile))return {};
        }
        for(const auto& row:out.points_){
            if(row.canonical>=out.canonicalSize_||row.field>=out.fieldSize_||
                static_cast<unsigned>(row.use)>static_cast<unsigned>(PointUse::CallableEntry))return {};
            if(row.use==PointUse::ReaderStorage){
                if(!bounded(row.canonical,row.readerBytes,out.canonicalSize_)||!bounded(row.field,row.readerBytes,out.fieldSize_))return {};
            }else if(row.readerBytes)return {};
            if(row.bindingSlotOffset){
                if(row.use!=PointUse::TypedModulePointer||
                    row.bindingSlotOffset>=out.canonicalSize_-row.canonical||
                    row.bindingSlotOffset>=out.fieldSize_-row.field)return {};
                const auto anchor=out.readableField(row.canonical+row.bindingSlotOffset,8);
                if(!anchor||*anchor!=row.field+row.bindingSlotOffset)return {};
            }
            if(row.use!=PointUse::AddressOnly)for(const auto& domain:out.domains_)if(domain.kind==RangeKind::SourceFile&&
                (graph_detail::overlaps(row.canonical,(std::max)(row.readerBytes,std::size_t(1)),domain.canonical,domain.canonicalSize)||
                 graph_detail::overlaps(row.field,(std::max)(row.readerBytes,std::size_t(1)),domain.field,domain.fieldSize)))return {};
        }
        for(const auto& source:out.domains_)if(source.kind==RangeKind::SourceFile)
            for(const auto& other:out.domains_)if(other.kind!=RangeKind::SourceFile&&
                (graph_detail::overlaps(source.canonical,source.canonicalSize,other.canonical,other.canonicalSize)||
                 graph_detail::overlaps(source.field,source.fieldSize,other.field,other.fieldSize)))return {};
        // Enforce both directions at every interval boundary and explicit point.
        // For affine equal-size domains, boundaries and overlap endpoints prove
        // their complete intersections; no section/global delta is admitted.
        for(const auto& row:out.domains_){
            const auto first=out.toField(row.canonical),back=out.toCanonical(row.field);
            if(!first||*first!=row.field||!back||*back!=row.canonical)return {};
            if(row.kind!=RangeKind::SourceFile){
                const auto last=out.toField(row.canonical+row.canonicalSize-1),reverse=out.toCanonical(row.field+row.fieldSize-1);
                if(!last||*last!=row.field+row.fieldSize-1||!reverse||*reverse!=row.canonical+row.canonicalSize-1)return {};
            }
        }
        for(const auto& row:out.points_){
            const auto next=out.toField(row.canonical),back=out.toCanonical(row.field);
            if(!next||*next!=row.field||!back||*back!=row.canonical)return {};
        }
        for(const auto& row:out.fields_){
            if(static_cast<unsigned>(row.kind)>static_cast<unsigned>(FixupKind::Relative8))return {};
            const auto width=fixupSize(row.kind);
            const auto at=out.readableField(row.canonical,width),target=out.toField(row.canonicalTarget,true);
            if(!at||*at!=row.field||!target||*target!=row.fieldTarget)return {};
            if(row.kind==FixupKind::Relative8||row.kind==FixupKind::Relative32){
                const auto origin=out.toField(row.canonicalOrigin,true);if(!origin||*origin!=row.fieldOrigin)return {};
            }else if(row.canonicalOrigin||row.fieldOrigin)return {};
        }
        std::vector<const Field*> order;order.reserve(out.fields_.size());
        for(const auto& row:out.fields_)order.push_back(&row);
        for(const bool inverse:{false,true}){
            std::sort(order.begin(),order.end(),[&](const auto* a,const auto* b){return (inverse?a->field:a->canonical)<(inverse?b->field:b->canonical);});
            for(std::size_t i=1;i<order.size();++i){const auto& a=*order[i-1];const auto& b=*order[i];
                if(graph_detail::overlaps(inverse?a.field:a.canonical,fixupSize(a.kind),inverse?b.field:b.canonical,fixupSize(b.kind))&&!(a==b))return {};
            }
        }
        return out;
    }
};
// Only already proved F bindings may compose with a C-F certificate. The
// section-relative resolver fallback is deliberately not a new mapping source.
inline std::optional<std::uint64_t> provedActual(const GraphResolution& graph,std::uint64_t field,bool endpoint=false){
    if(!graph.evidence.matched)return {};
    std::optional<std::uint64_t> found;
    const auto accept=[&](std::uint64_t value){if(found&&*found!=value)return false;found=value;return true;};
    for(const auto& point:graph.points)if(point.reference==field&&!accept(point.actual))return {};
    for(const auto& point:graph.importPoints)if(point.reference==field&&!accept(point.actual))return {};
    for(const auto& row:graph.ranges)if(field>=row.reference&&
        (field-row.reference<row.size||(endpoint&&field-row.reference==row.size))&&
        !accept(row.actual+(field-row.reference)))return {};
    return found;
}
inline std::optional<std::uint64_t> anchoredTypedActual(const GraphResolution& graph,const Point& point){
    if(!graph.evidence.matched||point.use!=PointUse::TypedModulePointer||!point.bindingSlotOffset)return {};
    const auto anchor=point.field+point.bindingSlotOffset;
    bool complete=false;
    for(const auto& row:graph.ranges)if(anchor>=row.reference&&anchor-row.reference<row.size&&
        8<=row.size-(anchor-row.reference))complete=true;
    if(!complete)return {};
    const auto actual=provedActual(graph,anchor);
    if(!actual||*actual<point.bindingSlotOffset)return {};
    return *actual-point.bindingSlotOffset;
}
inline std::optional<std::uint64_t> composedAddress(const Map& map,const GraphResolution& graph,std::uint64_t canonical,bool endpoint=false){
    const auto field=map.toField(canonical,endpoint);if(!field)return {};
    auto actual=provedActual(graph,*field,endpoint);
    for(const auto& point:map.points())if(point.canonical==canonical&&point.bindingSlotOffset){
        const auto anchored=anchoredTypedActual(graph,point);if(!anchored)return {};
        if(actual&&*actual!=*anchored)return {};actual=anchored;
    }
    return actual;
}
// Called only at a reader's explicit vptr/module-pointer operation. Ordinary
// scalar/PID/account/GameID reads must not call this function, even if their
// numeric value happens to equal a mapped module address.
inline std::optional<std::uint64_t> typedModulePointer(const Map& map,const GraphResolution& graph,
    std::uint64_t actualBase,std::uint64_t actualSize,std::uint64_t value){
    if(!graph.evidence.matched||value<actualBase||value-actualBase>=actualSize)return {};
    const auto actual=value-actualBase;std::optional<std::uint64_t> field;
    const auto accept=[&](std::uint64_t next){if(field&&*field!=next)return false;field=next;return true;};
    for(const auto& row:graph.points)if(row.actual==actual&&!accept(row.reference))return {};
    for(const auto& row:graph.ranges)if(actual>=row.actual&&actual-row.actual<row.size&&
        !accept(row.reference+(actual-row.actual)))return {};
    for(const auto& point:map.points())if(point.bindingSlotOffset){
        const auto anchored=anchoredTypedActual(graph,point);
        if(anchored&&*anchored==actual&&!accept(point.field))return {};
    }
    if(!field)return {};
    const auto canonical=map.toCanonical(*field);
    if(!canonical||!map.typedPointerRoot(*canonical,*field)||*canonical>(std::numeric_limits<std::uint64_t>::max)()-actualBase)return {};
    return actualBase+*canonical;
}
} // namespace ymcc::steamcompat::coordinates
