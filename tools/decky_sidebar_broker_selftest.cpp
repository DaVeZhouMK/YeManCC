#include "../native/decky_sidebar_broker.h"
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
int main(int argc,char** argv){
    if(argc<2)return protocolTests();
    const bool relayMode = std::string(argv[1])=="--serve-relay-fixture";
    if(!relayMode && std::string(argv[1])!="--serve-fixture")return 2;
    Broker broker;std::atomic<int> snapshots{0},mutations{0};
    std::mutex stdoutMutex;
    auto emit=[&](const Json& value){std::lock_guard lock(stdoutMutex);std::cout<<value.dump()<<std::endl;};
    if(!broker.start({[&](const std::string& connection,const Json& message){
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
    }}))return 3;
    const auto firstBootstrap=broker.bootstrap();emit(firstBootstrap);
    std::string line;
    while(std::getline(std::cin,line)) {
        if(line=="stop")break;
        if(line=="attach"){ auto value=broker.status();value["type"]="attach";emit(value);continue; }
        if(relayMode){const auto reply=Json::parse(line);broker.reply(reply.value("connection",std::string{}),reply["message"]);}
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
