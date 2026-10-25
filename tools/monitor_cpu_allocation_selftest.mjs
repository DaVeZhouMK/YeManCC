// Allocation counts and complete native-parser equivalence. Synthetic buffers only.
// No CPU timing/usage benchmark, live mapping, sensor read, app or game interaction.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const root=process.cwd(),task=path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-cpu-optimization-20261011'),out=path.join(task,'tests');
const old=fs.readFileSync(path.join(task,'before/native/main.cpp'),'utf8'),current=fs.readFileSync('native/main.cpp','utf8');
function cut(source,start,end){const a=source.indexOf(start),b=source.indexOf(end,a);if(a<0||b<a)throw Error('source anchor missing: '+start);return source.slice(a,b);}
const at=current.indexOf('struct NativeMonitorHw {'),structure=current.slice(at,current.indexOf('\n};',at)+3);
const beforeParser=cut(old,'static bool monitorParseHWiNFOView(','static bool monitorReadHWiNFO('),afterParser=cut(current,'static bool monitorParseHWiNFOView(','static bool monitorReadHWiNFO(');
const beforeField=cut(old,'static std::string monitorField(','// ── GP-926R17d');
const fields=[...structure.matchAll(/^\s*(?:double|bool|unsigned long long)\s+(\w+)\s*=.*;/gm)].map(match=>match[1]);if(fields.length<20)throw Error('incomplete NativeMonitorHw serialization');
const types=structure.replace('struct NativeMonitorHw {','struct NativeMonitorHw {');
const prelude=`#include <windows.h>\n#include <cmath>\n#include <cstdint>\n#include <cstdio>\n#include <cstdlib>\n#include <cstring>\n#include <fstream>\n#include <iostream>\n#include <new>\n#include <string>\n#include <string_view>\n#include <vector>\n#include <type_traits>\n#include <limits>\n#include "${root.replaceAll('\\','/')}/deps/json/json.hpp"\n#include "${root.replaceAll('\\','/')}/native/monitor_freshness.h"\nusing json=nlohmann::json;\nstatic bool tracking=false;static size_t allocations=0;\nvoid* operator new(size_t n){if(tracking)++allocations;auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}\nvoid* operator new[](size_t n){return ::operator new(n);}\nvoid operator delete(void* p) noexcept{std::free(p);}\nvoid operator delete[](void* p) noexcept{std::free(p);}\nvoid operator delete(void* p,size_t) noexcept{std::free(p);}\nvoid operator delete[](void* p,size_t) noexcept{std::free(p);}\nstatic BYTE* mutate=nullptr;static int offset=-1;\nstatic void monitorTestBarrierPause(int point){if(point==3&&mutate&&offset>=0)mutate[offset]^=1;}\n${types}\nstatic json serialize(const NativeMonitorHw& value){return json{${fields.map(field=>'{' +JSON.stringify(field)+',value.'+field+'}').join(',')}};}\n`;
const bodies=`namespace baseline {\n#include "${path.join(task,'before/native/monitor_text_fold.h').replaceAll('\\','/')}"\n${beforeField}\n${beforeParser}\n}\nnamespace candidate {\n#include "${root.replaceAll('\\','/')}/native/monitor_text_fold.h"\n${afterParser}\n}\n`;
const cases=String.raw`
struct Row {std::string label,unit;double value=0,maximum=0,average=0;};
template<class T> static void put(std::vector<BYTE>& data,size_t at,T value){std::memcpy(data.data()+at,&value,sizeof(value));}
static std::vector<BYTE> buffer(const std::vector<Row>& rows,uint32_t version=2){
 std::vector<BYTE> data(64+316*rows.size());
 put(data,0,uint32_t{0x53695748u});put(data,4,version);put(data,8,uint32_t{1});put(data,12,static_cast<unsigned long long>(1789905313ULL));
 put(data,20,uint32_t{48});put(data,24,uint32_t{268});put(data,28,uint32_t{0});put(data,32,uint32_t{64});put(data,36,uint32_t{316});put(data,40,static_cast<uint32_t>(rows.size()));put(data,44,uint32_t{1000});
 for(size_t i=0;i<rows.size();++i){size_t base=64+316*i;const auto& r=rows[i];std::memcpy(data.data()+base+12,r.label.data(),(std::min)(size_t{128},r.label.size()));std::memcpy(data.data()+base+268,r.unit.data(),(std::min)(size_t{16},r.unit.size()));put(data,base+284,r.value);put(data,base+300,r.maximum);put(data,base+308,r.average);}
 return data;
}
static int comparisons=0;
static void compare(std::vector<BYTE> original,size_t mapped=SIZE_MAX,int mutation=-1){
 auto a=original,b=original;NativeMonitorHw left,right;bool unstableLeft=false,unstableRight=false;
 offset=mutation;mutate=a.data();bool okLeft=baseline::monitorParseHWiNFOView(a.data(),mapped==SIZE_MAX?a.size():mapped,left,unstableLeft);
 mutate=b.data();bool okRight=candidate::monitorParseHWiNFOView(b.data(),mapped==SIZE_MAX?b.size():mapped,right,unstableRight);mutate=nullptr;offset=-1;
 if(okLeft!=okRight||unstableLeft!=unstableRight||serialize(left)!=serialize(right)){std::cerr<<"Parser mismatch #"<<comparisons<<"\n"<<serialize(left)<<"\n"<<serialize(right)<<"\n";std::exit(1);}++comparisons;
}
static void expect(bool yes,const char* why){if(!yes){std::cerr<<why<<"\n";std::exit(1);}}
int main(int argc,char** argv){
 expect(argc==2,"report path required");
 static_assert(!std::is_constructible_v<candidate::MonitorFoldedText,std::string&&>);
 const char* needles[]={"", "cpu", "package", "power", "thermal", "discharge", "rate", "w", "mhz", "unknown"};
 const std::vector<Row> samples={
 {"CPU Package Power","W",15.5,40,20},{"CPU Package","W",18,40,20},{"CPU (Tctl/Tdie)","°C",65,80,70},
 {"CPU Package","°C",50,80,60},{"Core Clocks","MHz",3500,4100,3700},{"Core Thermal Throttling","Yes/No",0,1,0},
 {"Thermal Throttling (HTC)","Yes/No",0,0,0},{"Virtual Memory Committed","MB",8000,10000,9000},
 {"Virtual Memory Load","%",35,50,40},{"GPU Power","W",120,160,140},{"GPU Clock","MHz",2400,2600,2500},
 {"GPU Core Load","%",98,100,80},{"Framerate","FPS",60,100,75},{"Framerate","FPS",0,0,0},
 {"Discharge Rate","W",20,25,20},{"Charge/Discharge Rate","mW",-20000,22000,-15000},
 {"Estimated Remaining Time","min",120,240,180},{"Estimated Remaining Time","h",2,4,3},
 {"CPU Very Long Unknown Sensor Label 芯片温度","W",5,10,8},{std::string(128,'X'),std::string(16,'W'),1,2,3},
 {std::string("CPU Package Power\0ignored",25),"W",0,0,0},{"", "",0,0,0},
 {"GPU Temperature","°C",std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),0}};
 for(const auto& sample:samples)for(const auto* needle:needles){
   const auto oldField=sample.label.substr(0,sample.label.find('\0'));candidate::MonitorFoldedText borrowed{std::string_view(oldField)};baseline::MonitorFoldedText oracle(oldField);
   expect(borrowed.find(needle)==oracle.find(needle)&&((borrowed==needle)==(oracle==needle)),"ASCII fold/substring semantics changed");
 }
 std::string mutableName="CPU Power";candidate::MonitorFoldedText mutableView(mutableName);mutableName="GPU Clock Long Replacement";expect(mutableView.find("gpu")==0,"old mutable lvalue-string behavior changed");
 expect(candidate::monitorTextFieldView(nullptr,16).empty()&&candidate::monitorTextFieldView("abc",0).empty(),"zero/null field view boundary");
 compare(buffer(samples));for(const auto& sample:samples)compare(buffer({sample}));
 uint32_t seed=0x12a7c91;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 for(int k=0;k<1200;++k){std::vector<Row> rows;for(unsigned i=0,count=1+random()%40;i<count;++i){auto r=samples[random()%samples.size()];r.value=static_cast<int>(random()%200000)-50000;r.maximum=random()%200000;r.average=random()%200000;if(random()%7==0)r.value=std::numeric_limits<double>::quiet_NaN();if(random()%3==0)for(auto& c:r.label)if(c>='a'&&c<='z')c-='a'-'A';rows.push_back(std::move(r));}compare(buffer(rows,k%2?1:2));}
 auto normal=buffer(samples);for(int n=0;n<48;++n)compare(normal,n);
 for(const auto& pair:std::vector<std::pair<size_t,uint32_t>>{{0,0},{4,9},{20,0},{20,0xffffffff},{32,0xffffffff},{36,100},{36,16385},{40,0},{40,100001},{40,100000}}){auto bad=normal;put(bad,pair.first,pair.second);compare(bad);}
 for(int off:{0,4,8,12,20,24,28,32,36,40})compare(normal,SIZE_MAX,off);
 // Same layout/poll time, different name/unit bytes must remain freshly classified.
 auto renamed=normal;std::memset(renamed.data()+64+12,0,128);std::memcpy(renamed.data()+64+12,"GPU Power",9);compare(renamed);
 std::vector<Row> many(256,{"Long Unknown Sensor Name For Allocation Validation","W",1,2,3});auto allocationRows=buffer(many);NativeMonitorHw warmLeft,warmRight;bool unstable=false;
 baseline::monitorParseHWiNFOView(allocationRows.data(),allocationRows.size(),warmLeft,unstable);candidate::monitorParseHWiNFOView(allocationRows.data(),allocationRows.size(),warmRight,unstable);
 NativeMonitorHw left,right;allocations=0;tracking=true;bool okBefore=baseline::monitorParseHWiNFOView(allocationRows.data(),allocationRows.size(),left,unstable);tracking=false;size_t before=allocations;
 allocations=0;tracking=true;bool okAfter=candidate::monitorParseHWiNFOView(allocationRows.data(),allocationRows.size(),right,unstable);tracking=false;size_t after=allocations;
 expect(okBefore&&okAfter&&serialize(left)==serialize(right),"allocation fixture changed result");expect(before>=many.size()&&after==0,"expected warm parser string heap allocations removed");
 json result={{"ok",true},{"completeParserComparisons",comparisons},{"parserMemberCount",serialize(left).size()},{"syntheticLongLabelRows",many.size()},{"warmParserHeapAllocations",{{"before",before},{"after",after}}},{"timingOrCpuUsageMeasured",false},{"hardwareIo",false},{"scope","unchanged complete native parser from before/current source; alias/unit/NaN/bounds/freshness-layout/reclassification and deterministic allocation counters"}};
 std::ofstream(argv[1])<<result.dump(2);std::cout<<result.dump(2)<<"\n";
}
`;
fs.writeFileSync(path.join(out,'native-text-equivalence.cpp'),prelude+bodies+cases);
fs.writeFileSync(path.join(out,'native-text-source-pin.json'),JSON.stringify({fields,beforeParserSha256:crypto.createHash('sha256').update(beforeParser).digest('hex'),afterParserSha256:crypto.createHash('sha256').update(afterParser).digest('hex'),beforeHeaderSha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(task,'before/native/monitor_text_fold.h'))).digest('hex'),afterHeaderSha256:crypto.createHash('sha256').update(fs.readFileSync('native/monitor_text_fold.h')).digest('hex')},null,2));
fs.writeFileSync(path.join(out,'build-text-equivalence.cmd'),`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 /DUNICODE /D_UNICODE "${out}/native-text-equivalence.cpp" /Fo"${out}/native-text-equivalence.obj" /Fe"${out}/native-text-equivalence.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/native-text-equivalence.exe" "${out}/native-text-result.json"\r\n`);
console.log('Prepared complete-source native parser equivalence and allocation test.');
