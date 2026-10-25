#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace ymcc::msi::modeTicket {
// Process-local queue ticket. No registry/driver/hardware access is present.
struct Ticket {
    bool dinput=false;
    uint64_t host=0, power=0, source=0, mode=0;
    std::string reason;
};
inline bool number(std::string_view text,uint64_t& out) {
    if(text.empty())return false;
    uint64_t value=0;
    for(const char c:text){
        if(c<'0'||c>'9')return false;
        const uint64_t digit=uint64_t(c-'0');
        if(value>(std::numeric_limits<uint64_t>::max()-digit)/10)return false;
        value=value*10+digit;
    }
    out=value;return true;
}
inline std::string encode(const Ticket& ticket) {
    return std::string("msi-mode-write:v2:")+(ticket.dinput?"dinput:":"xinput:")+
        std::to_string(ticket.host)+":"+std::to_string(ticket.power)+":"+
        std::to_string(ticket.source)+":"+std::to_string(ticket.mode)+":"+ticket.reason;
}
inline bool parse(std::string_view text,Ticket& out) {
    constexpr std::string_view prefix="msi-mode-write:v2:";
    if(text.size()>512||!text.starts_with(prefix))return false;
    text.remove_prefix(prefix.size());
    std::string_view fields[5];
    for(auto& field:fields){
        const auto colon=text.find(':');if(colon==std::string_view::npos)return false;
        field=text.substr(0,colon);text.remove_prefix(colon+1);
    }
    Ticket ticket;
    if(fields[0]=="dinput")ticket.dinput=true;
    else if(fields[0]!="xinput")return false;
    if(!number(fields[1],ticket.host)||!number(fields[2],ticket.power)||
       !number(fields[3],ticket.source)||!number(fields[4],ticket.mode)||
       !ticket.host||!ticket.power||!ticket.mode||text.empty()||text.size()>160||
       text.find(':')!=std::string_view::npos)return false;
    ticket.reason=std::string(text);out=std::move(ticket);return true;
}
struct Current {
    bool knownBoard=false,exitFence=false,powerReady=false;
    bool targetEnabled=false,routeArmed=false,routeConfirmed=false,sourceValid=false;
    uint64_t host=0,power=0,source=0,mode=0;
};
inline bool isRecovery(const Ticket& ticket) {
    return ticket.dinput&&(ticket.reason=="dinput-route-observed-xinput-return"||
                           ticket.reason=="dinput-route-window-resend");
}
inline const char* reject(const Ticket& ticket,const Current& current) {
    if(!current.knownBoard)return "unknown-msi-board";
    if(current.exitFence)return "exit-fence";
    if(!current.host||ticket.host!=current.host)return "host-generation-changed";
    if(!current.power||ticket.power!=current.power)return "power-generation-changed";
    if(ticket.source!=current.source)return "source-gate-generation-changed";
    if(ticket.mode!=current.mode)return "mode-generation-changed";
    if(!current.powerReady)return "power-write-gate-closed";
    if(isRecovery(ticket)){
        if(!current.targetEnabled)return "virtual-target-disabled";
        if(!current.routeArmed)return "route-disarmed";
        if(current.routeConfirmed||current.sourceValid)return "source-already-recovered";
    }
    return nullptr;
}
} // namespace ymcc::msi::modeTicket
