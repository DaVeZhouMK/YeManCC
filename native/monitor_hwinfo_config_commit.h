#pragma once
#include "monitor_hwinfo_config.h"
#include <optional>
#include <string>

namespace ymcc::hwinfo {
struct ConfigConflict:Error {
    ConfigConflict():Error(8,"HWiNFO configuration changed concurrently; update was not applied"){}
};
inline bool temporaryConfigLock(const Error& error) noexcept {
    return !error.elevation &&
        (error.win32==ERROR_SHARING_VIOLATION||error.win32==ERROR_LOCK_VIOLATION);
}
// Event-driven configuration only. No state is retained by the monitoring
// sampler. Each retry rechecks the producer and the exact original bytes.
template<class Stopped,class Read,class Write,class Wait>
void commitConfiguration(const std::optional<std::string>& previous,const std::string& plan,
                         Stopped stopped,Read read,Write write,Wait wait){
    constexpr unsigned attempts=5,delayMs=200;
    for(unsigned attempt=0;attempt<attempts;++attempt){
        stopped();
        try{
            const auto current=read();
            if(current!=previous){
                // A prior atomic commit may have completed while its verify
                // read was briefly locked. Accept only the exact planned bytes.
                if(attempt>0&&current&&*current==plan)return;
                throw ConfigConflict();
            }
            // Keep the first physical commit even for byte-identical input.
            // This preserves the existing upstream configuration restore path.
            write(plan);return;
        }catch(const Error& error){
            if(!temporaryConfigLock(error)||attempt+1==attempts)throw;
        }
        if(!wait(delayMs))throw Error(15,"HWiNFO configuration retry interrupted");
    }
}
}
