#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "windows_motion_submission.h"

namespace ymcc::sensors::view {
struct Part {
    std::array<double,3> axes{};
    bool usable=false;
    std::uint64_t generation=0,timestamp100ns=0,receipt=0,sequence=0,rejected=0;
    double timeOfDayMs=0;
    std::string standardSdkLayout;
};
template<class Metadata>struct Snapshot {
    Metadata metadata;
    std::array<Part,2> parts;
    std::array<std::string,2> providers;
    windows_motion::Stamp capture,choice,submission;
    bool coherent=false,privateSource=false,processingOwnerAccepted=false;
    std::uint64_t captureRequestId=0,choiceRequestId=0;
    std::string profile,calibrationKey;
};
template<class Metadata>bool current(const Snapshot<Metadata>& value) noexcept {
    return value.coherent&&windows_motion::current(value.submission)&&
        (!value.privateSource||(value.processingOwnerAccepted&&value.submission.captureLease));
}
// Pin metadata and its source lease throughout a processing call. Nested
// diagnostic/calibration calls may observe this same view without reading a
// second, possibly newer selection. A scope never mutates the default source.
template<class Metadata>class ProcessingScope final {
    const Metadata*& metadataSlot_;const windows_motion::Stamp*& stampSlot_;
    const Metadata* priorMetadata_;const windows_motion::Stamp* priorStamp_;
    const Snapshot<Metadata>** viewSlot_;const Snapshot<Metadata>* priorView_;
public:
    ProcessingScope(const Snapshot<Metadata>& value,const Metadata*& metadataSlot,
        const windows_motion::Stamp*& stampSlot,const Snapshot<Metadata>** viewSlot=nullptr)noexcept:metadataSlot_(metadataSlot),stampSlot_(stampSlot),
        priorMetadata_(metadataSlot),priorStamp_(stampSlot),viewSlot_(viewSlot),priorView_(viewSlot?*viewSlot:nullptr){metadataSlot_=&value.metadata;stampSlot_=&value.submission;if(viewSlot_)*viewSlot_=&value;}
    ProcessingScope(const ProcessingScope&)=delete;ProcessingScope& operator=(const ProcessingScope&)=delete;
    ~ProcessingScope(){metadataSlot_=priorMetadata_;stampSlot_=priorStamp_;if(viewSlot_)*viewSlot_=priorView_;}
};
} // namespace ymcc::sensors::view
