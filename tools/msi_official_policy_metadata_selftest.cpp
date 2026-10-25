#define YMCC_MSI_POLICY_METADATA_FAKE
#include "../native/msi_official_policy_metadata.h"
#include <iostream>
using namespace ymcc::msi::policyMetadata;
static unsigned checks=0;
static void check(bool ok,const char* msg){++checks;if(!ok){std::cerr<<"FAIL "<<msg<<"\n";std::exit(1);}}
static Read text(std::string s){Read r;r.state=State::Value;r.type=1;for(char c:s){r.bytes.push_back(uint8_t(c));r.bytes.push_back(0);}r.bytes.push_back(0);r.bytes.push_back(0);return r;}
struct FakeReader final:Reader {
 uint64_t tick=1000,increment=0;unsigned reads=0;bool throwReads=false;Read response=text("0");
 std::vector<std::pair<View,std::string>> requested;
 uint64_t now()override{const auto n=tick;tick+=increment;return n;}
 Read read(View v,const Field& f)override{++reads;requested.push_back({v,f.role});if(throwReads)throw 1;return response;}
};
int main(){
 for(const auto& s:{"0","1","XInput","Desktop",""})check(exactText(text(s))==s,"exact terminated REG_SZ preserved");
 for(unsigned change=0;change<6;++change){auto r=text("XInput");if(change==0)r.type=4;if(change==1)r.bytes.pop_back();if(change==2)r.bytes.back()=1;if(change==3)r.bytes[0]=0;if(change==4)r.bytes.resize(1026);if(change==5)r.state=State::Unknown;check(exactText(r).is_null(),"nonexact string no semantic claim");}
 FakeReader f;auto j=snapshot(f);check(f.reads==8&&j["readAttempts"]==8&&j["fields"].size()==8,"both views all four roles");
 check(j["fields"][0]["view"]==32&&j["fields"][4]["view"]==64,"explicit independent views no fallback");
 for(size_t i=0;i<8;++i){check(j["fields"][i]["subkey"].get<std::string>().find("WOW6432Node")==std::string::npos,"native subkey avoids double redirect");check(j["fields"][i]["writerProven"]==false&&j["fields"][i]["hardwareModeVerified"]==false,"config/cache never physical mode/writer proof");}
 check(j["fields"][2]["role"]=="ai-temporary-return-preference"&&j["fields"][3]["role"]=="official-mode-cache-not-firmware-proof","similarly named keys/cache not conflated");
 check(j["registryMutated"]==false&&j["installedComponentVersionBound"]==false&&j["loadedAiEngineIdentityVerified"]==false,"no invented mutating/version receipts");
 for(auto state:{State::KeyMissing,State::ValueMissing,State::Unknown,State::TooLarge,State::ChangedDuringRead}){FakeReader r;r.response.state=state;auto a=snapshot(r);check(a["fields"][0]["state"]==stateName(state),"unknown/missing status preserved");check(a["fields"][0]["exactRegSzText"].is_null(),"missing status not effective default value");}
 FakeReader malformed;malformed.response=text("not-a-mode");auto unknown=snapshot(malformed);
 check(unknown["fields"][0]["exactRegSzTextAvailable"]==true&&unknown["fields"][0]["valueRecognizedForRole"]==false,"exact text is not automatically a recognized auto value");
 check(unknown["fields"][1]["valueRecognizedForRole"]==false,"unknown preference kept distinct from XInput/Desktop");
 FakeReader budget;budget.increment=50;auto b=snapshot(budget,40);check(budget.reads==0&&b["readAttempts"]==0&&!b["complete"].get<bool>()&&b["fields"].size()==8,"spent soft budget marks all fields not-read");
 FakeReader throws;throws.throwReads=true;auto t=snapshot(throws);check(t["fields"][0]["state"]=="unknown-exception"&&!t["complete"].get<bool>(),"diagnostic exception contained");
 std::cout<<"MSI_POLICY_METADATA_FAKE_OK checks="<<checks<<" registry_io=none mode_writes=none\n";
}
