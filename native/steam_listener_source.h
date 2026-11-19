#pragma once
#include <windows.h>
#include <atomic>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
namespace ymcc::steamlive {
struct Endpoint {
    DWORD pid = 0;
    unsigned short port = 0;
    std::wstring host = L"127.0.0.1";
    bool operator==(const Endpoint&) const = default;
};
namespace listenersource {
using Read = std::function<std::vector<Endpoint>(const std::wstring&,DWORD,ULONGLONG)>;
inline std::atomic<bool> delegatedRole{false};
inline std::atomic<std::shared_ptr<const Read>> port;
inline thread_local ULONGLONG operationEnd = 0;
// A request scope, not a cache or a worker. Watch commands carry their own
// existing absolute end on the thread that makes the ownership check.
class Deadline {
    ULONGLONG previous_;
public:
    explicit Deadline(ULONGLONG end):previous_(operationEnd) {
        operationEnd=previous_&&end?(std::min)(previous_,end):previous_?previous_:end;
    }
    ~Deadline(){operationEnd=previous_;}
};
// Acquisition-local pure cancellation/deadline. The owned predicate remains
// on this callback stack and is restored before returning to the pool.
inline thread_local const std::function<bool()>* operationCurrent=nullptr;
class Current {
    const std::function<bool()> current_;
    const std::function<bool()>* previous_;
public:
    explicit Current(std::function<bool()> current):current_(std::move(current)),previous_(operationCurrent){operationCurrent=&current_;}
    Current(const Current&)=delete;
    ~Current(){operationCurrent=previous_;}
};
inline void checkpoint(){
    if((operationEnd&&GetTickCount64()>=operationEnd)||(operationCurrent&&(!*operationCurrent||!(*operationCurrent)())))
        throw std::runtime_error("live-action-cancelled");
}
class Delegated {
    std::shared_ptr<const Read> installed_;
public:
    explicit Delegated(Read read):installed_(std::make_shared<const Read>(std::move(read))) {
        if(delegatedRole.exchange(true))throw std::runtime_error("live-listener-proxy-invalid");
        port.store(installed_);
    }
    Delegated(const Delegated&)=delete;
    ~Delegated(){auto expected=installed_;port.compare_exchange_strong(expected,std::shared_ptr<const Read>{});}
};
inline std::vector<Endpoint> read(const std::wstring& root,DWORD pid) {
    const auto held=port.load();
    if(!held||!operationEnd||GetTickCount64()>=operationEnd)throw std::runtime_error("live-listener-proxy-unavailable");
    return (*held)(root,pid,operationEnd);
}
} // namespace listenersource
} // namespace ymcc::steamlive
