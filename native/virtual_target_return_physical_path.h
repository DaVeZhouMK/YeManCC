#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "virtual_target_return_record.h"
#ifdef _WIN32
#include <windows.h>
#include <cfgmgr32.h>
#include <array>

// Exactly one caller-selected target/root ancestry observation, not source,
// device-return, hidden-state or ownership authority. No enumeration or writes.
namespace ymcc::targetReturn::physicalPath {
inline constexpr unsigned maxDepth=16; // Nodes read, including target (depth 1).
enum class State { Proven, Rejected, Unknown };
enum class Reason {
    InvalidRoot, InvalidTarget, TargetLocateFailed, MissingNode,
    DeviceIdReadFailed, DeviceIdMissing, DeviceIdTruncated, DeviceIdInvalid,
    TargetChanged, Cycle, RepeatedInstance, RootMatched, TreeEnded,
    ParentQueryFailed, TreeRootQueryFailed, MissingParent, DepthLimit
};
struct Result {
    State state=State::Unknown;
    Reason reason=Reason::TargetLocateFailed;
    unsigned depth=0;
    CONFIGRET configurationReturn=CR_SUCCESS;
    bool proven()const noexcept{return state==State::Proven;}
};
inline const char* stateName(State state)noexcept{
    switch(state){case State::Proven:return "proven";case State::Rejected:return "rejected";default:return "unknown";}
}
inline const char* reasonName(Reason reason)noexcept{
    switch(reason){
    case Reason::InvalidRoot:return "invalid-root";
    case Reason::InvalidTarget:return "invalid-target";
    case Reason::TargetLocateFailed:return "target-locate-failed";
    case Reason::MissingNode:return "missing-devinst";
    case Reason::DeviceIdReadFailed:return "device-id-read-failed";
    case Reason::DeviceIdMissing:return "device-id-empty";
    case Reason::DeviceIdTruncated:return "device-id-truncated";
    case Reason::DeviceIdInvalid:return "device-id-invalid";
    case Reason::TargetChanged:return "located-target-id-changed";
    case Reason::Cycle:return "devinst-cycle";
    case Reason::RepeatedInstance:return "repeated-instance-id";
    case Reason::RootMatched:return "exact-root-reached";
    case Reason::TreeEnded:return "confirmed-tree-root-without-match";
    case Reason::ParentQueryFailed:return "parent-query-failed";
    case Reason::TreeRootQueryFailed:return "tree-root-query-failed";
    case Reason::MissingParent:return "missing-parent-devinst";
    case Reason::DepthLimit:return "depth-limit";
    default:return "unknown-reason";}
}
// Leaves expose only the three read-only CM operations used by verify. A null
// locate ID is a bounded query of the system tree root, not enumeration.
struct ProductionReader {
    CONFIGRET locate(PDEVINST node,DEVINSTID_W id,ULONG flags){return CM_Locate_DevNodeW(node,id,flags);}
    CONFIGRET deviceId(DEVINST node,PWCHAR id,ULONG chars,ULONG flags){return CM_Get_Device_IDW(node,id,chars,flags);}
    CONFIGRET parent(PDEVINST out,DEVINST node,ULONG flags){return CM_Get_Parent(out,node,flags);}
};
template<class Reader>
inline Result verify(std::string_view rootInstanceId,std::string_view targetInstanceId,Reader& reader){
    if(!ymcc::targetReturn::detail::instance(rootInstanceId))return {State::Rejected,Reason::InvalidRoot,0,CR_SUCCESS};
    if(!ymcc::targetReturn::detail::instance(targetInstanceId))return {State::Rejected,Reason::InvalidTarget,0,CR_SUCCESS};
    // record::instance admits only its ASCII instance-ID alphabet. Converting
    // these already validated bytes to WCHAR cannot require locale/UTF repair.
    const auto expectedRoot=ymcc::targetReturn::detail::fold(rootInstanceId);
    const auto expectedTarget=ymcc::targetReturn::detail::fold(targetInstanceId);
    std::wstring target(targetInstanceId.begin(),targetInstanceId.end());
    DEVINST node=0;
    const auto located=reader.locate(&node,target.data(),CM_LOCATE_DEVNODE_NORMAL);
    if(located!=CR_SUCCESS)return {State::Unknown,Reason::TargetLocateFailed,0,located};
    if(!node)return {State::Unknown,Reason::MissingNode,0,CR_SUCCESS};
    std::array<DEVINST,maxDepth> visited{};
    std::array<std::string,maxDepth> identities;
    for(unsigned index=0;index<maxDepth;++index){
        for(unsigned previous=0;previous<index;++previous)
            if(visited[previous]==node)return {State::Unknown,Reason::Cycle,index,CR_SUCCESS};
        visited[index]=node;
        const unsigned depth=index+1;
        std::array<wchar_t,maxInstanceBytes+1> chars;
        chars.fill(static_cast<wchar_t>(0xffff));
        const auto read=reader.deviceId(node,chars.data(),static_cast<ULONG>(chars.size()),0);
        if(read!=CR_SUCCESS)return {State::Unknown,Reason::DeviceIdReadFailed,depth,read};
        const auto end=std::find(chars.begin(),chars.end(),L'\0');
        if(end==chars.end())return {State::Unknown,Reason::DeviceIdTruncated,depth,CR_SUCCESS};
        if(end==chars.begin())return {State::Unknown,Reason::DeviceIdMissing,depth,CR_SUCCESS};
        std::string observed;
        observed.reserve(static_cast<std::size_t>(end-chars.begin()));
        for(auto it=chars.begin();it!=end;++it){
            if(static_cast<unsigned>(*it)>127u)return {State::Unknown,Reason::DeviceIdInvalid,depth,CR_SUCCESS};
            observed.push_back(static_cast<char>(*it));
        }
        if(!ymcc::targetReturn::detail::instance(observed))return {State::Unknown,Reason::DeviceIdInvalid,depth,CR_SUCCESS};
        const auto folded=ymcc::targetReturn::detail::fold(observed);
        if(index==0&&folded!=expectedTarget)return {State::Unknown,Reason::TargetChanged,depth,CR_SUCCESS};
        for(unsigned previous=0;previous<index;++previous)
            if(identities[previous]==folded)return {State::Unknown,Reason::RepeatedInstance,depth,CR_SUCCESS};
        identities[index]=folded;
        if(folded==expectedRoot)return {State::Proven,Reason::RootMatched,depth,CR_SUCCESS};
        DEVINST parent=0;
        const auto parentResult=reader.parent(&parent,node,0);
        if(parentResult==CR_NO_SUCH_DEVNODE){
            // The same status can mean no parent or a disappearing device.
            // Reject only after independently confirming this exact node is
            // the system tree root. Any ambiguity remains Unknown.
            DEVINST treeRoot=0;
            const auto rootResult=reader.locate(&treeRoot,nullptr,CM_LOCATE_DEVNODE_NORMAL);
            if(rootResult!=CR_SUCCESS)return {State::Unknown,Reason::TreeRootQueryFailed,depth,rootResult};
            if(treeRoot&&treeRoot==node)return {State::Rejected,Reason::TreeEnded,depth,parentResult};
            return {State::Unknown,Reason::ParentQueryFailed,depth,parentResult};
        }
        if(parentResult!=CR_SUCCESS)return {State::Unknown,Reason::ParentQueryFailed,depth,parentResult};
        if(!parent)return {State::Unknown,Reason::MissingParent,depth,CR_SUCCESS};
        node=parent;
    }
    return {State::Unknown,Reason::DepthLimit,maxDepth,CR_SUCCESS};
}
inline Result verify(std::string_view rootInstanceId,std::string_view targetInstanceId){
    ProductionReader reader;
    return verify(rootInstanceId,targetInstanceId,reader);
}
} // namespace ymcc::targetReturn::physicalPath
#endif
