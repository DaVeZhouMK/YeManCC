#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

// Pure, header-only validation of the Microsoft pack(8) THREADNAME_INFO ABI.
// No Win32 calls, allocation, logging, pointer dereference, or continuation side effects.
namespace threadname_policy {

inline constexpr std::uint32_t exception_code = 0x406d1388u;
inline constexpr std::uint32_t current_thread = 0xffffffffu;
inline constexpr std::uint32_t software_origin_flag = 0x80u;
enum class Architecture { x86, x64, unsupported };
enum class Reason {
    recognized,
    other_exception,
    unsupported_architecture,
    not_first_chance,
    exception_flags,
    parameter_count,
    truncated_parameters,
    parameter_width,
    type,
    reserved_flags,
    null_name,
    zero_thread_id
};

// Deliberately contains neither the name address nor its text.
// named_thread_id can name ANOTHER thread; current_thread means the event's thread.
struct Result {
    Reason reason;
    std::uint32_t named_thread_id = 0;
    [[nodiscard]] constexpr bool recognized() const noexcept {
        return reason == Reason::recognized;
    }
};

// Select architecture from the target, never from the debugger's sizeof(void*).
// Word may be a 32-bit or 64-bit unsigned ExceptionInformation element.
// When widening x86 records, zero-extend each of the FOUR words separately.
// Only NumberParameters defined elements may be read, even with a 15-word array.
// Recognition is structural, not proof of pointer validity or exception authenticity.
template <class Word, std::size_t Extent>
[[nodiscard]] constexpr Result classify(
    Architecture target, std::uint32_t code, std::uint32_t first_chance,
    std::uint32_t flags, std::uint32_t number_parameters,
    std::span<const Word, Extent> parameters) noexcept {
    static_assert(std::is_integral_v<Word> && std::is_unsigned_v<Word>);
    static_assert(sizeof(Word) == 4 || sizeof(Word) == 8);
    if (code != exception_code) return {Reason::other_exception};
    if (target != Architecture::x86 && target != Architecture::x64)
        return {Reason::unsupported_architecture};
    if (first_chance == 0) return {Reason::not_first_chance};
    // Permit only the OS-reserved EXCEPTION_SOFTWARE_ORIGINATE annotation.
    // Microsoft passes RaiseException flags=0; the OS may annotate the event.
    if ((flags & ~software_origin_flag) != 0) return {Reason::exception_flags};
    const std::uint32_t expected = target == Architecture::x64 ? 3u : 4u;
    if (number_parameters != expected) return {Reason::parameter_count};
    if (parameters.size() < expected) return {Reason::truncated_parameters};
    if (target == Architecture::x64 && sizeof(Word) < 8)
        return {Reason::parameter_width};
    if (target == Architecture::x86) {
        for (std::size_t i = 0; i < expected; ++i)
            if (static_cast<std::uint64_t>(parameters[i]) > 0xffffffffull)
                return {Reason::parameter_width};
    }
    // x64 word 0: low DWORD = dwType; high DWORD = UNSPECIFIED padding.
    // Do not demand that padding be zero (the Microsoft sample doesn't initialize it).
    if (static_cast<std::uint32_t>(parameters[0]) != 0x1000u)
        return {Reason::type};
    const auto thread_id = static_cast<std::uint32_t>(parameters[2]);
    const auto reserved = target == Architecture::x64
        ? static_cast<std::uint32_t>(static_cast<std::uint64_t>(parameters[2]) >> 32)
        : static_cast<std::uint32_t>(parameters[3]);
    if (reserved != 0) return {Reason::reserved_flags};
    // Opaque, non-null address only: never dereference, return, persist, or log it.
    if (parameters[1] == 0) return {Reason::null_name};
    if (thread_id == 0) return {Reason::zero_thread_id};
    return {Reason::recognized, thread_id};
}

template <class Word, std::size_t Count>
[[nodiscard]] constexpr Result classify(
    Architecture target, std::uint32_t code, std::uint32_t first_chance,
    std::uint32_t flags, std::uint32_t number_parameters,
    const Word (&parameters)[Count]) noexcept {
    return classify(target, code, first_chance, flags, number_parameters,
                    std::span<const Word, Count>{parameters});
}

[[nodiscard]] constexpr const char* reason_text(Reason reason) noexcept {
    switch (reason) {
    case Reason::recognized: return "recognized";
    case Reason::other_exception: return "other-exception";
    case Reason::unsupported_architecture: return "unsupported-architecture";
    case Reason::not_first_chance: return "not-first-chance";
    case Reason::exception_flags: return "exception-flags";
    case Reason::parameter_count: return "parameter-count";
    case Reason::truncated_parameters: return "truncated-parameters";
    case Reason::parameter_width: return "parameter-width";
    case Reason::type: return "type";
    case Reason::reserved_flags: return "reserved-flags";
    case Reason::null_name: return "null-name";
    case Reason::zero_thread_id: return "zero-thread-id";
    }
    return "invalid-reason";
}

} // namespace threadname_policy

