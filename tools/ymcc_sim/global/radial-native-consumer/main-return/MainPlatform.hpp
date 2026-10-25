#pragma once
#include "MemoryPorts.hpp"
#include <atomic>
#include <array>
#include <mutex>
#include <limits>
#include <cstddef>
using SHORT=std::int16_t;using DWORD=unsigned long;using UINT_PTR=std::uintptr_t;
struct XINPUT_GAMEPAD{WORD wButtons=0;BYTE bLeftTrigger=0,bRightTrigger=0;SHORT sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
constexpr WORD XINPUT_GAMEPAD_DPAD_UP=0x0001,XINPUT_GAMEPAD_DPAD_DOWN=0x0002,XINPUT_GAMEPAD_DPAD_LEFT=0x0004,XINPUT_GAMEPAD_DPAD_RIGHT=0x0008,XINPUT_GAMEPAD_START=0x0010,XINPUT_GAMEPAD_BACK=0x0020,XINPUT_GAMEPAD_LEFT_THUMB=0x0040,XINPUT_GAMEPAD_RIGHT_THUMB=0x0080,XINPUT_GAMEPAD_LEFT_SHOULDER=0x0100,XINPUT_GAMEPAD_RIGHT_SHOULDER=0x0200,XINPUT_GAMEPAD_A=0x1000,XINPUT_GAMEPAD_B=0x2000,XINPUT_GAMEPAD_X=0x4000,XINPUT_GAMEPAD_Y=0x8000;
constexpr BYTE XINPUT_GAMEPAD_TRIGGER_THRESHOLD=30;
namespace mainports {
inline XINPUT_GAMEPAD sample{};inline bool connected=true,foreground=true,customSuppressed=false,customForeground=false;
inline unsigned parentLaneCalls=0,returnDesktopCalls=0;inline Json events=Json::array(),actions=Json::array();inline std::vector<std::function<void()>> queued;
template<class T>T denied(const char*name){throw std::runtime_error(std::string("UNSELECTED_ENDPOINT_EXECUTED:")+name);}
inline void forbidden(const char*name){throw std::runtime_error(std::string("UNSELECTED_ENDPOINT_EXECUTED:")+name);}
}
namespace ymcc {
enum class FrontendButton {Invalid=0,Steam=1,SteamQuickAccess=2,Gamebar=3};
namespace screenpads {
inline screeninput::Runtime nativeInputRuntime;
inline void radialHaptic(bool contact,bool pulse){memory::log("port.radialHaptic",{{"contact",contact},{"pulse",pulse}});}
}
}
