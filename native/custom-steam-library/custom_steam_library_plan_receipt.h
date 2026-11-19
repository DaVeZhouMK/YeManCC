#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include "json.hpp"

namespace custom_steam_library::plan_receipt {
// Bind the original published document, excluding its in-memory write ACK.
// The receipt stays small even when the plan exceeds the pipe retention cap.
inline std::string digest(nlohmann::json document) {
    document.erase("planWriteConfirmed");
    const auto text = document.dump();
    if (text.size() > (64u << 20)) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    unsigned char hash[32]{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    const auto result = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
        static_cast<ULONG>(text.size()), hash, sizeof hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0) return {};
    std::string receipt;
    receipt.reserve(64);
    for (auto byte : hash) {
        receipt += "0123456789abcdef"[byte >> 4];
        receipt += "0123456789abcdef"[byte & 15];
    }
    return receipt;
}
// Publication diagnostics can grow after each failed UI-result destination.
// They do not alter the completed Steam transaction or its item identities.
inline std::string addCompletionDigest(nlohmann::json document) {
    document.erase("resultPublicationWarnings");
    return digest(std::move(document));
}
}
