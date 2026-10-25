#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#include <atomic>
#include <array>
#include <string>
#include <cstdint>
// These two types retain their exact current production declaration bytes.
#include "actual-common-types.inc"
struct WorkItem {XINPUT_GAMEPAD pad{};MsiCanonicalReadProof proof{};};
struct Output {InputCaptureGamepadSnapshot sample{};MsiCanonicalReadProof producer{};};
struct StateCounts {std::uint64_t seqlock=0,producer=0;};
struct BenchApi {
 const char* name;
 void (*reset)();
 bool (*publish)(const WorkItem*,Output*);
 bool (*snapshot)(const WorkItem*,Output*);
 bool (*pipeline)(const WorkItem*,Output*);
 bool (*apply)(const WorkItem*,Output*);
 StateCounts (*counts)();
 std::size_t stateBytes;
};
extern const BenchApi baselineApi,currentApi;