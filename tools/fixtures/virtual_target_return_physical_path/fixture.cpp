#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "virtual_target_return_physical_path.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <map>
#include <stdexcept>

namespace physical=ymcc::targetReturn::physicalPath;
using J=nlohmann::json;
// Exported and compiled, never called. Keeps the real ProductionReader ABI and
// CM imports in this EXE without executing any real device query in the test.
extern "C" __declspec(dllexport) unsigned productionReaderCompileOnly(const char* root,const char* target){
    return static_cast<unsigned>(physical::verify(root?root:"",target?target:"").state);
}
enum class IdMode { Normal, Empty, Unterminated, NonAscii };
struct Node {
    std::wstring id;
    DEVINST parent=0;
    CONFIGRET idResult=CR_SUCCESS,parentResult=CR_SUCCESS;
    IdMode idMode=IdMode::Normal;
};
struct Reader {
    std::map<DEVINST,Node> nodes;
    DEVINST target=1,treeRoot=100;
    CONFIGRET targetResult=CR_SUCCESS,rootResult=CR_SUCCESS;
    unsigned locateCalls=0,targetLocates=0,rootLocates=0,idCalls=0,parentCalls=0;
    bool flagsCorrect=true,queryPointersCorrect=true;
    std::wstring requestedTarget;
    std::vector<DEVINST> readNodes,parentNodes;
    J trace=J::array();
    CONFIGRET locate(PDEVINST out,DEVINSTID_W id,ULONG flags){
        ++locateCalls;flagsCorrect=flagsCorrect&&flags==CM_LOCATE_DEVNODE_NORMAL;queryPointersCorrect=queryPointersCorrect&&out;
        if(!out)return CR_FAILURE;
        if(!id){++rootLocates;trace.push_back({{"operation","locate-system-root"},{"result",rootResult},{"node",treeRoot}});*out=treeRoot;return rootResult;}
        ++targetLocates;requestedTarget=id;*out=target;trace.push_back({{"operation","locate-exact-target"},{"result",targetResult},{"node",target}});return targetResult;
    }
    CONFIGRET deviceId(DEVINST node,PWCHAR out,ULONG chars,ULONG flags){
        ++idCalls;readNodes.push_back(node);flagsCorrect=flagsCorrect&&flags==0;queryPointersCorrect=queryPointersCorrect&&out&&chars==ymcc::targetReturn::maxInstanceBytes+1;
        trace.push_back({{"operation","read-id"},{"node",node},{"bufferChars",chars}});
        if(!out||!chars)return CR_FAILURE;
        const auto found=nodes.find(node);if(found==nodes.end())return CR_NO_SUCH_DEVNODE;const auto& n=found->second;
        if(n.idResult!=CR_SUCCESS)return n.idResult;
        if(n.idMode==IdMode::Unterminated){std::fill_n(out,chars,L'A');return CR_SUCCESS;}
        if(n.idMode==IdMode::Empty){out[0]=0;return CR_SUCCESS;}
        if(n.idMode==IdMode::NonAscii){out[0]=L'\x4e2d';out[1]=0;return CR_SUCCESS;}
        if(n.id.size()+1>chars)return CR_BUFFER_SMALL;
        std::copy(n.id.begin(),n.id.end(),out);out[n.id.size()]=0;return CR_SUCCESS;
    }
    CONFIGRET parent(PDEVINST out,DEVINST node,ULONG flags){
        ++parentCalls;parentNodes.push_back(node);flagsCorrect=flagsCorrect&&flags==0;queryPointersCorrect=queryPointersCorrect&&out;
        trace.push_back({{"operation","parent"},{"node",node}});
        if(!out)return CR_FAILURE;
        const auto found=nodes.find(node);if(found==nodes.end())return CR_NO_SUCH_DEVNODE;
        *out=found->second.parent;return found->second.parentResult;
    }
};
const std::string rootId="USB\\VID_8086&PID_0028\\PHYSICAL_ROOT";
const std::string targetId="HID\\VID_8086&PID_0028&MI_00\\COLLECTION_A";
const std::string otherRoot="USB\\VID_8086&PID_0028\\PHYSICAL_ROOT_SIBLING";
const std::string systemRoot="HTREE\\ROOT\\0";
std::wstring wide(const std::string& id){return{id.begin(),id.end()};}
Reader exact(){Reader r;r.nodes[1]={wide(targetId),2};r.nodes[2]={L"USB\\INTERMEDIATE\\A",3};r.nodes[3]={wide(rootId),100};r.nodes[100]={wide(systemRoot),0,CR_SUCCESS,CR_NO_SUCH_DEVNODE};return r;}
Reader direct(){Reader r;r.nodes[1]={wide(rootId),100};r.nodes[100]={wide(systemRoot),0,CR_SUCCESS,CR_NO_SUCH_DEVNODE};return r;}
Reader unrelated(){Reader r;r.nodes[1]={wide(targetId),2};r.nodes[2]={wide(otherRoot),100};r.nodes[100]={wide(systemRoot),0,CR_SUCCESS,CR_NO_SUCH_DEVNODE};return r;}
Reader chain(unsigned length,unsigned selectedAt=0){Reader r;r.treeRoot=length;for(unsigned i=1;i<=length;++i){std::string id=i==1?targetId:i==selectedAt?rootId:"USB\\CHAIN\\NODE_"+std::to_string(i);r.nodes[i]={wide(id),i==length?0:i+1,CR_SUCCESS,static_cast<CONFIGRET>(i==length?CR_NO_SUCH_DEVNODE:CR_SUCCESS)};}return r;}
unsigned checks=0;J cases=J::array(),failures=J::array();std::string current;
void check(bool condition,const char* assertion){++checks;if(!condition)failures.push_back({{"case",current},{"assertion",assertion}});}
void expect(const physical::Result& result,physical::State state,physical::Reason reason,unsigned depth,CONFIGRET cr){
    check(result.state==state,"exact Proven/Rejected/Unknown classification");check(result.reason==reason,"precise diagnostic reason");check(result.depth==depth,"bounded exact depth diagnostic");check(result.configurationReturn==cr,"original CONFIGRET preserved");check(result.proven()==(state==physical::State::Proven),"proven helper never promotes another state");
}
void shape(const Reader& reader){
    check(reader.locateCalls<=2&&reader.targetLocates<=1&&reader.rootLocates<=1,"at most one target locate and one exceptional tree-root confirmation, no scan or retry");
    check(reader.idCalls<=16&&reader.parentCalls<=16,"at most sixteen ID and sixteen parent queries");
    check(reader.flagsCorrect&&reader.queryPointersCorrect,"read-only CM leaf arguments and fixed bounded buffer");
}
void run(const char* name,const std::function<void()>& body){current=name;const auto before=failures.size();try{body();}catch(const std::exception& e){failures.push_back({{"case",name},{"exception",e.what()}});}cases.push_back({{"name",name},{"basis","controlled CM reader; no physical-device API executed"},{"pass",failures.size()==before}});}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    run("ancestry/exact-parent-chain",[]{auto r=exact();expect(physical::verify(rootId,targetId,r),physical::State::Proven,physical::Reason::RootMatched,3,CR_SUCCESS);shape(r);check(r.targetLocates==1&&r.rootLocates==0&&r.idCalls==3&&r.parentCalls==2,"exact selected root reached by actual reader path, not inferred");check(r.requestedTarget==wide(targetId)&&r.readNodes==std::vector<DEVINST>({1,2,3}),"locate only exact target then follow parent order");});
    run("ancestry/direct-root-still-read",[]{auto r=direct();expect(physical::verify(rootId,rootId,r),physical::State::Proven,physical::Reason::RootMatched,1,CR_SUCCESS);shape(r);check(r.targetLocates==1&&r.idCalls==1&&r.parentCalls==0,"equal strings still locate and confirm actual ID");});
    run("ancestry/case-insensitive",[]{auto r=exact();expect(physical::verify("usb\\vid_8086&pid_0028\\physical_root","hid\\vid_8086&pid_0028&mi_00\\collection_a",r),physical::State::Proven,physical::Reason::RootMatched,3,CR_SUCCESS);shape(r);check(r.targetLocates==1&&r.idCalls==3,"case folding does not bypass any actual leaf read");});
    run("ancestry/same-vid-different-tree",[]{auto r=unrelated();expect(physical::verify(rootId,targetId,r),physical::State::Rejected,physical::Reason::TreeEnded,3,CR_NO_SUCH_DEVNODE);shape(r);check(r.rootLocates==1&&r.idCalls==3&&r.parentCalls==3,"same VID PID and path prefix do not prove physical-root ancestry");});
    run("ancestry/same-prefix-different-instance",[]{auto r=unrelated();r.nodes[2].id=wide(rootId+"_EXTRA");expect(physical::verify(rootId,targetId,r),physical::State::Rejected,physical::Reason::TreeEnded,3,CR_NO_SUCH_DEVNODE);shape(r);});
    run("ancestry/target-read-changed-to-root",[]{auto r=exact();r.nodes[1].id=wide(rootId);expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::TargetChanged,1,CR_SUCCESS);shape(r);check(r.parentCalls==0,"different target observed after locate cannot be accepted as requested root");});
    run("ancestry/same-vid-incomplete-tree-is-unknown",[]{auto r=unrelated();r.nodes[2].parentResult=CR_FAILURE;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::ParentQueryFailed,2,CR_FAILURE);shape(r);});
    const std::vector<std::string> invalid={"","USB\\ROOT","USB\\\\X","USB\\..\\X","USB\\ROOT\\.","USB\\ROOT\\X\\EXTRA","USB/ROOT/X","USB\\ROOT\\X Y",std::string("USB\\ROOT\\X\0Y",12),"USB\\ROOT\\"+std::string(504,'A')};
    for(std::size_t i=0;i<invalid.size();++i){const auto name="invalid/root/"+std::to_string(i);run(name.c_str(),[&,i]{auto r=exact();expect(physical::verify(invalid[i],targetId,r),physical::State::Rejected,physical::Reason::InvalidRoot,0,CR_SUCCESS);shape(r);check(r.locateCalls==0&&r.idCalls==0&&r.parentCalls==0,"invalid root cannot query any device");});}
    for(std::size_t i=0;i<invalid.size();++i){const auto name="invalid/target/"+std::to_string(i);run(name.c_str(),[&,i]{auto r=exact();expect(physical::verify(rootId,invalid[i],r),physical::State::Rejected,physical::Reason::InvalidTarget,0,CR_SUCCESS);shape(r);check(r.locateCalls==0&&r.idCalls==0&&r.parentCalls==0,"invalid target cannot query any device");});}
    run("limits/exact-512-byte-instance",[]{std::string id="USB\\ROOT\\";id.append(ymcc::targetReturn::maxInstanceBytes-id.size(),'A');Reader r;r.nodes[1]={wide(id),0};expect(physical::verify(id,id,r),physical::State::Proven,physical::Reason::RootMatched,1,CR_SUCCESS);shape(r);check(r.idCalls==1,"maximum valid record instance is read without truncation");});
    run("locate/device-missing",[]{auto r=exact();r.targetResult=CR_NO_SUCH_DEVNODE;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::TargetLocateFailed,0,CR_NO_SUCH_DEVNODE);shape(r);check(r.idCalls==0&&r.rootLocates==0,"missing target is not proof of unrelated complete tree");});
    run("locate/failure",[]{auto r=exact();r.targetResult=CR_FAILURE;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::TargetLocateFailed,0,CR_FAILURE);shape(r);});
    run("locate/success-with-null-node",[]{auto r=exact();r.target=0;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::MissingNode,0,CR_SUCCESS);shape(r);});
    run("direct/root-missing",[]{auto r=direct();r.targetResult=CR_NO_SUCH_DEVNODE;expect(physical::verify(rootId,rootId,r),physical::State::Unknown,physical::Reason::TargetLocateFailed,0,CR_NO_SUCH_DEVNODE);shape(r);check(r.targetLocates==1,"equal strings cannot bypass failed real locate leaf");});
    run("read/target-id-failure",[]{auto r=exact();r.nodes[1].idResult=CR_FAILURE;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::DeviceIdReadFailed,1,CR_FAILURE);shape(r);check(r.parentCalls==0,"read failure never follows an unverified node");});
    run("read/parent-id-failure",[]{auto r=exact();r.nodes[2].idResult=CR_NO_SUCH_DEVNODE;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::DeviceIdReadFailed,2,CR_NO_SUCH_DEVNODE);shape(r);});
    run("direct/root-id-read-failure",[]{auto r=direct();r.nodes[1].idResult=CR_FAILURE;expect(physical::verify(rootId,rootId,r),physical::State::Unknown,physical::Reason::DeviceIdReadFailed,1,CR_FAILURE);shape(r);check(r.idCalls==1,"equal strings cannot bypass actual ID-read failure");});
    run("read/empty-id",[]{auto r=exact();r.nodes[1].idMode=IdMode::Empty;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::DeviceIdMissing,1,CR_SUCCESS);shape(r);});
    run("read/nonterminated-success",[]{auto r=exact();r.nodes[1].idMode=IdMode::Unterminated;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::DeviceIdTruncated,1,CR_SUCCESS);shape(r);});
    run("read/buffer-small",[]{auto r=exact();r.nodes[1].idResult=CR_BUFFER_SMALL;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::DeviceIdReadFailed,1,CR_BUFFER_SMALL);shape(r);});
    run("read/non-ascii-id",[]{auto r=exact();r.nodes[1].idMode=IdMode::NonAscii;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::DeviceIdInvalid,1,CR_SUCCESS);shape(r);});
    run("read/malformed-id",[]{auto r=exact();r.nodes[1].id=L"USB\\..\\BROKEN";expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::DeviceIdInvalid,1,CR_SUCCESS);shape(r);});
    run("read/root-id-changed",[]{auto r=exact();r.nodes[3].id=wide(otherRoot);expect(physical::verify(rootId,targetId,r),physical::State::Rejected,physical::Reason::TreeEnded,4,CR_NO_SUCH_DEVNODE);shape(r);});
    run("parent/failure",[]{auto r=exact();r.nodes[1].parentResult=CR_FAILURE;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::ParentQueryFailed,1,CR_FAILURE);shape(r);check(r.rootLocates==0,"ordinary parent error is never tree-root completion");});
    run("parent/missing-middle-is-not-tree-end",[]{auto r=exact();r.nodes[1].parentResult=CR_NO_SUCH_DEVNODE;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::ParentQueryFailed,1,CR_NO_SUCH_DEVNODE);shape(r);check(r.rootLocates==1,"missing middle is distinguished from confirmed system root");});
    run("parent/system-root-query-failed",[]{auto r=unrelated();r.rootResult=CR_FAILURE;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::TreeRootQueryFailed,3,CR_FAILURE);shape(r);});
    run("parent/system-root-success-zero",[]{auto r=unrelated();r.treeRoot=0;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::ParentQueryFailed,3,CR_NO_SUCH_DEVNODE);shape(r);});
    run("parent/success-zero-parent",[]{auto r=exact();r.nodes[1].parent=0;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::MissingParent,1,CR_SUCCESS);shape(r);});
    run("cycle/self-devinst",[]{auto r=exact();r.nodes[1].parent=1;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::Cycle,1,CR_SUCCESS);shape(r);check(r.idCalls==1&&r.parentCalls==1,"cycle stops before rereading a devinst");});
    run("cycle/two-devinst",[]{auto r=exact();r.nodes[2].parent=1;expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::Cycle,2,CR_SUCCESS);shape(r);check(r.idCalls==2&&r.parentCalls==2,"two-node cycle stops before another read");});
    run("cycle/repeated-id-different-handles",[]{auto r=exact();r.nodes[2].id=wide(targetId);expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::RepeatedInstance,2,CR_SUCCESS);shape(r);check(r.parentCalls==1,"repeated identity cannot continue toward convenient root");});
    run("limits/root-at-sixteenth-node",[]{auto r=chain(17,16);expect(physical::verify(rootId,targetId,r),physical::State::Proven,physical::Reason::RootMatched,16,CR_SUCCESS);shape(r);check(r.idCalls==16&&r.parentCalls==15,"sixteenth exact node may prove root without reading seventeenth");});
    run("limits/root-at-seventeenth-node",[]{auto r=chain(18,17);expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::DepthLimit,16,CR_SUCCESS);shape(r);check(r.idCalls==16&&r.parentCalls==16&&std::find(r.readNodes.begin(),r.readNodes.end(),static_cast<DEVINST>(17))==r.readNodes.end(),"never query seventeenth node or claim its root");});
    run("limits/confirmed-tree-end-at-sixteen",[]{auto r=chain(16);expect(physical::verify(rootId,targetId,r),physical::State::Rejected,physical::Reason::TreeEnded,16,CR_NO_SUCH_DEVNODE);shape(r);check(r.idCalls==16&&r.parentCalls==16&&r.rootLocates==1,"tree-root confirmation is bounded even at depth cap");});
    run("limits/deep-unrelated-tree-truncated",[]{auto r=chain(40);expect(physical::verify(rootId,targetId,r),physical::State::Unknown,physical::Reason::DepthLimit,16,CR_SUCCESS);shape(r);});
    run("diagnostics/names",[]{check(std::string(physical::stateName(physical::State::Proven))=="proven","stable proven diagnostic");check(std::string(physical::stateName(physical::State::Unknown))=="unknown","stable unknown diagnostic");check(std::string(physical::reasonName(physical::Reason::TreeEnded))=="confirmed-tree-root-without-match","tree completion diagnostic records positive confirmation");});
    const J result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",checks},{"cases",cases},{"failures",failures},{"realDeviceApiExecuted",false},{"readerBasis","controlled CM leaf responses through actual production verify template"},{"productionReaderCompiledAndExportedNotCalled",true},{"enumerationWritesRetriesThreadsOrTimers",false},{"productOrHostRun",false},{"hardwareAccepted",false}};
    std::ofstream output(std::filesystem::path(argv[1]),std::ios::binary);output<<result.dump(2)<<"\n";
    std::cout<<J({{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",checks},{"failures",failures}}).dump()<<"\n";
    return failures.empty()?0:1;
}
