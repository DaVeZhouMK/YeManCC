#pragma once
// The ordinary native build injects this literal into main.cpp before compile.
// It identifies the executing code; never substitute a JSON value read from the
// installation directory. A receipt binds it to the sealed EXE SHA after mt.exe.
// Builds made outside that recipe remain usable but supply no upgrade allowance.
#ifndef YMCC_NATIVE_BUILD_MARKER
#define YMCC_NATIVE_BUILD_MARKER ""
#endif
namespace ymcc::buildidentity {
inline constexpr char compiledMarker[]=YMCC_NATIVE_BUILD_MARKER;
inline constexpr char prefix[]="YMCC-NATIVE-BUILD:v1:";
inline constexpr unsigned prefixSize=sizeof(prefix)-1;
inline constexpr unsigned markerSize=prefixSize+64;
constexpr bool valid(const char* value,unsigned size) noexcept {
    if(!value||size!=markerSize)return false;
    for(unsigned n=0;n<prefixSize;++n)if(value[n]!=prefix[n])return false;
    for(unsigned n=prefixSize;n<size;++n)
        if(!((value[n]>='0'&&value[n]<='9')||(value[n]>='a'&&value[n]<='f')))return false;
    return true;
}
inline constexpr bool available=valid(compiledMarker,sizeof(compiledMarker)-1);
inline const char* marker() noexcept {return available?compiledMarker:"";}
}
