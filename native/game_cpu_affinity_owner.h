#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// One process owner for CCD, per-game core choice and SMT. The caller serializes
// this owner; ports are the existing Win32 process APIs, not another detector.
namespace ymcc::gamecpu {
using Mask = std::uintptr_t;
using Handle = std::uintptr_t;
using Error = std::uint32_t;
struct Target {
    std::uint32_t pid = 0;
    std::uint64_t birth = 0, identity = 0, power = 0;
    std::wstring path;
    bool operator==(const Target&) const = default;
    bool valid() const { return pid > 4 && birth && identity && !path.empty(); }
    bool sameProcess(const Target& b) const { return pid == b.pid && birth == b.birth && path == b.path; }
};
struct Cpu { std::uint32_t id = 0; Mask bit = 0; };
struct Request {
    Target target;
    std::uint64_t intent = 0;
    Mask allowed = 0;
    std::vector<Cpu> cpus;
    bool sets = false, release = false;
    std::string core = "default", smt = "default";
    bool operator==(const Request& b) const {
        if (!(target == b.target) || intent != b.intent || allowed != b.allowed || sets != b.sets ||
            release != b.release || core != b.core || smt != b.smt || cpus.size() != b.cpus.size()) return false;
        for (std::size_t i = 0; i < cpus.size(); ++i)
            if (cpus[i].id != b.cpus[i].id || cpus[i].bit != b.cpus[i].bit) return false;
        return true;
    }
};
struct Ports {
    std::function<Handle(const Target&, Error&)> open;
    std::function<void(Handle)> close;
    std::function<bool(Handle, const Target&)> identity;
    std::function<bool(const Target&)> current;
    std::function<bool()> ready;
    std::function<bool(Handle, Mask&, Mask&, Error&)> affinity;
    std::function<bool(Handle, std::vector<std::uint32_t>&, Error&)> cpuSets;
    std::function<bool(Handle, Mask, Error&)> setAffinity;
    std::function<bool(Handle, const std::vector<std::uint32_t>&, Error&)> setCpuSets;
};
struct Result {
    bool applied = false, pending = false, attempted = false, cached = false;
    std::string reason;
    Error error = 0;
    Mask mask = 0;
    int setCount = 0;
};
struct Lease {
    bool active = false, affinityOwned = false, setsOwned = false, setsKnown = false;
    Target target;
    Mask original = 0, system = 0, written = 0;
    std::vector<std::uint32_t> originalSets, writtenSets;
};
class Owner {
    Lease lease_;
    Request last_;
    Result lastResult_;
    bool lastKnown_ = false;
    static bool sameSets(std::vector<std::uint32_t> a, std::vector<std::uint32_t> b) {
        std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end()); return a == b;
    }
    static std::string openReason(Error e) { return e == 5 ? "access_denied" : "process_open_failed"; }
    // Called only with an identity-bound handle. External ownership changes are
    // respected; failed restoration retains the debt and blocks a second owner.
    bool restore(Ports& p, Result& out, Handle existing = 0) {
        if (!lease_.active) return true;
        Error e = 0;
        const auto h = existing ? existing : p.open(lease_.target, e);
        if (!h) {
            if (e == 87 || e == 1168) { lease_ = {}; return true; }
            out.reason = "restore_" + openReason(e); out.error = e; return false;
        }
        const auto finish = [&] { if (!existing) p.close(h); };
        if (!p.identity(h, lease_.target)) { lease_ = {}; finish(); return true; }
        Mask mask = 0, system = 0;
        std::vector<std::uint32_t> ids;
        if (!p.affinity(h, mask, system, e) || (lease_.setsOwned && !p.cpuSets(h, ids, e))) {
            out.reason = "restore_read_failed"; out.error = e; finish(); return false;
        }
        if ((lease_.affinityOwned && mask != lease_.written) ||
            (lease_.setsOwned && !sameSets(ids, lease_.writtenSets))) {
            lease_ = {}; out.reason = "external_owner_changed"; finish(); return true;
        }
        if (!p.ready()) { out.reason = "power_not_ready"; out.pending = true; finish(); return false; }
        if (lease_.setsOwned) {
            if (!p.setCpuSets(h, lease_.originalSets, e)) {
                out.reason = "restore_cpu_sets_failed"; out.error = e; finish(); return false;
            }
            lease_.writtenSets = lease_.originalSets;
        }
        if (lease_.affinityOwned && !p.setAffinity(h, lease_.original, e)) {
            out.reason = "restore_affinity_failed"; out.error = e; finish(); return false;
        }
        lease_.written = lease_.original;
        ids.clear(); mask = 0;
        const bool verified = p.affinity(h, mask, system, e) && mask == lease_.original &&
            (!lease_.setsOwned || (p.cpuSets(h, ids, e) && sameSets(ids, lease_.originalSets)));
        finish();
        if (!verified) { out.reason = "restore_readback_unconfirmed"; out.error = e; return false; }
        out.mask = lease_.original; lease_ = {}; return true;
    }
public:
    const Lease& lease() const { return lease_; }
    void forgetAttempt() { lastKnown_ = false; }
    Result run(const Request& r, Ports& p) {
        if (lastKnown_ && r == last_) { auto result = lastResult_; result.cached = true; return result; }
        Result out;
        const auto finish = [&] { last_ = r; lastResult_ = out; lastKnown_ = true; return out; };
        if (!p.ready()) { out.reason = "power_not_ready"; out.pending = true; return finish(); }
        if (lease_.active && (!r.target.valid() || !lease_.target.sameProcess(r.target))) {
            if (!restore(p, out)) return finish();
        }
        if (!r.target.valid()) { out.reason = "no_current_game"; out.pending = true; return finish(); }
        if (!p.current(r.target)) { out.reason = "target_changed"; out.pending = true; return finish(); }
        if (r.release) {
            if (!restore(p, out)) return finish();
            out.applied = true; out.reason = "restored"; return finish();
        }
        Error e = 0;
        const auto h = p.open(r.target, e);
        out.attempted = true;
        if (!h) { out.reason = openReason(e); out.error = e; return finish(); }
        const auto close = [&] { p.close(h); };
        if (!p.identity(h, r.target) || !p.current(r.target)) {
            out.reason = "target_identity_changed"; out.pending = true; close(); return finish();
        }
        Mask before = 0, system = 0;
        std::vector<std::uint32_t> ids;
        if (!p.affinity(h, before, system, e) || !before || !system) {
            out.reason = "affinity_read_failed"; out.error = e; close(); return finish();
        }
        if (r.sets && !p.cpuSets(h, ids, e)) {
            out.reason = "cpu_sets_read_failed"; out.error = e; close(); return finish();
        }
        if (lease_.active && ((lease_.affinityOwned && before != lease_.written) ||
            (lease_.setsOwned && (!p.cpuSets(h, ids, e) || !sameSets(ids, lease_.writtenSets))))) {
            // A fresh explicit request may adopt the external owner's new limits,
            // but cannot widen them or restore the former owner's baseline.
            lease_ = {};
        }
        if (!lease_.active) {
            lease_.active = true; lease_.target = r.target;
            lease_.original = before; lease_.system = system; lease_.written = before;
        }
        if (r.sets && !lease_.setsKnown) {
            lease_.originalSets = ids; lease_.writtenSets = ids; lease_.setsKnown = true;
        }
        Mask desired = r.allowed & lease_.original & system;
        std::vector<std::uint32_t> wantedIds;
        if (r.sets) {
            Mask setMask = 0;
            for (const auto& cpu : r.cpus) {
                if (!(cpu.bit & desired)) continue;
                if (!lease_.originalSets.empty() && std::find(lease_.originalSets.begin(), lease_.originalSets.end(), cpu.id) == lease_.originalSets.end()) continue;
                wantedIds.push_back(cpu.id); setMask |= cpu.bit;
            }
            desired &= setMask;
        }
        if (!desired || (r.sets && wantedIds.empty())) {
            out.reason = "empty_policy_intersection"; close(); return finish();
        }
        if (!p.ready() || !p.current(r.target)) {
            out.reason = "target_changed_before_write"; out.pending = true; close(); return finish();
        }
        if (r.sets) {
            if (!p.setCpuSets(h, wantedIds, e)) {
                out.reason = "cpu_sets_write_failed"; out.error = e; close(); return finish();
            }
            lease_.setsOwned = true; lease_.writtenSets = wantedIds;
        } else if (lease_.setsOwned) {
            if (!p.setCpuSets(h, lease_.originalSets, e)) {
                out.reason = "cpu_sets_restore_failed"; out.error = e; close(); return finish();
            }
            lease_.writtenSets = lease_.originalSets;
        }
        if (!p.ready() || !p.current(r.target)) {
            out.reason = "target_changed_before_affinity"; out.pending = true;
            Result rollback; restore(p, rollback, h); close(); return finish();
        }
        if (!p.setAffinity(h, desired, e)) {
            out.reason = e == 5 ? "access_denied" : "affinity_write_failed"; out.error = e;
            Result rollback; restore(p, rollback, h); close(); return finish();
        }
        lease_.affinityOwned = true; lease_.written = desired;
        Mask observed = 0; ids.clear();
        const bool verified = p.affinity(h, observed, system, e) && observed == desired &&
            (!r.sets || (p.cpuSets(h, ids, e) && sameSets(ids, wantedIds)));
        if (!verified || !p.ready() || !p.current(r.target)) {
            out.reason = !verified ? "affinity_readback_unconfirmed" : "target_changed_after_write";
            out.error = e; out.pending = !verified ? false : true;
            Result rollback; restore(p, rollback, h); close(); return finish();
        }
        out.applied = true; out.mask = desired; out.setCount = static_cast<int>(wantedIds.size());
        close(); return finish();
    }
    Result stop(Ports& p) {
        Result out;
        if (restore(p, out)) out.applied = true;
        lastKnown_ = false;
        return out;
    }
};
} // namespace ymcc::gamecpu
