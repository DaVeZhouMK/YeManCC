#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

// Game-owner metadata, shared by current-global and explicitly qualified
// foreground consumers. It grants no process liveness or action permission.
// The sole builder below copies an existing game-layer qualification. It never
// elects another game or borrows the global selection generation for that game.
class NativeGameFactScope;
static std::optional<NativeGameFactScope> nativeGameFactScope(
    const NativeDetectedGame&, unsigned long long, unsigned long long);
class NativeGameFactScope final {
    NativeGameFactScope(std::uint32_t process, std::uint64_t created,
        std::wstring image, std::uint64_t rules, std::uint64_t power)
        : pid(process), birth(created), path(std::move(image)), rulesEpoch(rules), powerGeneration(power) {}
    friend std::optional<NativeGameFactScope> nativeGameFactScope(
        const NativeDetectedGame&, unsigned long long, unsigned long long);
public:
    const std::uint32_t pid;
    const std::uint64_t birth;
    const std::wstring path;
    const std::uint64_t rulesEpoch, powerGeneration;
};
static bool nativeGameFactScopeCurrent(const NativeGameFactScope& scope) {
    return !g_exitRequested.load(std::memory_order_acquire) &&
        g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready &&
        scope.rulesEpoch == g_gameRulesEpoch.load(std::memory_order_acquire) &&
        scope.powerGeneration == currentPowerGeneration();
}
static std::optional<NativeGameFactScope> nativeGameFactScope(
    const NativeDetectedGame& game, unsigned long long rules, unsigned long long power) {
    if (!game.pid || !game.processCreated || game.path.empty() ||
        game.path.size() > 32768 || game.path.find(L'\0') != std::wstring::npos || !rules || !power) return {};
    NativeGameFactScope scope(game.pid, game.processCreated, game.path, rules, power);
    if (!nativeGameFactScopeCurrent(scope)) return {};
    return scope;
}
