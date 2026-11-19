#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#if defined(YMCC_STEAM_SESSION_CACHE_ISOLATED_CONTROL)
#define ymcc ymcc_session_cache_control
#endif
#if defined(YMCC_STEAM_COLD_FILE_FLIGHT)
#include "fixtures/steam_cold_file_flight/ports.h"
#define ReadFile cold_file_ports::read
#endif
#include "../native/steam_compatibility_win32.h"
#if defined(YMCC_STEAM_COLD_FILE_FLIGHT)
#undef ReadFile
#endif
#if defined(YMCC_STEAM_SESSION_CACHE_ISOLATED_CONTROL)
#undef ymcc
namespace proof=ymcc_session_cache_control::steamcompat;
#else
namespace proof=ymcc::steamcompat;
#endif
extern "C" bool steamSessionCachePeer(const wchar_t* path,const char* expected,bool* same,unsigned long long* bytes,unsigned long long* hashed) {
 const auto value=proof::qualifyFile(path,proof::ModuleKind::Client);
 *same=value.layout.has_value()&&proof::referenceBindingCurrent(value)&&value.sha256==expected;
 *bytes=value.fileBytesRead;*hashed=value.digestBytesHashed;return value.cacheHit;
}

#if defined(YMCC_STEAM_COLD_FILE_FLIGHT)
extern "C" void steamColdFilePeer(const wchar_t* path,bool digest,proof::Qualification* value){*value=proof::qualifyFile(path,proof::ModuleKind::Client,digest);}
#endif
