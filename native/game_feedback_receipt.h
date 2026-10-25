#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
namespace ymcc::game_feedback {
inline constexpr std::array<std::string_view,6> fieldNames={
    "gameFeedbackGeneration","gameFeedbackRevision","gameFeedbackLeft",
    "gameFeedbackRight","gameFeedbackCompatibility","gameFeedbackSdkSequence"};
inline int fieldId(std::string_view name) noexcept {
    for(unsigned i=0;i<fieldNames.size();++i)if(name==fieldNames[i])return static_cast<int>(i);
    return -1;
}
struct Sample {
    bool present=false,valid=false;
    std::uint64_t generation=0,revision=0;
    std::uint32_t sdkSequence=0;
    std::uint8_t left=0,right=0,compatibility=0;
};
inline Sample fromWords(const std::array<std::uint64_t,6>& words,unsigned seen,bool invalid=false) noexcept {
    Sample r;r.present=seen!=0;
    if(seen!=0x3fu||invalid||words[0]==0||words[1]==0||words[2]>255||words[3]>255||
        (words[4]!=1&&words[4]!=2&&words[4]!=3&&words[4]!=4)||words[5]>std::numeric_limits<std::uint32_t>::max())return r;
    r.valid=true;r.generation=words[0];r.revision=words[1];r.left=static_cast<std::uint8_t>(words[2]);
    r.right=static_cast<std::uint8_t>(words[3]);r.compatibility=static_cast<std::uint8_t>(words[4]);
    r.sdkSequence=static_cast<std::uint32_t>(words[5]);return r;
}
inline bool same(const Sample& a,const Sample& b) noexcept {
    return a.present==b.present&&a.valid==b.valid&&(!a.valid||
        (a.generation==b.generation&&a.revision==b.revision&&a.sdkSequence==b.sdkSequence&&
         a.left==b.left&&a.right==b.right&&a.compatibility==b.compatibility));
}
struct Stats {
    bool bound=false,pending=false;
    std::uint64_t epoch=0,power=0,generation=0,received=0,rejected=0;
    Sample last;
};
// The native caller owns g_inputHostMx for every operation. This inbox records
// validated, admitted receipts only; it grants no physical output authority.
class Inbox {
    Stats state_{};
    unsigned protocol_=0;
    Sample staged_{};
    std::uint64_t stagedSequence_=0,lastRevision_=0;
public:
    void revoke() noexcept {
        state_.bound=false;state_.pending=false;state_.last={};protocol_=0;
        staged_={};stagedSequence_=0;lastRevision_=0;
    }
    void bind(std::uint64_t epoch,std::uint64_t power,std::uint64_t generation,unsigned protocol) noexcept {
        state_={};protocol_=0;staged_={};stagedSequence_=0;lastRevision_=0;
        if(epoch&&power&&generation&&(protocol==1||protocol==2||protocol==3)){protocol_=protocol;state_.bound=true;state_.epoch=epoch;state_.power=power;state_.generation=generation;}
    }
    void stage(const Sample& sample,std::uint64_t sequence) noexcept {staged_=sample;stagedSequence_=sequence;}
    bool commit(std::uint64_t sequence,std::uint64_t epoch,std::uint64_t power,bool sourceAdmitted) noexcept {
        const auto sample=staged_;const auto stagedSequence=stagedSequence_;staged_={};stagedSequence_=0;
        if(!sample.present)return false;
        if(!state_.bound||!sourceAdmitted||sequence!=stagedSequence||epoch!=state_.epoch||power!=state_.power||
            !sample.valid||((protocol_==1&&sample.compatibility!=1&&sample.compatibility!=2)||(protocol_==2&&sample.compatibility!=3)||(protocol_==3&&sample.compatibility!=4))||sample.generation!=state_.generation||sample.revision<=lastRevision_){++state_.rejected;return false;}
        state_.last=sample;state_.pending=true;lastRevision_=sample.revision;++state_.received;return true;
    }
    Stats snapshot() const noexcept {return state_;}
};
}
