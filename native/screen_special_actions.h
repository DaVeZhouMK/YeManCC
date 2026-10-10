#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <array>
#include <functional>
namespace ymcc::screenactions {
// Complete down/up batches only. Never release physical modifiers or inject into
// an arbitrary game: the caller verifies a Steam-owned foreground window first.
inline std::array<INPUT,4> menuChord(bool quick,ULONG_PTR tag) {
    std::array<INPUT,4> inputs{};
    for(auto& input:inputs){input.type=INPUT_KEYBOARD;input.ki.dwExtraInfo=tag;}
    inputs[0].ki.wVk=VK_LCONTROL;inputs[1].ki.wVk=quick?'2':'1';
    inputs[2].ki.wVk=inputs[1].ki.wVk;inputs[2].ki.dwFlags=KEYEVENTF_KEYUP;
    inputs[3].ki.wVk=VK_LCONTROL;inputs[3].ki.dwFlags=KEYEVENTF_KEYUP;
    return inputs;
}
inline bool modifiersHeld() {
    for(int vk:{VK_CONTROL,VK_SHIFT,VK_MENU,VK_LWIN,VK_RWIN})if(GetAsyncKeyState(vk)&0x8000)return true;
    return false;
}
using InputSender=UINT (WINAPI*)(UINT,LPINPUT,int);
inline bool sendMenuBatch(bool quick,ULONG_PTR tag,InputSender send=SendInput) {
    auto inputs=menuChord(quick,tag);
    const UINT sent=send((UINT)inputs.size(),inputs.data(),sizeof(INPUT));
    if(sent && sent<inputs.size())send(2,inputs.data()+2,sizeof(INPUT));
    return sent==inputs.size();
}
inline bool sendMenuChord(bool quick,ULONG_PTR tag,HWND expectedWindow) {
    if(!expectedWindow || GetForegroundWindow()!=expectedWindow || modifiersHeld())return false;
    return sendMenuBatch(quick,tag);
}
// Read-back is required; never silently report success after a rejected toggle.
template<class Endpoint> inline bool toggleMicrophoneEndpoint(Endpoint* volume,const std::function<bool()>& allowed={}) {
    if(!volume)return false;
    BOOL before=FALSE,after=FALSE;
    HRESULT hr=volume->GetMute(&before);
    if(SUCCEEDED(hr) && allowed && !allowed())return false;
    if(SUCCEEDED(hr))hr=volume->SetMute(!before,nullptr);
    if(SUCCEEDED(hr))hr=volume->GetMute(&after);
    return SUCCEEDED(hr) && (after!=FALSE)==(before==FALSE);
}
// PS Mute means microphone mute, not VK_VOLUME_MUTE (speaker mute).
inline bool toggleDefaultMicrophoneMute(const std::function<bool()>& allowed={}) {
    const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(apartment) && apartment!=RPC_E_CHANGED_MODE)return false;
    IMMDeviceEnumerator* enumerator=nullptr;IMMDevice* device=nullptr;IAudioEndpointVolume* volume=nullptr;
    HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,__uuidof(IMMDeviceEnumerator),(void**)&enumerator);
    if(SUCCEEDED(hr))hr=enumerator->GetDefaultAudioEndpoint(eCapture,eCommunications,&device);
    if(hr==E_NOTFOUND && enumerator)hr=enumerator->GetDefaultAudioEndpoint(eCapture,eConsole,&device);
    if(SUCCEEDED(hr))hr=device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_INPROC_SERVER,nullptr,(void**)&volume);
    const bool ok=SUCCEEDED(hr) && toggleMicrophoneEndpoint(volume,allowed);
    if(volume)volume->Release();if(device)device->Release();if(enumerator)enumerator->Release();
    if(SUCCEEDED(apartment))CoUninitialize();
    return ok;
}
}
