#pragma once
// YMCC Decky sidebar authenticated, single-peer loopback WebSocket transport.
// No business commands are executed here. One event-driven worker, no idle timer.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
namespace ymcc::deckymirror {
using Json = nlohmann::json;
constexpr size_t kMaxPayload = 65536, kMaxHeader = 8192, kMaxQueue = 16;
constexpr ULONGLONG kWriteProgressTimeoutMs = 10000;
inline std::string randomHex(size_t count) {
    std::vector<unsigned char> bytes(count);
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("mirror-entropy-unavailable");
    std::string result; static constexpr char alphabet[] = "0123456789abcdef";
    for (const auto b : bytes) { result += alphabet[b >> 4]; result += alphabet[b & 15]; } return result;
}
inline std::string websocketAccept(const std::string& key) {
    const auto material = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) throw std::runtime_error("sha1-unavailable");
    std::array<BYTE,20> digest{}; DWORD bytes = static_cast<DWORD>(digest.size());
    const bool ok = CryptCreateHash(provider, CALG_SHA1, 0, 0, &hash) &&
        CryptHashData(hash, reinterpret_cast<const BYTE*>(material.data()), static_cast<DWORD>(material.size()), 0) &&
        CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &bytes, 0);
    if (hash) CryptDestroyHash(hash); CryptReleaseContext(provider,0);
    if (!ok) throw std::runtime_error("sha1-failed");
    DWORD size = 0; CryptBinaryToStringA(digest.data(), bytes, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size);
    std::string result(size, '\0');
    if (!CryptBinaryToStringA(digest.data(), bytes, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &size)) throw std::runtime_error("base64-failed");
    result.resize(size); while (!result.empty() && result.back() == '\0') result.pop_back(); return result;
}
inline std::string lower(std::string value) { for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return value; }
inline std::string trim(const std::string& value) { const auto a=value.find_first_not_of(" \t"); if(a==std::string::npos)return {}; return value.substr(a,value.find_last_not_of(" \t")-a+1); }
inline bool constantEqual(const std::string& a, const std::string& b) {
    unsigned diff=static_cast<unsigned>(a.size() ^ b.size());
    for (size_t i=0;i<std::max(a.size(),b.size());++i) diff |= (i<a.size()?static_cast<unsigned char>(a[i]):0) ^ (i<b.size()?static_cast<unsigned char>(b[i]):0);
    return diff==0;
}
// Pure parsing functions are exercised by the exact production-header tests.
inline std::string handshake(const std::string& header, const std::string& token, const std::string& host) {
    if (header.size()>kMaxHeader || header.find('\0')!=std::string::npos) throw std::runtime_error("invalid-header");
    const auto first=header.find("\r\n"); if(first==std::string::npos) throw std::runtime_error("invalid-header");
    const auto line=header.substr(0,first); const std::string prefix="GET /mirror?token=";
    if(line.rfind(prefix,0)!=0 || line.size()<prefix.size()+9 || line.substr(line.size()-9)!=" HTTP/1.1") throw std::runtime_error("invalid-request");
    if(!constantEqual(line.substr(prefix.size(),line.size()-prefix.size()-9),token)) throw std::runtime_error("unauthorized");
    std::map<std::string,std::string> fields;
    for(size_t pos=first+2;pos<header.size();) {
        const auto end=header.find("\r\n",pos); if(end==std::string::npos)throw std::runtime_error("invalid-header");
        if(end==pos)break; const auto colon=header.find(':',pos); if(colon==std::string::npos||colon>=end)throw std::runtime_error("invalid-header");
        const auto name=lower(header.substr(pos,colon-pos));
        if(name.empty()||!fields.emplace(name,trim(header.substr(colon+1,end-colon-1))).second)throw std::runtime_error("duplicate-header"); pos=end+2;
    }
    if(fields["origin"]!="https://steamloopback.host" || fields["host"]!=host)throw std::runtime_error("origin-denied");
    if(lower(fields["upgrade"])!="websocket" || fields["sec-websocket-version"]!="13")throw std::runtime_error("invalid-upgrade");
    const auto connection=lower(fields["connection"]); bool upgrade=false;
    for(size_t pos=0;pos<=connection.size();) { const auto end=connection.find(',',pos); if(trim(connection.substr(pos,end==std::string::npos?std::string::npos:end-pos))=="upgrade")upgrade=true; if(end==std::string::npos)break;pos=end+1; }
    if(!upgrade)throw std::runtime_error("invalid-upgrade");
    const auto key=fields["sec-websocket-key"];
    if(key.size()!=24 || key.substr(22)!="==" || key.substr(0,22).find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/")!=std::string::npos)
        throw std::runtime_error("invalid-key");
    DWORD decoded=0;
    if(!CryptStringToBinaryA(key.data(),static_cast<DWORD>(key.size()),CRYPT_STRING_BASE64,nullptr,&decoded,nullptr,nullptr)||decoded!=16)throw std::runtime_error("invalid-key");
    if(fields.contains("content-length")||fields.contains("transfer-encoding"))throw std::runtime_error("request-body-denied");
    return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+websocketAccept(key)+"\r\n\r\n";
}
struct Frame { bool complete=false; size_t bytes=0; unsigned opcode=0; std::string payload; };
inline Frame decode(const std::vector<unsigned char>& input) {
    if(input.size()<2)return {};
    if((input[0]&0x80)==0||(input[0]&0x70)!=0||(input[1]&0x80)==0)throw std::runtime_error("invalid-frame");
    const unsigned opcode=input[0]&15; if(opcode!=1&&opcode!=8&&opcode!=9&&opcode!=10)throw std::runtime_error("unsupported-frame");
    size_t offset=2; uint64_t length=input[1]&127;
    if(length==126){if(input.size()<4)return {};length=(static_cast<uint64_t>(input[2])<<8)|input[3];offset=4;if(length<126)throw std::runtime_error("noncanonical-frame");}
    else if(length==127){if(input.size()<10)return {};length=0;for(size_t i=2;i<10;i++)length=(length<<8)|input[i];offset=10;if(length<65536)throw std::runtime_error("noncanonical-frame");}
    if(length>kMaxPayload || (opcode>=8&&length>125))throw std::runtime_error("oversized-frame");
    if(input.size()<offset+4+length)return {};
    std::string payload(static_cast<size_t>(length),'\0');for(size_t i=0;i<length;++i)payload[i]=static_cast<char>(input[offset+4+i]^input[offset+(i%4)]);
    return {true,offset+4+static_cast<size_t>(length),opcode,std::move(payload)};
}
inline std::optional<unsigned> numericCloseCode(const Frame& frame) {
    if(!frame.complete||frame.opcode!=8||frame.payload.size()<2)return std::nullopt;
    return (static_cast<unsigned char>(frame.payload[0])<<8)|static_cast<unsigned char>(frame.payload[1]);
}
inline std::string encode(const std::string& payload,unsigned opcode=1) {
    if(payload.size()>kMaxPayload)throw std::runtime_error("oversized-response");
    std::string out;out+=static_cast<char>(0x80|opcode);
    if(payload.size()<126)out+=static_cast<char>(payload.size());
    else if(payload.size()<=65535){out+=static_cast<char>(126);out+=static_cast<char>(payload.size()>>8);out+=static_cast<char>(payload.size());}
    else{out+=static_cast<char>(127);for(int i=7;i>=0;--i)out+=static_cast<char>(static_cast<uint64_t>(payload.size())>>(i*8));}
    return out+payload;
}
struct Config {
    std::function<void(const std::string&,const Json&)> request;
    std::function<void(const std::string&,const std::string&,bool)> peerChanged;
};
class Broker {
    std::mutex life_,state_;
    std::thread worker_;
    SOCKET listener_=INVALID_SOCKET;
    WSAEVENT stop_=WSA_INVALID_EVENT,listenEvent_=WSA_INVALID_EVENT,sendEvent_=WSA_INVALID_EVENT;
    bool active_=false;
    std::atomic<bool> reporting_{true};
    unsigned short port_=0;
    std::string token_,runId_,connection_;
    ULONGLONG authenticatedAt_=0; // Current authenticated peer only, guarded by state_.
    struct Reply {std::string connection,text;std::function<bool()> owner;size_t sent=0;};
    std::deque<Reply> replies_;
    static bool requestCurrent(const std::function<bool()>& owner){try{return !owner||owner();}catch(...){return false;}}
    Config config_;
    Json lastPeerClose_; // Last authenticated edge only; no timer or payload logging.
    unsigned long long nextConnection_=0, revision_=0, connectionRevision_=0;
    void run() {
        SOCKET peer=INVALID_SOCKET;WSAEVENT peerEvent=WSA_INVALID_EVENT;
        std::vector<unsigned char> input;std::string connection;
        std::deque<Reply> output;size_t outputBytes=0;
        const auto append=[&](std::string text){outputBytes+=text.size();output.push_back({connection,std::move(text),{},0});};
        bool authenticated=false;ULONGLONG deadline=0,fragmentDeadline=0,writeDeadline=0,connectedAt=0,lastReceive=0;
        const char* exitReason="owner-stop";int exitError=0;
        auto changed=[&](bool connected){try{if(reporting_.load()&&config_.peerChanged)config_.peerChanged(connection,runId_,connected);}catch(...){}};
        auto closePeer=[&](const char* reason,int error=0,unsigned closeCode=0,bool closeCodeKnown=false){
            Json facts;
            if(authenticated){
                const auto now=GetTickCount64();
                facts={{"closeReason",reason},{"socketError",error},{"websocketCloseCodeKnown",closeCodeKnown},
                    {"connectionAgeMs",now-connectedAt},{"receiveIdleMs",now-lastReceive},
                    {"pendingOutputBytes",outputBytes},{"bufferedInputBytes",input.size()}};
                if(closeCodeKnown)facts["websocketCloseCode"]=closeCode;
            }
            if(peer!=INVALID_SOCKET){closesocket(peer);peer=INVALID_SOCKET;}
            if(peerEvent!=WSA_INVALID_EVENT){WSACloseEvent(peerEvent);peerEvent=WSA_INVALID_EVENT;}
            {std::lock_guard lock(state_);if(!connection_.empty())++connectionRevision_;connection_.clear();replies_.clear();if(!facts.is_null())lastPeerClose_=std::move(facts);}
            if(authenticated)changed(false);authenticated=false;connection.clear();input.clear();output.clear();outputBytes=0;deadline=0;fragmentDeadline=0;writeDeadline=0;
        };
        auto drain=[&]{
            // Only actual socket progress renews pending output; more replies/pings do not.
            if(authenticated&&!output.empty()&&!writeDeadline)writeDeadline=GetTickCount64()+kWriteProgressTimeoutMs;
            while(peer!=INVALID_SOCKET&&!output.empty()) {
                auto& item=output.front();
                // A retired partial frame cannot be removed from a live stream.
                // Close our peer instead of delivering its stale remaining bytes.
                if(!requestCurrent(item.owner)){
                    if(item.sent){closePeer("reply-owner-retired");break;}
                    outputBytes-=item.text.size();output.pop_front();if(output.empty())writeDeadline=0;continue;
                }
                const int n=send(peer,item.text.data()+item.sent,static_cast<int>(item.text.size()-item.sent),0);
                if(n>0){item.sent+=static_cast<size_t>(n);outputBytes-=static_cast<size_t>(n);if(!requestCurrent(item.owner)){closePeer("reply-owner-retired");break;}if(item.sent==item.text.size())output.pop_front();writeDeadline=output.empty()?0:GetTickCount64()+kWriteProgressTimeoutMs;}
                else{const int error=n==SOCKET_ERROR?WSAGetLastError():0;if(error==WSAEWOULDBLOCK)break;closePeer("send-error",error);break;}
            }
        };
        for(;;) {
            WSAEVENT events[4]{stop_,listenEvent_,sendEvent_,peerEvent};const DWORD count=peer==INVALID_SOCKET?3:4;
            DWORD timeout=WSA_INFINITE;
            ULONGLONG until=peer==INVALID_SOCKET?0:authenticated?fragmentDeadline:deadline;
            if(authenticated&&writeDeadline&&(!until||writeDeadline<until))until=writeDeadline;
            const auto timeoutReason=[&]{return writeDeadline&&until==writeDeadline?"write-progress-timeout":authenticated?"frame-timeout":"handshake-timeout";};
            if(until){
                const auto now=GetTickCount64();
                // Check before waiting too: a busy event must not starve an expired deadline.
                if(now>=until){closePeer(timeoutReason());continue;}
                timeout=static_cast<DWORD>(until-now);
            }
            const auto result=WSAWaitForMultipleEvents(count,events,FALSE,timeout,FALSE);
            if(result==WSA_WAIT_EVENT_0||result==WSA_WAIT_FAILED){if(result==WSA_WAIT_FAILED){exitReason="wait-error";exitError=WSAGetLastError();}break;}
            if(result==WSA_WAIT_TIMEOUT){closePeer(timeoutReason());continue;}
            const DWORD index=result-WSA_WAIT_EVENT_0;
            if(index==1){
                WSANETWORKEVENTS network{};if(WSAEnumNetworkEvents(listener_,listenEvent_,&network)==SOCKET_ERROR){exitReason="listener-events-error";exitError=WSAGetLastError();break;}
                if(network.lNetworkEvents&FD_ACCEPT) {
                    sockaddr_in address{};int size=sizeof(address);const SOCKET accepted=accept(listener_,reinterpret_cast<sockaddr*>(&address),&size);
                    if(accepted!=INVALID_SOCKET){
                        if(peer!=INVALID_SOCKET||address.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)){closesocket(accepted);continue;}
                        peer=accepted;peerEvent=WSACreateEvent();deadline=GetTickCount64()+3000;
                        if(peerEvent==WSA_INVALID_EVENT||WSAEventSelect(peer,peerEvent,FD_READ|FD_WRITE|FD_CLOSE)==SOCKET_ERROR){closePeer("peer-events-setup-error",WSAGetLastError());continue;}
                    }
                }
            } else if(index==2){
                WSAResetEvent(sendEvent_);std::deque<Reply> queue;
                {std::lock_guard lock(state_);queue.swap(replies_);}
                for(auto& item:queue)if(authenticated&&item.connection==connection){
                    if(!requestCurrent(item.owner))continue;
                    if(outputBytes+item.text.size()>kMaxPayload*2){
                        // Give the socket a nonblocking write before treating a
                        // batch of queued replies as receiver backpressure.
                        drain();
                        if(peer==INVALID_SOCKET)break;
                        if(!requestCurrent(item.owner))continue;
                        if(outputBytes+item.text.size()>kMaxPayload*2){closePeer("output-limit");break;}
                    }
                    outputBytes+=item.text.size();output.push_back(std::move(item));
                }
                drain();
            } else if(index==3&&peer!=INVALID_SOCKET){
                WSANETWORKEVENTS network{};if(WSAEnumNetworkEvents(peer,peerEvent,&network)==SOCKET_ERROR){closePeer("socket-events-error",WSAGetLastError());continue;}
                if(network.lNetworkEvents&FD_READ){
                    char buffer[8192];bool failed=false,eof=false;const char* readReason="remote-eof";int readError=0;
                    for(;;){const int n=recv(peer,buffer,sizeof(buffer),0);if(n>0){lastReceive=GetTickCount64();input.insert(input.end(),buffer,buffer+n);if(input.size()>kMaxPayload+32){readReason="input-limit";failed=true;break;}}
                        else{readError=n==SOCKET_ERROR?WSAGetLastError():0;if(readError==WSAEWOULDBLOCK)break;if(n==SOCKET_ERROR)readReason="receive-error";eof=n==0;failed=true;break;}}
                    if(failed){
                        // A closing client may send its Close and FIN together.
                        // Recover only the leading close code, never dispatch terminal input.
                        std::optional<unsigned> code;
                        if(eof&&authenticated&&!input.empty()&&(input.front()&15)==8){
                            try{code=numericCloseCode(decode(input));}catch(...){}
                        }
                        if(code)closePeer("remote-close",0,*code,true);else closePeer(readReason,readError);
                        continue;
                    }
                    const char* inputFailure="handshake-error";
                    try {
                        if(!authenticated){
                            const std::string header(input.begin(),input.end());const auto end=header.find("\r\n\r\n");
                            if(end==std::string::npos){if(input.size()>kMaxHeader)throw std::runtime_error("header-too-large");}
                            else {
                                append(handshake(header.substr(0,end+4),token_,"127.0.0.1:"+std::to_string(port_)));
                                input.erase(input.begin(),input.begin()+static_cast<std::ptrdiff_t>(end+4));
                                authenticated=true;connectedAt=lastReceive=GetTickCount64();connection=std::to_string(++nextConnection_);
                                {std::lock_guard lock(state_);connection_=connection;authenticatedAt_=connectedAt;++connectionRevision_;}
                                changed(true);
                            }
                        }
                        unsigned processed=0;
                        while(authenticated&&peer!=INVALID_SOCKET){inputFailure="frame-error";const auto frame=decode(input);if(!frame.complete)break;
                            if(++processed>32)throw std::runtime_error("message-burst-limit");
                            fragmentDeadline=0;
                            input.erase(input.begin(),input.begin()+static_cast<std::ptrdiff_t>(frame.bytes));
                            if(frame.opcode==8){const auto code=numericCloseCode(frame);closePeer("remote-close",0,code.value_or(0),code.has_value());break;}
                            if(frame.opcode==9){append(encode(frame.payload,10));continue;}
                            if(frame.opcode==10)continue;
                            inputFailure="request-json-error";const auto request=Json::parse(frame.payload);
                            inputFailure="request-identity-error";if(!request.is_object()||request.value("runId",std::string{})!=runId_)throw std::runtime_error("stale-run");
                            inputFailure="request-command-error";const auto command=request.value("command",std::string{});
                            if(command!="snapshot"&&command!="game.setField"&&command!="fan.setEnabled"&&command!="fan.setPreset"&&command!="fan.setChoice"&&command!="frame.setField"&&command!="touchpad.setField"&&command!="global.setField"&&command!="audioOptimization.setEnabled"&&command!="monitor.setEnabled"&&command!="performance.setControls"&&command!="hud.status"&&command!="hud.ack"&&command!="wallpaper.context"&&command!="wallpaper.status"&&command!="wallpaper.neighbors"&&command!="updater.status"&&command!="updater.request")throw std::runtime_error("command-denied");
                            inputFailure="request-callback-error";if(reporting_.load()&&config_.request)config_.request(connection,request);
                        }
                        if(authenticated&&peer!=INVALID_SOCKET&&!input.empty()&&!fragmentDeadline)fragmentDeadline=GetTickCount64()+3000;
                        drain();
                    }catch(...){closePeer(inputFailure);continue;}
                }
                if(peer!=INVALID_SOCKET&&(network.lNetworkEvents&FD_WRITE))drain();
                if(peer!=INVALID_SOCKET&&(network.lNetworkEvents&FD_CLOSE))closePeer("socket-close",network.iErrorCode[FD_CLOSE_BIT]);
            }
        }
        closePeer(exitReason,exitError);
        { std::lock_guard lock(state_); active_ = false; }
    }
    void stopLocked() {
        if(stop_!=WSA_INVALID_EVENT)WSASetEvent(stop_);
        if(worker_.joinable())worker_.join();
        if(listener_!=INVALID_SOCKET){closesocket(listener_);listener_=INVALID_SOCKET;}
        for(auto* event:{&stop_,&listenEvent_,&sendEvent_})if(*event!=WSA_INVALID_EVENT){WSACloseEvent(*event);*event=WSA_INVALID_EVENT;}
        {std::lock_guard lock(state_);active_=false;port_=0;token_.clear();runId_.clear();++connectionRevision_;connection_.clear();replies_.clear();revision_=0;}
        if(wsaStarted_){WSACleanup();wsaStarted_=false;}
    }
    bool wsaStarted_=false;
public:
    ~Broker(){reporting_.store(false);stop();}
    bool start(Config config) {
        std::lock_guard lifecycle(life_);{std::lock_guard lock(state_);if(active_)return true;}
        if (worker_.joinable()) stopLocked();
        try {
            WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return false;wsaStarted_=true;
            listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener_==INVALID_SOCKET)throw std::runtime_error("socket-failed");
            const BOOL exclusive=TRUE;if(setsockopt(listener_,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))==SOCKET_ERROR)throw std::runtime_error("exclusive-failed");
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
            if(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR||listen(listener_,4)==SOCKET_ERROR)throw std::runtime_error("bind-failed");
            int size=sizeof(address);if(getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&size)==SOCKET_ERROR)throw std::runtime_error("endpoint-failed");
            stop_=WSACreateEvent();listenEvent_=WSACreateEvent();sendEvent_=WSACreateEvent();
            if(stop_==WSA_INVALID_EVENT||listenEvent_==WSA_INVALID_EVENT||sendEvent_==WSA_INVALID_EVENT||WSAEventSelect(listener_,listenEvent_,FD_ACCEPT)==SOCKET_ERROR)throw std::runtime_error("events-failed");
            config_=std::move(config);
            {std::lock_guard lock(state_);port_=ntohs(address.sin_port);token_=randomHex(32);runId_=randomHex(16);lastPeerClose_=Json();active_=true;}
            worker_=std::thread([this]{run();});return true;
        }catch(...){stopLocked();return false;}
    }
    Json bootstrap() {std::lock_guard lock(state_);if(!active_)return Json::object();return {{"endpoint","ws://127.0.0.1:"+std::to_string(port_)+"/mirror"},{"token",token_},{"runId",runId_},{"hudProtocol",1},{"wallpaperProtocol",1},{"wallpaperPrefetchProtocol",0},{"updaterProtocol",1}};}
    Json status() {std::lock_guard lock(state_);Json result={{"active",active_},{"runId",runId_},{"connection",connection_},{"connected",!connection_.empty()},{"revision",revision_},{"connectionRevision",connectionRevision_}};
        if(connection_.empty()&&lastPeerClose_.is_object())result["peerClose"]=lastPeerClose_;return result;}
    // Watch hints can request a read before the mirror authenticates.
    // Matching the run is not authenticated-peer proof.
    bool matchesRunId(const std::string& runId) {
        std::lock_guard lock(state_);
        return !runId.empty()&&runId==runId_;
    }
    std::optional<ULONGLONG> authenticatedPeerTick(unsigned long long* revision=nullptr) {
        std::lock_guard lock(state_);
        if(revision)*revision=0;
        if(!active_||connection_.empty())return std::nullopt;
        if(revision)*revision=connectionRevision_;
        return authenticatedAt_;
    }
    struct WorkerCpu {DWORD id;ULONGLONG birth,kernel,user;};
    // Borrow the owned thread without competing with a stop/rebind that is joining it.
    std::optional<WorkerCpu> diagnosticWorkerCpu() noexcept {
        const DWORD savedError=GetLastError();
        struct RestoreError{DWORD value;~RestoreError(){SetLastError(value);}} restore{savedError};
        try{
            std::unique_lock lock(life_,std::try_to_lock);
            if(!lock.owns_lock()||!worker_.joinable())return std::nullopt;
            const HANDLE thread=worker_.native_handle();
            const DWORD id=GetThreadId(thread);
            FILETIME born{},exited{},kernel{},user{};
            if(!id||!GetThreadTimes(thread,&born,&exited,&kernel,&user)||WaitForSingleObject(thread,0)!=WAIT_TIMEOUT)return std::nullopt;
            const auto ticks=[](const FILETIME& t){return (static_cast<ULONGLONG>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
            const auto birth=ticks(born);
            if(birth)return WorkerCpu{id,birth,ticks(kernel),ticks(user)};
        }catch(...){}
        return std::nullopt;
    }
    struct RequestStamp {std::string runId;unsigned long long revision,connectionRevision;};
    // Ingress already owns the authenticated connection. Copy just its current
    // receipt under the same lock, not a full diagnostic JSON per request.
    // A queued event still needs acceptsRequest() at the actual UI dispatch.
    std::optional<RequestStamp> requestStamp(const std::string& connection) {
        std::lock_guard lock(state_);
        if(!active_||connection.empty()||connection!=connection_)return std::nullopt;
        return RequestStamp{runId_,revision_,connectionRevision_};
    }
    // Recheck queued private events at UI dispatch, not only socket receipt.
    // Client payload flags never participate in this native transport fact.
    bool acceptsRequest(const std::string& connection,const std::string& runId,unsigned long long connectionRevision) {
        std::lock_guard lock(state_);
        return active_&&!connection.empty()&&connection==connection_&&runId==runId_&&connectionRevision==connectionRevision_;
    }
    bool reply(const std::string& connection,const Json& message,std::function<bool()> owner={}) {
        const auto text=message.dump();if(text.size()>kMaxPayload)return false;
        if(owner&&(message.value("type",std::string{})!="hud"||!requestCurrent(owner)))return false;
        std::lock_guard lock(state_);
        if(!active_||connection.empty()||connection!=connection_||replies_.size()>=kMaxQueue||message.value("runId",std::string{})!=runId_)return false;
        if(message.value("type",std::string{})=="snapshot") {
            if(!message.contains("snapshot") || !message["snapshot"].is_object())return false;
            const auto& snapshot=message["snapshot"];
            if(!snapshot.contains("revision") || (!snapshot["revision"].is_number_unsigned() && !snapshot["revision"].is_number_integer()))return false;
            const auto revision=snapshot["revision"].get<long long>();
            if(revision<=0 || revision>9007199254740991LL || static_cast<unsigned long long>(revision)<revision_ || snapshot.value("runId",std::string{})!=runId_)return false;
            revision_=static_cast<unsigned long long>(revision);
        }
        replies_.push_back({connection,encode(text),std::move(owner),0});WSASetEvent(sendEvent_);return true;
    }
    void stop(){std::lock_guard lifecycle(life_);stopLocked();}
};
} // namespace ymcc::deckymirror
