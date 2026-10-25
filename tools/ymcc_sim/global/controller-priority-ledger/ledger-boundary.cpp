#include "../../../../native/external_controller_priority_ledger.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace ep=ymcc::externalpad;
static unsigned checks=0;
static void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
static std::wstring full(const std::wstring& name){std::wstring result(32768,L'\0');const auto n=GetFullPathNameW(name.c_str(),static_cast<DWORD>(result.size()),result.data(),nullptr);check(n>0&&n<result.size(),"full owned path");result.resize(n);return result;}
static bool within(std::wstring parent,const std::wstring& file){if(parent.back()!=L'\\')parent+=L'\\';return file.size()>parent.size()&&_wcsnicmp(parent.c_str(),file.c_str(),parent.size())==0;}
static std::string bytes(const std::wstring& path){std::ifstream file(path,std::ios::binary);check(file.good(),"read actual ledger bytes");return std::string(std::istreambuf_iterator<char>(file),{});}
static void snapshot(const std::wstring& root,const char* name,const std::string& data){std::wstring file=root+L"\\";while(*name)file+=static_cast<wchar_t>(*name++);std::ofstream out(file,std::ios::binary);check(out.good(),"write owned snapshot");out<<data;out.close();}
int wmain(int argc,wchar_t** argv){try{
 check(argc==3,"explicit owned case and ledger root required");const auto owned=full(argv[1]),root=full(argv[2]);check(within(owned,root),"ledger root remains inside owned case");
 const auto attributes=GetFileAttributesW(root.c_str());check(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"ordinary owned ledger directory");
 const auto marker=bytes(root+L"\\case.marker");check(marker=="ymcc-ledger-boundary-owned-v1","owned marker credential");const auto file=root+L"\\ledger.json";
 check(GetFileAttributesW(file.c_str())==INVALID_FILE_ATTRIBUTES,"new boundary ledger starts absent");std::string error;ep::VisibilityDebt debt;
 debt.hiddenPaths={L"HID\\VID_0DB0&PID_1902&MI_00&COL01\\8&226AD&0&0000",L"USB\\VID_0DB0&PID_1902\\5&3AF6909B&0&4"};debt.applications={L"C:\\Games\\Fixture.EXE"};debt.cloakChanged=true;
 check(ep::saveDebt(file,debt,error),"actual initial atomic save");snapshot(root,"before.json",bytes(file));
 ep::VisibilityDebt read;check(ep::loadDebt(file,read,error)&&read.hiddenPaths.size()==2&&read.cloakChanged,"actual initial load");
 debt.hiddenPaths.erase(debt.hiddenPaths.begin());debt.applications.clear();debt.cloakChanged=false;check(ep::saveDebt(file,debt,error),"actual partial return save");snapshot(root,"partial.json",bytes(file));check(ep::loadDebt(file,read,error)&&read.hiddenPaths.size()==1&&read.applications.empty()&&!read.cloakChanged,"partial debt persists exact remaining target");
 check(ep::saveDebt(file,ep::VisibilityDebt{},error),"actual empty atomic overwrite");snapshot(root,"cleared.json",bytes(file));check(ep::loadDebt(file,read,error)&&read.hiddenPaths.empty()&&read.applications.empty()&&!read.cloakChanged,"actual cleared load");
 std::cout<<"LEDGER_BOUNDARY_SUMMARY checks="<<checks<<" snapshots=3 hardwareAccess=false explicitOwnedRoot=true\n";return 0;
 }catch(const std::exception& error){std::cerr<<"LEDGER_BOUNDARY_FAIL "<<error.what()<<"\n";return 1;}}
