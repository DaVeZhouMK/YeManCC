#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/decky_sidebar_bootstrap.h"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char**argv){try{
 if(argc!=3)throw std::runtime_error("fixture directory and built plugin required");namespace fs=std::filesystem;const fs::path root=fs::absolute(argv[1]);fs::create_directories(root);int checks=0;const auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);checks++;};const std::string id(64,'a');
 const auto write=[&](const char* name,const std::string& text){auto file=root/name;std::ofstream(file,std::ios::binary)<<text;return file.wstring();};
 check(ymcc::deckymirror::frontendBuildId(write("valid.js","// YMCC_DECKY_FRONTEND_BUILD_ID="+id+"\nexport default()=>({});"))==id,"exact version prefix accepted");
 check(ymcc::deckymirror::frontendBuildId(write("crlf.js","// YMCC_DECKY_FRONTEND_BUILD_ID="+id+"\r\n"))==id,"Windows line endings accepted");
 check(ymcc::deckymirror::frontendBuildId(write("legacy.js","export default()=>({});")).empty(),"legacy metadata absent remains a tolerant empty hint");
 check(ymcc::deckymirror::frontendBuildId(write("badhex.js","// YMCC_DECKY_FRONTEND_BUILD_ID="+std::string(64,'z')+"\n")).empty(),"nonhex version ignored");
 check(ymcc::deckymirror::frontendBuildId(write("suffix.js","// YMCC_DECKY_FRONTEND_BUILD_ID="+id+"evil\n")).empty(),"longer token cannot impersonate exact build");
 check(ymcc::deckymirror::frontendBuildId(write("short.js","// YMCC_DECKY_FRONTEND_BUILD_ID=aa\n")).empty(),"truncated metadata ignored");
 check(ymcc::deckymirror::frontendBuildId((root/L"missing.js").wstring()).empty(),"missing file does not block ordinary runtime");
 const auto actual=ymcc::deckymirror::frontendBuildId(fs::path(argv[2]).wstring());check(actual.size()==64,"actual new plugin header readable by native owner");
 std::cout<<nlohmann::json({{"ok",true},{"checks",checks},{"actualBuildId",actual},{"actualSteam",false}}).dump()<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
