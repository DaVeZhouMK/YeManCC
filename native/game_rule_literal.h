#pragma once
#include <string>
#include <string_view>
#include <type_traits>

// A rule is not a Windows process name. Preserve repeated executable suffixes
// through a tagged literal instead of feeding an already-normalized stem back
// into filename normalization. The payload is the full real basename.
namespace ymcc::gameruleliteral {
template<class C> constexpr std::basic_string_view<C> prefix();
template<> inline constexpr std::string_view prefix<char>() { return "@exe:"; }
template<> inline constexpr std::wstring_view prefix<wchar_t>() { return L"@exe:"; }
template<class C> constexpr std::basic_string_view<C> repeatedSuffix();
template<> inline constexpr std::string_view repeatedSuffix<char>() { return ".exe.exe"; }
template<> inline constexpr std::wstring_view repeatedSuffix<wchar_t>() { return L".exe.exe"; }

template<class C> inline bool reserved(std::basic_string_view<C> value) {
    const auto tag = prefix<C>();
    return value.starts_with(tag.substr(0, 4)) && value.find(C(':')) != value.npos;
}
template<class C> inline bool basename(std::basic_string_view<C> value) {
    if (value.empty() || value.front() == C(' ') || value.back() == C(' ')) return false;
    for (const C character : value) {
        const auto c = static_cast<std::make_unsigned_t<C>>(character);
        if (c < 32 || (c >= 'A' && c <= 'Z') || character == C('\\') || character == C('/') ||
            character == C(':') || character == C('*') || character == C('?') ||
            character == C('[') || character == C(']') || character == C('<') ||
            character == C('>') || character == C('|') || character == C('#') || character == C('"')) return false;
    }
    return value.ends_with(repeatedSuffix<C>());
}
template<class C> inline bool valid(std::basic_string_view<C> rule) {
    const auto tag = prefix<C>();
    return rule.starts_with(tag) && basename(rule.substr(tag.size()));
}
template<class C> inline std::basic_string<C> encode(std::basic_string_view<C> filename) {
    if (!basename(filename)) return {};
    std::basic_string<C> result(prefix<C>());
    result.append(filename);
    return result;
}
template<class C> inline bool matchesStem(std::basic_string_view<C> stem, std::basic_string_view<C> rule) {
    if (!valid(rule)) return false;
    const auto filename = rule.substr(prefix<C>().size());
    return stem == filename.substr(0, filename.size() - 4);
}
}
