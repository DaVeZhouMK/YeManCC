#pragma once
#include "msi_rear_source_observation.h"
#include <json.hpp>
namespace ymcc::msi::rearObservation {
inline nlohmann::json exportSnapshot(const Snapshot& s,uint64_t tick) {
    return {{"schema",1},{"exportTick",tick},{"counterScope","process-lifetime, possibly multiple source/power generations"},
        {"sequence",s.sequence},{"activityRowsReserved",s.activityReservations},{"activityRowLimit",48},
        {"lastReservedSequence",s.lastReservedSequence},{"counterTransitionsAfterLastReservation",s.sequence!=s.lastReservedSequence},
        {"reservationIsDurableLogProof",false},{"snapshotAtomicAcrossProducers",false},
        {"rawKeyboardCountsF11",nlohmann::json::array({s.keyboard[0],s.keyboard[1],s.keyboard[2],s.keyboard[3]})},
        {"rawKeyboardCountsF14",nlohmann::json::array({s.keyboard[4],s.keyboard[5],s.keyboard[6],s.keyboard[7]})},
        {"keyboardCountOrder","nonInjectedDown,nonInjectedUp,injectedDown,injectedUp"},
        {"lastObservedVk",s.lastVk},{"lastObservedScan",s.lastScan},{"lastObservedFlags",s.lastFlags},
        {"dinputSamples",s.samples},{"latestDinputBindGeneration",s.generation},{"latestRawButtonMask",s.rawMask},
        {"dinputButtonDownSampleCounts",s.buttonDown},{"capturedDinputButtonLimit",32},
        {"hardwareWriteIssued",false},{"keyboardPhysicalDeviceIdentityVerified",false},{"mappingReadbackVerified",false},
        {"coverageMeaning","cumulative counters continue beyond activity row cap; not a source-validity, firmware acceptance or Steam consumption proof"}};
}
}
