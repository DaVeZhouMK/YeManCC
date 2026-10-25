#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/shared_game_media_shortcut.h"
#include <iostream>
using namespace ymcc::media;
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("usage: data-root shortcuts-file; read-only");
    const auto root=fs::path(wide(argv[1])),file=fs::path(wide(argv[2]));Store store(root);const auto local=store.inventory();const auto shortcuts=shortcutInventory(file);const auto combined=store.inventory(shortcuts);std::set<std::string> known;for(const auto& item:local)known.insert(normalized(item.value("executable",std::string{})));
    Json details=Json::array();size_t pendingShortcuts=0;for(const auto& row:shortcuts){const auto identity=store.identity(stringField(row,"executable"));if(!identity.value("verified",false))++pendingShortcuts;details.push_back({{"name",stringField(row,"name")},{"appId",identity.value("appId",int64_t{0})},{"verified",identity.value("verified",false)}});}
    size_t added=0,confirmed=0;for(const auto& item:combined){if(!known.count(normalized(item.value("executable",std::string{}))))++added;if(item.value("verified",false))++confirmed;}
    std::cout<<Json({{"readOnly",true},{"localEligible",local.size()},{"validShortcutRows",shortcuts.size()},{"combinedUnique",combined.size()},{"SteamOnlyAdded",added},{"verifiedLocalIdentity",confirmed},{"pendingFileIdentity",combined.size()-confirmed},{"shortcutPendingIdentity",pendingShortcuts},{"shortcuts",details},{"sharedIndexExists",fs::exists(root/L"config"/L"game-media.json")}}).dump()<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
