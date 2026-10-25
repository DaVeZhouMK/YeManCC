#pragma once
#include <charconv>
#include <cstdint>
#include <string_view>
namespace ymcc::steamui {
struct ModeEpoch { std::string_view document; uint64_t entry=0; bool valid=false; };
// The live watcher emits document-id:entry-counter. Changing the document
// establishes a baseline; only a monotonic counter within it proves re-entry.
inline ModeEpoch parseModeEpoch(std::string_view text) {
    if(text.empty() || text.size()>96)return {};
    const auto separator=text.find(':');
    if(separator==std::string_view::npos || separator==0)return {};
    const auto suffix=text.substr(separator+1);
    if(suffix.empty() || suffix.size()>20 || (suffix.size()>1 && suffix.front()=='0'))return {};
    for(const auto ch:suffix)if(ch<'0'||ch>'9')return {};
    uint64_t entry=0;const auto result=std::from_chars(suffix.data(),suffix.data()+suffix.size(),entry);
    if(result.ec!=std::errc{} || result.ptr!=suffix.data()+suffix.size())return {};
    return {text.substr(0,separator),entry,true};
}
inline bool laterModeEntry(std::string_view before, std::string_view after) {
    const auto previous=parseModeEpoch(before),current=parseModeEpoch(after);
    return previous.valid && current.valid && previous.document==current.document && current.entry>previous.entry;
}
inline bool updateModeEpoch(std::string_view before, std::string_view after) {
    const auto previous=parseModeEpoch(before),current=parseModeEpoch(after);
    return current.valid && (!previous.valid || previous.document!=current.document || current.entry>=previous.entry);
}
}