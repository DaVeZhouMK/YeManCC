#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include "legion_input_protocol.h"
#include "legion_motion_source.h"
#include "legion_motion_message_clock.h"

namespace ymcc::legion_motion {
// Diagnostic bank data is independent of the single selected output lease.
struct BankPacket {
    Context context;
    Source captureSelection;
    std::uint64_t queuedAt=0,serial=0;
    std::size_t logicalBytes=0;
    unsigned bankCount=0;
};
struct BankView {Observation observation;std::uint64_t reportSerial=0;bool inLastReport=false;};
struct BankSnapshot {
    std::optional<BankPacket> lastReport;
    std::array<BankView,2> banks;
    bool reportContextCurrent=false;
};
// Caller serializes with the existing source mutex. Only two bounded value
// slots are retained. No GamepadMotion, calibration, selection or Host calls.
class BankEvidence {
    std::array<Lease,2> banks_;
    std::array<std::uint64_t,2> serials_{};
    std::optional<BankPacket> packet_;
    std::uint64_t serial_=0;
    static bool same(Context a,Context b) noexcept {
        return b.power!=0&&b.rawGeneration!=0&&a.power==b.power&&a.rawGeneration==b.rawGeneration;
    }
    static bool same(const Source& a,const Source& b) noexcept {
        return !b.physicalIdentity.empty()&&b.captureEpoch!=0&&a.physicalIdentity==b.physicalIdentity&&
            a.captureEpoch==b.captureEpoch&&a.sensorIndex==b.sensorIndex&&a.capabilityToken==b.capabilityToken;
    }
public:
    bool observe(const std::array<std::optional<legion_input::MotionAxes>,2>& axes,
        BankPacket packet,Context current,const Source& selected,bool ready,std::uint64_t epochStart,unsigned bankCount) {
        if(!ready||(bankCount!=1&&bankCount!=2)||packet.bankCount!=bankCount||selected.sensorIndex>=bankCount||
            !same(packet.context,current)||!same(packet.captureSelection,selected)||
            !legion_motion_clock::belongsToEpoch(packet.queuedAt,epochStart))return false;
        if(++serial_==0){serial_=1;serials_={};}
        packet.serial=serial_;packet_=packet;
        for(unsigned index=0;index<bankCount;++index){
            if(!axes[index])continue;
            const auto& value=*axes[index];auto source=packet.captureSelection;source.sensorIndex=index;
            const Sample sample{{value.gx,value.gy,value.gz},{value.ax,value.ay,value.az},packet.queuedAt,packet.context,source};
            auto expected=selected;expected.sensorIndex=index;
            if(banks_[index].publish(sample,current,expected,true))serials_[index]=serial_;
        }
        return true;
    }
    BankSnapshot inspect(Context context,const Source& selected,bool ready,std::uint64_t epochStart,
        unsigned bankCount,std::uint64_t now,std::uint64_t maxAge) const {
        BankSnapshot result;result.lastReport=packet_;
        result.reportContextCurrent=ready&&packet_&&(bankCount==1||bankCount==2)&&packet_->bankCount==bankCount&&
            selected.sensorIndex<bankCount&&same(packet_->context,context)&&same(packet_->captureSelection,selected)&&
            legion_motion_clock::belongsToEpoch(packet_->queuedAt,epochStart);
        for(unsigned index=0;index<bankCount&&index<2;++index){
            auto source=selected;source.sensorIndex=index;
            result.banks[index].observation=banks_[index].inspect(context,source,ready&&result.reportContextCurrent,now,maxAge);
            result.banks[index].reportSerial=serials_[index];
            result.banks[index].inLastReport=result.reportContextCurrent&&result.banks[index].observation.lastAccepted&&
                serials_[index]!=0&&serials_[index]==packet_->serial;
        }
        return result;
    }
};
} // namespace ymcc::legion_motion
