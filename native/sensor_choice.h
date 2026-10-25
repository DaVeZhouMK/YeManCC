#pragma once
#include <string>
#include <vector>
namespace ymcc::sensors {
struct Candidate {std::string id;bool enabled=false;};
struct ChoiceReceipt {
    const char* stage="default";
    unsigned enumerated=0,enabled=0,opened=0,errors=0;
    bool usedDefault=false,ambiguous=false;
};
// Preserve the existing successful default. If absent, follow HC's enabled
// WinRT enumeration/FromId path, but never choose the first of multiple valid
// physical providers. All constructors are supplied by the real native caller.
template<class Sensor,class Default,class Enumerate,class Open>
Sensor chooseAvailable(Default getDefault,Enumerate enumerate,Open open,ChoiceReceipt& receipt) {
    try {Sensor sensor=getDefault();if(sensor){receipt.usedDefault=true;return sensor;}}
    catch(...){++receipt.errors;}
    receipt.stage="enumerate";
    std::vector<Candidate> candidates;
    try {candidates=enumerate();receipt.enumerated=static_cast<unsigned>(candidates.size());}
    catch(...){++receipt.errors;return Sensor{nullptr};}
    Sensor selected{nullptr};
    for(const auto& candidate:candidates) {
        if(!candidate.enabled)continue;++receipt.enabled;
        try {Sensor sensor=open(candidate.id);if(!sensor)continue;++receipt.opened;
            if(receipt.opened>1){receipt.ambiguous=true;receipt.stage="ambiguous";return Sensor{nullptr};}
            selected=sensor;}
        catch(...){++receipt.errors;}
    }
    receipt.stage=selected?"nondefault-selected":"unavailable";
    return selected;
}
} // namespace ymcc::sensors
