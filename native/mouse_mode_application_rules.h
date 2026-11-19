#pragma once
#include "mouse_mode_policy.h"
#include "game_application_catalog.h"
#include <utility>
namespace ymcc::mousePolicy {
struct AutoOverrides {
 bool valid=true;std::uint64_t epoch=0;
 std::vector<std::wstring> blacklist,whitelist;
};
inline bool matchesAutoOverrides(const std::wstring& image,const std::vector<std::wstring>& rules){
 const auto name=ymcc::gameApplications::base(image);
 return std::any_of(rules.begin(),rules.end(),[&](const auto& rule){return ymcc::gameApplications::base(rule)==name;});
}
inline Context applyAutoOverrides(Context context,const std::wstring& image,const AutoOverrides& rules){
 context.customRulesKnown=rules.valid;context.customRulesEpoch=rules.epoch;
 if(!context.identityKnown||image.empty())return context;
 context.customRule=matchesAutoOverrides(image,rules.blacklist)?-1:
  matchesAutoOverrides(image,rules.whitelist)?1:0;
 return context;
}
struct GameContextRules{
 bool observationKnown=false;std::uint32_t targetPid=0;std::uint64_t targetCreated=0,rulesEpoch=0;
 std::wstring targetPath;std::vector<std::wstring> whitelist,configured;
 std::string observation;
};
// Lists are borrowed only during this synchronous call; Context stores values.
inline Context classifyApplicationsBorrowed(Context context,const std::wstring& image,const GameContextRules& rules,
 const std::vector<std::wstring>& whitelist,const std::vector<std::wstring>& configured){
 using namespace ymcc::gameApplications;
 context.rulesEpoch=rules.rulesEpoch;context.gameTargetPid=rules.targetPid;context.gameObservation=rules.observation;
 if(!context.identityKnown||image.empty())return context;
 // Reuse one normalized executable name across all rule sets in this read.
 const auto executable=base(image);
 const auto matchesName=[&](const std::vector<std::wstring>& patterns){
  return std::any_of(patterns.begin(),patterns.end(),[&](const auto& rule){return patternMatches(executable,rule);});
 };
 context.emulator=matchesName(emulators());context.gameLibrary=!context.emulator&&matchesName(gameLibraries());
 context.classificationKnown=rules.observationKnown;
 // Whitelisted executable names are explicit games, including renamed emulators.
 // A game blacklist alone does not exclude ordinary browser/office mouse use.
 context.game=matchesName(whitelist)||matchesName(configured)||
  // Keep a positively identified game blocked during a shared observation refresh.
  // PID, process birth and full path must still identify the current foreground.
  (context.pid==rules.targetPid&&context.created!=0&&context.created==rules.targetCreated&&
   !rules.targetPath.empty()&&image==rules.targetPath);
 context.applicationCategory=context.emulator?"emulator":context.gameLibrary?"game-library":context.game?"game":"ordinary";
 return context;
}
inline Context classifyApplications(Context context,const std::wstring& image,const GameContextRules& rules){
 return classifyApplicationsBorrowed(std::move(context),image,rules,rules.whitelist,rules.configured);
}
}
