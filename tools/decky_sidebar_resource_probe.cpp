// Read-only production resource inspection. Never prepares Steam, starts a loader,
// invokes YMCC or reads/writes business settings.
#define WIN32_LEAN_AND_MEAN
#include "../native/decky_sidebar_setup.h"
#include <bcrypt.h>
#include <fstream>
#include <iostream>
#include <vector>
static std::string digest(const std::wstring& path){
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD bytes=0,length=0;
 if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
 if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&bytes,0)<0){BCryptCloseAlgorithmProvider(algorithm,0);return {};}
 std::vector<unsigned char> object(length),buffer(65536),value(32);bool ok=BCryptCreateHash(algorithm,&hash,object.data(),length,nullptr,0,0)>=0;
 std::ifstream input(std::filesystem::path(path),std::ios::binary);ok=ok&&input.good();
 while(ok&&input){input.read(reinterpret_cast<char*>(buffer.data()),buffer.size());auto n=input.gcount();if(n>0)ok=BCryptHashData(hash,buffer.data(),static_cast<ULONG>(n),0)>=0;}
 ok=ok&&!input.bad();if(ok)ok=BCryptFinishHash(hash,value.data(),value.size(),0)>=0;
 if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);if(!ok)return {};
 const char* hex="0123456789abcdef";std::string result;for(auto b:value){result+=hex[b>>4];result+=hex[b&15];}return result;
}
int wmain(int argc,wchar_t** argv){if(argc!=3)return 2;auto result=ymcc::deckysetup::inspect(argv[1],argv[2],digest);
 result["inspectionOnly"]=true;result["loaderLaunched"]=false;result["settingsWrites"]=0;result["steamWrites"]=0;std::cout<<result.dump()<<std::endl;return 0;}
