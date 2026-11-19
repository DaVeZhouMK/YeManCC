#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace ymcc::steamgameroles {

inline constexpr std::uint32_t kMaxMembers = 32u;
inline constexpr std::uint32_t kNoIndex = 0xffffffffu;

struct MemberIdentity {
    std::uint32_t pid{};
    std::uint32_t session{};
    std::uint32_t parentPid{};
    std::uint64_t birth{};
};

struct Graph {
    std::uint32_t count{};
    std::uint32_t selectedPid{};
    std::array<MemberIdentity, kMaxMembers> members{};
};

// Explicit values: no implicit bool conversion or "nonzero means verified".
enum class State : std::uint32_t {
    Unknown = 0u,
    Invalid = 1u,
    ExactSelected = 2u,
    VerifiedGroupRoles = 3u,
};

enum class Reason : std::uint32_t {
    None = 0u,
    NotInspected = 1u,
    CountOutOfRange = 2u,
    SelectedPidZero = 3u,
    NativePrimaryPidZero = 4u,
    NativeForegroundPidZero = 5u,
    MemberPidZero = 6u,
    MemberBirthZero = 7u,
    DuplicateMemberPid = 8u,
    SessionMismatch = 9u,
    SelfParent = 10u,
    Cycle = 11u,
    MultipleRoots = 12u,
    ParentBirthNotEarlier = 13u,
    SelectedMissing = 14u,
    NativePrimaryMissing = 15u,
    NativeForegroundMissing = 16u,
    NativePrimaryNotAncestor = 17u,
    NativeForegroundNotAncestor = 18u,
};

struct Role {
    std::uint32_t pid{};
    std::uint32_t index{kNoIndex};
};

struct Result {
    State state{State::Unknown};
    Reason reason{Reason::NotInspected};
    Role selected{};
    Role root{};
    Role nativePrimary{};
    Role nativeForeground{};
    // Raw input equality bits (0 or 1), never topology/permission substitutes.
    // Even zero == zero is equal; only state/reason classify the inspection.
    std::uint32_t nativePrimaryExact{};
    std::uint32_t nativeForegroundExact{};
    std::uint32_t offendingIndex{kNoIndex};
    std::uint32_t relatedIndex{kNoIndex};
};

static_assert(kMaxMembers == 32u);
static_assert(sizeof(std::uint32_t) == 4u && sizeof(std::uint64_t) == 8u);
static_assert(sizeof(State) == 4u && sizeof(Reason) == 4u);
static_assert(sizeof(Role) == 8u && sizeof(Result) == 56u);
static_assert(std::is_standard_layout<MemberIdentity>::value &&
              std::is_trivially_copyable<MemberIdentity>::value);
static_assert(std::is_standard_layout<Graph>::value &&
              std::is_trivially_copyable<Graph>::value);
static_assert(std::is_standard_layout<Result>::value &&
              std::is_trivially_copyable<Result>::value);

namespace detail {

// Called only after inspect has bounded count and rejected duplicate PIDs.
[[nodiscard]] constexpr std::uint32_t indexOf(
    const Graph& graph, std::uint32_t pid) noexcept {
    for (std::uint32_t i = 0u; i < graph.count; ++i) {
        if (graph.members[i].pid == pid) return i;
    }
    return kNoIndex;
}

[[nodiscard]] constexpr Result fail(
    Result result, State state, Reason reason,
    std::uint32_t offendingIndex = kNoIndex,
    std::uint32_t relatedIndex = kNoIndex) noexcept {
    result.state = state;
    result.reason = reason;
    result.offendingIndex = offendingIndex;
    result.relatedIndex = relatedIndex;
    return result;
}

} // namespace detail

// Topology only. The caller owns source completeness/full GameID, object pins,
// parent-object attribution, liveness, account/Steam/power epochs and final
// action admission. Neither successful state grants any of those guarantees.
// Input must remain immutable for this call; only [0, count) is inspected.
// parentPid == 0 or a PID outside the group terminates a chain. It is not
// evidence of an external parent's identity, nor of Steam's launch-primary.
// Natural C++ layout is not a serialized Packet/IPC ABI; no packed copy here.
[[nodiscard]] constexpr Result inspect(
    const Graph& graph,
    std::uint32_t nativePrimaryPid,
    std::uint32_t nativeForegroundPid) noexcept {
    Result result{};
    result.selected.pid = graph.selectedPid;
    result.nativePrimary.pid = nativePrimaryPid;
    result.nativeForeground.pid = nativeForegroundPid;
    result.nativePrimaryExact = static_cast<std::uint32_t>(
        nativePrimaryPid == graph.selectedPid);
    result.nativeForegroundExact = static_cast<std::uint32_t>(
        nativeForegroundPid == graph.selectedPid);

    if (graph.count == 0u || graph.count > kMaxMembers) {
        return detail::fail(result, State::Invalid, Reason::CountOutOfRange);
    }

    // Reject malformed identities before following any parent index.
    // Session zero is valid when shared by all members.
    for (std::uint32_t i = 0u; i < graph.count; ++i) {
        const auto& member = graph.members[i];
        if (member.pid == 0u) {
            return detail::fail(result, State::Invalid, Reason::MemberPidZero, i);
        }
        if (member.birth == 0u) {
            return detail::fail(result, State::Unknown, Reason::MemberBirthZero, i);
        }
        if (member.parentPid == member.pid) {
            return detail::fail(result, State::Invalid, Reason::SelfParent, i, i);
        }
        if (member.session != graph.members[0u].session) {
            return detail::fail(result, State::Invalid, Reason::SessionMismatch, i, 0u);
        }
        for (std::uint32_t j = 0u; j < i; ++j) {
            if (graph.members[j].pid == member.pid) {
                return detail::fail(
                    result, State::Invalid, Reason::DuplicateMemberPid, i, j);
            }
        }
    }

    std::array<std::uint32_t, kMaxMembers> parents{};
    for (std::uint32_t i = 0u; i < graph.count; ++i) {
        parents[i] = detail::indexOf(graph, graph.members[i].parentPid);
    }

    // Every member must reach exactly the same external-parent root.
    // A rootless component necessarily exhausts count steps (a cycle).
    std::uint32_t rootIndex = kNoIndex;
    for (std::uint32_t i = 0u; i < graph.count; ++i) {
        std::uint32_t cursor = i;
        std::uint32_t terminalRoot = kNoIndex;
        for (std::uint32_t step = 0u; step < graph.count; ++step) {
            if (parents[cursor] == kNoIndex) {
                terminalRoot = cursor;
                break;
            }
            cursor = parents[cursor];
        }
        if (terminalRoot == kNoIndex) {
            return detail::fail(result, State::Invalid, Reason::Cycle, i, cursor);
        }
        if (rootIndex == kNoIndex) {
            rootIndex = terminalRoot;
        } else if (rootIndex != terminalRoot) {
            return detail::fail(
                result, State::Invalid, Reason::MultipleRoots, rootIndex, terminalRoot);
        }
    }

    // Deliberately strict: equal creation stamps do not establish an earlier
    // parent. Report the ambiguity instead of silently accepting the edge.
    for (std::uint32_t i = 0u; i < graph.count; ++i) {
        const auto parentIndex = parents[i];
        if (parentIndex != kNoIndex &&
            graph.members[parentIndex].birth >= graph.members[i].birth) {
            return detail::fail(
                result, State::Invalid, Reason::ParentBirthNotEarlier, i, parentIndex);
        }
    }
    result.root = Role{graph.members[rootIndex].pid, rootIndex};

    if (graph.selectedPid == 0u) {
        return detail::fail(result, State::Unknown, Reason::SelectedPidZero);
    }
    if (nativePrimaryPid == 0u) {
        return detail::fail(result, State::Unknown, Reason::NativePrimaryPidZero);
    }
    if (nativeForegroundPid == 0u) {
        return detail::fail(result, State::Unknown, Reason::NativeForegroundPidZero);
    }
    result.selected.index = detail::indexOf(graph, graph.selectedPid);
    result.nativePrimary.index = detail::indexOf(graph, nativePrimaryPid);
    result.nativeForeground.index = detail::indexOf(graph, nativeForegroundPid);
    if (result.selected.index == kNoIndex) {
        return detail::fail(result, State::Unknown, Reason::SelectedMissing);
    }
    if (result.nativePrimary.index == kNoIndex) {
        return detail::fail(result, State::Unknown, Reason::NativePrimaryMissing);
    }
    if (result.nativeForeground.index == kNoIndex) {
        return detail::fail(result, State::Unknown, Reason::NativeForegroundMissing);
    }

    // Include selected and root. Siblings and other child subtrees may exist
    // in the graph but cannot supply either native role for this selected.
    std::uint64_t selectedAncestors = 0u;
    std::uint32_t cursor = result.selected.index;
    for (std::uint32_t step = 0u; step < graph.count; ++step) {
        selectedAncestors |= (std::uint64_t{1u} << cursor);
        if (parents[cursor] == kNoIndex) break;
        cursor = parents[cursor];
    }
    if ((selectedAncestors &
         (std::uint64_t{1u} << result.nativePrimary.index)) == 0u) {
        return detail::fail(
            result, State::Invalid, Reason::NativePrimaryNotAncestor,
            result.nativePrimary.index, result.selected.index);
    }
    if ((selectedAncestors &
         (std::uint64_t{1u} << result.nativeForeground.index)) == 0u) {
        return detail::fail(
            result, State::Invalid, Reason::NativeForegroundNotAncestor,
            result.nativeForeground.index, result.selected.index);
    }

    result.state = (result.nativePrimaryExact == 1u &&
                    result.nativeForegroundExact == 1u)
        ? State::ExactSelected : State::VerifiedGroupRoles;
    result.reason = Reason::None;
    return result;
}


// Structural classification only. A true result grants no record, process,
// account or publication authority; consumers must hold and recheck those.
inline bool directLauncherTuple(const Graph& graph,std::uint32_t primary,
    std::uint32_t foreground) noexcept {
    if(graph.count!=2u||!primary||primary!=foreground)return false;
    const auto result=inspect(graph,primary,foreground);
    if(result.state!=State::VerifiedGroupRoles||result.nativePrimaryExact||
        result.nativeForegroundExact||result.root.pid!=primary||
        result.nativePrimary.index!=result.root.index||
        result.nativeForeground.index!=result.root.index||
        result.selected.index>=graph.count||result.root.index>=graph.count||
        result.selected.index==result.root.index)return false;
    const auto& child=graph.members[result.selected.index];
    const auto& parent=graph.members[result.root.index];
    return child.parentPid==parent.pid&&parent.birth<child.birth&&
        child.session==parent.session;
}

} // namespace ymcc::steamgameroles
