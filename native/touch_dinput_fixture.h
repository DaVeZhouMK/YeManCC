#pragma once
// Output-selftest only. A COM-shaped, in-memory DirectInput device lets the
// actual production pump handle INPUTLOST without enumerating/touching hardware.
class TouchDinputFixture final : public IDirectInputDevice8W {
public:
    HRESULT acquireResult=DI_OK,stateResult=DI_OK,pollResult=DI_NOEFFECT;
    unsigned acquires=0,reads=0,unacquires=0,releases=0;DIJOYSTATE2 state{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID* value) override {if(value)*value=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return 1;}
    ULONG STDMETHODCALLTYPE Release() override {++releases;return 1;}
    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW,LPVOID,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID,LPDIPROPHEADER) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID,LPCDIPROPHEADER) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Acquire() override {++acquires;return acquireResult;}
    HRESULT STDMETHODCALLTYPE Unacquire() override {++unacquires;return DI_OK;}
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD bytes, LPVOID value) override {++reads;if(FAILED(stateResult))return stateResult;if(bytes!=sizeof(state)||!value)return E_INVALIDARG;memcpy(value,&state,sizeof(state));return DI_OK;}
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW,DWORD,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD,REFGUID) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID,LPCDIEFFECT,LPDIRECTINPUTEFFECT *,LPUNKNOWN) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW,LPVOID,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW,REFGUID) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK,LPVOID,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Poll() override {return pollResult;}
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD,LPCDIDEVICEOBJECTDATA,LPDWORD,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR,LPDIENUMEFFECTSINFILECALLBACK,LPVOID,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR,DWORD,LPDIFILEEFFECT,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATW,LPCWSTR,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATW,LPCWSTR,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW) override {return E_NOTIMPL;}
};
