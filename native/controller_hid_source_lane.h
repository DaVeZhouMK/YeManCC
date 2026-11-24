#pragma once
#include <windows.h>
#include <Xinput.h>
#include <cstdint>

namespace ymcc::commonhid {
// Source data only. The existing reader mutex owns this state, on both the
// Raw Input and BUS paths. It never creates a target, worker, timer or retry.
struct Epoch {
    std::uint64_t power=0,topology=0,identity=0,occupant=0;
    unsigned slot=4;
    bool operator==(const Epoch&) const = default;
};
struct Sample {
    XINPUT_GAMEPAD pad{};
    std::uint64_t sequence=0,observedTick=0;
    bool independentTriggers=false;
    // Source facts only: combined Z never becomes independent LT or RT.
    bool completeBasic=false;
    std::uint32_t combinedTriggerRaw=0;
    bool isolationSharedRead=false;
};
inline bool basicActive(const XINPUT_GAMEPAD& p) noexcept {
    return p.wButtons || p.sThumbLX>1000 || p.sThumbLX< -1000 ||
        p.sThumbLY>1000 || p.sThumbLY< -1000 || p.sThumbRX>1000 ||
        p.sThumbRX< -1000 || p.sThumbRY>1000 || p.sThumbRY< -1000;
}
// A cached Raw report can still own buttons and triggers when XInput resumes
// updating the sticks. Do not switch those fields or age out a steady hold.
class CachedRawStickHandoff {
    Epoch epoch_{};
    std::uint64_t host_=0, sequence_=0;
    XINPUT_GAMEPAD nativeAtObservation_{};
    unsigned nativeStickMask_=0;
public:
    unsigned apply(std::uint64_t host,const Epoch& epoch,const XINPUT_GAMEPAD& native,Sample* raw) noexcept {
        if(!host || !raw || !raw->sequence || !raw->completeBasic || raw->isolationSharedRead){host_=0;return 0;}
        const auto moved=[](SHORT a,SHORT b){const int delta=int(a)-int(b);return delta>1000 || delta< -1000;};
        if(host_!=host || epoch_!=epoch || sequence_!=raw->sequence) {
            host_=host;epoch_=epoch;sequence_=raw->sequence;nativeAtObservation_=native;nativeStickMask_=0;
        }else{
            if(moved(native.sThumbLX,nativeAtObservation_.sThumbLX) || moved(native.sThumbLY,nativeAtObservation_.sThumbLY))nativeStickMask_|=1u;
            if(moved(native.sThumbRX,nativeAtObservation_.sThumbRX) || moved(native.sThumbRY,nativeAtObservation_.sThumbRY))nativeStickMask_|=2u;
        }
        if(nativeStickMask_&1u){raw->pad.sThumbLX=native.sThumbLX;raw->pad.sThumbLY=native.sThumbLY;}
        if(nativeStickMask_&2u){raw->pad.sThumbRX=native.sThumbRX;raw->pad.sThumbRY=native.sThumbRY;}
        return nativeStickMask_;
    }
};
class Lane {
    Epoch epoch_{};
    Sample sample_{};
    bool have_=false,selected_=false;
public:
    void clear() noexcept {epoch_={};sample_={};have_=selected_=false;}
    bool hasSample() const noexcept {return have_;}
    bool selected() const noexcept {return selected_;}
    void observe(const Epoch& e,const Sample& sample) noexcept {
        if(!e.power || !e.identity || !e.occupant || e.slot>=4 ||
           !sample.sequence || !sample.observedTick) {clear();return;}
        if(!have_ || !(epoch_==e))clear();
        if(have_ && sample.sequence<=sample_.sequence)return;
        epoch_=e;sample_=sample;have_=true;
    }
    bool read(const Epoch& e,bool eligible,const XINPUT_GAMEPAD& native,Sample& out,bool requireDisagreement=true) noexcept {
        // A lane without a sample is already clear.
        if(!have_)return false;
        if(!eligible || !(epoch_==e)){clear();return false;}
        // Actual disagreement arms the alternative. Neutral input is normal
        // and cannot trigger a restart, a controller fault or an acquisition.
        if(!selected_ && (!requireDisagreement || (basicActive(sample_.pad) && !basicActive(native))))selected_=true;
        if(!selected_)return false;
        out=sample_;
        if(!out.independentTriggers) {
            // A combined HID Z cannot recover simultaneous independent LT/RT.
            out.pad.bLeftTrigger=native.bLeftTrigger;
            out.pad.bRightTrigger=native.bRightTrigger;
        }
        // Event-driven hold/release is preserved until an actual report or
        // current source/registration/power change. A BUS cache read does not
        // advance sequence, refresh observedTick, or prove hardware freshness.
        return true;
    }
    bool readWakeFallback(const Epoch& e,bool eligible,const XINPUT_GAMEPAD& native,
        std::uint64_t now,Sample& out) noexcept {
        constexpr std::uint64_t maxAgeMs=250;
        if(!have_)return false;
        if(!sample_.completeBasic || now<sample_.observedTick || (!selected_ && now-sample_.observedTick>maxAgeMs) ||
           basicActive(native) || native.bLeftTrigger || native.bRightTrigger){clear();return false;}
        return read(e,eligible,native,out);
    }
    bool readColdPrimary(const Epoch& e,const XINPUT_GAMEPAD& native,
        std::uint64_t now,bool replacingDirect,Sample& out) noexcept {
        if(!have_ || !(epoch_==e) || !sample_.completeBasic)return false;
        if(!selected_ && (now<sample_.observedTick || now-sample_.observedTick>250))return false;
        // A fresh neutral report can retire a previously selected direct DOWN.
        return read(e,true,native,out,!replacingDirect);
    }
};
} // namespace ymcc::commonhid
