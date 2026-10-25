#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>
namespace fspath=std::filesystem;
//__PRODUCTION_RESOLVE__
static std::wstring installTarget(){const std::wstring POWER_CONTROL_DIR=resolve_power_control_dir();
//__PRODUCTION_INSTALL_TARGET__
return pcDir;}
int wmain(int argc,wchar_t**argv){try{
 if(argc!=2)return 2;const auto root=fspath::absolute(argv[1]);fspath::create_directories(root);
 SetEnvironmentVariableW(L"YEMAN_POWER_CONTROL_DIR",nullptr);
 if(installTarget()!=L"C:\\SOFT\\YeMan\\PowerControl")throw std::runtime_error("canonical default changed");
 SetEnvironmentVariableW(L"YEMAN_POWER_CONTROL_DIR",root.c_str());
 if(installTarget()!=root.wstring())throw std::runtime_error("updater ignored valid active PowerControl override");
 SetEnvironmentVariableW(L"YEMAN_POWER_CONTROL_DIR",(root/L"missing").c_str());
 if(installTarget()!=L"C:\\SOFT\\YeMan\\PowerControl")throw std::runtime_error("invalid override changed canonical fallback");
 std::cout<<"UPDATER_TARGET_ROOT_PASS checks=3 consumer=production-resolver-and-install-target hardwareIo=false canonicalWrites=false\n";return 0;
 }catch(const std::exception&error){std::cerr<<error.what()<<"\n";return 1;}}
