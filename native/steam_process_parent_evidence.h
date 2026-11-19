#pragma once
#include "steam_game_process_facts.h"
#include <memory>
#include <utility>

// PRIVATE, SOURCE ONLY: factual object/parent evidence, never Steam identity,
// account, GameID, current health, source provenance or action authority.
namespace ymcc::steamsession {class WindowProcessCache;}
namespace ymcc::steamgamefacts {
class ProcessParentEvidenceOwner;
class HistoricalExitedParentEvidence;
class StartupParentObjectEvidence;

class ProcessObjectEvidence final : public std::enable_shared_from_this<ProcessObjectEvidence> {
    friend class ProcessParentEvidenceOwner;
    const detail::Handle handle_;
    const ProcessWitness witness_;
    const DWORD windowsSession_, creatorPid_;
    ProcessObjectEvidence(detail::Handle handle, ProcessWitness witness,
        DWORD session, DWORD creator) noexcept
        : handle_(std::move(handle)), witness_(std::move(witness)),
          windowsSession_(session), creatorPid_(creator) {}
public:
    ProcessObjectEvidence(const ProcessObjectEvidence&) = delete;
    ProcessObjectEvidence& operator=(const ProcessObjectEvidence&) = delete;
    const ProcessWitness& witness() const noexcept { return witness_; }
    DWORD windowsSession() const noexcept { return windowsSession_; }
    // Point observations only. Wrong creator or wait failure => false for BOTH;
    // exited() is deliberately not the negation of stillAlive().
    bool stillAlive() const noexcept {
        return GetCurrentProcessId() == creatorPid_ && detail::alive(handle_.value);
    }
    bool exited() const noexcept {
        return GetCurrentProcessId() == creatorPid_ && handle_.value &&
            WaitForSingleObject(handle_.value, 0) == WAIT_OBJECT_0;
    }
};

class ProcessParentEvidence final {
    friend class ProcessParentEvidenceOwner;
    const std::shared_ptr<const ProcessObjectEvidence> child_, parent_;
    ProcessParentEvidence(std::shared_ptr<const ProcessObjectEvidence> child,
        std::shared_ptr<const ProcessObjectEvidence> parent) noexcept
        : child_(std::move(child)), parent_(std::move(parent)) {}
public:
    ProcessParentEvidence(const ProcessParentEvidence&) = delete;
    ProcessParentEvidence& operator=(const ProcessParentEvidence&) = delete;
    const std::shared_ptr<const ProcessObjectEvidence>& child() const noexcept { return child_; }
    const std::shared_ptr<const ProcessObjectEvidence>& parent() const noexcept { return parent_; }
    bool childStillAlive() const noexcept { return child_->stillAlive(); }
};

class ProcessParentEvidenceOwner final {
    friend ProcessParentEvidenceOwner& processParentEvidenceOwner() noexcept;
    ProcessParentEvidenceOwner() = default;
public:
    ProcessParentEvidenceOwner(const ProcessParentEvidenceOwner&) = delete;
    ProcessParentEvidenceOwner& operator=(const ProcessParentEvidenceOwner&) = delete;

    // Caller keeps the borrowed handle valid through duplication. One reduced,
    // noninheritable owned handle; no PID reopening or caller witness accepted.
    std::shared_ptr<const ProcessObjectEvidence> capture(HANDLE borrowed) const noexcept {
        try {
            if (!borrowed) return {};
            detail::Handle held;
            if (!DuplicateHandle(GetCurrentProcess(), borrowed, GetCurrentProcess(),
                &held.value, PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, 0) ||
                !detail::alive(held.value)) return {};
            ProcessWitness witness{GetProcessId(held.value), detail::birth(held.value),
                detail::image(held.value)};
            DWORD session = 0;
            if (!detail::complete(witness) || !detail::alive(held.value) ||
                !ProcessIdToSessionId(witness.pid, &session) ||
                !detail::alive(held.value)) return {};
            const DWORD creator = GetCurrentProcessId();
            if (!creator) return {};
            auto result = std::shared_ptr<const ProcessObjectEvidence>(
                new ProcessObjectEvidence(std::move(held), std::move(witness), session, creator));
            if (!result->stillAlive()) return {};
            return result;
        } catch (...) { return {}; }
    }

    std::shared_ptr<const HistoricalExitedParentEvidence> captureHistoricalWindowParent(
        ymcc::steamsession::WindowProcessCache& cache,DWORD pid,DWORD event) const noexcept;

    std::shared_ptr<const StartupParentObjectEvidence> captureStartupWindowParent(
        ymcc::steamsession::WindowProcessCache&,DWORD,DWORD)const noexcept;
    std::shared_ptr<const HistoricalExitedParentEvidence> completeStartupWindowParent(
        const std::shared_ptr<const StartupParentObjectEvidence>&)const noexcept;

    // Independent historical fact; original live capture stays unchanged.
    std::shared_ptr<const HistoricalExitedParentEvidence> captureHistoricalParent(
        std::shared_ptr<const ProcessObjectEvidence> child) const noexcept;

    std::shared_ptr<const ProcessObjectEvidence> captureGame(const std::shared_ptr<ProcessLease>& selected) const noexcept {
        try {
            if(!selected||!selected->processCurrent())return {};
            const auto object=capture(selected->request_->gameHandle_.value);
            const auto& expected=selected->gameWitness();
            return object&&object->witness().pid==expected.pid&&object->witness().created==expected.created&&
                samePath(object->witness().image,expected.image)&&object->windowsSession()==selected->windowsSession()&&selected->processCurrent()
                ?object:std::shared_ptr<const ProcessObjectEvidence>{};
        }catch(...){return {};}
    }

    std::shared_ptr<const ProcessParentEvidence> relate(
        std::shared_ptr<const ProcessObjectEvidence> child,
        std::shared_ptr<const ProcessObjectEvidence> parent) const noexcept {
        try {
            if (!child || !parent) return {};
            // Recover issuer-created control blocks, including when the caller
            // supplies an alias/no-op shared_ptr wrapper around a real object.
            child = child->weak_from_this().lock();
            parent = parent->weak_from_this().lock();
            if (!child || !parent) return {};
            const DWORD creator = GetCurrentProcessId();
            const auto& c = child->witness_;
            const auto& p = parent->witness_;
            if (!creator || child->creatorPid_ != creator || parent->creatorPid_ != creator ||
                !detail::complete(c) || !detail::complete(p) || c.pid == p.pid ||
                p.created >= c.created || child->windowsSession_ != parent->windowsSession_ ||
                !child->stillAlive()) return {};
            // Parent was captured live, so an already-signalled parent is legal.
            // A wait error is unknown and refuses rather than pretending exit.
            const DWORD parentState = WaitForSingleObject(parent->handle_.value, 0);
            if (parentState != WAIT_TIMEOUT && parentState != WAIT_OBJECT_0) return {};
            if (detail::parent(child->handle_.value) != p.pid || !child->stillAlive()) return {};
            auto result = std::shared_ptr<const ProcessParentEvidence>(
                new ProcessParentEvidence(std::move(child), std::move(parent)));
            if (!result->childStillAlive()) return {};
            return result;
        } catch (...) { return {}; }
    }
};

inline ProcessParentEvidenceOwner& processParentEvidenceOwner() noexcept {
    // Singleton issuer retains no objects, cache, borrowed Observer or workers.
    static ProcessParentEvidenceOwner value;
    return value;
}
} // namespace ymcc::steamgamefacts
