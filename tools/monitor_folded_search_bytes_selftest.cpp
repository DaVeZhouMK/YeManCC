// Exhaustive bytes, dynamic NUL-terminated needles and lvalue view lifetimes.
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include "../native/monitor_text_fold.h"
namespace baseline {
#include "fixtures/monitor_text_fold_search_before_20261017.h"
}
static unsigned long long checks=0;
static void check(bool ok){++checks;if(!ok)throw "folded view behavior changed";}
static std::string folded(std::string s){for(char&c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return s;}
int main(){try{
 static_assert(!std::is_constructible_v<MonitorFoldedText,std::string&&>);
 static_assert(sizeof(MonitorFoldedText)==sizeof(baseline::MonitorFoldedText));
 static_assert(sizeof(MonitorFoldedText)==24);
 std::vector<std::string> names={"","GPU","GPU Power","gpu power","gpu power limit","Physical Memory Used",std::string(128,'A'),std::string(129,'A'),std::string(1024,'B'),std::string("GPU Power\0tail",14)};
 std::vector<std::string> needles={"","gpu","gpu power","GPU Power","physical memory used",std::string("gpu\0power",9),std::string(128,'a')};
 for(unsigned b=0;b<256;++b){names.push_back(std::string(1,static_cast<char>(b)));names.push_back(std::string("GPU ")+static_cast<char>(b)+" Power");needles.push_back(std::string(1,static_cast<char>(b)));}
 std::mt19937_64 rng(20261016);for(unsigned n=0;n<2048;++n){std::string s;for(unsigned i=0,count=rng()%129;i<count;++i)s.push_back(static_cast<char>(rng()%256));names.push_back(std::move(s));}
 for(const auto&name:names){const MonitorFoldedText after{std::string_view(name)};const baseline::MonitorFoldedText before{std::string_view(name)};const auto oracle=folded(name);
  for(const auto&needle:needles){const char*p=needle.c_str();const std::string expected(p);check((after==p)==(before==p));check((after==p)==(oracle==expected));check((after!=p)==(oracle!=expected));check(after.find(p)==before.find(p)&&after.find(p)==oracle.find(p));check(after.startsWith(p)==before.startsWith(p)&&after.startsWith(p)==(oracle.rfind(expected,0)==0));}
  for(std::size_t offset:{0u,1u,2u,3u,127u,128u,129u}){check(after.asciiAt(offset)==before.asciiAt(offset));const auto a=after.after(offset);const auto b=before.after(offset);for(const auto*p:{"","gpu","power","a"})check((a==p)==(b==p));}
 }

 // Copy/assignment before and after lazy search must not access unused cache bytes.
 for(const std::string name:std::vector<std::string>{"CPU Package Power","GPU Core Voltage",std::string(128,'A'),std::string(129,'A'),""}) {
  MonitorFoldedText original{std::string_view(name)};
  for(unsigned phase=0;phase<4;++phase){
   if(phase)check(original.find("power")==folded(name).find("power"));
   MonitorFoldedText copied(original);const std::string empty;MonitorFoldedText assigned{std::string_view(empty)};assigned=original;assigned=assigned;
   for(const char* key:{"","cpu package power","power","GPU","a","voltage"}) {
    check(copied.find(key)==original.find(key));check(assigned.find(key)==original.find(key));
    check((copied==key)==(original==key));check((assigned==key)==(original==key));
   }
   const auto suffix=original.after(4);check(suffix.find("power")==folded(name.substr((std::min)(std::size_t{4},name.size()))).find("power"));
  }
 }
 // Lvalue strings are mutable; repeated searches must never cache their old bytes.
 {std::string name="CPU Package Power";MonitorFoldedText value(name);
  check(value.find("cpu")==0);check(value.find("power")==12);
  name="GPU Core Voltage";check(value.find("cpu")==std::string::npos);check(value.find("voltage")==9);
  name=std::string(1024,'A');check(value.find("aa")==0);check(value.find("b")==std::string::npos);
  name.clear();check(value.find("")==0);check(value.find("power")==std::string::npos);
 }
 std::string mutableName="CPU Power";MonitorFoldedText after(mutableName);baseline::MonitorFoldedText before(mutableName);
 for(const std::string replacement:std::vector<std::string>{"GPU Clock",std::string(256,'A'),std::string{},"Physical Memory Used"}){mutableName=replacement;for(const auto*needle:{"gpu clock","physical memory used","","a"})check((after==needle)==(before==needle));}
 for(unsigned count=0;count<=128;++count){std::array<unsigned char,128> bytes{};bytes.fill(0xff);for(unsigned i=0;i<count;++i)bytes[i]='A';if(count<128)bytes[count]=0;const auto a=monitorTextFieldView(bytes.data(),bytes.size()),b=baseline::monitorTextFieldView(bytes.data(),bytes.size());check(a==b);check(a.size()==count);}
 check(monitorTextFieldView(nullptr,128).empty());check(monitorTextFieldView("x",0).empty());
 std::cout<<"FOLD_FIRST_BYTE_BYTE_AND_LIFETIME_PASS "<<checks<<" checks; no hardware or CPU timing\n";return 0;
}catch(const char*e){std::cerr<<e<<" at "<<checks<<"\n";return 1;}}
