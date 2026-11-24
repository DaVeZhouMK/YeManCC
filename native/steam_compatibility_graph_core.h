#pragma once
#include "steam_compatibility_core.h"
#include <optional>
#include <memory>
#include <map>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
namespace ymcc::steamcompat {
enum class RangeKind { Code, Data, Pdata, Unwind, Literal, SourceFile };
enum class FixupKind { Relative32, Absolute64, Rva32, Rva64, Relative8 };
struct GraphRange {
    std::uint64_t refRva = 0;
    const unsigned char* bytes = nullptr;
    std::size_t size = 0;
    RangeKind kind = RangeKind::Data;
    std::string_view sourceSuffix{};
    bool loadedRequired = false;
};
struct GraphFixup {
    std::uint64_t fieldRva = 0, originEndRva = 0, targetRva = 0;
    FixupKind kind = FixupKind::Relative32;
};
struct GraphImport {
    std::uint64_t slotRva = 0, lookupRva = 0, nameRva = 0, descriptorRva = 0,
        lookupTableRva = 0, firstThunkRva = 0, libraryNameRva = 0;
    std::string_view library{}, symbol{};
};
struct GraphSpec {
    std::span<const GraphRange> ranges;
    std::span<const GraphFixup> fixups;
    std::span<const SectionSpec> sections;
    std::uint64_t preferredBase = 0;
    std::span<const GraphImport> imports{};
};
inline std::size_t fixupSize(FixupKind kind) noexcept {
    return kind==FixupKind::Relative8?1:(kind == FixupKind::Absolute64 || kind == FixupKind::Rva64 ? 8 : 4);
}
struct BoundRange { std::uint64_t reference = 0, actual = 0; std::size_t size = 0; RangeKind kind = RangeKind::Data; };
struct BoundPoint { std::uint64_t reference = 0, actual = 0; };
struct GraphStorage { std::vector<GraphRange> ranges; std::vector<GraphFixup> fixups; std::vector<SectionSpec> sections; std::vector<GraphImport> imports; };
struct GraphResolution {
    std::shared_ptr<const GraphStorage> storage;
    Evidence evidence;
    GraphSpec specification;
    std::vector<BoundRange> ranges;
    std::vector<BoundPoint> points;
    // Only exact, independently checked import bytes and explicit base points.
    std::vector<BoundRange> importRanges;
    std::vector<BoundPoint> importPoints;
    std::vector<std::uint64_t> verifiedImports;
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
inline bool overlaps(std::uint64_t a,std::size_t n,std::uint64_t b,std::size_t m){
    return n&&m&&(a<=b?b-a<n:a-b<m);
}
inline bool sameImport(const GraphImport& a,const GraphImport& b){
    return a.slotRva==b.slotRva&&a.lookupRva==b.lookupRva&&a.nameRva==b.nameRva&&
        a.descriptorRva==b.descriptorRva&&a.lookupTableRva==b.lookupTableRva&&
        a.firstThunkRva==b.firstThunkRva&&a.libraryNameRva==b.libraryNameRva&&a.library==b.library&&a.symbol==b.symbol;
}
inline bool asciiName(std::string_view value,std::size_t maximum){
    if(value.empty()||value.size()>maximum)return false;
    for(const unsigned char c:value)if(c<0x20||c>0x7e)return false;
    return true;
}
inline bool libraryEqual(std::string_view a,std::string_view b){
    if(!asciiName(a,255)||!asciiName(b,255)||a.size()!=b.size())return false;
    const auto lower=[](unsigned char c){return c>='A'&&c<='Z'?c+('a'-'A'):c;};
    for(std::size_t i=0;i<a.size();++i)if(lower(a[i])!=lower(b[i]))return false;
    return true;
}
inline bool importShape(const GraphImport& item){
    const auto rva=[](std::uint64_t value){return value&&value<=0xffffffffULL;};
    if(!rva(item.slotRva)||!rva(item.lookupRva)||!rva(item.nameRva)||!rva(item.descriptorRva)||
        !rva(item.firstThunkRva)||!rva(item.libraryNameRva)||item.lookupTableRva>0xffffffffULL||
        !asciiName(item.library,255)||!asciiName(item.symbol,511)||item.symbol.front()=='#')return false;
    const auto base=item.lookupTableRva?item.lookupTableRva:item.firstThunkRva;
    return item.slotRva>=item.firstThunkRva&&item.lookupRva>=base&&
        item.slotRva-item.firstThunkRva==item.lookupRva-base&&
        (item.slotRva-item.firstThunkRva)%8==0&&(item.slotRva-item.firstThunkRva)/8<65536;
}
struct ImportPart { std::uint64_t rva;std::size_t size; };
inline std::array<ImportPart,7> importParts(const GraphImport& item){
    return {{{item.slotRva,8},{item.lookupRva,8},{item.descriptorRva,4},
        {item.descriptorRva+12,4},{item.descriptorRva+16,4},
        {item.nameRva+2,item.symbol.size()+1},{item.libraryNameRva,item.library.size()+1}}};
}
inline bool importPartsCompatible(const GraphImport& a,const GraphImport& b){
    const auto left=importParts(a),right=importParts(b);
    for(std::size_t i=0;i<left.size();++i)for(std::size_t j=0;j<right.size();++j){
        if(!overlaps(left[i].rva,left[i].size,right[j].rva,right[j].size))continue;
        if(left[i].rva!=right[j].rva||left[i].size!=right[j].size)return false;
        if(i<2&&j<2){if(a.nameRva!=b.nameRva)return false;continue;}
        if(i!=j)return false;
        if((i==2&&a.lookupTableRva!=b.lookupTableRva)||(i==3&&a.libraryNameRva!=b.libraryNameRva)||
            (i==4&&a.firstThunkRva!=b.firstThunkRva)||(i==5&&a.symbol!=b.symbol)||(i==6&&a.library!=b.library))return false;
    }
    return true;
}
// Protect descriptor gaps, the unmodeled hint and table prefixes as well as
// modeled bytes. None may acquire a translation by section/ordinal guessing.
inline bool importProtected(const GraphSpec& spec,std::uint64_t rva,std::size_t bytes){
    for(const auto& item:spec.imports){
        for(const auto& part:importParts(item))if(overlaps(rva,bytes,part.rva,part.size))return true;
        if(overlaps(rva,bytes,item.descriptorRva,20)||overlaps(rva,bytes,item.nameRva,item.symbol.size()+3)||
            overlaps(rva,bytes,item.firstThunkRva,static_cast<std::size_t>(item.slotRva-item.firstThunkRva+8)))return true;
        const auto base=item.lookupTableRva?item.lookupTableRva:item.firstThunkRva;
        if(overlaps(rva,bytes,base,static_cast<std::size_t>(item.lookupRva-base+8)))return true;
    }
    return false;
}
inline std::optional<std::uint64_t> importAddress(const GraphResolution& result,std::uint64_t rva){
    std::optional<std::uint64_t> value;
    for(const auto& point:result.importPoints)if(point.reference==rva){
        if(value&&*value!=point.actual)return {};value=point.actual;
    }
    for(const auto& range:result.importRanges)if(rva>=range.reference&&rva-range.reference<range.size){
        const auto next=range.actual+(rva-range.reference);if(value&&*value!=next)return {};value=next;
    }
    return value;
}
inline bool importSpan(const GraphResolution& result,std::uint64_t rva,std::uint64_t actual,std::size_t bytes){
    for(std::size_t offset=0;offset<bytes;){
        std::size_t covered=0;
        for(const auto& range:result.importRanges)if(rva+offset>=range.reference&&rva+offset-range.reference<range.size){
            const auto at=rva+offset-range.reference;
            if(range.actual+at!=actual+offset)return false;
            covered=(std::max)(covered,(std::min)(bytes-offset,range.size-static_cast<std::size_t>(at)));
        }
        if(!covered)return false;offset+=covered;
    }
    return bytes!=0;
}
inline bool referenceValue(const GraphSpec& spec,std::uint64_t rva,const void* expected,std::size_t bytes,bool required=false){
    bool any=false;
    std::size_t covered=0;
    const auto* wanted=static_cast<const unsigned char*>(expected);
    for(const auto& range:spec.ranges){
        if(!overlaps(rva,bytes,range.refRva,range.size))continue;
        any=true;
        const auto offset=range.refRva>rva?static_cast<std::size_t>(range.refRva-rva):0;
        const auto start=rva>range.refRva?static_cast<std::size_t>(rva-range.refRva):0;
        const auto length=(std::min)(bytes-offset,range.size-start);
        if(!range.bytes||std::memcmp(range.bytes+start,wanted+offset,length)!=0)return false;
        if(offset<=covered)covered=(std::max)(covered,offset+length);
    }
    if(!any)return !required;
    // All overlaps were checked, even after full coverage. Revisit only when
    // split ranges may have arrived before the prefix that connects them.
    while(covered<bytes){
        const auto before=covered;
        for(const auto& range:spec.ranges){
            const auto next=rva+covered;
            if(next>=range.refRva&&next-range.refRva<range.size){
                const auto remaining=range.size-static_cast<std::size_t>(next-range.refRva);
                covered+=(std::min)(bytes-covered,remaining);
                if(covered==bytes)return true;
            }
        }
        if(covered==before)return false;
    }
    return true;
}
inline bool referenceImport(const GraphSpec& spec,const GraphImport& item){
    const std::array<std::uint32_t,3> fields{{static_cast<std::uint32_t>(item.lookupTableRva),
        static_cast<std::uint32_t>(item.libraryNameRva),static_cast<std::uint32_t>(item.firstThunkRva)}};
    const std::array<std::uint64_t,3> at{{item.descriptorRva,item.descriptorRva+12,item.descriptorRva+16}};
    if(!importShape(item)||!referenceValue(spec,item.slotRva,&item.nameRva,8,true)||
        !referenceValue(spec,item.lookupRva,&item.nameRva,8))return false;
    for(std::size_t i=0;i<fields.size();++i)if(!referenceValue(spec,at[i],&fields[i],4))return false;
    const std::string symbol(item.symbol),library(item.library);
    return referenceValue(spec,item.nameRva+2,symbol.c_str(),symbol.size()+1)&&
        referenceValue(spec,item.libraryNameRva,library.c_str(),library.size()+1);
}
// A hint has no symbol identity. Only an already modeled complete hint/name
// block may expose its two advisory bytes; unmodeled hints remain unmapped.
inline bool referenceImportHint(const GraphSpec& spec,const GraphImport& item,
    std::array<unsigned char,2>& hint){
    const std::string symbol(item.symbol);
    if(!referenceValue(spec,item.nameRva+2,symbol.c_str(),symbol.size()+1,true))return false;
    for(std::size_t i=0;i<hint.size();++i){bool found=false;
        for(const auto& range:spec.ranges)if(item.nameRva+i>=range.refRva&&item.nameRva+i-range.refRva<range.size){
            if(range.kind!=RangeKind::Data||!range.bytes)return false;
            const auto value=range.bytes[item.nameRva+i-range.refRva];
            if(found&&hint[i]!=value)return false;hint[i]=value;found=true;
        }
        if(!found)return false;
    }
    return true;
}
inline bool validFixups(std::span<const GraphFixup> fields){
    std::vector<const GraphFixup*> order;order.reserve(fields.size());
    for(const auto& field:fields){if(field.fieldRva>(std::numeric_limits<std::uint64_t>::max)()-fixupSize(field.kind))return false;order.push_back(&field);}
    std::sort(order.begin(),order.end(),[](const auto* a,const auto* b){return a->fieldRva<b->fieldRva;});
    for(std::size_t i=1;i<order.size();++i){const auto& a=*order[i-1];const auto& b=*order[i];
        if(overlaps(a.fieldRva,fixupSize(a.kind),b.fieldRva,fixupSize(b.kind))&&
            (a.fieldRva!=b.fieldRva||a.originEndRva!=b.originEndRva||a.targetRva!=b.targetRva||a.kind!=b.kind))return false;
    }
    return true;
}
inline bool normalizeImportMetadata(const GraphResolution& result,const ReadRva& read,std::uint64_t rva,void* target,std::size_t bytes){
    for(const auto& item:result.specification.imports){
        std::array<unsigned char,2> hint{};
        if(overlaps(rva,bytes,item.nameRva,2)&&referenceImportHint(result.specification,item,hint)){
            if(std::find(result.verifiedImports.begin(),result.verifiedImports.end(),item.slotRva)==result.verifiedImports.end())return false;
            const auto at=importAddress(result,item.nameRva);
            if(!at||!importSpan(result,item.nameRva,*at,item.symbol.size()+3))return false;
            std::array<unsigned char,2> observed{};std::vector<char> symbol(item.symbol.size()+1);
            if(!read(*at,observed.data(),observed.size())||!read(*at+2,symbol.data(),symbol.size())||
                symbol.back()!=0||std::string_view(symbol.data(),symbol.size()-1)!=item.symbol)return false;
            const auto low=(std::max)(rva,item.nameRva),high=(std::min)(rva+bytes,item.nameRva+2);
            std::memcpy(static_cast<unsigned char*>(target)+(low-rva),hint.data()+(low-item.nameRva),static_cast<std::size_t>(high-low));
        }
        if(!overlaps(rva,bytes,item.libraryNameRva,item.library.size()+1))continue;
        if(std::find(result.verifiedImports.begin(),result.verifiedImports.end(),item.slotRva)==result.verifiedImports.end())return false;
        const auto at=importAddress(result,item.libraryNameRva);if(!at||!importSpan(result,item.libraryNameRva,*at,item.library.size()+1))return false;
        std::vector<char> name(item.library.size()+1);
        if(!read(*at,name.data(),name.size())||name.back()!=0||!libraryEqual(std::string_view(name.data(),name.size()-1),item.library))return false;
        const std::string reference(item.library);
        const auto low=(std::max)(rva,item.libraryNameRva),high=(std::min)(rva+bytes,item.libraryNameRva+name.size());
        std::memcpy(static_cast<unsigned char*>(target)+(low-rva),reference.c_str()+(low-item.libraryNameRva),static_cast<std::size_t>(high-low));
    }
    return true;
}
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
    std::vector<unsigned char>* maskOut=nullptr,const GraphResolution* resolved=nullptr) {
    if(!range.bytes||!range.size||range.size>1024*1024||actual>=image.imageSize||range.size>image.imageSize-actual)return false;
    std::vector<unsigned char> bytes(range.size),mask(range.size,1);
    if(!image.read(actual,bytes.data(),bytes.size()))return false;
    if(resolved&&!normalizeImportMetadata(*resolved,image.read,range.refRva,bytes.data(),bytes.size()))return false;
    // Literal bytes, including their terminator, never contain masked fixups.
    if(range.kind!=RangeKind::Literal) for(const auto& field:specification.fixups){const auto size=fixupSize(field.kind);
        if(field.fieldRva>=range.refRva&&field.fieldRva-range.refRva<range.size){
            if(size>range.size-(field.fieldRva-range.refRva))return false;
            std::fill_n(mask.begin()+static_cast<std::ptrdiff_t>(field.fieldRva-range.refRva),size,static_cast<unsigned char>(0));
        }
    }
    for(std::size_t i=0;i<range.size;++i)if(mask[i]&&bytes[i]!=range.bytes[i])return false;
    if(maskOut)*maskOut=std::move(mask);return true;
}
// __FILE is a source identity, not a stable build-worker directory. Only a
// complete, bounded absolute path with the pinned source suffix may rebind.
inline bool sourcePathMatches(std::string_view path,std::string_view suffix){
    const auto slash=[](char c){return c=='/'?'\\':c;};
    if(path.size()<4||suffix.empty()||slash(suffix.front())!='\\'||path.size()<suffix.size()+3||
        !((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z'))||path[1]!=':'||slash(path[2])!='\\')return false;
    for(std::size_t i=0;i<path.size();++i){const auto c=static_cast<unsigned char>(path[i]);
        if(c<32||c>126||c=='"'||c=='<'||c=='>'||c=='|'||c=='?'||c=='*'||(c==':'&&i!=1))return false;
    }
    std::size_t segment=3;
    for(std::size_t i=3;i<=path.size();++i)if(i==path.size()||slash(path[i])=='\\'){
        const auto part=path.substr(segment,i-segment);if(part.empty()||part=="."||part=="..")return false;segment=i+1;
    }
    const auto at=path.size()-suffix.size();
    for(std::size_t i=0;i<suffix.size();++i)if(slash(path[at+i])!=slash(suffix[i]))return false;
    return true;
}
inline bool sourceFileEqual(const Image& image,const GraphResolution& result,const GraphRange& range,std::uint64_t actual,
    std::size_t* observedSize=nullptr){
    if(!range.bytes||range.size<5||range.size>512||range.bytes[range.size-1]!=0||
        !sourcePathMatches(std::string_view(reinterpret_cast<const char*>(range.bytes),range.size-1),range.sourceSuffix))return false;
    const auto* source=containingSection(result.specification.sections,range.refRva,range.size);
    const auto* destination=containingSection(result.actualSections,actual,1);
    if(!source||!destination||(source->characteristics&0xe0000000u)!=0x40000000u||
        (destination->characteristics&0xe0000000u)!=0x40000000u||!typedLocation(result,range.refRva,actual,1))return false;
    for(const auto& field:result.specification.fixups)if(field.fieldRva>=range.refRva&&field.fieldRva-range.refRva<range.size)return false;
    std::array<unsigned char,512> bytes{};
    const auto extent=(std::max)(destination->virtualSize,destination->rawSize);
    const auto remaining=(std::min)(extent-(actual-destination->rva),result.imageSize-actual);
    const auto count=static_cast<std::size_t>((std::min)(static_cast<std::uint64_t>(bytes.size()),remaining));
    if(!count||!image.read(actual,bytes.data(),count))return false;
    const auto nul=std::find(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(count),static_cast<unsigned char>(0));
    if(nul==bytes.begin()+static_cast<std::ptrdiff_t>(count))return false;
    const auto length=static_cast<std::size_t>(nul-bytes.begin());
    if(!sourcePathMatches(std::string_view(reinterpret_cast<const char*>(bytes.data()),length),range.sourceSuffix))return false;
    if(observedSize)*observedSize=length+1;return true;
}
struct Buffer { SectionSpec section;std::vector<unsigned char> bytes; };
struct Resolver {
    const Image& image;GraphResolution result;std::vector<std::optional<std::uint64_t>> bindings;
    std::vector<bool> importBindings;
    std::vector<Buffer> executable;bool changed=false;const char* failure=nullptr;
    explicit Resolver(const Image& value,const GraphSpec& specification):image(value){
        result.specification=specification;result.actualPreferredBase=image.preferredBase;result.imageSize=image.imageSize;
        result.actualSections.assign(image.sections.begin(),image.sections.end());bindings.resize(specification.ranges.size());
        importBindings.resize(specification.imports.size());
    }
    bool importStorage(std::uint64_t reference,std::uint64_t actual,std::size_t size){
        const auto* old=containingSection(result.specification.sections,reference,size);
        const auto* next=containingSection(result.actualSections,actual,size);
        return old&&next&&(old->characteristics&0xe0000000u)==0x40000000u&&
            (next->characteristics&0xe0000000u)==0x40000000u&&typedLocation(result,reference,actual,size);
    }
    bool registerImportSpan(std::uint64_t reference,std::uint64_t actual,std::size_t size){
        if(!importStorage(reference,actual,size))return false;
        for(const auto& range:result.ranges){
            const auto low=(std::max)(reference,range.reference);
            if(overlaps(reference,size,range.reference,range.size)&&actual+(low-reference)!=range.actual+(low-range.reference))return false;
        }
        for(const auto& point:result.points)if(point.reference>=reference&&point.reference-reference<size&&
            point.actual!=actual+(point.reference-reference))return false;
        // Shared descriptor/name blocks are recorded once when their mapping agrees.
        for(const auto& range:result.importRanges)if(range.reference==reference&&range.actual==actual&&range.size==size)return true;
        result.importRanges.push_back({reference,actual,size,RangeKind::Data});
        result.ranges.push_back({reference,actual,size,RangeKind::Data});changed=true;return true;
    }
    bool registerImportPoint(std::uint64_t reference,std::uint64_t actual){
        if(!importStorage(reference,actual,1)||!bindPoint(reference,actual))return false;
        for(const auto& point:result.importPoints)if(point.reference==reference)return point.actual==actual;
        result.importPoints.push_back({reference,actual});return true;
    }
    bool proveImport(std::size_t index,std::uint64_t entry,std::uint64_t actual){
        if(image.loadedImage||index>=result.specification.imports.size()||!image.read)return false;
        const auto& item=result.specification.imports[index];const ImportSlot* row=nullptr;
        if(!referenceImport(result.specification,item)||(entry!=item.slotRva&&entry!=item.lookupRva)||!importPartsCompatible(item,item))return false;
        for(const auto& candidate:image.imports)if((entry==item.slotRva?candidate.rva:candidate.lookupRva)==actual){
            if(row)return false;row=&candidate;
        }
        if(!row)return false;
        // Lookup facts and file IAT values are distinct observations. Never
        // substitute the parser's lookup value for a read of the original IAT.
        const GraphImport facts{row->rva,row->lookupRva,row->originalThunk,row->descriptorRva,
            row->lookupTableRva,row->firstThunkRva,row->libraryNameRva,row->library,row->symbol};
        if(!importShape(facts)||!importPartsCompatible(facts,facts)||!libraryEqual(item.library,facts.library)||item.symbol!=facts.symbol)return false;
        for(const auto& other:image.imports)if(&other!=row&&(other.rva==row->rva||other.lookupRva==row->lookupRva))return false;
        std::uint64_t iat=0,lookup=0;
        if(!image.read(facts.slotRva,&iat,8)||!image.read(facts.lookupRva,&lookup,8)||iat!=row->originalThunk||lookup!=row->originalThunk)return false;
        const std::array<std::uint32_t,3> expected{{row->lookupTableRva,row->libraryNameRva,row->firstThunkRva}};
        for(std::size_t i=0;i<expected.size();++i){std::uint32_t value=0;
            const std::uint64_t offset=i==0?0:(i==1?12:16);
            if(!image.read(facts.descriptorRva+offset,&value,4)||value!=expected[i])return false;
        }
        std::vector<char> symbol(item.symbol.size()+1),library(item.library.size()+1);
        if(!image.read(facts.nameRva+2,symbol.data(),symbol.size())||symbol.back()!=0||
            std::string_view(symbol.data(),symbol.size()-1)!=item.symbol||
            !image.read(facts.libraryNameRva,library.data(),library.size())||library.back()!=0||
            !libraryEqual(std::string_view(library.data(),library.size()-1),item.library))return false;
        const auto before=importParts(item),after=importParts(facts);
        for(std::size_t i=0;i<before.size();++i)if(before[i].size!=after[i].size||!registerImportSpan(before[i].rva,after[i].rva,before[i].size))return false;
        std::array<unsigned char,2> referenceHint{};
        if(referenceImportHint(result.specification,item,referenceHint)){
            std::array<unsigned char,2> actualHint{};
            if(!image.read(facts.nameRva,actualHint.data(),actualHint.size())||
                !registerImportSpan(item.nameRva,facts.nameRva,actualHint.size()))return false;
        }
        if(!registerImportPoint(item.nameRva,facts.nameRva)||
            !registerImportPoint(item.lookupTableRva?item.lookupTableRva:item.firstThunkRva,
                facts.lookupTableRva?facts.lookupTableRva:facts.firstThunkRva)||
            !registerImportPoint(item.firstThunkRva,facts.firstThunkRva))return false;
        if(!importBindings[index]){importBindings[index]=true;changed=true;
            if(std::find(result.verifiedImports.begin(),result.verifiedImports.end(),item.slotRva)==result.verifiedImports.end())result.verifiedImports.push_back(item.slotRva);}
        return true;
    }
    bool bind(std::size_t index,std::uint64_t actual){
        const auto& range=result.specification.ranges[index];
        if(importProtected(result.specification,range.refRva,range.size)&&!importSpan(result,range.refRva,actual,range.size))return false;
        if(range.kind==RangeKind::Literal){
            const auto* section=containingSection(result.specification.sections,range.refRva,range.size);
            if(!range.bytes||!range.size||range.bytes[range.size-1]!=0||!section||
                (section->characteristics&0xe0000000u)!=0x40000000u)return false;
        }
        if(bindings[index])return *bindings[index]==actual;
        // Variable build roots bind the source-file pointer only. Never invent
        // equal-length byte translations inside a differently sized path.
        const auto bindingSize=range.kind==RangeKind::SourceFile?std::size_t(1):range.size;
        if(range.kind==RangeKind::SourceFile){if(!sourceFileEqual(image,result,range,actual))return false;}
        else if(!typedLocation(result,range.refRva,actual,range.size)||!maskedEqual(image,result.specification,range,actual,nullptr,&result))return false;
        for(const auto& bound:result.ranges){
            const auto low=(std::max)(bound.reference,range.refRva),high=(std::min)(bound.reference+bound.size,range.refRva+bindingSize);
            if(low<high&&bound.actual+(low-bound.reference)!=actual+(low-range.refRva))return false;
        }
        for(const auto& point:result.points)if(point.reference>=range.refRva&&point.reference-range.refRva<bindingSize&&
            point.actual!=actual+(point.reference-range.refRva))return false;
        bindings[index]=actual;result.ranges.push_back({range.refRva,actual,bindingSize,range.kind});changed=true;return true;
    }
    bool bindPoint(std::uint64_t reference,std::uint64_t actual){
        if(actual>=image.imageSize)return false;
        for(const auto& point:result.points)if(point.reference==reference)return point.actual==actual;
        for(const auto& range:result.ranges)if(reference>=range.reference&&reference-range.reference<range.size)
            return range.actual+(reference-range.reference)==actual;
        result.points.push_back({reference,actual});changed=true;return true;
    }
    bool target(std::uint64_t reference,std::uint64_t actual,bool importEntry=false){
        for(std::size_t i=0;i<result.specification.imports.size();++i){const auto& item=result.specification.imports[i];
            if(importEntry&&(reference==item.slotRva||reference==item.lookupRva)&&!proveImport(i,reference,actual))return false;
        }
        if(importProtected(result.specification,reference,1)){
            const auto expected=importAddress(result,reference);
            // A descriptor/name reference may be encountered before its caller.
            // Defer it; it creates no mapping and final propagation rechecks it.
            if(!expected)return !std::all_of(importBindings.begin(),importBindings.end(),[](bool bound){return bound;});
            if(*expected!=actual)return false;
            for(std::size_t i=0;i<result.specification.ranges.size();++i){const auto& range=result.specification.ranges[i];
                if(reference>=range.refRva&&reference-range.refRva<range.size){const auto offset=reference-range.refRva;
                    if(actual<offset||!bind(i,actual-offset))return false;}
            }
            return true;
        }
        bool contained=false;
        for(std::size_t i=0;i<result.specification.ranges.size();++i){const auto& range=result.specification.ranges[i];
            if(reference>=range.refRva&&reference-range.refRva<range.size){
                contained=true;const auto offset=reference-range.refRva;
                if(range.kind==RangeKind::SourceFile&&offset!=0)return false;
                if(actual<offset||!bind(i,actual-offset))return false;
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
                std::uint64_t actual=0;if(!fieldValue(image,result,source,field,actual)||
                    !target(field.targetRva,actual,range.kind==RangeKind::Code||importSpan(result,range.refRva,*bindings[i],range.size))){result.evidence.failedRva=field.fieldRva;return false;}
            }
        }
        // Exception table positions can change independently of their code.
        // The uniquely resolved BeginAddress selects its real pdata entry.
        for(std::size_t i=0;i<spec.ranges.size();++i)if(!bindings[i]&&spec.ranges[i].kind==RangeKind::Pdata&&spec.ranges[i].size==12){
            std::uint32_t referenceBegin=0;std::memcpy(&referenceBegin,spec.ranges[i].bytes,4);
            const auto begin=translateRva(result,referenceBegin);if(!begin)continue;
            const RuntimeFunction* found=nullptr;
            for(const auto& function:image.functions)if(function.begin==*begin){if(found){result.evidence.failedRva=spec.ranges[i].refRva;return false;}found=&function;}
            if(found&&!bind(i,found->tableRva)){result.evidence.failedRva=spec.ranges[i].refRva;return false;}
        }
        for(std::size_t i=0;i<spec.ranges.size();++i)if(!bindings[i]&&importProtected(spec,spec.ranges[i].refRva,spec.ranges[i].size)){
            const auto at=importAddress(result,spec.ranges[i].refRva);
            if(at&&importSpan(result,spec.ranges[i].refRva,*at,spec.ranges[i].size)&&!bind(i,*at))return false;
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
    GraphResolution run(bool calibrated=false){
        const auto& spec=result.specification;
        if(!image.read||image.sections.empty()||spec.sections.empty()||spec.ranges.empty()||spec.ranges.size()>2048||spec.fixups.size()>32768||spec.imports.size()>2048){
            result.evidence.reason="graph-input-invalid";return std::move(result);
        }
        std::uint64_t bytes=0;
        for(const auto& range:spec.ranges){
            if(!range.bytes||!range.size||range.refRva>(std::numeric_limits<std::uint64_t>::max)()-range.size||range.size>1024*1024||bytes>2*1024*1024-range.size){result.evidence.reason="graph-range-budget";return std::move(result);}bytes+=range.size;
        }
        if(!validFixups(spec.fixups)){result.evidence.reason="graph-field-contract-conflict";return std::move(result);}
        for(std::size_t i=0;i<spec.imports.size();++i){const auto& item=spec.imports[i];
            if(image.loadedImage||!referenceImport(spec,item)||!importPartsCompatible(item,item)){result.evidence.reason="graph-import-contract-invalid";return std::move(result);}
            for(std::size_t j=0;j<i;++j){const auto& old=spec.imports[j];
                if(!importPartsCompatible(old,item)||(old.slotRva==item.slotRva&&!sameImport(old,item))||
                    (old.nameRva==item.nameRva&&old.symbol!=item.symbol)||
                    (old.libraryNameRva==item.libraryNameRva&&old.library!=item.library)||
                    (old.descriptorRva==item.descriptorRva&&(old.lookupTableRva!=item.lookupTableRva||
                        old.firstThunkRva!=item.firstThunkRva||old.libraryNameRva!=item.libraryNameRva||old.library!=item.library))){
                    result.evidence.reason="graph-import-contract-conflict";return std::move(result);}
            }
        }
        if(!calibrated&&!loadCode()){result.evidence.reason="graph-code-sections-unavailable";return std::move(result);}
        std::vector<std::size_t> order;for(std::size_t i=0;i<spec.ranges.size();++i)if(spec.ranges[i].kind==RangeKind::Code)order.push_back(i);
        std::sort(order.begin(),order.end(),[&](auto a,auto b){return spec.ranges[a].size>spec.ranges[b].size;});
        for(const auto index:order){if(bindings[index])continue;const auto candidates=calibrated?std::vector<std::uint64_t>{spec.ranges[index].refRva}:scan(index);if(failure){result.evidence.reason=failure;return std::move(result);}
            if(candidates.size()==1&&!bind(index,candidates.front())){result.evidence.reason="graph-code-binding-conflict";return std::move(result);}
        }
        for(std::size_t round=0;round<spec.ranges.size()+2;++round){
            changed=false;if(!propagate()){result.evidence.reason="graph-reference-contract-incompatible";return std::move(result);}
            if(changed)continue;
            bool fallback=false;
            for(std::size_t i=0;i<spec.ranges.size();++i)if(!bindings[i]&&spec.ranges[i].kind!=RangeKind::Code&&spec.ranges[i].kind!=RangeKind::SourceFile&&
                !importProtected(spec,spec.ranges[i].refRva,spec.ranges[i].size)){
                const auto actual=sectionTranslate(result,spec.ranges[i].refRva);
                if(actual&&bind(i,*actual)){fallback=true;break;}
            }
            if(!fallback)break;
        }
        for(std::size_t i=0;i<importBindings.size();++i)if(!importBindings[i]){
            result.evidence.failedRva=spec.imports[i].slotRva;result.evidence.reason="graph-import-unbound";return std::move(result);
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
    if(graph_detail::importProtected(result.specification,rva,1))return graph_detail::importAddress(result,rva);
    if(endpoint)for(const auto& range:result.importRanges)if(rva==range.reference+range.size)
        return graph_detail::importAddress(result,rva);
    for(const auto& source:result.specification.ranges)if(source.kind==RangeKind::SourceFile&&rva>=source.refRva&&
        (rva-source.refRva<source.size||(endpoint&&rva-source.refRva==source.size))){
        if(rva!=source.refRva)return {};
        for(const auto& bound:result.ranges)if(bound.kind==RangeKind::SourceFile&&bound.reference==rva)return bound.actual;
        return {};
    }
    for(const auto& point:result.points)if(point.reference==rva)return point.actual;
    std::optional<std::uint64_t> value;
    for(const auto& range:result.ranges)if(rva>=range.reference&&(rva-range.reference<range.size||(endpoint&&rva-range.reference==range.size))){
        const auto next=range.actual+(rva-range.reference);if(value&&*value!=next)return {};value=next;
    }
    if(value)return value;
    // Import-protected bytes already returned above. Preserve the original
    // fallback policy for unrelated code, data and readonly contracts.
    return graph_detail::sectionTranslate(result,rva,endpoint);
}
inline GraphResolution resolveGraph(const Image& image,const GraphSpec& specification){
    try{return graph_detail::Resolver(image,specification).run();}
    catch(...){GraphResolution result;result.specification=specification;result.evidence.reason="graph-resolution-exception";return result;}
}
// Used only after the complete historical fixed code/data contracts matched.
// It verifies every graph node, reference and import at those known positions;
// it does not replace that proof with an ambiguous whole-image code search.
inline GraphResolution resolveCalibratedGraph(const Image& image,const GraphSpec& specification){
    try{return graph_detail::Resolver(image,specification).run(true);}
    catch(...){GraphResolution result;result.specification=specification;result.evidence.reason="graph-resolution-exception";return result;}
}
inline GraphResolution resolveGraphs(const Image& image,std::span<const GraphSpec> graphs,bool calibrated=false){
    if(graphs.empty()){GraphResolution result;result.evidence.reason="graph-empty";return result;}
    auto storage=std::make_shared<GraphStorage>();storage->sections.assign(graphs[0].sections.begin(),graphs[0].sections.end());
    const auto preferred=graphs[0].preferredBase;
    for(const auto& graph:graphs){
        if(graph.preferredBase!=preferred){GraphResolution result;result.evidence.reason="graph-reference-base-mismatch";return result;}
        if(graph.sections.size()!=storage->sections.size()){GraphResolution result;result.evidence.reason="graph-reference-sections-mismatch";return result;}
        for(std::size_t i=0;i<graph.sections.size();++i){const auto& a=graph.sections[i];const auto& b=storage->sections[i];
            if(a.rva!=b.rva||a.virtualSize!=b.virtualSize||a.rawSize!=b.rawSize||a.characteristics!=b.characteristics||a.name!=b.name){
                GraphResolution result;result.evidence.reason="graph-reference-sections-mismatch";return result;}
        }
        storage->ranges.insert(storage->ranges.end(),graph.ranges.begin(),graph.ranges.end());
        for(const auto& field:graph.fixups){
            bool duplicate=false;for(const auto& old:storage->fixups)if(graph_detail::overlaps(old.fieldRva,fixupSize(old.kind),field.fieldRva,fixupSize(field.kind))){
                if(old.fieldRva!=field.fieldRva||old.originEndRva!=field.originEndRva||old.targetRva!=field.targetRva||old.kind!=field.kind){GraphResolution result;result.evidence.reason="graph-field-contract-conflict";return result;}duplicate=true;break;
            }
            if(!duplicate)storage->fixups.push_back(field);
        }
        for(const auto& item:graph.imports){bool duplicate=false;
            for(const auto& old:storage->imports)if(old.slotRva==item.slotRva){
                if(!graph_detail::sameImport(old,item)){GraphResolution result;result.evidence.reason="graph-import-contract-conflict";return result;}duplicate=true;break;
            }
            if(!duplicate)storage->imports.push_back(item);
        }
    }
    const GraphSpec combined{storage->ranges,storage->fixups,storage->sections,preferred,storage->imports};
    auto result=calibrated?resolveCalibratedGraph(image,combined):resolveGraph(image,combined);result.storage=std::move(storage);return result;
}
inline ReadRva referenceImageRead(const GraphResolution& result,const ReadRva& actualRead,std::uint64_t actualBase,
    std::uint64_t referenceBase){
    return [&result,actualRead,actualBase,referenceBase](std::uint64_t rva,void* target,std::size_t bytes){
        if(!result.evidence.matched||!actualRead||!target||!bytes||bytes>(std::numeric_limits<std::uint64_t>::max)()-rva)return false;
        // Complete SourceFile contracts are normalized only after independently
        // rereading the current bounded string and pinned suffix. This grants no
        // byte-offset mapping into a different-length build path.
        for(const auto& source:result.specification.ranges)if(source.kind==RangeKind::SourceFile&&
            rva==source.refRva&&bytes==source.size){
            const auto start=translateRva(result,rva);if(!start)return false;
            const Image image{actualRead,actualBase,result.imageSize,result.actualSections};
            if(!graph_detail::sourceFileEqual(image,result,source,*start))return false;
            std::memcpy(target,source.bytes,bytes);return true;
        }
        const auto actual=translateRva(result,rva),last=translateRva(result,rva+bytes-1);
        if(!actual||!last||*last<*actual||*last-*actual!=bytes-1||
            (graph_detail::importProtected(result.specification,rva,bytes)&&!graph_detail::importSpan(result,rva,*actual,bytes))||
            !actualRead(*actual,target,bytes)||!graph_detail::normalizeImportMetadata(result,actualRead,rva,target,bytes))return false;
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
// The disk graph is a location proof, not a substitute for loaded bytes. The
// caller supplies the actual normalized image and its reference contract view.
inline Evidence checkLoadedRequired(const GraphSpec& spec,const Image& normalized,const Image& contract,
    const GraphResolution* resolved=nullptr){
    Evidence proof{true,0,0,0,"graph-loaded-required-matched"};
    if(std::none_of(spec.ranges.begin(),spec.ranges.end(),[](const auto& range){return range.loadedRequired;}))return proof;
    if(resolved&&!resolved->evidence.matched)return {false,0,0,0,"graph-loaded-required-unqualified"};
    GraphResolution fixed;
    if(!resolved){fixed.specification=spec;fixed.imageSize=normalized.imageSize;
        fixed.actualSections.assign(normalized.sections.begin(),normalized.sections.end());resolved=&fixed;}
    Image strict=contract;strict.loadedImage=false;
    try {for(const auto& range:spec.ranges)if(range.loadedRequired){
        proof.failedRva=range.refRva;
        const auto actual=resolved==&fixed?std::optional<std::uint64_t>(range.refRva):translateRva(*resolved,range.refRva);
        if(!actual){proof.matched=false;proof.reason="graph-loaded-required-unbound";return proof;}
        if(resolved!=&fixed){bool bound=false;
            for(const auto& item:resolved->ranges)if(item.reference==range.refRva&&item.actual==*actual&&
                item.size>=(range.kind==RangeKind::SourceFile?std::size_t(1):range.size))bound=true;
            if(!bound){proof.matched=false;proof.reason="graph-loaded-required-unbound";return proof;}
        }
        if(range.kind==RangeKind::SourceFile){std::size_t observed=0;
            if(!graph_detail::sourceFileEqual(normalized,*resolved,range,*actual,&observed)){
                proof.matched=false;proof.reason="graph-loaded-source-file-incompatible";return proof;}
            ++proof.rangesChecked;proof.bytesChecked+=observed;
        }else{
            if(!graph_detail::typedLocation(*resolved,range.refRva,*actual,range.size)){
                proof.matched=false;proof.reason="graph-loaded-required-storage";return proof;}
            const Range required{range.refRva,range.bytes,range.size};
            const auto checked=checkRanges(strict,std::span<const Range>(&required,1));
            proof.rangesChecked+=checked.rangesChecked;proof.bytesChecked+=checked.bytesChecked;
            if(!checked.matched){proof.matched=false;proof.failedRva=checked.failedRva;proof.reason=checked.reason;return proof;}
        }
    }}catch(...){proof.matched=false;proof.reason="graph-loaded-required-exception";return proof;}
    proof.failedRva=0;return proof;
}
}
