#include "ports.h"
#include "source/tools/custom_steam_library_storage_isolation.h"
#define wmain unusedOriginalWorkerMain
#include "source/native/custom-steam-library/steam_artwork_lab.cpp"
#undef wmain
extern "C" const char* wsFixtureWorker(const char* raw){
 static thread_local std::string answer;auto input=json::parse(raw);const auto data=fs::path(toWide(input.at("dataRoot").get<std::string>()));const auto shortcut=fs::path(toWide(input.at("shortcuts").get<std::string>()));const auto output=fs::path(toWide(input.at("output").get<std::string>()));
 wsfixture::path(data);wsfixture::path(shortcut);wsfixture::path(output);g_workerDataRoot=data;const SteamShortcutTarget target{shortcut,"19627"};json reply;
 try{std::set<std::string> selected;for(const auto& p:input.value("selected",json::array())){const auto path=fs::path(toWide(p.get<std::string>()));wsfixture::path(path);selected.insert(canonicalPathKey(path));}const bool explicitSelection=!selected.empty();json result;
 if(input.value("commit",false)&&input.value("stale",false)){
  auto planned=buildSteamLibraryAddPlan(data,data/L"state"/L"library-scan.json",target,output,selected,explicitSelection);const auto original=readBinaryFile(shortcut,16u<<20);auto edited=BinaryVdfDocument::parse(original);edited.root().objectValue.push_back({"OwnedExternalEdit",vdfString("after-real-plan-before-original-guard")});const auto changed=edited.serialize();writeAtomic(shortcut,changed);
  writeJsonAtomic(data/L"state"/L"fixture-stale-injection.json",{{"beforeSha256",sha256(original)},{"externalEditSha256",sha256(changed)},{"readyToAdd",planned["summary"].value("readyToAdd",0)}});
  result=commitPreparedSteamAddPlan(data,target,output,input.value("confirm",false),planned);
 }else result=input.at("commit").get<bool>()?commitSteamLibraryAddPlan(data,data/L"state"/L"library-scan.json",target,output,input.value("confirm",false),selected,explicitSelection):buildSteamLibraryAddPlan(data,data/L"state"/L"library-scan.json",target,output,selected,explicitSelection);
 reply={{"exitCode",0},{"output","OWNED_ORIGINAL_WORKER_PLAN_OR_COMMIT"},{"result",result}};
 }
 catch(const std::exception& e){reply={{"exitCode",1},{"output",e.what()}};}
 answer=reply.dump();return answer.c_str();
}
// Read-only inspection uses the actual product parser and serializer.
static json fixtureVdfSummary(const fs::path& target){
 wsfixture::path(target);const auto raw=readBinaryFile(target,16u<<20);auto doc=BinaryVdfDocument::parse(raw);auto* list=vdfFindObject(doc.root(),"shortcuts");if(!list)throw std::runtime_error("fixture VDF missing shortcuts");json indexes=json::array();for(const auto& row:list->objectValue)indexes.push_back(row.first);json rootKeys=json::array();for(const auto& row:doc.root().objectValue)rootKeys.push_back(row.first);
 return {{"sha256",sha256(raw)},{"roundtripSha256",sha256(doc.serialize())},{"count",readShortcuts(target,"19627").size()},{"indexes",indexes},{"rootKeys",rootKeys}};
}
extern "C" const char* wsFixtureVdf(const char* raw){
 static thread_local std::string output;const auto r=json::parse(raw);const auto target=fs::path(toWide(r.at("target").get<std::string>()));wsfixture::path(target);const auto op=r.at("op").get<std::string>();json result;
 if(op=="seed"){
  if(fs::exists(target))throw std::runtime_error("fresh owned VDF seed required");auto doc=BinaryVdfDocument::emptySteamShortcuts();auto* shortcuts=vdfFindObject(doc.root(),"shortcuts");
  auto first=steamShortcutNode(-2147483001,"Untouched Alpha",quotedSteamPath(target.parent_path()/L"LegacyA.exe"),quotedSteamPath(target.parent_path()),123,{"original-tag","untouched"});
  BinaryVdfNode wide;wide.type=5;wide.stringValue="Owned wide metadata";BinaryVdfNode number;number.type=7;number.u64Value=0x1122334455667788ULL;
  first.objectValue.push_back({"UnknownNested",vdfObject({{"UnknownText",vdfString("preserve-byte-order")},{"Wide",wide},{"Counter",number}})});
  auto second=steamShortcutNode(-2147483002,"Untouched Beta",quotedSteamPath(target.parent_path()/L"LegacyB.exe"),quotedSteamPath(target.parent_path()),456,{"beta"});second.objectValue.push_back({"UnknownBoolean",vdfInt32(77)});
  shortcuts->objectValue.push_back({"7",first});shortcuts->objectValue.push_back({"2",second});doc.root().objectValue.insert(doc.root().objectValue.begin(),{"UnknownRootBefore",vdfString("before-shortcuts")});doc.root().objectValue.push_back({"UnknownRootAfter",vdfString("after-shortcuts")});writeAtomic(target,doc.serialize());
  result=fixtureVdfSummary(target);
 }else if(op=="inspect"){
  result=fixtureVdfSummary(target);if(r.contains("before")){const auto before=fs::path(toWide(r.at("before").get<std::string>()));wsfixture::path(before);const auto original=readBinaryFile(before,16u<<20),bytes=readBinaryFile(target,16u<<20);auto old=BinaryVdfDocument::parse(original),now=BinaryVdfDocument::parse(bytes);auto* oldItems=vdfFindObject(old.root(),"shortcuts");auto* nowItems=vdfFindObject(now.root(),"shortcuts");const auto beforeCount=oldItems->objectValue.size();const bool oneAdded=nowItems->objectValue.size()==beforeCount+1;result["oneAdded"]=oneAdded;result["originalBytesPreserved"]=false;if(oneAdded){result["addedIndex"]=nowItems->objectValue.back().first;nowItems->objectValue.pop_back();result["originalBytesPreserved"]=now.serialize()==original;}json names=json::array(),exes=json::array();for(const auto& s:readShortcuts(target,"19627")){names.push_back(s.appName);exes.push_back(s.exe);}result["names"]=names;result["executables"]=exes;}
 }else throw std::runtime_error("unknown owned VDF helper operation");output=result.dump();return output.c_str();
}
