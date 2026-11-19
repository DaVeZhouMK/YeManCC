#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>

namespace ymcc::processInventory {struct Snapshot;}

namespace ymcc::steamlaunch {
// The launcher never creates a second Steam fact producer. Only the optional
// generic addon check borrows rows of the same complete census.
struct LaunchObservation {
    int steam=-1;
    std::shared_ptr<const processInventory::Snapshot> addonRows;
};
struct LaunchFacts {
    std::function<LaunchObservation(DWORD heldSuspendedChild,bool includeAddons)> observe;
    std::function<std::wstring()> executable;
    explicit operator bool() const noexcept {return observe&&executable;}
};
}
