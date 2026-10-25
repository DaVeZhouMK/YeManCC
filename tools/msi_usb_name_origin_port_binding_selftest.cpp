// Actual diagnostic parser/port scan plus fully injected IoHooks. No live I/O.
#define NOMINMAX
#include "msi_usb_name_origin_windows.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#ifdef MSI_PORT_BINDING_LEGACY_REFERENCE
#include "port-binding-legacy-reference.h"
#endif
namespace usb=ymcc::msi::usbName;
using Bytes=std::vector<uint8_t>;
using Json=usb::Json;
const std::wstring rootKey=L"{36fc9e60-c465-11cf-8056-444553540000}\\0020";
const std::wstring otherKey=L"{36fc9e60-c465-11cf-8056-444553540000}\\0019";
void require(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
Bytes keyBytes(unsigned port,const std::wstring&value,size_t tail=sizeof(wchar_t)){
    const size_t offset=offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName);
    Bytes b(offset+(value.size()+1)*sizeof(wchar_t)+tail,0);
    auto*h=reinterpret_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(b.data());h->ConnectionIndex=port;h->ActualLength=ULONG(b.size());
    std::memcpy(b.data()+offset,value.c_str(),(value.size()+1)*sizeof(wchar_t));return b;
}
usb::IoReply keyReply(Bytes b){usb::IoReply r;r.ok=true;r.bytes=std::move(b);return r;}
// Independent fixed 35-byte empty-port response: no sizeof/offsetof or struct
// writer is used to synthesize the header under test. Index=1, status=0.
const Bytes empty35={
    0x01,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00};
struct Port {USB_CONNECTION_STATUS status=NoDeviceConnected;std::wstring key;DWORD connectionError=0,keyError=0;bool badConnectionIndex=false,badKeyIndex=false,truncated=false;Bytes literalHeader;};
struct Fake {
    std::map<unsigned,Port> ports;uint64_t now=100,advance=0;DWORD lastError=0,count=0;
    unsigned connectionQueries=0,keyQueries=0,descriptorQueries=0,opens=0,closes=0,waits=0,cancels=0,results=0;
    bool asynchronous=false,cancelIssued=false;unsigned activePort=0,cancelPort=0;DWORD activeCode=0,cancelCode=0;
    std::vector<unsigned> requestedKeys;std::vector<std::string> operations;
    Fake(){ports[4]={DeviceConnected,rootKey};ports[7]={DeviceConnected,otherKey};}
    usb::IoHooks hooks(){return{
        [&](const wchar_t*p){require(std::wstring(p)==L"fixture-parent-hub","unexpected hub path");operations.push_back("open");++opens;return reinterpret_cast<HANDLE>(uintptr_t(1));},
        [&]{operations.push_back("event");return reinterpret_cast<HANDLE>(uintptr_t(2));},[&](HANDLE h){operations.push_back(h==reinterpret_cast<HANDLE>(uintptr_t(2))?"close-event":"close-file");++closes;},
        [&](HANDLE,DWORD code,void*buffer,DWORD capacity,DWORD&returned,OVERLAPPED&){
            const unsigned i=*static_cast<ULONG*>(buffer);Port&port=ports[i];Bytes reply;DWORD failure=0;
            activePort=i;activeCode=code;cancelIssued=false;operations.push_back("issue:"+std::to_string(code)+":"+std::to_string(i));
            if(code==IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX){
                ++connectionQueries;failure=port.connectionError;
                if(!port.literalHeader.empty())reply=port.literalHeader;
                else{reply.resize(sizeof(USB_NODE_CONNECTION_INFORMATION_EX),0);
                    auto*c=reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(reply.data());c->ConnectionIndex=port.badConnectionIndex?i+1:i;c->ConnectionStatus=port.status;
                    if(port.truncated)reply.resize(offsetof(USB_NODE_CONNECTION_INFORMATION_EX,PipeList)-1);}
            }else if(code==IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME){
                ++keyQueries;requestedKeys.push_back(i);failure=port.status==NoDeviceConnected?ERROR_INVALID_PARAMETER:port.keyError;
                reply=keyBytes(port.badKeyIndex?i+1:i,port.key);
            }else{++descriptorQueries;throw std::runtime_error("port scan issued non-binding IOCTL");}
            now+=advance;if(failure){lastError=failure;returned=0;return false;}
            require(reply.size()<=capacity,"fake response exceeds request capacity");std::memcpy(buffer,reply.data(),reply.size());
            count=DWORD(reply.size());returned=count;lastError=asynchronous?ERROR_IO_PENDING:0;return !asynchronous;
        },
        [&](HANDLE,DWORD budget){operations.push_back("wait:"+std::to_string(budget));++waits;return DWORD(activePort==cancelPort&&activeCode==cancelCode&&!cancelIssued?WAIT_TIMEOUT:WAIT_OBJECT_0);},
        [&](HANDLE,OVERLAPPED&){operations.push_back("cancel");++cancels;cancelIssued=true;return true;},
        [&](HANDLE,OVERLAPPED&,DWORD&returned){operations.push_back("result");++results;returned=count;lastError=cancelIssued?ERROR_OPERATION_ABORTED:0;return !cancelIssued;},
        [&]{operations.push_back("error");return lastError;},[&]{operations.push_back("tick");return now;}};}
};
Json observations=Json::array();std::string activeCase;unsigned differentialScans=0;
Json oldQualification(const Json&r){
    Json projected={{"allPortBindingsKnown",r.at("allPortBindingsKnown")},{"driverKeyMatches",r.at("driverKeyMatches")},
        {"connectionIndex",r.at("connectionIndex")},{"portBindings",Json::array()}};
    for(const auto&port:r.at("portBindings")){Json item=Json::object();
        for(const auto*key:{"connectionIndex","connectionKnown","driverKeyRequired","driverKeyKnown","resolved","connectionStatus","driverKeyActualLength","driverKeyMatchesRoot","state"})
            if(port.contains(key))item[key]=port.at(key);
        projected["portBindings"].push_back(std::move(item));}
    return projected;
}
Json scan(Fake&fake,unsigned ports=12,const std::wstring&expected=rootKey,uint64_t deadline=500,bool pending=false){
#ifdef MSI_PORT_BINDING_LEGACY_REFERENCE
    Fake before=fake;
#endif
    Json result;
    {usb::ReadOnlyUsbIo io(fake.hooks());result=usb::exactRootPort(ports,expected,
        [&](DWORD code,Bytes request){return io.query(L"fixture-parent-hub",code,std::move(request),deadline);},
        [&]{fake.operations.push_back("scan-clock");return fake.now;},[&]{fake.operations.push_back("scan-pending");return pending||io.pending()!=0;},deadline);}
#ifdef MSI_PORT_BINDING_LEGACY_REFERENCE
    Json original;
    {usb::ReadOnlyUsbIo io(before.hooks());original=usb::beforeDiagnostics::exactRootPort(ports,expected,
        [&](DWORD code,Bytes request){return io.query(L"fixture-parent-hub",code,std::move(request),deadline);},
        [&]{before.operations.push_back("scan-clock");return before.now;},[&]{before.operations.push_back("scan-pending");return pending||io.pending()!=0;},deadline);}
    require(oldQualification(result)==oldQualification(original),"legacy port qualification changed");
    require(fake.operations==before.operations&&fake.now==before.now,"legacy I/O, cleanup or clock/pending query trace changed");
    require(fake.connectionQueries==before.connectionQueries&&fake.keyQueries==before.keyQueries&&fake.descriptorQueries==before.descriptorQueries&&
        fake.requestedKeys==before.requestedKeys&&fake.opens==before.opens&&fake.closes==before.closes&&fake.cancels==before.cancels,"legacy I/O counters changed");
    ++differentialScans;
#endif
    observations.push_back({{"case",activeCase},{"result",result},{"connectionQueries",fake.connectionQueries},{"keyQueries",fake.keyQueries},{"cancels",fake.cancels}});
    return result;
}
const Json& firstPort(const Json&r){return r.at("portBindings").at(0);}
void reasonIs(const Json&port,const char*reason){require(port.at("reason")==reason,"wrong diagnostic reason");}

bool admitted(const Json&r){return r.at("allPortBindingsKnown").get<bool>()&&r.at("driverKeyMatches")==1;}
int main(int argc,char**argv){
    if(argc!=2)return 2;Json cases=Json::array();unsigned failed=0;
    auto test=[&](const char*name,auto body){activeCase=name;try{body();cases.push_back({{"name",name},{"passed",true}});}catch(const std::exception&e){++failed;cases.push_back({{"name",name},{"passed",false},{"error",e.what()}});}};
    test("WDK-size-98-padded-key",[]{auto r=keyReply(keyBytes(4,rootKey));std::wstring key;
        require(r.bytes.size()==98,"WDK fixture layout changed");require(usb::driverKey(r,key)&&key==rootKey,"bounded padded key rejected");});
    test("key-with-terminator-at-boundary",[]{std::wstring key;require(usb::driverKey(keyReply(keyBytes(4,rootKey,0)),key)&&key==rootKey,"exact-boundary key rejected");});
    test("structure-tail-is-not-name",[]{auto r=keyReply(keyBytes(4,rootKey));r.bytes.back()=0x7f;std::wstring key;
        require(usb::driverKey(r,key)&&key==rootKey,"bytes after bounded NUL interpreted as name");});
    test("failed-reply-clears-key",[]{auto r=keyReply(keyBytes(4,rootKey));r.ok=false;std::wstring key=L"stale";require(!usb::driverKey(r,key)&&key.empty(),"failed reply reused key");});
    test("empty-driver-key-rejected",[]{std::wstring key;require(!usb::driverKey(keyReply(keyBytes(4,L"")),key),"empty key accepted");});
    test("unterminated-key-rejected",[]{auto r=keyReply(keyBytes(4,rootKey,0));r.bytes[r.bytes.size()-2]=1;std::wstring key;require(!usb::driverKey(r,key),"unterminated key accepted");});
    test("short-header-rejected",[]{std::wstring key;require(!usb::driverKey(keyReply(Bytes(7,0)),key),"short header accepted");});
    test("actual-length-exceeds-returned-rejected",[]{auto r=keyReply(keyBytes(4,rootKey));++reinterpret_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(r.bytes.data())->ActualLength;std::wstring key;require(!usb::driverKey(r,key),"unreturned bytes accepted");});
    test("actual-length-too-short-rejected",[]{auto r=keyReply(keyBytes(4,rootKey));reinterpret_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(r.bytes.data())->ActualLength=8;std::wstring key;require(!usb::driverKey(r,key),"missing string accepted");});
    test("odd-UTF16-span-rejected",[]{auto r=keyReply(keyBytes(4,rootKey));--reinterpret_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(r.bytes.data())->ActualLength;std::wstring key;require(!usb::driverKey(r,key),"odd UTF16 span accepted");});
    test("over-budget-key-rejected",[]{auto r=keyReply(keyBytes(4,rootKey));r.bytes.resize(4098);reinterpret_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(r.bytes.data())->ActualLength=4098;std::wstring key;require(!usb::driverKey(r,key),"oversized key accepted");});
    for(bool async:{false,true})test(async?"12-ports-10-empty-2-connected-pending":"12-ports-10-empty-2-connected-immediate",[=]{
        Fake f;f.asynchronous=async;auto r=scan(f);require(admitted(r)&&r["connectionIndex"]==4,"unique exact root port not found");
        require(f.connectionQueries==12&&f.keyQueries==2&&f.requestedKeys==std::vector<unsigned>({4,7}),"empty ports queried for keys");
        require(f.descriptorQueries==0&&f.closes==2*f.opens,"unexpected I/O or cleanup");});
    test("case-insensitive-full-key-match",[]{Fake f;f.ports[4].key=L"{36FC9E60-C465-11CF-8056-444553540000}\\0020";require(admitted(scan(f)),"exact case-insensitive key failed");});
    test("prefix-is-not-ownership",[]{Fake f;f.ports[4].key=rootKey+L"-other";require(!admitted(scan(f)),"prefix accepted as ownership");});
    test("different-full-key-rejected",[]{Fake f;f.ports[4].key=otherKey;require(!admitted(scan(f)),"unrelated full key admitted");});
    test("duplicate-root-key-rejected",[]{Fake f;f.ports[7].key=rootKey;auto r=scan(f);require(!admitted(r)&&r["driverKeyMatches"]==2,"duplicate root key admitted");});
    test("unknown-connected-port-blocks-known-match",[]{Fake f;f.ports[7].keyError=ERROR_INVALID_PARAMETER;auto r=scan(f);require(!admitted(r)&&!r["allPortBindingsKnown"].get<bool>()&&r["driverKeyMatches"]==1,"unreadable connected port ignored");});
    test("key-response-port-mismatch-rejected",[]{Fake f;f.ports[4].badKeyIndex=true;require(!admitted(scan(f)),"wrong key response port admitted");});
    test("connection-response-port-mismatch-rejected",[]{Fake f;f.ports[1].badConnectionIndex=true;require(!admitted(scan(f)),"wrong connection response port ignored");});
    test("error87-without-empty-receipt-still-rejected",[]{Fake f;f.ports[1].connectionError=ERROR_INVALID_PARAMETER;require(!admitted(scan(f)),"error87 guessed to be empty");});
    test("truncated-connection-rejected",[]{Fake f;f.ports[1].truncated=true;require(!admitted(scan(f)),"truncated connection ignored");});
    test("enumeration-failure-is-not-empty",[]{Fake f;f.ports[1].status=DeviceFailedEnumeration;require(!admitted(scan(f)),"failed enumeration guessed to be empty");});
    test("all-empty-is-not-root-match",[]{Fake f;f.ports.clear();auto r=scan(f);require(!admitted(r)&&r["driverKeyMatches"]==0&&f.keyQueries==0,"empty hub admitted");});
    for(unsigned n:{0u,65u})test(n==0?"zero-port-bound":"over-64-port-bound",[=]{Fake f;require(!admitted(scan(f,n))&&f.opens==0,"invalid port count issued I/O");});
    test("empty-root-key-no-query",[]{Fake f;require(!admitted(scan(f,12,L""))&&f.opens==0,"unknown root key queried");});
    test("expired-deadline-no-query",[]{Fake f;require(!admitted(scan(f,12,rootKey,100))&&f.opens==0,"expired scan issued I/O");});
    test("deadline-during-scan-rejects-partial-match",[]{Fake f;f.advance=50;auto r=scan(f,12,rootKey,400);require(!admitted(r)&&f.connectionQueries<12,"partial scan admitted after deadline");});
    test("pending-before-scan-no-query",[]{Fake f;require(!admitted(scan(f,12,rootKey,500,true))&&f.opens==0,"pending scan issued more I/O");});
    test("literal-35-byte-empty-matching-index",[]{
        require(empty35.size()==35,"literal header is not exactly 35 bytes");Fake f;f.ports[1].literalHeader=empty35;auto r=scan(f);const auto&p=firstPort(r);
        require(admitted(r)&&p["connectionKnown"]==true&&p["resolved"]==true,"matched empty header changed qualification");
        require(p["returnedHeaderBytes"]==35&&p["minHeaderBytes"]==35&&p["returnedHeaderHex"].get<std::string>().size()==70,"header witness not bounded");
        require(p["headerConnectionIndex"]==1&&p["requestIndexMatches"]==true&&p["headerConnectionStatus"]==0&&p["headerDeviceIsHub"]==0&&p["headerNumberOfOpenPipes"]==0,"literal header decoded incorrectly");
        reasonIs(p,"proven-empty-port");});
    test("literal-35-byte-empty-index-zero-still-unknown",[]{
        Fake f;f.ports[1].literalHeader=empty35;f.ports[1].literalHeader[0]=0;auto r=scan(f);const auto&p=firstPort(r);
        require(!admitted(r)&&r["driverKeyMatches"]==1&&r["allPortBindingsKnown"]==false,"one matched root with unknown empty port changed qualification");
        require(p["returnedHeaderBytes"]==35&&p["minHeaderBytes"]==35&&p["headerConnectionIndex"]==0&&p["requestIndexMatches"]==false&&p["headerConnectionStatus"]==0&&p["connectionKnown"]==false,"index-zero failure not exposed");
        reasonIs(p,"connection-index-mismatch");reasonIs(r,"port-binding-unresolved");});
    test("literal-34-byte-truncation-exposes-partial-header",[]{
        Fake f;f.ports[1].literalHeader=empty35;f.ports[1].literalHeader.resize(34);auto r=scan(f);const auto&p=firstPort(r);
        require(!admitted(r)&&r["driverKeyMatches"]==1&&p["returnedHeaderBytes"]==34&&p["minHeaderBytes"]==35,"short header changed qualification");
        require(p["headerConnectionIndex"]==1&&p["requestIndexMatches"]==true&&p["headerConnectionStatus"].is_null()&&p["returnedHeaderHex"].get<std::string>().size()==68,"truncated field guessed");
        reasonIs(p,"connection-header-too-short");});
    test("literal-header-witness-capped-at-35-with-extra-returned-data",[]{
        Fake f;f.ports[1].literalHeader=empty35;f.ports[1].literalHeader.resize(4096,0xff);auto r=scan(f);const auto&p=firstPort(r);
        require(admitted(r)&&p["returnedHeaderBytes"]==35&&p["minHeaderBytes"]==35&&p["returnedHeaderHex"].get<std::string>().size()==70,"witness copied unbounded trailing bytes or changed qualification");});
    test("literal-three-byte-index-is-not-decoded",[]{
        Fake f;f.ports[1].literalHeader=Bytes{1,0,0};auto r=scan(f);const auto&p=firstPort(r);
        require(!admitted(r)&&p["returnedHeaderBytes"]==3&&p["headerConnectionIndex"].is_null()&&p["requestIndexMatches"].is_null()&&p["headerConnectionStatus"].is_null(),"partial index read past returned bytes");reasonIs(p,"connection-header-too-short");});
    test("literal-header-fields-are-diagnostics-only",[]{
        Fake f;f.ports[1].literalHeader=empty35;f.ports[1].literalHeader[24]=1;f.ports[1].literalHeader[27]=2;auto r=scan(f);const auto&p=firstPort(r);
        require(admitted(r)&&p["headerDeviceIsHub"]==1&&p["headerNumberOfOpenPipes"]==2,"raw fields introduced an admission gate");});
    test("literal-unknown-status-keeps-known-header-unresolved",[]{
        Fake f;f.ports[1].literalHeader=empty35;f.ports[1].literalHeader[31]=127;auto r=scan(f);const auto&p=firstPort(r);
        require(!admitted(r)&&r["driverKeyMatches"]==1&&p["connectionKnown"]==true&&p["headerConnectionStatus"]==127&&p["resolved"]==false,"unknown status changed qualification");
        reasonIs(p,"unsupported-connection-status");});
    test("connection-api-failure-does-not-decode-request-buffer",[]{
        Fake f;f.ports[1].connectionError=ERROR_INVALID_PARAMETER;auto r=scan(f);const auto&p=firstPort(r);
        require(!admitted(r)&&r["driverKeyMatches"]==1&&p["connectionQueryOk"]==false&&p["returnedHeaderBytes"]==0&&p["headerConnectionIndex"].is_null()&&p["requestIndexMatches"].is_null(),"failed API leaked request header as a reply");
        require(p["connectionIoState"]=="start-failed"&&p["connectionIoError"]==ERROR_INVALID_PARAMETER,"API failure details absent");reasonIs(p,"connection-query-failed");});
    test("duplicate-match-has-explicit-summary",[]{Fake f;f.ports[7].key=rootKey;auto r=scan(f);
        require(!admitted(r)&&r["allPortBindingsKnown"]==true&&r["driverKeyMatches"]==2,"duplicate-match gate changed");reasonIs(r,"duplicate-root-driver-key");});
    test("driver-key-query-failure-reason",[]{Fake f;f.ports[7].keyError=ERROR_ACCESS_DENIED;auto r=scan(f);
        require(!admitted(r)&&r["driverKeyMatches"]==1,"unreadable peer allowed root join");reasonIs(r["portBindings"][6],"driver-key-query-failed");});
    test("driver-key-response-index-failure-reason",[]{Fake f;f.ports[4].badKeyIndex=true;auto r=scan(f);require(!admitted(r),"wrong driver-key index admitted");reasonIs(r["portBindings"][3],"driver-key-index-mismatch");});
    test("asynchronous-literal-index-zero-same-unknown",[]{Fake f;f.asynchronous=true;f.ports[1].literalHeader=empty35;f.ports[1].literalHeader[0]=0;auto r=scan(f);
        require(!admitted(r)&&r["driverKeyMatches"]==1&&f.waits==f.opens&&f.results==f.opens&&f.cancels==0&&f.closes==2*f.opens,"pending completion changed qualification or cleanup");reasonIs(firstPort(r),"connection-index-mismatch");});
    test("asynchronous-cancelled-connection-no-admitted-header",[]{Fake f;f.asynchronous=true;f.cancelPort=1;f.cancelCode=IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX;auto r=scan(f);const auto&p=firstPort(r);
        require(!admitted(r)&&r["driverKeyMatches"]==1&&f.cancels==1&&f.closes==2*f.opens,"cancelled response changed qualification or leaked handles");
        require(p["returnedHeaderBytes"]==0&&p["headerConnectionIndex"].is_null()&&p["connectionIoState"]=="completed-without-admitted-data","late/cancelled bytes reused");reasonIs(p,"connection-query-failed");});
    test("asynchronous-cancelled-peer-key-preserves-one-match-unknown",[]{Fake f;f.asynchronous=true;f.cancelPort=7;f.cancelCode=IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME;auto r=scan(f);
        require(!admitted(r)&&r["driverKeyMatches"]==1&&r["allPortBindingsKnown"]==false&&f.cancels==1&&f.closes==2*f.opens,"cancelled peer key relaxed one-match/unknown gate");reasonIs(r["portBindings"][6],"driver-key-query-failed");});
    test("budget-expired-before-scan-has-reason",[]{Fake f;auto r=scan(f,12,rootKey,100);require(!admitted(r)&&f.opens==0,"expired budget issued I/O");reasonIs(r,"scan-deadline-expired");});
    test("pending-before-scan-has-reason",[]{Fake f;auto r=scan(f,12,rootKey,500,true);require(!admitted(r)&&f.opens==0,"pending scan issued I/O");reasonIs(r,"prior-read-pending");});
    test("budget-completion-rejects-late-header",[]{Fake f;f.advance=400;auto r=scan(f);const auto&p=firstPort(r);
        require(!admitted(r)&&f.connectionQueries==1&&p["returnedHeaderBytes"]==0&&p["connectionIoState"]=="completed-without-admitted-data","late completion supplied admission bytes");reasonIs(p,"connection-query-failed");});
    test("log31-shaped-seven-index-zero-ports-one-match-remains-unknown",[]{
        Fake f;f.ports.clear();f.ports[2]={DeviceConnected,rootKey};f.ports[4]={DeviceConnected,otherKey};f.ports[6]={DeviceConnected,otherKey};
        for(unsigned i:{1u,3u,5u,7u,8u,9u,10u}){f.ports[i].literalHeader=empty35;f.ports[i].literalHeader[0]=0;}
        auto r=scan(f,10);require(!admitted(r)&&r["driverKeyMatches"]==1&&r["connectionIndex"]==2&&r["allPortBindingsKnown"]==false&&f.connectionQueries==10&&f.keyQueries==3,"log31-shaped old unknown decision changed");
        for(unsigned i:{1u,3u,5u,7u,8u,9u,10u})reasonIs(r["portBindings"][i-1],"connection-index-mismatch");
        // This is a synthetic hypothesis fixture, not reconstruction of absent log31 bytes.
    });
    Json report={{"passed",failed==0},{"caseCount",cases.size()},{"failureCount",failed},{"actualProductionFunctions",Json::array({"driverKey","exactRootPort","ReadOnlyUsbIo::query"})},{"systemHooksUsed",false},{"liveDeviceIo",false},{"realPlatformHandleOperations",false},{"differentialScanCount",differentialScans},{"originalGateAndIoTraceMatch",differentialScans>0},{"cases",cases},{"observations",observations}};
    std::ofstream output(argv[1],std::ios::binary);if(!output)return 2;output<<report.dump(2)<<'\n';output.close();if(!output)return 2;
    std::cout<<"MSI_USB_NAME_ORIGIN_PORT_BINDING_SELFTEST cases="<<cases.size()<<" failures="<<failed<<'\n';return failed?1:0;
}
