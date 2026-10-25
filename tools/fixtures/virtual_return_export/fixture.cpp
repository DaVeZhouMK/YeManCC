#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "log_export_evidence.h"
namespace fspath=std::filesystem;
using json=nlohmann::json;
static std::string W2U(const std::wstring& v){const auto u=fspath::path(v).u8string();return {reinterpret_cast<const char*>(u.data()),u.size()};}
static std::string bytes(const fspath::path& p){std::ifstream s(p,std::ios::binary);return {(std::istreambuf_iterator<char>(s)),{}};}
static void write(const fspath::path& p,const std::string& v){fspath::create_directories(p.parent_path());std::ofstream s(p,std::ios::binary);s<<v;if(!s)throw std::runtime_error("private seed failed");}
int wmain(int argc,wchar_t**argv){
 if(argc!=2)return 2;json checks=json::array();const auto check=[&](const char* name,bool pass){checks.push_back({{"assertion",name},{"passed",pass}});};
 const fspath::path root=argv[1];const auto dir=root/L"product-data",unrelated=root/L"title-data";
 fspath::create_directories(dir);fspath::create_directories(unrelated);
 write(unrelated/L"controller-return"/L"target-return.json","foreign-title-root");
 const auto collect=[&](){const auto dataDir=dir.wstring();std::vector<std::wstring> files;json availability={{"files",json::array()}};
 #include "actual-add-file.inc"
 #include "actual-return-files.inc"
 return json{{"files",files},{"availability",availability}};};
 auto missing=collect();check("missing historical records produce only missing availability rows",missing["files"].empty()&&missing["availability"]["files"].size()==2);
 check("missing records are not created or repaired by collection",!fspath::exists(dir/L"controller-return"));
 check("missing is ordinary not-created evidence",missing["availability"]["files"][0]["missingReason"]=="not-created-or-not-retained"&&missing["availability"]["files"][1]["missingReason"]=="not-created-or-not-retained");
 const auto target=dir/L"controller-return"/L"target-return.json",writer=dir/L"controller-return"/L"hidhide-cli-writer.json";
 const std::string duty="{\"revision\":7,\"obligations\":[{\"targetMayExist\":true}]}";
 write(target,duty);write(writer,"{broken-retained-writer-evidence");write(target.wstring()+L".owner","private-sentinel");write(writer.wstring()+L".tmp","private-uncommitted-candidate");
 const auto targetTime=fspath::last_write_time(target),writerTime=fspath::last_write_time(writer);const auto selected=collect();
 check("actual exporter selects exactly two existing historical files",selected["files"].size()==2&&selected["availability"]["files"][0]["present"]==true&&selected["availability"]["files"][1]["present"]==true);
 check("exact stable product data root wins over configurable title root",selected["files"][0].get<std::wstring>()==target.wstring()&&selected["files"][1].get<std::wstring>()==writer.wstring());
 check("existing and malformed evidence bytes remain unchanged",bytes(target)==duty&&bytes(writer)=="{broken-retained-writer-evidence");
 check("export does not update historical record timestamps",fspath::last_write_time(target)==targetTime&&fspath::last_write_time(writer)==writerTime);
 check("lock sentinel and temporary candidate remain outside selected files",selected["files"].dump().find(".owner")==std::string::npos&&selected["files"].dump().find(".tmp")==std::string::npos);
 check("private sentinel and candidate bytes are untouched",bytes(target.wstring()+L".owner")=="private-sentinel"&&bytes(writer.wstring()+L".tmp")=="private-uncommitted-candidate");
 for(const auto& value:selected["files"]){const fspath::path source=value.get<std::wstring>();const auto dest=root/L"staged"/source.filename();fspath::create_directories(dest.parent_path());fspath::copy_file(source,dest);
  const auto evidence=ymcc::logexport::stagedFileEvidence(dest,W2U(source.filename().wstring()));check("existing staged evidence inspector retains original JSON record bytes as opaque file",evidence.value("format","")=="opaque-file"&&bytes(dest)==bytes(source));}
 check("foreign title root record is unchanged",bytes(unrelated/L"controller-return"/L"target-return.json")=="foreign-title-root");
 unsigned failures=0;for(const auto& c:checks)if(!c["passed"].get<bool>())++failures;
 const json result={{"status",failures?"FAIL":"PASS"},{"checks",checks},{"checkCount",checks.size()},{"failures",failures},{"actualAddFileAndSelection",true},{"privateFileReadOnlyCollection",true},{"zipExporterOrLifecycleExecuted",false},{"deviceCalls",0}};
 std::cout<<result.dump(2)<<'\n';return failures?1:0;
}
