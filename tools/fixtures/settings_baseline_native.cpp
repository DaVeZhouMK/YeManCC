// The runner appends actual storage consumers extracted from native/main.cpp.
// Disk, mutex and host/device admission are inert fixtures, explicitly excluded
// from this test's claims. The production JSON merge/guards/writer bodies run.
#include "settings_baseline.h"
#include "controller_persona_settings.h"
#include <iostream>
#include <map>
#include <limits>
#include <stdexcept>
using json = nlohmann::json;
static const std::wstring YM_SETTINGS_FILE=L"fixture\\yeman-settings.json";
static const std::wstring YM_SETTINGS_BACKUP_FILE=YM_SETTINGS_FILE+L".bak";
static std::map<std::wstring,std::string> files;
static int writes=0, admitted=0;
static bool failMainWrite=false;
struct SettingsFileGuard {};
static std::string ymSettingsReadRaw(const std::wstring& file){return files.contains(file)?files[file]:std::string{};}
static bool sgWriteFileAtomic(const std::wstring& file,const std::string& value){if(failMainWrite&&file==YM_SETTINGS_FILE)return false;files[file]=value;++writes;return true;}
static unsigned long long GetTickCount64(){return 1234;}
static unsigned long GetCurrentProcessId(){return 42;}
static std::string W2U(const std::wstring& value){return std::string(value.begin(),value.end());}
namespace fspath{static bool exists(const std::wstring& file){return files.contains(file);}}
struct InputHostTargetWriteGuard{bool admit(const json&,const json&){++admitted;return true;}void commit(){}};
// SETTINGS_BASELINE_PRODUCTION_BODIES
static void need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void seed(const json& current){files.clear();files[YM_SETTINGS_FILE]=current.dump();writes=0;admitted=0;failMainWrite=false;}
static json disk(){return json::parse(files.at(YM_SETTINGS_FILE));}
int main(int argc,char** argv){try{
 need(argc==2,"default snapshot input missing");std::ifstream input(argv[1]);json defaults;input>>defaults;int passed=0;
 auto pass=[&](const char* name){++passed;std::cout<<"PASS "<<name<<"\n";};
 json legacy={{"schemaVersion",1},{"ui",{{"theme","user"}}},{"input",{{"revision",17}}},{"future",{{"keep",true}}}};
 seed(legacy);auto next=defaults;next["future"]=legacy["future"];next["ui"]["theme"]="user";next["input"]["revision"]=17;
 need(ymSettingsWriteDocumentUnlocked(next.dump(),nullptr,&legacy),"legacy delta failed");need(disk()["future"]["keep"]==true,"lost unknown");need(disk()["configurationBaseline"]["revision"]==1,"no durable receipt");need(files[YM_SETTINGS_BACKUP_FILE]==legacy.dump(),"no exact before image");pass("legacy delta persists baseline, settings and exact backup");
 auto baseline=defaults;auto concurrentlyChanged=defaults;concurrentlyChanged["ui"]["theme"]="concurrent-user";concurrentlyChanged["input"]["revision"]=22;concurrentlyChanged["domainLogs"]={{"virtual",true}};
 seed(concurrentlyChanged);next=baseline;next["music"]["volume"]=0.41;
 need(ymSettingsWriteDocumentUnlocked(next.dump(),nullptr,&baseline),"concurrent delta failed");need(disk()["ui"]["theme"]=="concurrent-user","overwrote sibling");need(disk()["input"]["revision"]==22,"rolled back input");need(disk()["domainLogs"]["virtual"]==true,"lost native receipt");pass("migration delta preserves concurrent user/module and native-owned changes");
 seed(defaults);next=defaults;next.erase("configurationBaseline");need(ymSettingsWriteDocumentUnlocked(next.dump()),"old module write failed");need(disk().contains("configurationBaseline"),"old module erased baseline");pass("old full-document module writes cannot erase completed migration");
 seed(defaults);need(ymSettingsWriteSection("music",{{"folder","custom"}}),"module section failed");need(disk()["configurationBaseline"]==defaults["configurationBaseline"],"section erased receipt");need(ymSettingsPatchSection("ui",{{"custom","keep"}}),"module patch failed");pass("autonomous module section and patch writes preserve baseline");
 auto jsonWhole=defaults;jsonWhole["schemaVersion"]=1.0;jsonWhole["configurationBaseline"]["revision"]=1.0;seed(jsonWhole);need(ymSettingsWriteSection("music",{{"volume",0.31}}),"whole JSON numeric versions rejected");pass("whole JSON numeric versions match frontend integer semantics");
 int negatives=0;for(const auto& stamp:json::array({nullptr,json::array(),"future",json{{"id","other"},{"revision",1},{"legacyImported",true}},json{{"id","ymcc-settings"},{"revision",2},{"legacyImported",true}},json{{"id","ymcc-settings"},{"revision","1"},{"legacyImported",true}},json{{"id","ymcc-settings"},{"revision",1},{"legacyImported",false}}})){
 auto future=defaults;future["configurationBaseline"]=stamp;seed(future);auto before=files;
 need(!ymSettingsWriteDocumentUnlocked(defaults.dump()),"future document accepted");need(!ymSettingsWriteSection("ui",json::object()),"future section accepted");need(!ymSettingsPatchSection("music",json::object()),"future patch accepted");need(files==before&&writes==0&&admitted==0,"future caused writes");negatives+=3;}
 pass("21 future/malformed-baseline native writes reject before backup or admission");
 for(const auto& schema:json::array({99,-1,1.5,"future"})){auto future=defaults;future["schemaVersion"]=schema;seed(future);auto before=files;need(!ymSettingsWriteDocumentUnlocked(defaults.dump()),"future schema doc accepted");need(!ymSettingsWriteSection("ui",json::object()),"future schema section accepted");need(!ymSettingsPatchSection("music",json::object()),"future schema patch accepted");need(files==before&&writes==0,"future schema changed bytes");}pass("12 unsupported-document-schema writes reject without changes");
 seed(defaults);auto future=defaults;future["configurationBaseline"]["revision"]=2;files[YM_SETTINGS_FILE]="{broken";files[YM_SETTINGS_BACKUP_FILE]=future.dump();writes=0;auto before=files;json empty=json::object();need(!ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&empty),"future backup overwritten");need(files==before&&writes==0,"future backup caused corrupt archive");pass("unsupported backup blocks recovery before corrupt-file archiving");
 auto malformed=defaults;malformed["rtss"]=nullptr;seed(malformed);need(ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&malformed),"repair delta failed");need(disk()["rtss"].is_object(),"shape repair not durable");pass("malformed RTSS shape repairs in the actual native delta writer");
 auto brokenBackup=defaults;brokenBackup["rtss"]=nullptr;brokenBackup["future"]={{"preserve",true}};files.clear();files[YM_SETTINGS_BACKUP_FILE]=brokenBackup.dump();writes=0;auto recovered=brokenBackup;recovered["rtss"]=defaults["rtss"];need(ymSettingsWriteDocumentUnlocked(recovered.dump(),nullptr,&brokenBackup),"backup delta repair failed");need(disk()["rtss"].is_object()&&disk()["future"]["preserve"]==true,"backup delta not durable");need(files[YM_SETTINGS_BACKUP_FILE]==brokenBackup.dump(),"backup preimage changed");pass("actual native recovered-backup delta persists repairs without replacing its preimage");
 seed(defaults);failMainWrite=true;next=defaults;next["music"]["volume"]=0.22;need(!ymSettingsWriteDocumentUnlocked(next.dump(),nullptr,&defaults),"interrupted write accepted");need(disk()==defaults,"failed write changed main");failMainWrite=false;need(ymSettingsWriteDocumentUnlocked(next.dump(),nullptr,&defaults),"retry failed");need(disk()["music"]["volume"]==0.22,"retry did not commit");pass("interrupted atomic commit retains main and later retry succeeds");

 for(const auto& broken:std::vector<std::string>{"{broken","","[]"}){files.clear();files[YM_SETTINGS_FILE]=broken;files[YM_SETTINGS_BACKUP_FILE]="{broken-backup";writes=0;admitted=0;auto original=files;need(!ymSettingsWriteDocumentUnlocked(defaults.dump()),"corrupt native document overwritten");need(!ymSettingsWriteSection("ui",json::object()),"corrupt native section overwritten");need(!ymSettingsPatchSection("music",json::object()),"corrupt native patch overwritten");need(files==original&&writes==0,"corrupt originals changed");}pass("all native writers preserve existing corrupt/empty originals without a usable backup");
 files.clear();files[YM_SETTINGS_FILE]="{broken";files[YM_SETTINGS_BACKUP_FILE]=defaults.dump();writes=0;need(ymSettingsWriteSection("music",{{"volume",0.29}}),"native compatible-backup recovery failed");need(disk()["configurationBaseline"]==defaults["configurationBaseline"],"native backup lost baseline");pass("ordinary native module save can recover a compatible backup");
 files.clear();writes=0;need(ymSettingsWriteSection("music",{{"volume",0.2}}),"first-run native write blocked");pass("genuine absent first-run files still allow native defaults");
 auto protectedConfig=defaults;protectedConfig["input"]["revision"]=27;protectedConfig["fan"]["deviceIdentity"]="owned-module";protectedConfig["future"]={{"keep",true}};protectedConfig["rtss"]["frameRates"]["ac"]["fps"]=37;seed(protectedConfig);auto recovery=ymSettingsResetApplication(defaults);need(recovery["ok"]==true,"explicit recovery failed");need(disk()["input"]["revision"]==27&&disk()["fan"]["deviceIdentity"]=="owned-module"&&disk()["future"]["keep"]==true,"reset lost protected data");need(disk()["rtss"]==defaults["rtss"],"reset ignored RTSS");need(disk()["configurationBaseline"]==defaults["configurationBaseline"],"reset omitted baseline");need(disk()["tdp"]["autoApply"]["boot"]==false,"reset activates defaults");pass("explicit archived reset establishes baseline and retains protected modules");
 std::cout<<"{\"status\":\"PASS\",\"cases\":"<<passed<<",\"negativeWrites\":"<<negatives+12<<",\"storage\":\"memory\",\"realSettingsWritten\":false,\"hardwareValidated\":false}\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
