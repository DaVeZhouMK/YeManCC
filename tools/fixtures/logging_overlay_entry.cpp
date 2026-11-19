#define NOMINMAX
#include <windows.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include "log_ai_index.h"
using json=nlohmann::json;
namespace reference {using EvidenceJson=nlohmann::json;
// ACTUAL_REFERENCE_DELIVERY
}
static json checks=json::array();
static void check(bool v,const char* n){checks.push_back({{"name",n},{"pass",v}});}
int main(int argc,char** argv){try{
 if(argc!=3||(std::strcmp(argv[2],"necessary")&&std::strcmp(argv[2],"current")))return 2;
 std::ifstream f(argv[1],std::ios::binary);json root;f>>root;const auto source=root.at("evidence");
 const auto finish=[&](){unsigned failures=0;for(const auto& c:checks)failures+=!c["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"nativeProjected",ymcc::logexport::rendererDeliveryEvidence(source)},{"productExecution",false}}.dump()<<std::endl;return failures?3:0;};
 if(!std::strcmp(argv[2],"necessary")){auto old=reference::rendererDeliveryEvidence(source);check(old.contains("overlaySessionEntry"),"necessary original Native projection retains session entry counts");return finish();}
 using ymcc::logexport::rendererDeliveryEvidence;
 auto full=rendererDeliveryEvidence(source),view=full.at("overlaySessionEntry");
 check(view["status"]=="projected"&&view["known"]==true&&view["counts"]==source["overlaySessionEntry"]["counts"],"actual frontend counters survive complete Native export projection");
 auto ordinary=full;ordinary.erase("overlaySessionEntry");check(ordinary==reference::rendererDeliveryEvidence(source),"all original sender fields and channels remain exact");
 auto oldSource=source;oldSource.erase("overlaySessionEntry");auto missing=rendererDeliveryEvidence(oldSource)["overlaySessionEntry"];
 check(missing["status"]=="not-reported"&&missing["known"]==false&&missing["counts"]["activePass"].is_null(),"older renderer is unknown rather than measured zero");
 auto fixture=source;fixture["overlaySessionEntry"]=nullptr;check(rendererDeliveryEvidence(fixture)["overlaySessionEntry"]["status"]=="reported-null","explicit null is preserved separately from absence");
 fixture=source;for(auto& value:fixture["overlaySessionEntry"]["counts"].items())value.value()=0;auto zero=rendererDeliveryEvidence(fixture)["overlaySessionEntry"];
 check(zero["known"]==true&&zero["counts"]["activePass"]==0&&zero["zeroIsNoInputProof"]==false&&zero["actionOrDeliveryProven"]==false,"reported zero remains a session comparison count without input or action proof");
 fixture=source;fixture["overlaySessionEntry"]["counts"].erase("activePass");auto partial=rendererDeliveryEvidence(fixture)["overlaySessionEntry"];
 check(partial["counterFieldsKnown"]==3&&partial["counts"]["activePass"].is_null()&&partial["status"]=="partial-counters","partial counter objects preserve per-field unknown values");
 bool invalid=true;for(const auto& bad:json::array({true,-1,1.0,"1",json::array(),json::object(),9007199254740992ULL})){fixture=source;fixture["overlaySessionEntry"]["counts"]["activePass"]=bad;auto v=rendererDeliveryEvidence(fixture)["overlaySessionEntry"];invalid=invalid&&v["counts"]["activePass"].is_null()&&v["counterFieldsKnown"]==3;}
 check(invalid,"invalid number types signs and overflow never become zero");
 fixture=source;for(auto& value:fixture["overlaySessionEntry"]["counts"].items())value.value()=9007199254740991ULL;auto max=rendererDeliveryEvidence(fixture)["overlaySessionEntry"];
 check(max["known"]==true&&max["atLimit"]["activePass"]==true&&max["counts"]["activeReject"]==9007199254740991ULL,"saturation bound stays exact and explicitly at limit");
 bool badSchema=true;for(const auto& bad:json::array({true,2,"1",nullptr})){fixture=source;fixture["overlaySessionEntry"]["schema"]=bad;badSchema=badSchema&&rendererDeliveryEvidence(fixture)["overlaySessionEntry"]["status"]=="unsupported-schema";}
 check(badSchema,"unsupported nested schema remains unknown");
 fixture=source;fixture["overlaySessionEntry"]["scope"]="action-success";check(rendererDeliveryEvidence(fixture)["overlaySessionEntry"]["status"]=="unsupported-scope-or-limit","unrecognized scope cannot relabel counts as actions");
 fixture=source;fixture["overlaySessionEntry"]["session"]="private-id";check(!rendererDeliveryEvidence(fixture)["overlaySessionEntry"].contains("session"),"fixed projection does not export session IDs or arbitrary fields");
 check(ymcc::logexport::aiReadingGuide().contains("rendererOverlaySessionEntry"),"actual compiled guide exposes field scope and limits");
 return finish();
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 4;}}
