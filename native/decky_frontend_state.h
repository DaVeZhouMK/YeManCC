#pragma once
// Observation labels only. Never a connection proof or recovery permission.
#include <string_view>
#include <initializer_list>
namespace ymcc::deckyfrontend {
inline bool qamPanelState(std::string_view value) noexcept {
    return value=="unobserved"||value=="committed"||value=="native-ready-commit";
}
inline bool backendState(std::string_view value) noexcept {
    for(const auto allowed:{std::string_view{"socket-missing"},std::string_view{"socket-connecting"},std::string_view{"socket-open"},std::string_view{"socket-closing"},std::string_view{"socket-closed"},std::string_view{"socket-unavailable"}})if(value==allowed)return true;
    return false;
}
inline bool clientState(std::string_view value) noexcept {
    for(const auto allowed:{std::string_view{"disposed"},std::string_view{"native-run-missing"},std::string_view{"binding-run-missing"},std::string_view{"run-mismatch"},std::string_view{"socket-missing"},std::string_view{"socket-connecting"},std::string_view{"socket-closing"},std::string_view{"socket-closed"},std::string_view{"socket-unavailable"},std::string_view{"transport-unconfirmed"},std::string_view{"opening-reply-missing"},std::string_view{"connected"},std::string_view{"module-dismounted"},std::string_view{"frontend-absent"},std::string_view{"frontend-inactive"},std::string_view{"read-failed"}})if(value==allowed)return true;
    return false;
}
}
