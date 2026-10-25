#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include "windows_motion_submission.h"
namespace ymcc::hc_model_motion { struct Recipe; }

// Data owned by the capture thread for the 51 source-qualified Windows models.
// A stored record, a current source and an applied offset are separate facts.
namespace ymcc::reviewed_calibration {
struct Source {
    void* handle = nullptr;
    std::chrono::steady_clock::time_point session{};
    const hc_model_motion::Recipe* recipe = nullptr;
    windows_motion::Stamp sample;
    std::uint64_t power = 0;
    std::string gyroId, accelId, gyroFamily, accelFamily;
    std::string calibrationKey;
};
struct Offset { float x=0, y=0, z=0; int weight=0; };
struct StorageReceipt {
    bool replaced=false, verified=false, saved=false;
    unsigned long error=0;
    std::string reason="not-attempted", recoveryPath;
};
struct Owner {
    void* handle=nullptr;
    std::chrono::steady_clock::time_point session{};
    std::string storageKey;
    int weight=0;
};
struct Transaction {
    bool active=false, fromAuto=false, priorTrusted=false, priorFinished=false;
    Source source;
    std::string storageKey;
    Offset prior;
};
struct Context {
    Owner owner;
    bool observed=false;
    Source lastSource;
    Transaction transaction;
};
} // namespace ymcc::reviewed_calibration
