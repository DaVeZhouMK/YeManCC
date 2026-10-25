#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/shared_game_media_server.h"
#include <iostream>
using namespace ymcc::media;
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("usage: isolated-root video image");
    Store store(fs::path(wide(argv[1])));auto entry=store.begin({{"appId",570},{"name","Dota 2 decode fixture"}});
    entry=store.publish(entry,fs::path(wide(argv[2])),false);entry=store.publish(entry,fs::path(wide(argv[3])),false);store.finish(entry);
    Server server;auto image=entry;image["asset"]=entry["assets"]["static"];std::cout<<Json({{"video",server.expose(store,entry)},{"image",server.expose(store,image)},{"bytes",entry["asset"]["bytes"]}}).dump()<<std::endl;
    std::string done;std::getline(std::cin,done);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
