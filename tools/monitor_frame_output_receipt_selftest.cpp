#include <iostream>
#include <fstream>
#include "../deps/json/json.hpp"
using json=nlohmann::json;
#include "../native/monitor_frame_output.h"
using namespace ymcc::monitorframe;
int main(int argc,char**argv){if(argc!=2)return 2;json c=json::array();auto check=[&](const char*n,bool ok){c.push_back({{"name",n},{"ok",ok}});};Output o;o.bind(77,99);check("invalid RTSS receipt does not admit a fast publication",!o.observeRtss({false,0,0,1700000000000ULL}));check("new RTSS observation is admitted",o.observeRtss({true,8.3,120,1700000000100ULL}));check("older RTSS observation is rejected",!o.observeRtss({true,8.4,121,1700000000000ULL}));check("same timestamp receipt does not advance its observation timestamp",o.observeRtss({true,8.3,120,1700000000100ULL})&&o.packet(1700000000200ULL)["fpsObservedAt"]==1700000000100ULL);bool ok=true;for(auto&v:c)ok=ok&&v["ok"].get<bool>();std::ofstream(argv[1])<<json({{"ok",ok},{"cases",c},{"scope","Actual production Output admission receipt; no live RTSS mapping or CPU measurement"}}).dump(2);return ok?0:1;}
