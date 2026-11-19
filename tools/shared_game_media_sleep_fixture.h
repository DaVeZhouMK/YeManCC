#pragma once
// Explicit Ready memory source for original non-sleep media tests. The original
// Native runtime authenticates actual children; no OS power query is invented.
#include "../native/sleep_power_native_task.h"
#include "../native/sleep_power_remote_transport.h"
namespace mediaSleepFixture {
using namespace ymcc::sleeppower;
inline std::atomic<bool> permitted{true};inline std::atomic<uint64_t> serial{1};
inline int __cdecl read(SharedPowerV1* out,uint32_t bytes) noexcept {if(!out||bytes<sizeof(*out))return 0;*out=SharedPowerV1{};out->generation=8159;out->publicationRevision=1;out->observedNotificationSequence=1;out->suspendSerial=serial.load();out->phase=permitted.load()?0:2;out->flags=kKnown|(permitted.load()?kHardwareAllowed:0u);return 1;}
inline RevocationMirrors& mirrors(){static RevocationMirrors value;return value;}
inline void revoke(){permitted=false;serial.fetch_add(1);mirrors().revoke();}
inline void ready(){permitted=true;}
inline remote::NativeRemoteRuntime& runtime() noexcept {static remote::NativeRemoteRuntime value(&read,mirrors());return value;}
inline remote::NativeRemoteTransport* transport() noexcept {return runtime().transport();}
inline void bind(){static const task::Access access{&read,&mirrors(),&transport};if(!task::bind(access))throw std::runtime_error("fixture-public-sleep-binding-failed");}
inline std::shared_ptr<task::Permit> permit(){return task::Scope::capture();}
inline std::shared_ptr<task::Permit> child(int argc,wchar_t** argv){if((argc!=7&&argc!=13)||std::wstring_view(argv[argc-2])!=task::argument)throw std::runtime_error("fixture-child-missing-public-sleep");ymcc::steaminfo::transfer::Handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(std::stoul(argv[2]))));if(!parent.value||ymcc::media::processBirthToken(static_cast<DWORD>(std::stoul(argv[2])))!=ymcc::media::utf8(argv[3]))throw std::runtime_error("fixture-child-wrong-parent");auto value=task::import(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::stoull(argv[argc-1]))),parent.value,ymcc::media::utf8(argv[4]),true);if(!value||!value->current())throw std::runtime_error("fixture-child-public-sleep-refused");return value;}
}
