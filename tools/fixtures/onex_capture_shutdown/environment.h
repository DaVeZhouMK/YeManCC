#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
using BYTE=unsigned char;
using DWORD=uint32_t;
using USHORT=uint16_t;
using BOOL=int;
using ULONGLONG=unsigned long long;
struct FakeHandle { DWORD wait=258, bytes=0, error=996; bool ok=false, closed=false; };
using HANDLE=FakeHandle*;
static HANDLE const INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
struct OVERLAPPED { HANDLE hEvent=nullptr; };
static constexpr BOOL FALSE=0;
static constexpr DWORD WAIT_OBJECT_0=0,WAIT_TIMEOUT=258,WAIT_FAILED=0xffffffffu;
static constexpr DWORD ERROR_IO_INCOMPLETE=996,ERROR_OPERATION_ABORTED=995;
static ULONGLONG fixtureTick=10000;
static DWORD fixtureError=0;
static int fixtureCancels=0,fixtureWaits=0,fixtureResults=0,fixtureCloses=0;
static int fixtureParses=0,fixtureOpened=0,fixtureBindAttempts=0,fixtureInit=0,fixtureStartRead=0;
static std::array<FakeHandle,3> fixtureHandles{};
static std::vector<json> fixtureLogs;

static int fixtureProducerRevokes=0;
static std::array<int,4> fixtureSiblingCloses{};
static void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static ULONGLONG GetTickCount64(){return fixtureTick;}
static DWORD GetLastError(){return fixtureError;}
static DWORD WaitForSingleObject(HANDLE h,DWORD timeout){require(timeout==0,"blocking OS wait forbidden");require(h&&!h->closed,"wait on closed event");++fixtureWaits;return h->wait;}
static BOOL GetOverlappedResult(HANDLE h,OVERLAPPED* ov,DWORD* bytes,BOOL wait){require(!wait,"blocking overlapped result forbidden");require(h!=INVALID_HANDLE_VALUE&&!h->closed,"result on closed device");require(ov&&ov->hEvent&&!ov->hEvent->closed,"result event invalid");++fixtureResults;const auto& e=*ov->hEvent;*bytes=e.bytes;fixtureError=e.error;return e.ok;}
static BOOL CancelIoEx(HANDLE h,OVERLAPPED*){require(h!=INVALID_HANDLE_VALUE&&h&&!h->closed,"cancel on invalid device");++fixtureCancels;return 1;}
static BOOL CloseHandle(HANDLE);
static void appendNativeLifecycleLog(const char* event,const json& detail){if(std::string(event)=="oem-producer-edge"&&detail.value("status","")=="cancelled")++fixtureProducerRevokes;auto item=detail;item["event"]=event;fixtureLogs.push_back(std::move(item));}
struct FixtureSibling {bool enabled=true;};
static FixtureSibling g_legionBack,g_legionSBack,g_gpdBack,g_rogOemKey;
static std::atomic<bool> g_gpdBackEnabledSnap{true};
static std::atomic<int> g_oemBackState{0};
static std::atomic<uint64_t> g_oemKeyMaskHid{0};
static void legionBackClose(){++fixtureSiblingCloses[0];}
static void legionSBackClose(){++fixtureSiblingCloses[1];}
static void gpdBackClose(){++fixtureSiblingCloses[2];}
static void rogOemKeyClose(){++fixtureSiblingCloses[3];}
