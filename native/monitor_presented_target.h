#pragma once
#include <string_view>
// HWiNFO already publishes its selected PresentMon process in the sensor's
// user caption. Consume it beside YMCC's existing elected game, without IO,
// process enumeration, display selection, or another recognition source.
namespace ymcc::monitorpresented {
inline unsigned asciiLower(unsigned c) noexcept {return c>='A'&&c<='Z'?c+('a'-'A'):c;}
inline bool targetMatches(std::string_view caption,std::wstring_view game) noexcept {
    constexpr std::string_view prefix="PresentMon [";
    if(caption.size()<=prefix.size()+1||caption.size()>127||caption.substr(0,prefix.size())!=prefix||caption.back()!=']')return true;
    auto name=caption.substr(prefix.size(),caption.size()-prefix.size()-1);
    const auto slash=name.find_last_of("/\\");if(slash!=std::string_view::npos)name.remove_prefix(slash+1);
    if(name.size()<=4||name[name.size()-4]!='.'||asciiLower(name[name.size()-3])!='e'||asciiLower(name[name.size()-2])!='x'||asciiLower(name.back())!='e')return true;
    name.remove_suffix(4);
    const auto gameSlash=game.find_last_of(L"/\\");if(gameSlash!=std::wstring_view::npos)game.remove_prefix(gameSlash+1);
    if(game.size()>4&&game[game.size()-4]==L'.'&&asciiLower(game[game.size()-3])=='e'&&asciiLower(game[game.size()-2])=='x'&&asciiLower(game.back())=='e')game.remove_suffix(4);
    if(name.empty()||game.empty())return true;
    // Old/custom captions and non-ASCII encodings cannot prove a mismatch.
    // Preserve the existing compatibility path instead of guessing an ACP.
    for(unsigned char c:name)if(c<32||c>126)return true;
    for(wchar_t c:game)if(c<32||c>126)return true;
    if(name.size()!=game.size())return false;
    for(std::size_t i=0;i<name.size();++i)if(asciiLower(static_cast<unsigned char>(name[i]))!=asciiLower(game[i]))return false;
    return true;
}
}
