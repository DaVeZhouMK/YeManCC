#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_cssloader_legacy_links.h"
#include <fstream>
#include <iostream>
static void write(const std::filesystem::path& p,const std::string& s){std::ofstream(p,std::ios::binary)<<s;}
static std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
int main(int argc,char** argv){try{
 if(argc!=2)throw std::runtime_error("task evidence required");const auto root=std::filesystem::absolute(argv[1])/L"PowerControl";const auto legacy=root/L"YeManSteam",pack=root/L"steam-appearance";std::filesystem::create_directories(legacy);std::filesystem::create_directories(pack);
 unsigned checks=0;const auto check=[&](bool v,const char* n){if(!v)throw std::runtime_error(n);++checks;std::cout<<"PASS "<<n<<'\n';};
 const std::string css="D:\\Portable CSS\\CssLoader-Standalone-Headless.exe",other="C:\\Other App\\other.exe";
 write(pack/L"backend.path","C:\\SOFT\\CSSLoader Desktop\\CssLoader-Standalone-Headless.exe");write(legacy/L"CssLoader-Standalone-Headless.txt",css);write(legacy/L"custom-css.txt",css);write(legacy/L"custom-old.txt.disabled",css);write(legacy/L"LosslessScaling.txt",other);
 auto result=ymcc::steamcss::retireLegacyLinks(root.wstring());
 check(result.retired==3&&result.failed==0,"legacy preset, custom alias and disabled CSS alias are all retired once");
 check(!std::filesystem::exists(legacy/L"CssLoader-Standalone-Headless.txt")&&!std::filesystem::exists(legacy/L"custom-css.txt"),"boot BAT wildcard and VBS no longer discover a CSS launch descriptor");
 check(read(pack/L"backend.path")==css,"portable CSS path migrates into dedicated appearance config");
 check(read(legacy/L"LosslessScaling.txt")==other,"other linked apps keep exact original bytes");
 check(read(legacy/L"custom-css.txt.ymcc-retired")==css,"retirement preserves original bytes outside active addon discovery");
 result=ymcc::steamcss::retireLegacyLinks(root.wstring());check(result.retired==0&&result.failed==0,"migration is idempotent and performs no recurring work");
 write(pack/L"backend.path","E:\\Own CSS\\CssLoader-Standalone-Headless.exe");write(legacy/L"CssLoader-Standalone-Headless.txt",css);
 result=ymcc::steamcss::retireLegacyLinks(root.wstring());check(result.retired==1&&result.failed==0,"a reimported old preset is retired again");
 check(read(pack/L"backend.path")=="E:\\Own CSS\\CssLoader-Standalone-Headless.exe","existing customized appearance backend is never overwritten by an old preset");
 check(read(legacy/L"CssLoader-Standalone-Headless.txt.ymcc-retired")==css,"a previous retirement record is not overwritten");
 std::cout<<"STEAM_CSS_LINK_RETIRE_OK checks="<<checks<<" realSteamActions=0 realCssLaunches=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
