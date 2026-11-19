#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
// Memory fixture only: no public issuer, retained child or launch record.
// A request carrying either handoff argument is unsupported by these ports.
namespace ymcc::steamgamefacts {
using TrackerOwnerCurrent=std::function<bool()>;
class StartupHandoffEvidence final {
    StartupHandoffEvidence()=delete;
public:
    struct Witness {std::uint32_t pid=0;std::uint64_t created=0;std::wstring image;};
    struct Launcher {
        const Witness& witness()const {throw std::logic_error("fixture-startup-handoff-unsupported");}
    };
    struct Launch {
        std::uint64_t fullGameId()const {throw std::logic_error("fixture-startup-handoff-unsupported");}
        std::uint32_t appid()const {throw std::logic_error("fixture-startup-handoff-unsupported");}
        std::uint64_t sourceRevision()const {throw std::logic_error("fixture-startup-handoff-unsupported");}
        std::uint64_t sourceLine()const {throw std::logic_error("fixture-startup-handoff-unsupported");}
        const Launcher* launcher()const {throw std::logic_error("fixture-startup-handoff-unsupported");}
    };
    const Launch* launch()const {throw std::logic_error("fixture-startup-handoff-unsupported");}
    std::uint64_t freshSourceRevision()const {throw std::logic_error("fixture-startup-handoff-unsupported");}
    bool launcherRemoved()const {throw std::logic_error("fixture-startup-handoff-unsupported");}
};
// No issuer exists here. Even the nested projections are unconstructible;
// every accessor throws rather than manufacturing a positive historical fact.
class HistoricalStartupHandoffEvidence final {
    HistoricalStartupHandoffEvidence()=delete;
public:
    using Witness=StartupHandoffEvidence::Witness;
    struct Child {
        Child()=delete;
        const Witness& witness()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
        std::uint32_t windowsSession()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    };
    struct HistoricalParent {
        HistoricalParent()=delete;
        const Child* child()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
        std::uint32_t parentPid()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
        std::uint32_t windowsSession()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
        std::uint64_t parentCreated()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
        std::uint64_t parentExitedAt()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
        const std::wstring& parentNativeImage()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
        const std::wstring& childNativeImage()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
        bool parentExited()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
        bool childStillAlive()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
        bool relationCurrent()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    };
    struct SteamReceipt {bool present=false,rootKnown=false,windowsSessionKnown=false;std::uint32_t pid=0,windowsSession=0;std::uint64_t birth=0;std::wstring root;};
    const HistoricalParent* historicalParent()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    const SteamReceipt& steam()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    std::uint64_t fullGameId()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    std::uint32_t appid()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    std::uint64_t sourceRevision()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    unsigned sourceLine()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    const std::string& sourcePath()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    const std::string& exactLine()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    bool launcherRemoved()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
    bool retainedChildCurrent()const {throw std::logic_error("fixture-historical-startup-handoff-unsupported");}
};
class ProcessGroupLease;class RequestCancellation;
}
namespace ymcc::steamcommand {class Intent;class AvailabilityEvent;}
namespace ymcc::steamOriginalRecovery {
struct StartupHandoffTarget {
    struct Witness {std::uint32_t pid=0;std::uint64_t created=0;std::wstring image;};
    Witness steam,game;std::uint32_t windowsSession=0;std::uint64_t fullGameId=0;std::uint32_t appid=0;
};
inline bool startupHandoffMatches(const std::shared_ptr<const steamgamefacts::StartupHandoffEvidence>&,
    const StartupHandoffTarget&)noexcept {return false;}
inline bool startupHandoffOwnerCurrent(const std::shared_ptr<const steamgamefacts::StartupHandoffEvidence>& evidence,
    const std::function<bool()>& ownerCurrent)noexcept {return !evidence&&!ownerCurrent;}
inline bool historicalStartupHandoffMatches(const std::shared_ptr<const steamgamefacts::HistoricalStartupHandoffEvidence>&,
    const StartupHandoffTarget&)noexcept {return false;}
inline bool historicalStartupHandoffOwnerCurrent(const std::shared_ptr<const steamgamefacts::HistoricalStartupHandoffEvidence>& evidence,
    const std::function<bool()>& ownerCurrent)noexcept {return !evidence&&!ownerCurrent;}
}
