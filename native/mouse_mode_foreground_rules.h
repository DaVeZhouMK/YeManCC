#pragma once
#include <string>
namespace ymcc::mousePolicy {
// Inputs are the observed lower-case complete executable path and Windows root.
// Explorer owns desktop interaction surfaces beyond the main desktop HWND: tray
// overflow, Start hosts and XAML popup class names vary with Windows releases.
inline bool desktopSurface(const std::wstring& image,const std::wstring& windows,const std::wstring& windowClass) {
 (void)windowClass;
 if(windows.empty()||image.empty())return false;
 if(image==windows+L"\\explorer.exe")return true;
 return image==windows+L"\\systemapps\\microsoft.windows.startmenuexperiencehost_cw5n1h2txyewy\\startmenuexperiencehost.exe"||
  image==windows+L"\\systemapps\\microsoftwindows.client.cbs_cw5n1h2txyewy\\searchhost.exe"||
  image==windows+L"\\systemapps\\microsoft.windows.shellexperiencehost_cw5n1h2txyewy\\shellexperiencehost.exe";
}
}
