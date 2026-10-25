// Test fixture only: capture a batch compatibility launch without managing HW.
#include <windows.h>
#include <fstream>
#include <filesystem>
#include <string>
#include "../deps/json/json.hpp"
int wmain(int argc,wchar_t**argv){wchar_t path[32768]{};if(!GetEnvironmentVariableW(L"YMCC_HWINFO_BAT_FIXTURE",path,32768))return 90;nlohmann::json args=nlohmann::json::array();for(int i=1;i<argc;++i){const std::wstring value=argv[i];const auto n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string text(n,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),text.data(),n,nullptr,nullptr);args.push_back(text);}std::ofstream(std::filesystem::path(path))<<args.dump();return 23;}
