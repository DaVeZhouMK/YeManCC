#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "shared_game_media_server.h"
#include <iostream>
using namespace ymcc::media;
int wmain(int argc,wchar_t**argv){try{if(argc!=2)throw std::runtime_error("fresh root required");const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("exists");fs::create_directories(root);Store store(root);Server server;int checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("preload-not-disabled");};for(int i=0;i<100;i++)check(server.prewarm(store,Json::array({{{"asset",{{"id","absent"}}}}}))==0);const auto stats=server.prewarmStats();check(stats.bytes==0&&stats.completed==0&&stats.queued==0&&!stats.active);check(store.snapshot().value("entries",Json::object()).empty());check(server.prewarm(store,Json::object())==0);std::cout<<Json({{"ok",true},{"checks",checks},{"scope","production Server compatibility entry; 100 old-client calls create no read/thread/queued work or Store entries"}}).dump()<<std::endl;return 0;}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}}
