#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <set>
#include "shared_game_media.h"
#include "shared_game_media_prewarm.h"
#pragma comment(lib, "ws2_32.lib")
namespace ymcc::media {
struct ByteRange { uint64_t first=0,last=0;bool partial=false,valid=true; };
inline ByteRange parseRange(const std::string& text,uint64_t size) {
    ByteRange r{0,size?size-1:0,false,size>0};if(text.empty())return r;
    r.partial=true;if(text.rfind("bytes=",0)!=0||text.find(',')!=std::string::npos){r.valid=false;return r;}
    auto v=text.substr(6);auto dash=v.find('-');if(dash==std::string::npos){r.valid=false;return r;}
    auto number=[](const std::string& s,uint64_t& n){if(s.empty()||s.size()>20||!std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';}))return false;try{n=std::stoull(s);return true;}catch(...){return false;}};
    auto a=v.substr(0,dash),b=v.substr(dash+1);uint64_t n=0;
    if(a.empty()){if(!number(b,n)||n==0){r.valid=false;return r;}r.first=n>=size?0:size-n;}
    else {if(!number(a,r.first)||r.first>=size){r.valid=false;return r;}if(!b.empty()){if(!number(b,n)||n<r.first){r.valid=false;return r;}r.last=std::min(n,size-1);}}
    return r;
}
class Server {
    struct Asset {fs::path path;std::string mime,key;};
    SOCKET listener_=INVALID_SOCKET;unsigned short port_=0;std::string token_=randomId()+randomId();
    std::atomic<bool> stopping_{false};std::thread acceptor_;std::vector<std::thread> workers_;
    std::mutex mutex_;std::condition_variable ready_;std::deque<SOCKET> queue_;std::set<SOCKET> active_;std::map<std::string,Asset> assets_;
    static bool sendAll(SOCKET s,const char* p,size_t n){while(n){int sent=send(s,p,static_cast<int>(std::min<size_t>(n,65536)),0);if(sent<=0)return false;p+=sent;n-=sent;}return true;}
    static void error(SOCKET s,int code,const std::string& reason,const std::string& extra={}){auto text="HTTP/1.1 "+std::to_string(code)+" "+reason+"\r\nContent-Length: 0\r\nConnection: close\r\n"+extra+"\r\n";sendAll(s,text.data(),text.size());}
    void serve(SOCKET s){
        DWORD timeout=5000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof timeout);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&timeout),sizeof timeout);
        std::string h;char buffer[8192];while(h.find("\r\n\r\n")==std::string::npos){auto n=recv(s,buffer,sizeof buffer,0);if(n<=0)return;h.append(buffer,n);if(h.size()>8192){error(s,431,"Headers Too Large");return;}}
        auto e=h.find("\r\n");auto line=h.substr(0,e);auto sp=line.find(' '),last=line.rfind(' ');if(sp==std::string::npos||last==sp){error(s,400,"Bad Request");return;}
        auto method=line.substr(0,sp),path=line.substr(sp+1,last-sp-1);if(method!="GET"&&method!="HEAD"&&method!="OPTIONS"){error(s,405,"Method Not Allowed");return;}
        std::map<std::string,std::string> headers;for(size_t p=e+2;p<h.size();){auto end=h.find("\r\n",p);if(end==std::string::npos||end==p)break;auto colon=h.find(':',p);if(colon>=end){error(s,400,"Bad Request");return;}auto name=h.substr(p,colon-p);for(auto& c:name)c=static_cast<char>(tolower(static_cast<unsigned char>(c)));auto value=h.substr(colon+1,end-colon-1);auto first=value.find_first_not_of(" \t");value=first==std::string::npos?"":value.substr(first);if(!headers.emplace(name,value).second){error(s,400,"Bad Request");return;}p=end+2;}
        if(headers["host"]!="127.0.0.1:"+std::to_string(port_)){error(s,403,"Forbidden");return;}
        const auto origin=headers["origin"];if(!origin.empty()&&origin!="https://steamloopback.host"&&origin!="https://app.localhost"&&origin!="http://app.localhost"){error(s,403,"Forbidden");return;}
        const auto suffix="?token="+token_;if(path.rfind("/asset/",0)!=0||path.size()<=7+suffix.size()||path.substr(path.size()-suffix.size())!=suffix){error(s,403,"Forbidden");return;}
        auto id=path.substr(7,path.size()-7-suffix.size());Asset asset;
        {std::lock_guard lock(mutex_);auto it=assets_.find(id);if(it==assets_.end()){error(s,404,"Not Found");return;}asset=it->second;}
        if(method=="OPTIONS"){
            if(origin.empty()||(headers["access-control-request-method"]!="GET"&&headers["access-control-request-method"]!="HEAD")){error(s,403,"Forbidden");return;}
            error(s,204,"No Content","Access-Control-Allow-Origin: "+origin+"\r\nAccess-Control-Allow-Methods: GET, HEAD, OPTIONS\r\nAccess-Control-Allow-Headers: Range\r\nAccess-Control-Allow-Private-Network: true\r\nVary: Origin\r\n");return;
        }
        std::error_code ec;auto size=fs::file_size(asset.path,ec);if(ec||!size){error(s,404,"Not Found");return;}
        auto range=parseRange(headers["range"],size);if(!range.valid){error(s,416,"Range Not Satisfiable","Content-Range: bytes */"+std::to_string(size)+"\r\n");return;}
        std::ifstream file(asset.path,std::ios::binary);if(!file){error(s,404,"Not Found");return;}
        auto count=range.last-range.first+1;std::string response=range.partial?"HTTP/1.1 206 Partial Content\r\n":"HTTP/1.1 200 OK\r\n";
        response+="Content-Type: "+asset.mime+"\r\nContent-Length: "+std::to_string(count)+"\r\nAccept-Ranges: bytes\r\nCache-Control: private, max-age=31536000, immutable\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n";
        if(range.partial)response+="Content-Range: bytes "+std::to_string(range.first)+"-"+std::to_string(range.last)+"/"+std::to_string(size)+"\r\n";
        if(!origin.empty())response+="Access-Control-Allow-Origin: "+origin+"\r\nVary: Origin\r\n";
        response+="\r\n";if(!sendAll(s,response.data(),response.size())||method=="HEAD")return;
        file.seekg(static_cast<std::streamoff>(range.first));char chunk[65536];while(count&&!stopping_){auto n=static_cast<std::streamsize>(std::min<uint64_t>(count,sizeof chunk));file.read(chunk,n);auto got=file.gcount();if(got<=0||!sendAll(s,chunk,static_cast<size_t>(got)))return;count-=got;}
    }
public:
    Server(){WSADATA w{};if(WSAStartup(MAKEWORD(2,2),&w))throw std::runtime_error("media-winsock-failed");listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener_==INVALID_SOCKET){WSACleanup();throw std::runtime_error("media-socket-failed");}BOOL exclusive=TRUE;setsockopt(listener_,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char*>(&exclusive),sizeof exclusive);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(bind(listener_,reinterpret_cast<sockaddr*>(&a),sizeof a)||listen(listener_,8)){closesocket(listener_);WSACleanup();throw std::runtime_error("media-listen-failed");}int n=sizeof a;getsockname(listener_,reinterpret_cast<sockaddr*>(&a),&n);port_=ntohs(a.sin_port);
        for(int i=0;i<2;i++)workers_.emplace_back([this]{for(;;){SOCKET s;{std::unique_lock lock(mutex_);ready_.wait(lock,[this]{return stopping_||!queue_.empty();});if(stopping_)return;s=queue_.front();queue_.pop_front();active_.insert(s);}try{serve(s);}catch(...){error(s,500,"Media Error");}{std::lock_guard lock(mutex_);active_.erase(s);}shutdown(s,SD_BOTH);closesocket(s);}});
        acceptor_=std::thread([this]{while(!stopping_){auto s=accept(listener_,nullptr,nullptr);if(s==INVALID_SOCKET)break;bool accepted=false;{std::lock_guard lock(mutex_);if(!stopping_&&queue_.size()<4){queue_.push_back(s);accepted=true;}}if(accepted)ready_.notify_one();else closesocket(s);}});
    }
    ~Server(){stopping_=true;shutdown(listener_,SD_BOTH);closesocket(listener_);{std::lock_guard lock(mutex_);for(auto s:active_)shutdown(s,SD_BOTH);for(auto s:queue_)closesocket(s);queue_.clear();}ready_.notify_all();if(acceptor_.joinable())acceptor_.join();for(auto& t:workers_)if(t.joinable())t.join();WSACleanup();}
    // Old clients may still send a warm request. It cannot schedule file reads.
    size_t prewarm(const Store&,const Json&){return 0;}
    CachePrewarmer::Stats prewarmStats(){return {};}
    void reconcile(const Json& entry){std::lock_guard lock(mutex_);const auto prefix=entry.value("key",std::string{})+":";std::set<std::string> valid;
        if(entry.contains("assets")&&!entry.value("deleted",false))for(const auto* slot:{"static","dynamic"})if(entry["assets"].contains(slot)&&(!entry.contains("deletedSlots")||!entry["deletedSlots"].value(slot,false))){const auto& asset=entry["assets"][slot];if(entry.contains("invalidSlots")&&entry["invalidSlots"].value(slot,std::string{})==asset.value("id",std::string{}))continue;valid.insert(asset.value("id",std::string{})+"."+asset.value("extension",std::string{}));}
        for(auto it=assets_.begin();it!=assets_.end();)if(it->second.key.rfind(prefix,0)==0&&!valid.count(it->first))it=assets_.erase(it);else ++it;
    }
    std::string expose(const Store& store,const Json& entry){reconcile(entry);if(!entry.contains("asset")||entry.value("deleted",false))return {};auto asset=entry["asset"];const auto slot=asset.value("kind",std::string{})=="video"?"dynamic":"static";if(entry.contains("invalidSlots")&&entry["invalidSlots"].value(slot,std::string{})==asset.value("id",std::string{}))return {};auto id=asset.value("id",std::string{})+"."+asset.value("extension",std::string{});auto path=store.assetPath(asset);std::lock_guard lock(mutex_);const auto key=entry.value("key",std::string{})+":"+asset.value("kind",std::string{});for(auto it=assets_.begin();it!=assets_.end();)if(it->second.key==key&&it->first!=id)it=assets_.erase(it);else ++it;assets_[id]={path,asset.value("mime",std::string{}),key};return "http://127.0.0.1:"+std::to_string(port_)+"/asset/"+id+"?token="+token_;}
};
} // namespace ymcc::media
