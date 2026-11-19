#pragma once
// One request-local selection pass. Callers supply their existing transport;
// no discovery source, reusable endpoint cache or action permission lives here.
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ymcc::steamdiscovery {
enum class Kind { Live, Documents, Bootstrap };
inline const char* ambiguity(Kind kind) {
    switch(kind){
    case Kind::Live:return "live-context-ambiguous";
    case Kind::Documents:return "document-context-ambiguous";
    case Kind::Bootstrap:return "mirror-bootstrap-ambiguous";
    }
    throw std::runtime_error("invalid-steam-discovery-kind");
}
inline bool terminal(Kind kind,std::string_view reason) {
    switch(kind){
    case Kind::Live:return reason=="live-context-ambiguous"||reason=="live-targets-invalid";
    case Kind::Documents:return reason.starts_with("document-")&&reason!="document-transport-unavailable";
    case Kind::Bootstrap:return reason=="mirror-bootstrap-ambiguous"||reason=="live-context-ambiguous"||
        reason=="live-targets-invalid"||reason=="mirror-bootstrap-canceled"||reason=="mirror-bootstrap-deadline";
    }
    throw std::runtime_error("invalid-steam-discovery-kind");
}
template<class Endpoint,class Payload> struct Selection {
    std::optional<Endpoint> endpoint;
    std::optional<Payload> payload;
};
template<class Payload,class Candidates,class Checkpoint,class Eligible,class Probe,class Rejected>
inline auto select(Kind kind,const Candidates& candidates,Checkpoint&& checkpoint,Eligible&& eligible,Probe&& probe,Rejected&& rejected) {
    using Endpoint=typename Candidates::value_type;
    const auto ambiguous=ambiguity(kind); // Reject unknown kinds before any port.
    Selection<Endpoint,Payload> selected;
    for(const auto& candidate:candidates){
        checkpoint(); // Pure current/deadline only; never an action-admit hook.
        if(selected.endpoint&&selected.endpoint->pid==candidate.pid&&selected.endpoint->port==candidate.port)continue;
        if(!eligible(candidate))continue;
        std::optional<Payload> found;
        try{found=probe(candidate);}
        catch(const std::exception& error){
            if(terminal(kind,error.what()))throw;
            rejected(error,selected.endpoint.has_value());
            checkpoint(); // A failed IO must not hide cancellation/deadline.
            continue;
        }
        checkpoint(); // Never accept a reply from a request revoked during IO.
        if(!found)continue;
        if(selected.endpoint)throw std::runtime_error(ambiguous);
        selected.endpoint=candidate;selected.payload=std::move(found);
    }
    return selected;
}
} // namespace ymcc::steamdiscovery
