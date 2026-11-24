#pragma once
#include "steam_session_observer.h"

namespace ymcc::deckysetup {
// Only the installation whose marker we created can inherit this requirement.
// Accounts and UI modes are not part of Steam process identity.
class SteamRestartRequirement {
    std::mutex mutex_;
    std::wstring root_;
    std::uint64_t revision_ = 0;
    ULONGLONG createdThrough_ = 0;

    static bool sameInstall(std::wstring_view a, std::wstring_view b) noexcept {
        const auto slash=[](wchar_t c) { return c==L'/' || c==L'\\'; };
        while (!a.empty() && slash(a.back())) a.remove_suffix(1);
        while (!b.empty() && slash(b.back())) b.remove_suffix(1);
        return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[&](wchar_t x,wchar_t y) {
            return slash(x) ? slash(y) : std::towlower(x)==std::towlower(y);
        });
    }
    static bool verifiedFor(const steamsession::IdentitySnapshot& source, std::wstring_view root) noexcept {
        return source.presence==1 && source.session.pid && source.session.created && sameInstall(root,source.session.root);
    }
    void observeLocked(const steamsession::IdentitySnapshot& source) {
        if (root_.empty() || source.revision<=revision_) return;
        if (source.presence==0 || (createdThrough_ && verifiedFor(source,root_) && source.session.created>createdThrough_))
            root_.clear();
    }
public:
    template<class ReadSource>
    void markerCreated(const std::wstring& root, ULONGLONG createdThrough, ReadSource&& readSource) {
        std::lock_guard lock(mutex_);
        const auto source=readSource();
        if (source && source->presence==0) { root_.clear(); return; }
        root_=root;
        revision_=source?source->revision:0;
        // A newer PID alone is not proof: Steam can start during preparation,
        // before the actual marker exists. Compare birth with the commit bound.
        createdThrough_=source && verifiedFor(*source,root_) && source->session.created<=createdThrough ? createdThrough : 0;
    }
    void observe(const steamsession::IdentitySnapshot& source) {
        std::lock_guard lock(mutex_);
        observeLocked(source);
    }
    template<class ReadSource>
    bool requiredFor(std::wstring_view root, ReadSource&& readSource) {
        std::lock_guard lock(mutex_);
        if (root_.empty() || !sameInstall(root_,root)) return false;
        // A publication can arrive while the preparation worker rechecks files,
        // before the observer callback has forwarded it. Use the same cache.
        if (const auto source=readSource()) observeLocked(*source);
        return !root_.empty();
    }
};
}
