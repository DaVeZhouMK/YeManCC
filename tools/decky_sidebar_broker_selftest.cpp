#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
// Observation only: delegate to real Winsock without changing return values,
// socket buffers, clocks or errors. Enabled only by --write-deadline.
namespace writeProbe {
    static std::atomic<bool> enabled{false};
    static std::atomic<unsigned long long> attempts{0},blocked{0},bytes{0},lastProgress{0},infiniteWaits{0};
}
static int observedBrokerSend(SOCKET socket,const char* data,int size,int flags){
    const int result=::send(socket,data,size,flags);const int error=result==SOCKET_ERROR?WSAGetLastError():0;
    if(writeProbe::enabled.load()){
        if(result>0){writeProbe::bytes.fetch_add(static_cast<unsigned>(result));writeProbe::lastProgress.store(GetTickCount64());}
        if(result==SOCKET_ERROR&&error==WSAEWOULDBLOCK)writeProbe::blocked.fetch_add(1);
        writeProbe::attempts.fetch_add(1);
    }
    if(result==SOCKET_ERROR)WSASetLastError(error);return result;
}
static DWORD observedBrokerWait(DWORD count,const WSAEVENT* events,BOOL all,DWORD timeout,BOOL alertable){
    if(writeProbe::enabled.load()&&timeout==WSA_INFINITE)writeProbe::infiniteWaits.fetch_add(1);
    return ::WSAWaitForMultipleEvents(count,events,all,timeout,alertable);
}
// Hold just the selected real read until its client has half-closed.
namespace closeProbe {
    static std::atomic<bool> arm{false},release{true},entered{false};
    static std::atomic<unsigned> eof{0};
}
static int observedBrokerRecv(SOCKET socket,char* data,int size,int flags){
    if(closeProbe::arm.exchange(false)){
        closeProbe::entered.store(true);
        const auto until=GetTickCount64()+2000;
        while(!closeProbe::release.load()&&GetTickCount64()<until)Sleep(1);
    }
    const int n=::recv(socket,data,size,flags);
    if(n==0)closeProbe::eof.fetch_add(1);
    return n;
}
#define send observedBrokerSend
#define recv observedBrokerRecv
#define WSAWaitForMultipleEvents observedBrokerWait
#include "../native/decky_sidebar_broker.h"
#undef WSAWaitForMultipleEvents
#undef recv
#undef send
#include "../native/decky_sidebar_diagnostics.h"
#include <iostream>
#include <stdexcept>
using namespace ymcc::deckymirror;
static int checks=0;
static void check(bool condition,const char* label){if(!condition)throw std::runtime_error(label);++checks;}
static void rejects(const std::function<void()>& action,const char* label){bool failed=false;try{action();}catch(...){failed=true;}check(failed,label);}
static std::string request(const std::string& token="TOKEN",const std::string& origin="https://steamloopback.host"){
    return "GET /mirror?token="+token+" HTTP/1.1\r\nHost: 127.0.0.1:12345\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: "+origin+"\r\n\r\n";
}
static std::vector<unsigned char> masked(const std::string& body,unsigned opcode=1){
    std::vector<unsigned char> result{static_cast<unsigned char>(0x80|opcode)};
    if(body.size()<126)result.push_back(static_cast<unsigned char>(0x80|body.size()));
    else{result.push_back(0xfe);result.push_back(static_cast<unsigned char>(body.size()>>8));result.push_back(static_cast<unsigned char>(body.size()));}
    const std::array<unsigned char,4> mask{1,2,3,4};result.insert(result.end(),mask.begin(),mask.end());
    for(size_t i=0;i<body.size();++i)result.push_back(static_cast<unsigned char>(body[i])^mask[i%4]);return result;
}
static int protocolTests(){
    check(websocketAccept("dGhlIHNhbXBsZSBub25jZQ==")=="s3pPLMBiTxaQ9kYGzzhZRbK+xOo=","RFC WebSocket accept sample");
    check(handshake(request(),"TOKEN","127.0.0.1:12345").rfind("HTTP/1.1 101",0)==0,"authenticated Steam origin accepted");
    rejects([&]{handshake(request("wrong"),"TOKEN","127.0.0.1:12345");},"wrong token rejected");
    rejects([&]{handshake(request("TOKEN","https://evil.example"),"TOKEN","127.0.0.1:12345");},"foreign Origin rejected");
    rejects([&]{handshake(request(),"TOKEN","127.0.0.1:54321");},"wrong Host rejected");
    rejects([&]{auto text=request();text.insert(text.size()-2,"Origin: https://steamloopback.host\r\n");handshake(text,"TOKEN","127.0.0.1:12345");},"duplicate Origin rejected");
    rejects([&]{auto text=request();text.insert(text.size()-2,"Content-Length: 1\r\n");handshake(text,"TOKEN","127.0.0.1:12345");},"upgrade request body rejected");
    rejects([&]{handshake(std::string(9000,'X'),"TOKEN","127.0.0.1:12345");},"oversize HTTP header rejected");
    rejects([&]{auto text=request();const auto pos=text.find("dGhlIHNhbXBsZSBub25jZQ==");text[pos]=' ';handshake(text,"TOKEN","127.0.0.1:12345");},"noncanonical WebSocket key rejected");

    check(constantEqual("abc","abc")&&!constantEqual("abc","abd")&&!constantEqual("abc","ab"),"token equality compares all bytes");
    check(randomHex(32).size()==64,"cryptographic token length");
    auto input=masked("hello");auto frame=decode(input);check(frame.complete&&frame.payload=="hello"&&frame.bytes==input.size(),"masked UTF8 text parsed");
    input.pop_back();check(!decode(input).complete,"partial frame remains incomplete");
    check(decode(masked(std::string(500,'x'))).payload.size()==500,"extended frame accepted");
    rejects([&]{decode({0x81,0x01,'x'});},"unmasked client frame rejected");
    rejects([&]{decode({0x01,0x80,1,2,3,4});},"fragmented frame rejected");
    rejects([&]{decode({0xc1,0x80,1,2,3,4});},"compressed/unnegotiated RSV frame rejected");
    rejects([&]{decode(masked("x",2));},"binary frame rejected");
    rejects([&]{decode(masked(std::string(126,'x'),9));},"oversize control frame rejected");
    rejects([&]{decode({0x81,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff});},"unbounded 64bit frame rejected");
    check(decode(masked("ping",9)).opcode==9,"ping accepted");
    check(encode("test").size()==6,"server emits unmasked frame");
    rejects([&]{encode(std::string(kMaxPayload+1,'x'));},"oversize server output rejected");
    Broker empty;check(!empty.status()["active"].get<bool>(),"default broker is inactive");check(empty.bootstrap().empty(),"disabled broker exposes no credential or endpoint");
    check(!empty.reply("1",{{"runId","none"}}),"disabled broker cannot enqueue replies");
    std::cout<<"Broker protocol tests passed: "<<checks<<"\n";return 0;
}

static bool waitUntil(const std::function<bool()>& predicate,ULONGLONG budget){
    const auto start=GetTickCount64();do{if(predicate())return true;Sleep(5);}while(GetTickCount64()-start<budget);return predicate();
}
struct LoopbackSocket {
    SOCKET value=INVALID_SOCKET;
    explicit LoopbackSocket(const Json& boot,bool authenticate=true){
        value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(value==INVALID_SOCKET)throw std::runtime_error("client socket failed");
        try{
            const int receiveBytes=1024;const DWORD timeout=2000;
            if(setsockopt(value,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<const char*>(&receiveBytes),sizeof(receiveBytes))==SOCKET_ERROR ||
               setsockopt(value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout))==SOCKET_ERROR ||
               setsockopt(value,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout))==SOCKET_ERROR)throw std::runtime_error("client options failed");
            const auto endpoint=boot["endpoint"].get<std::string>();const auto colon=endpoint.rfind(':');
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(static_cast<unsigned short>(std::stoul(endpoint.substr(colon+1))));
            if(connect(value,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR)throw std::runtime_error("loopback connect failed");
            if(authenticate){
                auto header=request(boot["token"].get<std::string>());const auto host=std::string("127.0.0.1:")+std::to_string(ntohs(address.sin_port));
                header.replace(header.find("127.0.0.1:12345"),15,host);sendBytes(header.data(),header.size());
                std::string response;while(response.find("\r\n\r\n")==std::string::npos){char c;readBytes(&c,1);response+=c;if(response.size()>8192)throw std::runtime_error("unbounded handshake response");}
                check(response.rfind("HTTP/1.1 101 ",0)==0,"real WebSocket handshake succeeds");
            }
        }catch(...){close();throw;}
    }
    LoopbackSocket(const LoopbackSocket&)=delete;LoopbackSocket& operator=(const LoopbackSocket&)=delete;
    ~LoopbackSocket(){close();}
    void close(){if(value!=INVALID_SOCKET){closesocket(value);value=INVALID_SOCKET;}}
    void sendBytes(const char* data,size_t size){while(size){const int n=::send(value,data,static_cast<int>(size),0);if(n<=0)throw std::runtime_error("client send failed");data+=n;size-=static_cast<size_t>(n);}}
    void sendFrame(const std::string& body,unsigned opcode=1){const auto wire=masked(body,opcode);sendBytes(reinterpret_cast<const char*>(wire.data()),wire.size());}
    void readBytes(char* data,size_t size){while(size){const int n=recv(value,data,static_cast<int>(size),0);if(n<=0)throw std::runtime_error("client read failed");data+=n;size-=static_cast<size_t>(n);}}
    std::string readFrame(){
        unsigned char header[2]{};readBytes(reinterpret_cast<char*>(header),2);check((header[1]&0x80)==0,"server frame remains unmasked");
        size_t size=header[1]&127;if(size==126){unsigned char length[2];readBytes(reinterpret_cast<char*>(length),2);size=(static_cast<size_t>(length[0])<<8)|length[1];}
        check(size<65536,"bounded test server frame");std::string body(size,'\0');readBytes(body.data(),size);return body;
    }
    void allowProgress(){
        const auto before=writeProbe::bytes.load();u_long nonblocking=1;check(ioctlsocket(value,FIONBIO,&nonblocking)==0,"client read window opens");
        const bool progressed=waitUntil([&]{char buffer[8192];while(recv(value,buffer,sizeof(buffer),0)>0){}return writeProbe::bytes.load()>before;},2000);
        nonblocking=0;check(ioctlsocket(value,FIONBIO,&nonblocking)==0,"client returns to bounded blocking reads");
        check(progressed,"reading actual receiver bytes restores real sender progress");
    }
};
// Explicit bounded current-peer branch; default protocol and write tests stay separate.
static int peerAdmissionTests(bool stampCost){
    Broker broker;std::mutex queueMutex;std::vector<Json> queued;
    std::atomic<DWORD> requestThread{0};Json cpuSamples=Json::array();
    const auto sampleCpu=[&](const char* stage){
        static constexpr DWORD sentinel=0x1234;
        SetLastError(sentinel);
        const auto sample=broker.diagnosticWorkerCpu();
        check(GetLastError()==sentinel,"broker CPU diagnostics preserve caller LastError");
        if(sample)cpuSamples.push_back({{"stage",stage},{"threadId",sample->id},{"threadBirth100ns",std::to_string(sample->birth)},
            {"kernel100ns",sample->kernel},{"user100ns",sample->user}});
        return sample;
    };
    Json stampSamples=Json::array();
    const auto accept=[&](const Json& packet){return broker.acceptsRequest(packet.value("connection",std::string{}),packet.value("runId",std::string{}),packet.value("connectionRevision",0ull));};
    check(!broker.acceptsRequest("1",std::string(32,'a'),1),"inactive transport refuses a purported current request");
    check(!broker.requestStamp("")&&!broker.requestStamp("1"),"inactive transport publishes no ingress stamp");
    check(!broker.authenticatedPeerTick(),"inactive transport has no authenticated tick");
    check(!broker.matchesRunId("")&&!broker.matchesRunId(std::string(32,'a')),"inactive broker cannot accept a watch run hint");
    check(!sampleCpu("before-start"),"inactive broker has no worker sample");
    const Config config{[&](const std::string& connection,const Json& request){
        const auto current=broker.requestStamp(connection);if(!current)return;
        requestThread.store(GetCurrentThreadId());
        std::lock_guard lock(queueMutex);
        queued.push_back({{"connection",connection},{"runId",current->runId},{"revision",current->revision},{"connectionRevision",current->connectionRevision},{"request",request}});
    },{}};
    check(broker.start(config),"start owned loopback broker");
    check(!broker.authenticatedPeerTick(),"listening alone has no authenticated tick");
    const auto listening=sampleCpu("listening");
    check(listening&&listening->birth&&listening->id!=GetCurrentThreadId(),"listening broker has a distinct real worker");
    const auto boot=broker.bootstrap();Json oldPacket,newPacket;
    const auto run=boot.at("runId").get<std::string>();
    auto wrongRun=run;wrongRun[0]=run[0]=='a'?'b':'a';
    check(broker.matchesRunId(run)&&!broker.matchesRunId(wrongRun),"only current watch run matches before peer authentication");
    {
        const auto firstBegan=GetTickCount64();LoopbackSocket first(boot);
        first.sendFrame(Json{{"command","snapshot"},{"runId",boot["runId"]},{"isCurrent",true},{"connectionRevision",999999}}.dump());
        check(waitUntil([&]{std::lock_guard lock(queueMutex);return queued.size()==1;},1000),"original authenticated A request reaches native callback");
        {std::lock_guard lock(queueMutex);oldPacket=queued.front();}
        check(accept(oldPacket),"A is accepted only while its native receipt is current");
        const auto firstTick=broker.authenticatedPeerTick();
        check(firstTick&&*firstTick>=firstBegan&&*firstTick<=GetTickCount64(),"A tick is latched in the real handshake window");
        const auto active=sampleCpu("authenticated-request");
        check(active&&active->id==requestThread.load()&&active->id==listening->id&&active->birth==listening->birth&&
            active->kernel>=listening->kernel&&active->user>=listening->user,"actual request worker matches sampled object and cumulative counters");
        first.close();check(waitUntil([&]{return !broker.status().value("connected",false);},1000),"actual A socket close retires the native peer");
        check(!accept(oldPacket),"queued A cannot dispatch after its actual disconnect");
        check(!broker.requestStamp(oldPacket.at("connection").get<std::string>()),"closed A publishes no ingress stamp");
        check(!broker.authenticatedPeerTick(),"closed A publishes no authenticated tick");
        const auto disconnected=sampleCpu("peer-disconnected");
        check(disconnected&&disconnected->id==active->id&&disconnected->birth==active->birth,"peer disconnect does not retire listening worker identity");
        check(broker.matchesRunId(run),"closed peer does not invalidate this broker run's wake hint");
        const auto closed=broker.status().at("peerClose");
        check(closed["closeReason"]=="remote-eof"||closed["closeReason"]=="socket-close","actual remote EOF has a bounded close cause");
        check(closed.at("socketError")==0&&!closed.at("websocketCloseCodeKnown").get<bool>(),"orderly remote EOF has no fabricated error or WS code");
        const auto secondBegan=GetTickCount64();LoopbackSocket second(boot);
        second.sendFrame(Json{{"command","snapshot"},{"runId",boot["runId"]},{"isCurrent",false},{"connectionRevision",0}}.dump());
        check(waitUntil([&]{std::lock_guard lock(queueMutex);return queued.size()==2;},1000),"authenticated B request reaches native callback");
        {std::lock_guard lock(queueMutex);newPacket=queued.back();}
        check(oldPacket["connection"]!=newPacket["connection"],"new socket receives a distinct native connection");
        check(accept(newPacket),"native B fact is valid even when client says isCurrent false");
        const auto secondTick=broker.authenticatedPeerTick();
        check(secondTick&&*secondTick>=secondBegan&&*secondTick<=GetTickCount64(),"B publishes its own authenticated tick");
        check(!accept(oldPacket),"retired A stays refused even when client says isCurrent true");
        auto forged=newPacket;forged["runId"]=std::string(32,'f');check(!accept(forged),"wrong native run is refused");
        forged=newPacket;forged["connection"]="999999";check(!accept(forged),"unknown connection is refused");
        forged=newPacket;forged["connectionRevision"]=newPacket["connectionRevision"].get<unsigned long long>()+1;check(!accept(forged),"unobserved future native serial is refused");
        forged=newPacket;forged["connectionRevision"]=0;check(!accept(forged),"missing native serial cannot borrow client payload metadata");
        const auto stable=broker.status();for(unsigned i=0;i<100;++i)check(accept(newPacket),"stable reads retain native B");
        check(broker.status()==stable,"dispatch checks do not change transport revision or state");
        const auto connection=newPacket.at("connection").get<std::string>();
        const auto beforeReply=broker.requestStamp(connection);
        check(beforeReply&&beforeReply->runId==stable.at("runId").get_ref<const std::string&>()&&beforeReply->connectionRevision==stable.at("connectionRevision").get<unsigned long long>()&&beforeReply->revision==stable.at("revision").get<unsigned long long>(),"ingress stamp matches same-state full diagnostic receipt");
        check(!broker.requestStamp(oldPacket.at("connection").get<std::string>())&&!broker.requestStamp(""),"A or no peer cannot borrow B receipt");
        check(broker.reply(connection,{{"type","snapshot"},{"runId",beforeReply->runId},{"snapshot",{{"revision",42u},{"runId",beforeReply->runId}}}}),"owner snapshot advances original revision");
        const auto nextStamp=broker.requestStamp(connection);
        check(nextStamp&&nextStamp->revision==42&&nextStamp->connectionRevision==beforeReply->connectionRevision&&nextStamp->runId==beforeReply->runId,"snapshot revision updates without changing the ingress owner");
        check(broker.authenticatedPeerTick()==secondTick,"snapshot reply does not renew the authentication tick");
        if(stampCost){
            // Within the same real stable peer, compare the exact old JSON
            // snapshot operation with the new small copy. No IPC/network sends.
            constexpr unsigned iterations=25000;
            const auto sample=[&](bool typed){
                ULONG64 c0=0,c1=0;LARGE_INTEGER t0{},t1{},frequency{};QueryPerformanceFrequency(&frequency);
                const bool counted=QueryThreadCycleTime(GetCurrentThread(),&c0)!=FALSE;QueryPerformanceCounter(&t0);
                unsigned long long checksum=0;
                for(unsigned i=0;i<iterations;++i){
                    if(typed){const auto stamp=broker.requestStamp(connection);if(stamp)checksum+=stamp->runId.size()+stamp->revision+stamp->connectionRevision;}
                    else{const auto status=broker.status();if(status.value("connection",std::string{})==connection)checksum+=status.at("runId").get_ref<const std::string&>().size()+status.at("revision").get<unsigned long long>()+status.at("connectionRevision").get<unsigned long long>();}
                }
                QueryPerformanceCounter(&t1);const bool cyclesKnown=QueryThreadCycleTime(GetCurrentThread(),&c1)!=FALSE&&counted;
                check(checksum==iterations*(nextStamp->runId.size()+nextStamp->revision+nextStamp->connectionRevision),"both paths return identical real peer receipt throughout cost window");
                stampSamples.push_back({{"typed",typed},{"iterations",iterations},{"elapsedUs",1000000.0*double(t1.QuadPart-t0.QuadPart)/double(frequency.QuadPart)},{"threadCyclesKnown",cyclesKnown},{"threadCycles",cyclesKnown?Json(c1-c0):Json(nullptr)}});
            };
            for(unsigned r=0;r<5;++r){sample((r&1)!=0);sample((r&1)==0);}
        }
        // Each currently exposed command family must cross the REAL native
        // broker whitelist; callback acceptance is separate from owner mutation.
        for(const char* command:{"snapshot","game.setField","fan.setEnabled","fan.setPreset","fan.setChoice","frame.setField","touchpad.setField","global.setField","audioOptimization.setEnabled","monitor.setEnabled","performance.setControls","hud.status","hud.ack","wallpaper.context","wallpaper.status","wallpaper.neighbors","updater.status","updater.request"}){
            size_t before=0;{std::lock_guard lock(queueMutex);before=queued.size();}
            second.sendFrame(Json{{"command",command},{"runId",boot["runId"]},{"args",Json::object()}}.dump());
            check(waitUntil([&]{std::lock_guard lock(queueMutex);return queued.size()==before+1;},1000),"visible command family reaches native callback");
            std::lock_guard lock(queueMutex);check(queued.back()["request"]["command"]==command&&accept(queued.back()),"original command and exact native peer reach owner boundary");
        }
        size_t beforeDenied=0;{std::lock_guard lock(queueMutex);beforeDenied=queued.size();}
        second.sendFrame(Json{{"command","unregistered.setField"},{"runId",boot["runId"]}}.dump());
        check(waitUntil([&]{return !broker.status().value("connected",false);},1000),"unregistered command closes peer before dispatch");
        {std::lock_guard lock(queueMutex);check(queued.size()==beforeDenied,"denied command never enters host callback");}
        check(broker.status().at("peerClose").at("closeReason")=="request-command-error","actual denied command identifies the closing stage without command payload");
        second.close();check(waitUntil([&]{return !broker.status().value("connected",false);},1000),"actual B close is observed");
        check(!accept(newPacket),"B request cannot dispatch after its socket closes");
    }
    Json observedCloseCodes=Json::array();
    for(const unsigned short closeCode:{1000,4001,4002}){LoopbackSocket closing(boot);std::string payload;payload+=char(closeCode>>8);payload+=char(closeCode&255);payload+="PRIVATE-CLOSE-REASON";
     closing.sendFrame(payload,8);check(waitUntil([&]{return !broker.status().value("connected",false);},1000),"actual WS close frame disconnects");
     const auto facts=broker.status().at("peerClose");check(facts.at("closeReason")=="remote-close"&&facts.at("websocketCloseCode")==closeCode&&facts.at("websocketCloseCodeKnown").get<bool>(),"actual WS numeric code retained");
     check(facts.dump().find("PRIVATE-CLOSE-REASON")==std::string::npos,"remote close reason text is never retained");
     check(ymcc::deckydiag::record("mirror-peer",{{"peerClose",facts}}).at("peerClose")==facts,"Actual numeric client cause survives the existing native filter");
     observedCloseCodes.push_back(facts.at("websocketCloseCode"));}
    Json terminalClose=Json::array();
    for(const char* scenario:{"close-and-fin","truncated-close-and-fin","request-and-fin"}){
        LoopbackSocket closing(boot);
        const std::string mode=scenario;
        auto wire=mode=="request-and-fin"?masked(Json{{"command","performance.setControls"},{"runId",run}}.dump()):masked(std::string("\x0f\xa2",2)+"PRIVATE-CLOSE-REASON",8);
        if(mode=="truncated-close-and-fin")wire.pop_back();
        size_t callbacks=0;{std::lock_guard lock(queueMutex);callbacks=queued.size();}
        const auto before=closeProbe::eof.load();
        closeProbe::entered.store(false);closeProbe::release.store(false);closeProbe::arm.store(true);
        closing.sendBytes(reinterpret_cast<const char*>(wire.data()),wire.size());
        const bool entered=waitUntil([]{return closeProbe::entered.load();},1000);
        const int ended=shutdown(closing.value,SD_SEND);closeProbe::release.store(true);
        check(entered&&ended==0,"real broker wait overlaps client half-close");
        check(waitUntil([&]{return !broker.status().value("connected",false);},1000),"terminal receive retires the owned peer");
        check(closeProbe::eof.load()==before+1,"real recv observed TCP EOF in the held drain");
        {std::lock_guard lock(queueMutex);check(queued.size()==callbacks,"terminal receive never dispatches a buffered request");}
        const auto facts=broker.status().at("peerClose");
        terminalClose.push_back({{"case",scenario},{"facts",facts}});
        const bool valid=mode=="close-and-fin";
        const bool correct=facts.at("closeReason")== (valid?"remote-close":"remote-eof")&&facts.at("websocketCloseCodeKnown")==valid&&
            (valid?facts.value("websocketCloseCode",0)==4002:!facts.contains("websocketCloseCode"));
        if(!correct)std::cerr<<Json{{"terminalClose",terminalClose},{"realRecvEof",true},{"callbacksAdded",0}}.dump()<<std::endl;
        check(correct,"terminal close keeps only a proven numeric cause");
        check(facts.dump().find("PRIVATE-CLOSE-REASON")==std::string::npos,"terminal diagnostics contain no reason text");
    }
    {LoopbackSocket owned(boot);broker.stop();const auto facts=broker.status().at("peerClose");check(facts.at("closeReason")=="owner-stop","owner stop differs from peer/network loss");
     const auto safe=ymcc::deckydiag::record("mirror-peer",{{"peerClose",facts}});check(safe.at("peerClose")==facts,"actual close facts survive the native allowlist");
     const auto privateFacts=ymcc::deckydiag::record("mirror-peer",{{"peerClose",{{"closeReason","PRIVATE"},{"token","SECRET"},{"socketError",-1},{"websocketCloseCodeKnown","true"}}}});
     check(!privateFacts.contains("peerClose"),"arbitrary strings and invalid close fact types rejected");
     check(!ymcc::deckydiag::record("runtime",{{"peerClose",facts}}).contains("peerClose"),"close facts cannot leak into unrelated events");}
    broker.stop();check(!accept(newPacket),"OFF/stop cannot be revived by a queued request");check(broker.bootstrap().empty(),"stopped broker exposes no endpoint");
    check(!broker.requestStamp(newPacket.at("connection").get<std::string>()),"stopped broker cannot emit an old receipt");
    check(!broker.authenticatedPeerTick(),"stopped broker cannot publish the old authenticated tick");
    check(!broker.matchesRunId(run),"stopped broker rejects the old watch run");
    check(!sampleCpu("stopped"),"stopped broker cannot publish old worker CPU");
    std::cout<<Json{{"ok",true},{"mode","peer-admission"},{"observedCloseCodes",std::move(observedCloseCodes)},{"checks",checks},{"actualWinsock",true},{"realSteam",false},{"clientCurrentFlagsTrusted",false},{"queuedRequestsReplayed",false},{"brokerStopped",true},{"requestStampCosts",stampSamples},{"workerCpuSamples",cpuSamples},{"terminalClose",terminalClose}}.dump()<<std::endl;return 0;
}
static void fillUntilBlocked(Broker& broker,const Json& boot){
    const auto blocked=writeProbe::blocked.load();const auto connection=broker.status()["connection"].get<std::string>();
    const Json message={{"type","write-deadline-test"},{"runId",boot["runId"]},{"payload",std::string(32768,'x')}};
    const auto start=GetTickCount64();unsigned sent=0;
    while(writeProbe::blocked.load()==blocked&&GetTickCount64()-start<6000&&sent<4096){
        const auto attempts=writeProbe::attempts.load();check(broker.reply(connection,message),"paced real reply accepted without queue overflow");++sent;
        check(waitUntil([&]{return writeProbe::attempts.load()>attempts||!broker.status()["connected"].get<bool>();},1000),"real socket send attempted");
        check(broker.status()["connected"].get<bool>(),"filling does not close peer via queue limit");
    }
    check(writeProbe::blocked.load()>blocked,"real non-reading loopback reaches WSAEWOULDBLOCK");
    std::cout<<Json{{"stage","actual-backpressure"},{"messages",sent},{"sendBytes",writeProbe::bytes.load()},{"blocked",writeProbe::blocked.load()}}.dump()<<std::endl;
}
static void outputBurstTests(){
    std::atomic<unsigned> accepted{0};const std::string payload(49152,'b');
    Broker broker;
    // All three replies are queued by the actual worker before its send event
    // runs. The receiver and the sender both use real Winsock.
    check(broker.start({[&](const std::string& connection,const Json& request){
        if(request.value("command",std::string{})!="hud.status")return;
        for(unsigned sequence=1;sequence<=3;++sequence){
            if(broker.reply(connection,{{"type","reply"},{"runId",request["runId"]},{"sequence",sequence},{"payload",payload}}))++accepted;
        }
    },{}}),"burst broker starts");
    const auto boot=broker.bootstrap();
    const auto sendBurst=[&](LoopbackSocket& socket){socket.sendFrame(Json{{"command","hud.status"},{"runId",boot["runId"]}}.dump());};
    {
        LoopbackSocket stalled(boot);fillUntilBlocked(broker,boot);sendBurst(stalled);
        check(waitUntil([&]{return !broker.status().value("connected",false);},2000),"real blocked peer still closes at the output limit");
        const auto facts=broker.status().at("peerClose");
        std::cout<<Json{{"stage","blocked-burst"},{"accepted",accepted.load()},{"peerClose",facts}}.dump()<<std::endl;
        check(accepted.load()==3&&facts.at("closeReason")=="output-limit"&&facts.at("pendingOutputBytes").get<size_t>()<=kMaxPayload*2,
            "genuine backpressure retains the same bound and reason");
        check(broker.bootstrap()==boot,"overflow retains the listener and run identity");
    }
    {
        LoopbackSocket reader(boot);sendBurst(reader);
        unsigned received=0;
        try{
            for(unsigned sequence=1;sequence<=3;++sequence){
                const auto reply=Json::parse(reader.readFrame());
                if(reply.at("sequence")!=sequence||reply.at("payload")!=payload)break;
                ++received;
            }
        }catch(const std::exception&){/* Report the actual close and frames received below. */}
        const auto state=broker.status();const bool connected=state.value("connected",false);
        std::cout<<Json{{"stage","reading-burst"},{"accepted",accepted.load()-3},{"receivedInOrder",received},
            {"connected",connected},{"peerClose",state.value("peerClose",Json())}}.dump()<<std::endl;
        check(accepted.load()==6&&received==3&&connected,"reading peer receives the whole burst in order without a false disconnect");
    }
    broker.stop();check(broker.bootstrap().empty(),"burst broker returns its endpoint");
}
static int writeDeadlineTests(bool burstOnly){
    WSADATA data{};check(WSAStartup(MAKEWORD(2,2),&data)==0,"test Winsock session starts");
    struct Cleanup{~Cleanup(){writeProbe::enabled.store(false);WSACleanup();}} cleanup;
    if(burstOnly){
        writeProbe::enabled.store(true);outputBurstTests();
        std::cout<<Json{{"ok",true},{"mode","write-deadline:output-burst"},{"checks",checks},{"actualWinsock",true}}.dump()<<std::endl;
        return 0;
    }
    Broker broker;std::atomic<unsigned> requests{0},disconnects{0};std::atomic<bool> replyFailed{false};
    Config config{[&](const std::string& connection,const Json& message){
        ++requests;if(!broker.reply(connection,{{"type","reply"},{"runId",message["runId"]},{"value",message.value("value",std::string{})}}))replyFailed=true;
    },[&](const auto&,const auto&,bool connected){if(!connected)++disconnects;}};
    auto normalMessage=[&](LoopbackSocket& client,const Json& boot){
        client.sendFrame(Json{{"command","hud.status"},{"runId",boot["runId"]},{"value","healthy"}}.dump());
        check(Json::parse(client.readFrame())["value"]=="healthy","authenticated normal request/reply works");
    };
    // Warm the same real Winsock consumer before taking a strict handle baseline.
    check(broker.start(config),"warm real broker starts");{
        const auto boot=broker.bootstrap();LoopbackSocket warm(boot);normalMessage(warm,boot);
    }broker.stop();DWORD handlesBefore=0;check(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=0,"initial owned handle count available");
    writeProbe::enabled.store(true);
    check(broker.start(config),"deadline broker starts");const auto boot=broker.bootstrap();
    LoopbackSocket stalled(boot);fillUntilBlocked(broker,boot);
    const auto firstProgress=writeProbe::lastProgress.load();unsigned appended=0,pings=0;
    auto keepBusy=[&](LoopbackSocket& client){
        const auto connection=broker.status()["connection"].get<std::string>();
        if(!connection.empty()){check(broker.reply(connection,{{"type","write-deadline-extra"},{"runId",boot["runId"]}}),"small appended reply fits bounded output");++appended;client.sendFrame("ping",9);++pings;}
    };
    const auto readAt=GetTickCount64()+4000;auto nextTraffic=GetTickCount64();
    while(GetTickCount64()<readAt){if(GetTickCount64()>=nextTraffic){keepBusy(stalled);nextTraffic+=750;}Sleep(5);}
    check(broker.status()["connected"].get<bool>(),"no premature write timeout before ten seconds");
    stalled.allowProgress();fillUntilBlocked(broker,boot);
    check(writeProbe::lastProgress.load()>=firstProgress+3000,"actual progress renews the original write deadline");
    nextTraffic=GetTickCount64();const auto waitStart=GetTickCount64();
    while(broker.status()["connected"].get<bool>()&&GetTickCount64()-waitStart<16000){
        if(GetTickCount64()-writeProbe::lastProgress.load()>12500)break;
        if(GetTickCount64()>=nextTraffic){keepBusy(stalled);nextTraffic+=750;}Sleep(5);
    }
    const bool expired=!broker.status()["connected"].get<bool>();const auto closedAt=GetTickCount64(),lastProgress=writeProbe::lastProgress.load();
    std::cout<<Json{{"stage","write-timeout"},{"closed",expired},{"sinceActualProgressMs",closedAt-lastProgress},{"sinceFirstProgressMs",closedAt-firstProgress},{"appended",appended},{"pings",pings}}.dump()<<std::endl;
    check(expired,"ten-second no-progress deadline closes authenticated non-reader despite new replies/pings");
    check(closedAt-lastProgress>=9950&&closedAt-lastProgress<=12500,"write timeout is ten real seconds from the last positive send");
    check(closedAt-firstProgress>11000,"actual byte progress extends beyond the original deadline");
    stalled.close();check(broker.bootstrap()==boot,"write timeout retains listener token and run identity");
    const auto idleWaits=writeProbe::infiniteWaits.load();LoopbackSocket recovered(boot);normalMessage(recovered,boot);
    const auto idleUntil=GetTickCount64()+10500;
    while(GetTickCount64()<idleUntil){check(broker.status()["connected"].get<bool>(),"healthy empty output has no write timeout");Sleep(25);}
    check(writeProbe::infiniteWaits.load()>idleWaits,"healthy production worker uses WSA_INFINITE");normalMessage(recovered,boot);
    fillUntilBlocked(broker,boot);const char fragment=static_cast<char>(0x81);recovered.sendBytes(&fragment,1);const auto fragmentAt=GetTickCount64();
    check(waitUntil([&]{return !broker.status()["connected"].get<bool>();},4500),"three-second fragment deadline wins over pending ten-second output");
    const auto fragmentMs=GetTickCount64()-fragmentAt;check(fragmentMs>=2900&&fragmentMs<4500,"fragment timeout retains original bound");recovered.close();
    LoopbackSocket stopping(boot);normalMessage(stopping,boot);fillUntilBlocked(broker,boot);
    const auto stoppedAt=GetTickCount64();broker.stop();const auto stopMs=GetTickCount64()-stoppedAt;stopping.close();
    check(stopMs<2000,"stop wakes real blocked writer without waiting ten seconds");
    check(!broker.status()["active"].get<bool>()&&!broker.status()["connected"].get<bool>()&&broker.bootstrap().empty(),"stop clears output ownership and transport metadata");
    DWORD handlesAfter=0;check(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=0,"final owned handle count available");
    check(handlesAfter<=handlesBefore,"real sockets events and worker handles return to warmed baseline");
    bool endpointClosed=false;try{LoopbackSocket closed(boot,false);}catch(...){endpointClosed=true;}check(endpointClosed,"stopped listener no longer accepts connections");
    check(!replyFailed.load(),"normal request callbacks never lose replies");
    std::cout<<Json{{"ok",true},{"mode","write-deadline"},{"checks",checks},{"actualWinsock",true},{"sendProgressMs",closedAt-lastProgress},{"fragmentMs",fragmentMs},{"stopMs",stopMs},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"requests",requests.load()},{"disconnects",disconnects.load()},{"tokenUnchangedAfterTimeout",true},{"endpointReleased",true}}.dump()<<std::endl;
    return 0;
}

int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="--peer-admission"){const bool stampCost=argc==3&&std::string(argv[2])=="--stamp-cost";if(argc!=2&&!stampCost)return 2;try{return peerAdmissionTests(stampCost);}catch(const std::exception& error){std::cerr<<"FAIL peer-admission: "<<error.what()<<std::endl;return 1;}}
    if(argc>1&&std::string(argv[1])=="--write-deadline"){const bool burstOnly=argc==3&&std::string(argv[2])=="--output-burst";if(argc!=2&&!burstOnly)return 2;try{return writeDeadlineTests(burstOnly);}catch(const std::exception& error){std::cerr<<"FAIL write-deadline: "<<error.what()<<std::endl;return 1;}}
    if(argc<2)return protocolTests();
    const bool relayMode = std::string(argv[1])=="--serve-relay-fixture";
    if(!relayMode && std::string(argv[1])!="--serve-fixture")return 2;
    Broker broker;std::atomic<int> snapshots{0},mutations{0};
    std::mutex stdoutMutex;
    auto emit=[&](const Json& value){std::lock_guard lock(stdoutMutex);std::cout<<value.dump()<<std::endl;};
    const Config relayConfig{[&](const std::string& connection,const Json& message){
        const auto runId=message["runId"].get<std::string>();
        if(relayMode){
            if(message["command"]=="snapshot")++snapshots;else ++mutations;
            emit({{"type","request"},{"connection",connection},{"runId",runId},{"revision",broker.status()["revision"]},{"request",message}});return;
        }
        if(message["command"]=="snapshot"){
            ++snapshots;
            const Json snapshot={{"runId",runId},{"generation",1},{"revision",1},{"ready",false},{"game",nullptr},
                {"fan",{{"supported",true},{"enabled",false},{"preset","balanced"},{"choices",Json::array({{{"data","balanced"},{"label","均衡转速"}}})}}},
                {"notice","Fixture owner; no hardware/config operations"}};
            broker.reply(connection,{{"type","snapshot"},{"runId",runId},{"snapshot",snapshot}});
        }else{++mutations;broker.reply(connection,{{"type","reply"},{"runId",runId},{"id",message.value("id",std::string{})},{"ok",false},{"error","READ_ONLY_IMPLEMENTATION"}});}
    },[&](const std::string& connection,const std::string& runId,bool connected) {
        if(relayMode)emit({{"type","peer"},{"connection",connection},{"runId",runId},{"connected",connected},{"revision",broker.status()["revision"]}});
    }};
    if(!broker.start(relayConfig))return 3;
    const auto firstBootstrap=broker.bootstrap();emit(firstBootstrap);
    std::string line;
    while(std::getline(std::cin,line)) {
        if(line=="stop")break;
        if(line=="attach"){ auto value=broker.status();value["type"]="attach";value["owner"]="YMCC-native";value["pid"]=GetCurrentProcessId();emit(value);continue; }
        // Fixture-only demand exercises the original Broker lifecycle.
        if(relayMode&&line=="rotate"){broker.stop();check(broker.start(relayConfig),"fixture broker renewal succeeds");emit(broker.bootstrap());continue;}
        if(relayMode){const auto reply=Json::parse(line);const bool accepted=broker.reply(reply.value("connection",std::string{}),reply["message"]);if(reply.contains("fixtureReplyProbe"))emit({{"type","deliveryProbe"},{"probe",reply["fixtureReplyProbe"]},{"accepted",accepted}});}
        else break;
    }
    broker.stop();
    check(!broker.status()["active"].get<bool>(),"stop releases broker");
    const bool restart=broker.start({[](const auto&,const auto&){},[](const auto&,const auto&,bool){}});
    check(restart,"explicit broker restart succeeds");
    check(broker.bootstrap()["runId"]!=firstBootstrap["runId"],"restart rotates session identity");
    check(broker.bootstrap()["token"]!=firstBootstrap["token"],"restart rotates authentication credential");
    broker.stop();
    std::cout<<Json{{"snapshots",snapshots.load()},{"mutationRequests",mutations.load()},{"activeAfterStop",false},{"endpointReleased",true},{"checks",checks}}.dump()<<std::endl;
}
