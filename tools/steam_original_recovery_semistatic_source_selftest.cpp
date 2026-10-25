#include "../native/steam_original_recovery_semistatic_source.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace source = ymcc::steamOriginalSemiStaticSource;
using Reason = source::Reason;
using Result = source::Result;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Request { std::uint64_t address; std::size_t bytes; };
struct InjectedException {};
struct Fixture {
    std::uint64_t client = 0x0000000180000000ULL;
    std::uint64_t clientSize = 0x02000000;
    std::uint64_t ui = 0x00007ffb40000000ULL;
    std::uint64_t uiSize = 0x02000000;
    std::uint64_t user = 0x0000015000000000ULL;
    std::uint64_t map = 0x0000015000010000ULL;
    std::map<std::uint64_t, std::uint8_t> memory;
    std::vector<Request> requests;
    std::function<void(Fixture&)> beforeRead;
    std::uint32_t calls = 0;
    std::uint32_t failAt = 0;
    std::uint32_t throwAt = 0;
    bool partialFailure = false;

    explicit Fixture(bool secondAslr = false) {
        if (secondAslr) {
            client = 0x00007ff800000000ULL;
            ui = 0x00007ff700000000ULL;
            user = 0x000001aa00000000ULL;
            map = 0x000001aa00100000ULL;
        }
        put(user, client + 0x136fa80, 8);
        put(user + 0x6b0, user, 8);
        put(user + 0x1e70, client + 0x136fbd0, 8);
        put(client + 0x136fc08, client + 0x9d71f0, 8);
        bytes(client + 0x9d71f0, {0x48,0x81,0xe9,0x70,0x1e,0,0,0xe9,0xe4,0xff,0xff,0xff});
        bytes(client + 0x9d71e0, {0xc6,0x81,0x21,0x69,0,0,1,0x48,0x83,0xc1,0x48,0xe9,0,0x99,0xbd,0xff});
        bytes(ui + 0x51fdb0, {0x48,0x8b,0x05,0x51,0xbb,0xc6,0,0xc3});
        bindMap(map, 7, 19);
        put(user + 0x40, 7, 4);
        put(ui + 0x118ba98, 19, 4);
        put(user + 0x178, 4, 4);
        put(user + 0x60c, 0x31415926, 4);
        put(user + 0x6921, 1, 1);
    }
    void put(std::uint64_t address, std::uint64_t value, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i)
            memory[address + i] = static_cast<std::uint8_t>((value >> (i * 8)) & 255U);
    }
    void bytes(std::uint64_t address, std::initializer_list<std::uint8_t> value) {
        for (const auto byte : value) memory[address++] = byte;
    }
    void bindMap(std::uint64_t at, std::uint32_t localUser, std::uint32_t pipe) {
        put(ui + 0x118b908, at, 8);
        put(at, client + 0x1329288, 8);
        put(at + 8, localUser, 4);
        put(at + 0xc, pipe, 4);
    }
    bool operator()(std::uint64_t address, void* destination, std::size_t size) {
        ++calls;
        requests.push_back({address, size});
        if (beforeRead) beforeRead(*this);
        if (calls == throwAt) throw InjectedException{};
        if (calls == failAt) {
            if (partialFailure && size != 0) std::memset(destination, 0xff, size);
            return false;
        }
        auto* out = static_cast<std::uint8_t*>(destination);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = memory.find(address + i);
            if (it == memory.end()) return false; // Unexpected ranges fail closed.
            out[i] = it->second;
        }
        return true;
    }
    Result observe(bool qualified = true) {
        return source::observe(*this, client, clientSize, ui, uiSize, user, qualified);
    }
};
static_assert(noexcept(source::observe(std::declval<Fixture&>(), 0, 0, 0, 0, 0, false)));
static_assert(std::is_trivially_copyable_v<Result>);
static_assert(source::kMaxReadCalls == 64 && source::kMaxRequestedBytes == 1024);
static_assert(source::kSuccessfulReadCalls == 32 && source::kSuccessfulRequestedBytes == 218);

void accounting(const Fixture& f, const Result& r) {
    std::uint64_t bytes = 0;
    for (const auto& request : f.requests) bytes += request.bytes;
    require(r.readCalls == f.calls && r.requestedBytes == bytes, "attempt accounting differs");
    require(r.readCalls <= 64 && r.requestedBytes <= 1024, "hard read budget exceeded");
}
void unavailable(const Fixture& f, const Result& r, Reason reason) {
    accounting(f, r);
    if (r.reason != reason) {
        std::cerr << "expected " << source::reasonName(reason) << " got " << source::reasonName(r.reason) << '\n';
        throw std::runtime_error("wrong reason");
    }
    require(!r.available && !r.userMapMatched && !r.userInterfaceMatched && !r.uiPipeMatched,
            "unavailable published match flags");
    require(r.cmStateRaw == 0 && !r.cmConnectionPresent && r.reconnectFlagRaw == 0 && r.uiLocalPipe == 0,
            "unavailable published partial data");
}
void healthy(Fixture& f, const Result& r) {
    accounting(f, r);
    require(r.available && r.reason == Reason::Available, "healthy unavailable");
    require(r.userMapMatched && r.userInterfaceMatched && r.uiPipeMatched, "healthy flags missing");
    require(r.readCalls == 32 && r.requestedBytes == 218, "exact healthy budget differs");
}
std::uint32_t cases = 0;
template<class Fn> void run(const std::string& name, Fn&& action) {
    action();
    ++cases;
    std::cout << "PASS " << name << '\n';
}
}

int main() {
    try {
        for (const bool relocated : {false, true}) {
            run(relocated ? "healthy-ASLR-B" : "healthy-ASLR-A", [=] {
                Fixture f(relocated); const auto r = f.observe(); healthy(f, r);
                require(r.cmStateRaw == 4 && r.cmConnectionPresent && r.reconnectFlagRaw == 1 && r.uiLocalPipe == 19,
                        "healthy fields differ");
            });
        }
        run("unknown-profile-zero-reads-even-invalid-addresses", [] {
            Fixture f; f.client = UINT64_MAX; f.ui = 0; f.user = UINT64_MAX;
            const auto r = f.observe(false); unavailable(f, r, Reason::FixedProfileUnqualified);
            require(f.calls == 0, "unqualified profile read memory");
        });
        run("unknown-raw-preserved-not-normalized", [] {
            Fixture f; f.put(f.user+0x178, 0xfefdfcfbU, 4); f.put(f.user+0x6921, 0xa5, 1);
            const auto r=f.observe(); healthy(f,r);
            require(r.cmStateRaw==0xfefdfcfbU && r.reconnectFlagRaw==0xa5 && r.cmConnectionPresent,
                    "unknown values normalized");
        });
        run("zero-connection-is-only-presence-false", [] {
            Fixture f; f.put(f.user+0x60c, 0, 4); const auto r=f.observe(); healthy(f,r);
            require(!r.cmConnectionPresent, "zero connection present");
        });
        run("matching-zero-local-user-does-not-invent-admission", [] {
            Fixture f; f.put(f.map+8,0,4); f.put(f.user+0x40,0,4); const auto r=f.observe(); healthy(f,r);
        });
        for (const auto value : {std::uint64_t{0}, std::uint64_t{0xffff}}) {
            run("user-lower-bound-"+std::to_string(value), [=] {
                Fixture f; f.user=value; const auto r=f.observe(); unavailable(f,r,Reason::InvalidUser);
                require(f.calls==0,"invalid user caused reads");
            });
        }
        for (unsigned which=0; which<3; ++which) {
            run("input-overflow-zero-reads-"+std::to_string(which), [=] {
                Fixture f;
                if (which==0) f.client=UINT64_MAX-16;
                if (which==1) f.ui=UINT64_MAX-16;
                if (which==2) f.user=UINT64_MAX-0x6921;
                const auto r=f.observe(); unavailable(f,r,Reason::AddressOverflow);
                require(f.calls==0,"overflow caused reads");
            });
        }
        for (unsigned which=0; which<6; ++which) {
            run("module-bounds-zero-reads-"+std::to_string(which), [=] {
                Fixture f;
                if (which==0) f.client=0;
                if (which==1) f.ui=0xffff;
                if (which==2) f.clientSize=0x136fc0f;
                if (which==3) f.uiSize=0x118ba9b;
                if (which==4) f.clientSize=0;
                if (which==5) f.uiSize=0;
                const auto r=f.observe(); unavailable(f,r,Reason::ModuleRangeInvalid);
                require(f.calls==0,"module bounds caused reads");
            });
        }
        run("minimum-module-ranges-inclusive-last-byte", [] {
            Fixture f; f.clientSize=0x136fc10; f.uiSize=0x118ba9c;
            const auto r=f.observe(); healthy(f,r);
        });
        for (const auto value : {std::uint64_t{0},std::uint64_t{0xffff},UINT64_MAX-7}) {
            run("map-pointer-lower-bound-or-overflow-"+std::to_string(value), [=] {
                Fixture f; f.put(f.ui+0x118b908,value,8); const auto r=f.observe();
                unavailable(f,r,value<0x10000?Reason::InvalidUserMap:Reason::AddressOverflow);
                require(r.readCalls==8 && r.requestedBytes==76,"map invalid-read stop differs");
            });
        }
        struct Pin { const char* name; std::uint64_t address; Reason reason; };
        Fixture offsets;
        const std::vector<Pin> pins={
            {"primary-vtable",offsets.user+7,Reason::VtableMismatch},
            {"owner",offsets.user+0x6b0,Reason::OwnerMismatch},
            {"secondary-vtable",offsets.user+0x1e70,Reason::VtableMismatch},
            {"secondary-slot-route",offsets.client+0x136fc08,Reason::RouteMismatch},
            {"actual-map-vtable",offsets.map,Reason::UserMapMismatch},
            {"map-local-user",offsets.map+8,Reason::UserInterfaceMismatch},
            {"owner-local-user",offsets.user+0x40,Reason::UserInterfaceMismatch},
            {"map-local-pipe",offsets.map+0xc,Reason::UiPipeMismatch},
            {"UI-local-pipe",offsets.ui+0x118ba98,Reason::UiPipeMismatch}
        };
        for (const auto& pin : pins) {
            run(std::string("initial-mismatch-")+pin.name,[=] {
                Fixture f; f.memory.at(pin.address)^=1; const auto r=f.observe(); unavailable(f,r,pin.reason);
            });
            run(std::string("after-raw-mismatch-")+pin.name,[=] {
                Fixture f; f.beforeRead=[pin](Fixture& g){if(g.calls==20) g.memory.at(pin.address)^=1;};
                const auto r=f.observe(); unavailable(f,r,pin.reason);
            });
        }
        const std::vector<std::pair<std::uint64_t,std::size_t>> anchors={
            {offsets.client+0x9d71f0,12},{offsets.client+0x9d71e0,16},{offsets.ui+0x51fdb0,8}};
        for (std::size_t k=0;k<anchors.size();++k) {
            for (std::size_t i=0;i<anchors[k].second;++i) {
                run("anchor-every-byte-"+std::to_string(k)+"-"+std::to_string(i),[=] {
                    Fixture f; f.memory.at(anchors[k].first+i)^=1;
                    const auto r=f.observe(); unavailable(f,r,Reason::AnchorMismatch);
                });
            }
            run("anchor-changed-after-raw-"+std::to_string(k),[=] {
                Fixture f; f.beforeRead=[=](Fixture& g){if(g.calls==20)g.memory.at(anchors[k].first)^=1;};
                const auto r=f.observe(); unavailable(f,r,Reason::AnchorMismatch);
            });
        }
        run("both-pipes-zero-rejected",[] {
            Fixture f; f.put(f.map+0xc,0,4); f.put(f.ui+0x118ba98,0,4);
            const auto r=f.observe(); unavailable(f,r,Reason::UiPipeMismatch);
        });
        run("map-replaced-with-matching-fields",[] {
            Fixture f; f.beforeRead=[](Fixture& g){if(g.calls==20)g.bindMap(g.map+0x1000,7,19);};
            const auto r=f.observe(); unavailable(f,r,Reason::UserMapChanged);
        });
        run("both-user-fields-changed-consistently",[] {
            Fixture f; f.beforeRead=[](Fixture& g){if(g.calls==20){g.put(g.map+8,8,4);g.put(g.user+0x40,8,4);}};
            const auto r=f.observe(); unavailable(f,r,Reason::UserInterfaceChanged);
        });
        run("both-pipes-changed-consistently",[] {
            Fixture f; f.beforeRead=[](Fixture& g){if(g.calls==20){g.put(g.map+0xc,20,4);g.put(g.ui+0x118ba98,20,4);}};
            const auto r=f.observe(); unavailable(f,r,Reason::UiPipeChanged);
        });
        for (unsigned which=0;which<3;++which) {
            run("raw-changed-between-collections-"+std::to_string(which),[=] {
                Fixture f; f.beforeRead=[=](Fixture& g){if(g.calls==17){
                    if(which==0)g.put(g.user+0x178,5,4);
                    if(which==1)g.put(g.user+0x60c,2,4); // Both are nonzero; raw change still matters.
                    if(which==2)g.put(g.user+0x6921,2,1);
                }};
                const auto r=f.observe(); unavailable(f,r,Reason::RawChanged);
                require(r.readCalls==32 && r.requestedBytes==218,"raw changed skipped final guards");
            });
        }
        Fixture trace; const auto traceResult=trace.observe(); healthy(trace,traceResult);
        for (std::uint32_t nth=1;nth<=32;++nth) {
            for (unsigned mode=0;mode<2;++mode) {
                run(std::string(mode==0?"read-failure-at-":"reader-exception-at-")+std::to_string(nth),[=] {
                    Fixture f; if(mode==0)f.failAt=nth;else f.throwAt=nth;
                    const auto r=f.observe(); unavailable(f,r,mode==0?Reason::ReadFailed:Reason::ReadException);
                    require(r.readCalls==nth,"reader failure did not stop immediately");
                    std::uint64_t bytes=0;
                    for(std::size_t i=0;i<nth;++i)bytes+=trace.requests[i].bytes;
                    require(r.requestedBytes==bytes,"reader failure byte accounting differs");
                });
            }
        }
        run("partial-buffer-failure-no-published-values",[] {
            Fixture f;f.failAt=18;f.partialFailure=true;const auto r=f.observe();unavailable(f,r,Reason::ReadFailed);
        });
        run("reason-name-total-and-unknown-fallback",[] {
            for(int n=static_cast<int>(Reason::Available);n<=static_cast<int>(Reason::RawChanged);++n)
                require(std::strcmp(source::reasonName(static_cast<Reason>(n)),"unknown")!=0,"missing reasonName");
            require(std::strcmp(source::reasonName(static_cast<Reason>(999)),"unknown")==0,"unknown reason fallback");
        });
        std::cout << "SUMMARY " << cases << " cases passed; healthy=32 calls/218B; cap=64 calls/1024B\n";
        std::cout << "Sparse synthetic memory only; no Steam/process/API invocation, writes, queues or poller.\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "FAIL after " << cases << " cases: " << e.what() << '\n';
    } catch(...) {
        std::cerr << "FAIL unexpected exception after " << cases << " cases\n";
    }
    return 1;
}
