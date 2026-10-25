#include "custom-steam-library/custom_steam_library_logo.h"
#include <iostream>
namespace logo=custom_steam_library::logos;
namespace fs=std::filesystem;
using json=nlohmann::json;
static void require(bool condition,const char* label){if(!condition)throw std::runtime_error(label);}
static void write(const fs::path& path,const json& data){fs::create_directories(path.parent_path());std::ofstream(path)<<data.dump();}
static void png(const fs::path& path){fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);const unsigned char data[]={137,80,78,71,13,10,26,10,0,0,0,0};f.write((const char*)data,sizeof(data));}
int main(int argc,char**argv){try{
 const fs::path out=argv[1];const auto root=out/(L"fixture-"+std::to_wstring(GetTickCount64()));const std::string exe="C:\\Games\\Example\\game.exe";int checks=0;
 png(root/L"logo.png");png(root/L"icon.png");
 write(root/L"state"/L"steam-add-plan.json",{{"items",json::array({{{"primaryExecutable",exe},{"artworkPreview",{{"logo",{{"portableFile",logo::utf8((root/L"logo.png").wstring())}}}}}}})}});
 auto result=logo::resolveAtRoot(root,exe);require(!result.is_null(),"exact EXE logo");++checks;
 require(result["image"].get<std::string>().starts_with("data:image/png;base64,"),"bounded inline raster");++checks;
 require(!logo::resolveAtRoot(root,"c:/games/EXAMPLE/GAME.EXE").is_null(),"case slash normalization");++checks;
 require(logo::resolveAtRoot(root,"C:\\Other\\game.exe").is_null(),"same basename must not match");++checks;
 require(logo::resolveAtRoot(root,"game.exe").is_null(),"relative targets denied");++checks;
 write(root/L"config"/L"manual-overrides.json",{{"items",{{exe,{{"artworkProtection",{{"logo","deleted"}}}}}}}});
 require(logo::resolveAtRoot(root,exe).is_null(),"explicit deletion respected");++checks;
 write(root/L"config"/L"manual-overrides.json",json::object());
 write(root/L"state"/L"steam-add-plan.json",{{"items",json::array({{{"primaryExecutable",exe},{"artworkPreview",{{"icon",{{"portableFile",logo::utf8((root/L"icon.png").wstring())}}}}}}})}});
 require(logo::resolveAtRoot(root,exe).is_null(),"no icon or cover fallback");++checks;
 write(root/L"artwork"/L"portable"/L"manifest.json",{{"exe",exe},{"artwork",json::array({{{"type","logo"},{"ok",true},{"portableFile",logo::utf8((root/L"logo.png").wstring())}}})}});
 require(!logo::resolveAtRoot(root,exe).is_null(),"portable manifest for Steam and non-Steam");++checks;
 std::ofstream(root/L"logo.png",std::ios::binary)<<"invalid raster payload";
 require(logo::resolveAtRoot(root,exe).is_null(),"invalid image header denied");++checks;
 if(argc>2 && std::string(argv[2])=="--installed-readonly") {
 auto real=logo::resolveAtRoot(L"D:\\YeMan\\CustomSteamLibrary\\data","D:\\Game\\Cyberpunk 2077\\bin\\x64\\Cyberpunk2077.exe");
 require(!real.is_null(),"real installed non-Steam Cyberpunk cached LOGO");++checks;
 write(out/L"CYBERPUNK-LOGO.json",real);
 }
 write(out/L"NATIVE-LOGO.json",{{"ok",true},{"checks",checks},{"scope","shared resolver fixtures; optional explicit read-only installed library cache; no game/native HUD launch"}});
 std::cout<<"PASS native logo resolver "<<checks<<" checks\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}