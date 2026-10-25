#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace ymcc::fxsound {
enum class ProcessState { unknown, stopped, running };
struct Device {
    std::wstring id, name;
    bool fx = false, usable = false;
    unsigned form = 0;
};
struct Snapshot {
    bool valid = false;
    ProcessState process = ProcessState::unknown;
    std::array<std::wstring, 3> defaults{};
    std::vector<Device> devices;
    std::vector<std::wstring> preferred;
    const Device* find(const std::wstring& id) const {
        for (const auto& d : devices) if (!id.empty() && d.id == id) return &d;
        return nullptr;
    }
    bool isFx(unsigned role) const {
        const auto* d = find(defaults[role]);
        return d && d->fx;
    }
    bool hasFxDefault() const { return isFx(0) || isFx(1) || isFx(2); }
};
struct Attempt {
    unsigned role;
    std::wstring source, target;
    bool ok;
    long error;
};
struct SetResult { bool ok; long error; };

// The production worker and the memory-port regression execute this consumer.
// No persisted Windows role is treated as an independent "original default".
class Recovery {
    std::array<std::wstring, 3> startupDefaults_{};
    std::array<bool, 3> affected_{};
    std::uint64_t since_ = 0, lastTick_ = 0;
    bool started_ = false;
    static int rank(unsigned form) {
        return form == 3 ? 0 : form == 5 ? 1 : form == 1 ? 2 : 3;
    }
    std::wstring target(const Snapshot& s) const {
        const auto usable = [&](const std::wstring& id) {
            const auto* d = s.find(id); return d && !d->fx && d->usable;
        };
        for (const auto& id : s.preferred) if (usable(id)) return id;
        for (unsigned r : {1u, 0u}) if (usable(s.defaults[r])) return s.defaults[r];
        const Device* best = nullptr;
        for (const auto& d : s.devices) {
            if (!d.usable || d.fx) continue;
            if (!best || rank(d.form) < rank(best->form) ||
                (rank(d.form) == rank(best->form) && d.id < best->id)) best = &d;
        }
        return best ? best->id : L"";
    }
public:
    static constexpr std::uint64_t confirmationMs = 3000, maxSampleGapMs = 1500;
    bool finished = false;
    template<class Port> std::vector<Attempt> step(Port& port, std::uint64_t now, bool enabled) {
        std::vector<Attempt> results;
        // This consumer is a single startup transaction. Every terminal result,
        // including cancellation or failure, permanently consumes that launch.
        if (finished) return results;
        if (!enabled) { finished = true; return results; }
        const auto s = port.read();
        if (!s.valid || s.process != ProcessState::stopped || !s.hasFxDefault()) {
            finished = true; return results;
        }
        if (!started_) {
            started_ = true; since_ = lastTick_ = now; startupDefaults_ = s.defaults;
            for (unsigned r = 0; r < 3; ++r) affected_[r] = s.isFx(r);
            return results;
        }
        // A suspended worker or a user/device default change cancels the launch
        // instead of rearming a monitor after startup has ended.
        if (now < lastTick_ || now - lastTick_ > maxSampleGapMs || s.defaults != startupDefaults_) {
            finished = true; return results;
        }
        lastTick_ = now;
        if (now - since_ < confirmationMs) return results;
        finished = true;
        for (unsigned role = 0; role < 3; ++role) {
            if (!affected_[role]) continue;
            const auto current = port.read();
            if (!current.valid || current.process != ProcessState::stopped) break;
            if (!current.isFx(role) || current.defaults[role] != startupDefaults_[role]) continue;
            const auto output = target(current);
            if (output.empty()) { results.push_back({role, startupDefaults_[role], L"", false, 1168}); continue; }
            const auto result = port.setDefault(role, startupDefaults_[role], output);
            results.push_back({role, startupDefaults_[role], output, result.ok, result.error});
        }
        return results;
    }
};
}
