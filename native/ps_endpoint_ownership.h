#pragma once
#include "json.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ymcc::psEndpointOwnership {
struct Expected {
    std::string hostInstanceId, runId, targetId, persona;
    std::uint64_t epoch = 0;
};
struct Snapshot {
    bool capable = false, valid = false;
    std::uint64_t generation = 0, revision = 0;
    std::string rootInstanceId, reason = "not-captured";
    std::vector<std::string> hidInstanceIds;
};
struct Retirement {
    bool required = false;
    Expected owner;
    Snapshot target;
};
// Owned by the existing Host transaction lock. Only revision() is read outside
// that lock, after a slow read, to revoke observations of a replaced target.
class State {
public:
    using Json = nlohmann::json;
    void hello(const Json& reply, Expected expected) {
        retirement_ = {};
        expected_ = std::move(expected); lastGeneration_ = replacementFloor_ = 0; replacementPending_ = false;
        state_ = {};
        try { state_.capable = expected_.persona == "dualsense-edge" &&
            reply.contains("psEndpointOwnershipVersion") && reply["psEndpointOwnershipVersion"].is_number_integer() &&
            reply["psEndpointOwnershipVersion"].get<int>() == 1; } catch (...) {}
        invalidate(state_.capable ? "awaiting-current-target-binding" : "ownership-capability-unavailable");
    }
    void clear() {
        retirement_ = {};
        expected_ = {}; lastGeneration_ = replacementFloor_ = 0; replacementPending_ = false; state_ = {};
        invalidate("host-cleared");
    }
    void invalidate(const char* reason) {
        state_.valid = false; state_.rootInstanceId.clear(); state_.hidInstanceIds.clear();
        state_.reason = reason ? reason : "binding-unavailable";
        state_.revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    void replacementRequested() {
        if (expected_.persona != "dualsense-edge") return;
        replacementFloor_ = lastGeneration_; replacementPending_ = true;
        invalidate("target-replacement-requested");
    }
    bool accept(const Json& reply) noexcept {
        try {
            if (!state_.capable) return reject("ownership-capability-unavailable");
            const auto it = reply.find("psEndpointOwnership");
            if (it == reply.end() || !it->is_object()) return reject("ownership-receipt-missing");
            const auto& record = *it;
            if(record.dump().size()>8192)return reject("ownership-evidence-size-limit");
            if (!sameTuple(reply, expected_) || !sameTuple(record, expected_) ||
                text(record,"persona") != "dualsense-edge") return reject("ownership-tuple-mismatch");
            if (!record.contains("schemaVersion") || !record["schemaVersion"].is_number_integer() ||
                record["schemaVersion"].get<int>() != 1 ||
                text(record,"source") != "sdk-instance-id-and-cm-subtree") return reject("ownership-source-unproven");
            const auto g = record.find("bindingGeneration");
            if (g == record.end() || !g->is_number_integer() ||
                (!g->is_number_unsigned() && g->get<std::int64_t>() <= 0)) return reject("ownership-generation-invalid");
            const auto generation = g->get<std::uint64_t>();
            if (!generation) return reject("ownership-generation-invalid");
            if (generation < lastGeneration_ || (replacementPending_ && generation <= replacementFloor_))
                return reject("ownership-generation-retired");
            const auto complete = record.find("complete");
            if (complete == record.end() || !complete->is_boolean()) return reject("ownership-completeness-invalid");
            const bool generationChanged = generation != lastGeneration_;
            lastGeneration_ = generation; state_.generation = generation; replacementPending_ = false;
            if (!complete->get<bool>()) {
                auto detail=text(record,"reason");if(detail.size()>160)detail.resize(160);
                return reject(("ownership-observation-incomplete:"+detail).c_str());
            }
            const auto root = text(record,"sdkRootInstanceId");
            if (!validRoot(root)) return reject("ownership-root-invalid");
            const auto ids = record.find("hidInstanceIds");
            if (ids == record.end() || !ids->is_array() || ids->empty() || ids->size() > 128)
                return reject("ownership-hid-set-invalid");
            std::vector<std::string> hids; std::set<std::string> unique;
            for (const auto& id : *ids) {
                if (!id.is_string()) return reject("ownership-hid-id-invalid");
                auto value = id.get<std::string>(); const auto normalized = fold(value);
                if (!validId(value) || normalized.rfind("HID\\",0) != 0 || !unique.insert(normalized).second)
                    return reject("ownership-hid-id-invalid");
                hids.push_back(normalized);
            }
            std::sort(hids.begin(),hids.end());
            if (state_.valid && !generationChanged && fold(state_.rootInstanceId) == fold(root) &&
                state_.hidInstanceIds == hids) return true;
            state_.valid = true; state_.rootInstanceId = root; state_.hidInstanceIds = std::move(hids);
            state_.reason = "sdk-owned-target-pinned";
            state_.revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
            return true;
        } catch (...) { return reject("ownership-receipt-malformed"); }
    }
    // Invalidate publication separately from the immutable old return identity.
    // Missing accepted identity is retained as an unknown-root obligation, not absence.
    void captureRetirement(bool targetMayExist) {
        if (!targetMayExist || expected_.persona != "dualsense-edge" || retirement_.required) return;
        retirement_ = {true, expected_, state_};
    }
    bool retirementRequired() const noexcept { return retirement_.required; }
    const Retirement& retirement() const noexcept { return retirement_; }
    bool confirmRetirement(const std::string& run, std::uint64_t epoch) {
        if (!retirement_.required) return true;
        if (!epoch || retirement_.owner.epoch != epoch || retirement_.owner.runId != run) return false;
        retirement_ = {}; return true;
    }
    Snapshot snapshot() const { return state_; }
    std::uint64_t revision() const noexcept { return revision_.load(std::memory_order_acquire); }
    bool capable() const noexcept { return state_.capable; }
private:
    static std::string text(const Json& object,const char* key) {
        const auto it=object.find(key);return it!=object.end()&&it->is_string()?it->get<std::string>():std::string{};
    }
    static std::string fold(std::string value) {
        for (auto& ch:value) if(ch>='a'&&ch<='z')ch=static_cast<char>(ch-'a'+'A');return value;
    }
    static bool validId(const std::string& value) {
        if(value.empty()||value.size()>512)return false;
        for(const unsigned char ch:value)if(ch<32||ch==127)return false;return true;
    }
    static bool validRoot(const std::string& value) {
        const auto n=fold(value);return validId(value)&&(n.rfind("SWD\\",0)==0||n.rfind("ROOT\\",0)==0||n.rfind("USB\\",0)==0);
    }
    static bool sameTuple(const Json& value,const Expected& e) {
        return value.is_object() && !e.hostInstanceId.empty() && !e.runId.empty() && !e.targetId.empty() && e.epoch &&
            text(value,"hostInstanceId")==e.hostInstanceId && text(value,"runId")==e.runId &&
            text(value,"targetId")==e.targetId && value.contains("epoch") && value["epoch"].is_number_integer() &&
            value["epoch"].get<std::uint64_t>()==e.epoch;
    }
    bool reject(const char* why) noexcept { try{invalidate(why);}catch(...){} return false; }
    Retirement retirement_;
    Expected expected_;
    Snapshot state_;
    std::uint64_t lastGeneration_ = 0, replacementFloor_ = 0;
    bool replacementPending_ = false;
    std::atomic<std::uint64_t> revision_{0};
};
} // namespace ymcc::psEndpointOwnership
