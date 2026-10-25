#pragma once
// Fixed Windows x64 ABI, corroborated by HC historical/a250 wrappers and PE audit.
// This is a test declaration header, not the recovered native implementation.
#include <cstdint>
namespace binary_test {
inline constexpr char kSha256[] = "e11d59f84b4a4a12e99b15ea998e79de06c6dfb8f1e9c637e973abd4d3d088a9";
inline constexpr std::uint64_t kBytes = 25600;
using Create = void* (__cdecl*)();
using Delete = void (__cdecl*)(void*);
using SetMode = void (__cdecl*)(void*, int);
using GetMode = int (__cdecl*)(void*);
using SetOffset = void (__cdecl*)(void*, float, float, float, int);
using Get3 = void (__cdecl*)(void*, float*, float*, float*);
using Process = void (__cdecl*)(void*, float, float, float, float, float, float, float);
using GetConfidence = float (__cdecl*)(void*);
struct Api {
    Create create = nullptr;
    Delete destroy = nullptr;
    SetMode setMode = nullptr;
    GetMode getMode = nullptr;
    SetOffset setOffset = nullptr;
    Get3 getOffset = nullptr;
    Process process = nullptr;
    GetConfidence getConfidence = nullptr;
    Get3 getCalibrated = nullptr;
    Get3 getGravity = nullptr;
};
}
