#pragma once
#include "power_source_state.h"
#include <tlhelp32.h>
#include <powrprof.h>
#include <powersetting.h>
#include <powerbase.h>
#include <atomic>
#include <cstring>
#include <cstddef>
#include <cwchar>
#pragma comment(lib, "powrprof.lib")

// One event producer at the root; children only read its private session map.
// No consumer path samples Windows, waits for a writer, or starts a timer.
namespace ymcc::powerSource::channel {
namespace detail {
struct Identity {
    DWORD pid = 0, session = 0;
    std::uint64_t birth = 0;
};
inline bool identity(HANDLE process, Identity& result) noexcept {
    FILETIME created{}, exited{}, kernel{}, user{};
    result.pid = GetProcessId(process);
    if (!result.pid || WaitForSingleObject(process, 0) != WAIT_TIMEOUT ||
        !GetProcessTimes(process, &created, &exited, &kernel, &user) ||
        !ProcessIdToSessionId(result.pid, &result.session)) return false;
    result.birth = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return result.birth && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}
inline bool directParent(DWORD pid) noexcept {
    HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (list == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    bool matches = false;
    if (Process32FirstW(list, &entry)) do {
        if (entry.th32ProcessID == GetCurrentProcessId()) {
            matches = entry.th32ParentProcessID == pid;
            break;
        }
    } while (Process32NextW(list, &entry));
    CloseHandle(list);
    return matches;
}
inline void mapName(const Identity& id, wchar_t (&name)[96]) noexcept {
    swprintf_s(name, L"Local\\YMCC.PowerSource.v1.%lu.%016llX", id.pid,
        static_cast<unsigned long long>(id.birth));
}
inline constexpr DWORD magic = 0x43535059, version = 1, rootKind = 1, redirectKind = 2;
struct alignas(8) Wire {
    volatile LONG sequence = 0;
    DWORD magic = detail::magic, version = detail::version, bytes = sizeof(Wire);
    DWORD ownerPid = 0, session = 0;
    std::uint64_t ownerBirth = 0;
    DWORD kind = rootKind, upstreamPid = 0;
    std::uint64_t upstreamBirth = 0;
    std::uint64_t sampleSequence = 0, revision = 0, observedAt = 0;
    SYSTEM_POWER_STATUS status{255,255,255,255,0xFFFFFFFFu,0xFFFFFFFFu};
    DWORD flags = 0;
    std::int32_t ac = -1;
    DWORD reserved = 0;
};
static_assert(sizeof(Wire) == 96 && alignof(Wire) == 8 && sizeof(SYSTEM_POWER_STATUS) == 12,
    "The v1 power channel ABI must be explicit");
static_assert(offsetof(Wire, sequence) == 0 && offsetof(Wire, ownerBirth) == 24 &&
    offsetof(Wire, upstreamBirth) == 40 && offsetof(Wire, sampleSequence) == 48 &&
    offsetof(Wire, revision) == 56 && offsetof(Wire, observedAt) == 64 &&
    offsetof(Wire, status) == 72 && offsetof(Wire, flags) == 84 &&
    offsetof(Wire, ac) == 88 && offsetof(Wire, reserved) == 92,
    "The v1 power channel field offsets must remain stable");
inline bool coherent(const Wire* view, Wire& copy) noexcept {
    // Interlocked RMW on a read-only view would fault. Aligned volatile loads
    // plus Windows memory barriers give a bounded Windows seqlock read.
    for (unsigned attempt = 0; attempt != 3; ++attempt) {
        const LONG before = view->sequence;
        if (!before || (before & 1)) continue;
        MemoryBarrier();
        std::memcpy(&copy, view, sizeof(copy));
        MemoryBarrier();
        if (before == view->sequence) return true;
    }
    return false;
}
inline bool valid(const Wire& wire, const Identity& id) noexcept {
    return wire.magic == magic && wire.version == version && wire.bytes == sizeof(Wire) &&
        wire.ownerPid == id.pid && wire.ownerBirth == id.birth && wire.session == id.session;
}
inline bool validPayload(const Wire& wire) noexcept {
    const bool readable = (wire.flags & 1u) != 0, known = (wire.flags & 2u) != 0;
    const bool batteryKnown = (wire.flags & 4u) != 0, hasBattery = (wire.flags & 8u) != 0;
    if (wire.reserved || (wire.flags & ~15u) || wire.ac < -1 || wire.ac > 1 ||
        known != (wire.ac >= 0) || (hasBattery && !batteryKnown) ||
        (known && wire.ac == 0 && (!batteryKnown || !hasBattery))) return false;
    // Failed samples may retain old raw telemetry. Source events may be known
    // before the first readable sample; no-battery AC inference and monitor
    // topology overrides also remain valid. Reject contradictory usable sources.
    if (wire.status.ACLineStatus <= 1 &&
        ((known && wire.ac != wire.status.ACLineStatus) || (readable && !known))) return false;
    return true;
}
struct Peer {
    HANDLE process = nullptr, mapping = nullptr;
    const Wire* view = nullptr;
    Identity id;
    Peer() = default;
    Peer(const Peer&) = delete;
    ~Peer() { reset(); }
    void reset() noexcept {
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
        if (process) CloseHandle(process);
        view = nullptr; mapping = process = nullptr; id = {};
    }
    bool open(HANDLE source, std::uint64_t expectedBirth, DWORD session) noexcept {
        reset();
        if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &process,
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, 0) ||
            !identity(process, id) || (expectedBirth && id.birth != expectedBirth) ||
            id.session != session) { reset(); return false; }
        wchar_t name[96]{}; mapName(id, name);
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping) view = static_cast<const Wire*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(Wire)));
        if (!view) { reset(); return false; }
        Wire wire;
        if (!coherent(view, wire) || !valid(wire, id)) { reset(); return false; }
        return true;
    }
    bool alive() const noexcept {
        return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }
};
} // namespace detail

class Publisher {
    std::mutex mutex_;
    HANDLE mapping_ = nullptr;
    detail::Wire* view_ = nullptr;
    detail::Wire wire_;
    bool published_ = false;
    void write() noexcept {
        InterlockedIncrement(&view_->sequence);
        std::memcpy(reinterpret_cast<unsigned char*>(view_) + sizeof(LONG),
            reinterpret_cast<const unsigned char*>(&wire_) + sizeof(LONG), sizeof(wire_) - sizeof(LONG));
        InterlockedIncrement(&view_->sequence);
    }
    bool create(DWORD kind, const detail::Identity& upstream) noexcept {
        std::lock_guard lock(mutex_);
        if (view_) return wire_.kind == kind && wire_.upstreamPid == upstream.pid && wire_.upstreamBirth == upstream.birth;
        detail::Identity self;
        if (!detail::identity(GetCurrentProcess(), self) ||
            (kind == detail::redirectKind && (!upstream.pid || upstream.session != self.session || upstream.birth > self.birth))) return false;
        wchar_t name[96]{}; detail::mapName(self, name);
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(detail::Wire), name);
        if (!mapping_) return false;
        if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mapping_); mapping_ = nullptr; return false; }
        view_ = static_cast<detail::Wire*>(MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, sizeof(detail::Wire)));
        if (!view_) { CloseHandle(mapping_); mapping_ = nullptr; return false; }
        wire_.ownerPid = self.pid; wire_.ownerBirth = self.birth; wire_.session = self.session;
        wire_.kind = kind; wire_.upstreamPid = upstream.pid; wire_.upstreamBirth = upstream.birth;
        write();
        return true;
    }
public:
    Publisher() = default;
    Publisher(const Publisher&) = delete;
    ~Publisher() { if (view_) UnmapViewOfFile(view_); if (mapping_) CloseHandle(mapping_); }
    bool start() noexcept { return create(detail::rootKind, {}); }
    bool redirect(const detail::Identity& upstream) noexcept { return create(detail::redirectKind, upstream); }
    bool publish(const Snapshot& sample) noexcept {
        std::lock_guard lock(mutex_);
        if (!view_ || wire_.kind != detail::rootKind ||
            (published_ && sample.sampleSequence <= wire_.sampleSequence)) return false;
        wire_.sampleSequence = sample.sampleSequence; wire_.revision = sample.revision;
        wire_.observedAt = sample.observedAt; wire_.status = sample.status;
        wire_.flags = (sample.readable ? 1u : 0u) | (sample.known ? 2u : 0u) |
            (sample.batteryKnown ? 4u : 0u) | (sample.hasBattery ? 8u : 0u);
        wire_.ac = sample.ac;
        write(); published_ = true;
        return true;
    }
};
inline Publisher& publisher() { static Publisher instance; return instance; }
inline bool startPublisher() noexcept { return publisher().start(); }
inline bool publish(const Snapshot& sample) noexcept { return publisher().publish(sample); }

class Consumer {
    mutable std::mutex mutex_;
    detail::Peer parent_, upstream_;
    detail::Identity root_;
    bool attached_ = false, redirected_ = false;
public:
    Consumer() = default;
    Consumer(const Consumer&) = delete;
    // source is the existing, validated lifetime handle. We retain our own
    // QUERY|SYNC duplicate and verify birth/session against that same handle.
    bool attach(HANDLE source, std::uint64_t expectedBirth = 0) noexcept {
        std::lock_guard lock(mutex_);
        attached_ = redirected_ = false; root_ = {}; upstream_.reset(); parent_.reset();
        detail::Identity self;
        if (!detail::identity(GetCurrentProcess(), self) || !parent_.open(source, expectedBirth, self.session) ||
            parent_.id.birth > self.birth || !detail::directParent(parent_.id.pid)) return false;
        detail::Wire wire;
        if (!detail::coherent(parent_.view, wire) || !detail::valid(wire, parent_.id)) return false;
        if (wire.kind == detail::rootKind) {
            if (wire.upstreamPid || wire.upstreamBirth) return false;
            root_ = parent_.id;
        } else if (wire.kind == detail::redirectKind) {
            if (!wire.upstreamPid || !wire.upstreamBirth || wire.upstreamBirth > parent_.id.birth) return false;
            HANDLE root = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, wire.upstreamPid);
            if (!root) return false;
            const bool opened = upstream_.open(root, wire.upstreamBirth, self.session);
            CloseHandle(root);
            if (!opened || !detail::coherent(upstream_.view, wire) || !detail::valid(wire, upstream_.id) ||
                wire.kind != detail::rootKind || wire.upstreamPid || wire.upstreamBirth) return false;
            root_ = upstream_.id; redirected_ = true;
        } else return false;
        attached_ = parent_.alive() && (!redirected_ || upstream_.alive());
        return attached_;
    }
    bool attach(DWORD parentPid) noexcept {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, parentPid);
        const bool result = attach(process); // A failed open also invalidates the old source.
        if (process) CloseHandle(process);
        return result;
    }
    detail::Identity rootIdentity() const noexcept {
        std::lock_guard lock(mutex_);
        return attached_ ? root_ : detail::Identity{};
    }
    Snapshot snapshot() const noexcept {
        std::lock_guard lock(mutex_);
        if (!attached_ || !parent_.alive() || (redirected_ && !upstream_.alive())) return {};
        const auto& peer = redirected_ ? upstream_ : parent_;
        detail::Wire wire;
        if (!detail::coherent(peer.view, wire) || !detail::valid(wire, peer.id) ||
            wire.kind != detail::rootKind || wire.upstreamPid || wire.upstreamBirth ||
            !detail::validPayload(wire) ||
            !parent_.alive() || (redirected_ && !upstream_.alive())) return {};
        Snapshot result;
        result.status = wire.status; result.readable = (wire.flags & 1u) != 0;
        result.known = (wire.flags & 2u) != 0; result.batteryKnown = (wire.flags & 4u) != 0;
        result.hasBattery = (wire.flags & 8u) != 0; result.ac = wire.ac;
        result.revision = wire.revision; result.observedAt = wire.observedAt; result.sampleSequence = wire.sampleSequence;
        return result;
    }
};
namespace detail {
inline std::mutex& bindingMutex() { static std::mutex mutex; return mutex; }
inline Consumer*& boundConsumer() { static Consumer* consumer = nullptr; return consumer; }
inline const void*& bindingOwner() { static const void* owner = nullptr; return owner; }
inline Snapshot externalSnapshot() noexcept {
    std::lock_guard lock(bindingMutex());
    return boundConsumer() ? boundConsumer()->snapshot() : Snapshot{};
}
} // namespace detail
class Binding {
    Consumer* consumer_ = nullptr;
public:
    Binding() = default;
    explicit Binding(Consumer& consumer) noexcept { bind(consumer); }
    Binding(const Binding&) = delete;
    void bind(Consumer& consumer) noexcept {
        std::lock_guard lock(detail::bindingMutex());
        consumer_ = &consumer; detail::boundConsumer() = consumer_; detail::bindingOwner() = this;
        setExternalReader(detail::externalSnapshot);
    }
    ~Binding() {
        std::lock_guard lock(detail::bindingMutex());
        if (detail::bindingOwner() == this) {
            detail::boundConsumer() = nullptr; detail::bindingOwner() = nullptr;
            // Consumer mode lasts for the process lifetime. Readers that arrive
            // after teardown must remain UNKNOWN without sampling Windows.
        }
    }
};

class StandaloneProducer {
    // Local GUID storage keeps this header independent of uuid.lib/initguid.
    inline static constexpr GUID settings_[] = {
        {0x5d3e9a59,0xe9d5,0x4b00,{0xa6,0xbd,0xff,0x34,0xff,0x51,0x65,0x48}}, // AC/DC
        {0xa7ad8041,0xb45a,0x4cae,{0x87,0xa3,0xee,0xcb,0xb4,0x68,0xa9,0xe1}}, // percent
        {0x7d263f15,0xfca4,0x49e5,{0x85,0x4b,0xa9,0xf2,0xbf,0xbd,0x5c,0x24}}, // battery topology
        {0xe00958c0,0xc213,0x4ace,{0xac,0x77,0xfe,0xcc,0xed,0x2e,0xee,0xa5}}, // saver
    };
    HPOWERNOTIFY notifications_[4]{}, resume_ = nullptr;
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS subscription_{notification, this};
    std::atomic<bool> active_{false};
    std::mutex sampleMutex_, lifecycleMutex_;
    void sample() noexcept {
        std::lock_guard lock(sampleMutex_);
        if (active_.load(std::memory_order_acquire)) channel::publish(powerSource::refresh(true));
    }
    // lifecycleMutex_ owns the registration array; callbacks only take the
    // sample mutex, so unregister may wait for them without a lock cycle.
    void stopLocked() noexcept {
        active_.store(false, std::memory_order_release);
        for (auto& notification : notifications_) if (notification) {
            PowerSettingUnregisterNotification(notification); notification = nullptr;
        }
        if (resume_) { PowerUnregisterSuspendResumeNotification(resume_); resume_ = nullptr; }
        std::lock_guard lock(sampleMutex_); // Drain any callback that was already sampling.
    }
public:
    StandaloneProducer() = default;
    StandaloneProducer(const StandaloneProducer&) = delete;
    ~StandaloneProducer() { stop(); }
    static ULONG CALLBACK notification(void* context, ULONG type, void* setting) noexcept {
        auto* self = static_cast<StandaloneProducer*>(context);
        if (!self) return ERROR_INVALID_PARAMETER;
        bool relevant = type == PBT_APMRESUMEAUTOMATIC || type == PBT_APMRESUMESUSPEND || type == PBT_APMRESUMECRITICAL;
        if (type == PBT_POWERSETTINGCHANGE && setting) {
            const auto* value = static_cast<const POWERBROADCAST_SETTING*>(setting);
            if (value->DataLength == sizeof(DWORD)) for (const auto& guid : settings_)
                if (IsEqualGUID(value->PowerSetting, guid)) { relevant = true; break; }
        }
        if (relevant) self->sample();
        return ERROR_SUCCESS;
    }
    bool start() noexcept {
        std::lock_guard lock(lifecycleMutex_);
        if (active_.load(std::memory_order_acquire)) return true;
        if (!startPublisher()) return false;
        for (unsigned i = 0; i != 4; ++i) {
            if (PowerSettingRegisterNotification(&settings_[i], DEVICE_NOTIFY_CALLBACK,
                &subscription_, &notifications_[i]) != ERROR_SUCCESS) { stopLocked(); return false; }
        }
        if (PowerRegisterSuspendResumeNotification(DEVICE_NOTIFY_CALLBACK, &subscription_, &resume_) != ERROR_SUCCESS) {
            stopLocked(); return false;
        }
        active_.store(true, std::memory_order_release);
        sample();
        return true;
    }
    void stop() noexcept {
        std::lock_guard lock(lifecycleMutex_);
        stopLocked();
    }
};

// Kept alive through host task cancellation/join. An integrated host publishes
// only a fixed redirect; its child worker opens the ultimate native root.
class Host {
    Consumer parent_;
    Binding binding_;
    StandaloneProducer producer_;
public:
    bool start(DWORD parentPid = 0) noexcept {
        if (!parentPid) {
            if (producer_.start()) return true;
            binding_.bind(parent_); // Unavailable publisher/notifications: UNKNOWN.
            return false;
        }
        const bool attached = parent_.attach(parentPid);
        binding_.bind(parent_); // Bind even on failure: never fall back to OS.
        return attached && publisher().redirect(parent_.rootIdentity());
    }
};
} // namespace ymcc::powerSource::channel
