#pragma once
#include <cstdint>
#include <string>

namespace ymcc {
// Presentation only. Callers serialize this state with the existing persona lock.
// Neither a waiting request nor its generations can authorize input or mode I/O.
class VirtualOutputAdmissionStatus {
public:
    struct Request {
        std::uint64_t generation = 0;
        bool enabled = false;
        std::string persona;
    };
    struct Context {
        std::uint64_t hostEpoch = 0, powerGeneration = 0, workGeneration = 0;
        bool operator==(const Context&) const = default;
    };
    void configure(const std::string& persona, bool enabled) {
        if (persona_ == persona && enabled_ == enabled) return;
        persona_ = persona;
        enabled_ = enabled;
        ++generation_;
        waiting_ = false;
    }
    Request request() const { return {generation_, enabled_, persona_}; }
    void defer(const Request& request, const Context& context) noexcept {
        if (!current(request) || !request.enabled || request.persona == "disabled" ||
            context.powerGeneration == 0) return;
        waiting_ = true;
        waitingGeneration_ = request.generation;
        context_ = context;
    }
    void clear(const Request& request) noexcept {
        if (current(request)) waiting_ = false;
    }
    bool pending(const Request& request, const Context& context) const noexcept {
        return waiting_ && current(request) && request.enabled && request.persona != "disabled" &&
            context.powerGeneration != 0 && waitingGeneration_ == request.generation && context_ == context;
    }
private:
    bool current(const Request& request) const noexcept {
        return request.generation == generation_ && request.enabled == enabled_ && request.persona == persona_;
    }
    std::uint64_t generation_ = 1, waitingGeneration_ = 0;
    bool enabled_ = false, waiting_ = false;
    std::string persona_ = "disabled";
    Context context_{};
};

// Allocation-free, stateless comparison. JSON types and unknown members remain significant.
namespace virtualOutputPublicationDetail {
template<class Json>
bool sameJsonValue(const Json& left, const Json& right) {
    if (left.type() != right.type()) return false;
    if (left.is_object()) {
        if (left.size() != right.size()) return false;
        for (auto item = left.cbegin(); item != left.cend(); ++item) {
            const auto other = right.find(item.key());
            if (other == right.cend() || !sameJsonValue(item.value(), other.value())) return false;
        }
        return true;
    }
    if (left.is_array()) {
        if (left.size() != right.size()) return false;
        auto other = right.cbegin();
        for (auto item = left.cbegin(); item != left.cend(); ++item, ++other)
            if (!sameJsonValue(*item, *other)) return false;
        return true;
    }
    return left == right;
}
template<class Json>
bool positiveInteger(const Json& value) {
    if (value.is_number_unsigned()) return value.template get<std::uint64_t>() != 0;
    return value.is_number_integer() && value.template get<std::int64_t>() > 0;
}
} // namespace virtualOutputPublicationDetail

// Only the transport-age projection of the same active request may differ.
// No mutation, clock read, cache, timer, or lifecycle authority is introduced.
template<class Json>
bool virtualOutputPublicationEquivalent(const Json& left, const Json& right) {
    using virtualOutputPublicationDetail::sameJsonValue;
    using virtualOutputPublicationDetail::positiveInteger;
    if (!left.is_object() || !right.is_object()) return sameJsonValue(left, right);
    const auto a = left.find("startProtection"), b = right.find("startProtection");
    if (a == left.cend() || b == right.cend() || !a->is_object() || !b->is_object())
        return sameJsonValue(left, right);
    const auto al = a->find("locked"), bl = b->find("locked");
    const auto ar = a->find("requestId"), br = b->find("requestId");
    const auto ae = a->find("expiresAtUnixMs"), be = b->find("expiresAtUnixMs");
    const bool sameActiveRequest = al != a->cend() && bl != b->cend() &&
        al->is_boolean() && bl->is_boolean() && al->template get<bool>() && bl->template get<bool>() &&
        ar != a->cend() && br != b->cend() && positiveInteger(*ar) && positiveInteger(*br) &&
        sameJsonValue(*ar, *br) && ae != a->cend() && be != b->cend() &&
        positiveInteger(*ae) && positiveInteger(*be);
    if (!sameActiveRequest) return sameJsonValue(left, right);
    if (left.size() != right.size() || a->size() != b->size()) return false;
    for (auto item = left.cbegin(); item != left.cend(); ++item) {
        const auto other = right.find(item.key());
        if (other == right.cend()) return false;
        if (item.key() != "startProtection") {
            if (!sameJsonValue(item.value(), other.value())) return false;
            continue;
        }
        for (auto member = a->cbegin(); member != a->cend(); ++member) {
            const auto counterpart = b->find(member.key());
            if (counterpart == b->cend()) return false;
            if (member.key() != "expiresAtUnixMs" && !sameJsonValue(member.value(), counterpart.value()))
                return false;
        }
    }
    return true;
}

} // namespace ymcc
