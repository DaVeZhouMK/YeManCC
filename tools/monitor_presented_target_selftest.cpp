// Compile the actual parser and ordinary worker's hardware-admission branch.
struct TargetGame {std::uint64_t pid=2077,processCreated=99;std::wstring name=L"cyberpunk2077";};
static std::string W2U(const std::wstring& s){return {s.begin(),s.end()};}
static void targetOrdinary(ymcc::monitorframe::Output& frameOutput,const NativeMonitorHw& hw,const TargetGame& game,std::uint64_t nowMs){
 bool hwReadable=true,hwFresh=true;
 const bool hwHealthy=monitorHwHealthyVerdict(hwReadable,hw.sharedOk,hw.producerPollTime,hwFresh);
 // TARGET_WORKER_HARDWARE_BRANCH
}
template<class T>static void targetPut(std::vector<BYTE>& b,size_t at,T value){memcpy(b.data()+at,&value,sizeof(value));}
static std::vector<BYTE> targetBuffer(const char* caption,unsigned sensorSize=268){
 constexpr unsigned count=12,slot=10,offset=48,rowSize=316;
 const unsigned rows=offset+count*sensorSize;std::vector<BYTE>b(rows+3*rowSize);
 targetPut(b,0,0x53695748u);targetPut(b,4,2u);targetPut(b,8,1u);targetPut(b,12,1700000000ULL);
 targetPut(b,20,offset);targetPut(b,24,sensorSize);targetPut(b,28,count);targetPut(b,32,rows);targetPut(b,36,rowSize);targetPut(b,40,3u);targetPut(b,44,3000u);
 memcpy(b.data()+offset+slot*sensorSize+8,"PresentMon",10);
 if(sensorSize>=264)memcpy(b.data()+offset+slot*sensorSize+136,caption,(std::min)(strlen(caption),size_t(127)));
 const char* labels[]={"Framerate Presented (avg)","Framerate Presented (1% low)","Frame Time Presented (avg)"};
 for(unsigned i=0;i<3;++i){const auto at=rows+i*rowSize;targetPut(b,at+4,slot);memcpy(b.data()+at+12,labels[i],strlen(labels[i]));memcpy(b.data()+at+268,i==2?"ms":"FPS",i==2?2:3);targetPut(b,at+284,i==0?120.:i==1?80.:8.3);}
 return b;
}
int main(int argc,char**argv){
 if(argc!=2)return 2;constexpr std::uint64_t now=1700000000000ULL;TargetGame game;json cases=json::array();
 auto check=[&](const std::string& name,bool ok){cases.push_back({{"name",name},{"ok",ok}});if(!ok)std::cerr<<name<<'\n';};
 auto parse=[&](std::vector<BYTE>&b,ymcc::monitorparse::Reuse<NativeMonitorHw>* reuse=nullptr){NativeMonitorHw hw;bool unstable=false;const bool ok=monitorParseHWiNFOView(b.data(),b.size(),hw,unstable,false,reuse);if(!ok||unstable)throw std::runtime_error("valid target fixture rejected");return hw;};
 auto correct=targetBuffer("PresentMon [Cyberpunk2077.exe]");auto hw=parse(correct);
 check("actual parser retains existing PresentMon process metadata",std::string(hw.fpsTargetCaption.data())=="PresentMon [Cyberpunk2077.exe]");
 ymcc::monitorframe::Output first;first.bind(game.pid,game.processCreated,now-10000);targetOrdinary(first,hw,game,now);
 auto p=first.packet(now);check("correct HW target retains primary FPS and existing PID/birth",p.is_object()&&p["fps"]==120&&p["fpsSource"]=="hwinfo-presented"&&p["pid"]==2077&&p["processCreated"]=="99");
 for(const char* other:{"YeManCC.exe","msedgewebview2.exe","steamwebhelper.exe","GameBar.exe","XboxPcApp.exe","dwm.exe","Among Us.exe"}){
  const auto caption=std::string("PresentMon [")+other+"]";auto b=targetBuffer(caption.c_str());auto wrong=parse(b);ymcc::monitorframe::Output o;o.bind(game.pid,game.processCreated,now-10000);targetOrdinary(o,wrong,game,now);
  check(std::string("known foreign process is not relabeled as game: ")+other,o.packet(now).is_null());
  o.observeRtss({true,8.3,100,now});const auto rescue=o.packet(now);
  check(std::string("PID-bound RTSS can rescue foreign HW target: ")+other,rescue.is_object()&&rescue["fpsSource"]=="rtss"&&rescue["pid"]==2077);
 }
 for(const char* caption:{"", "PresentMon", "Custom caption", "PresentMon [CYBERPUNK2077.EXE]"}){
  auto b=targetBuffer(caption);auto legacy=parse(b);ymcc::monitorframe::Output o;o.bind(game.pid,game.processCreated,now-10000);targetOrdinary(o,legacy,game,now);auto q=o.packet(now);
  check(std::string("unknown/custom or equivalent case caption remains compatible: ")+caption,q.is_object()&&q["fpsSource"]=="hwinfo-presented");
 }
 auto shortTable=targetBuffer("",136);auto shortHw=parse(shortTable);check("legacy short sensor record stays compatible without a caption read",shortHw.fpsTargetCaption[0]==0&&ymcc::monitorpresented::targetMatches(shortHw.fpsTargetCaption.data(),game.name));
 ymcc::monitorparse::Reuse<NativeMonitorHw> reuse;reuse.begin(1);reuse.bindSource(0);auto reused=parse(correct,&reuse);auto repeated=parse(correct,&reuse);
 check("unchanged parse reuse retains target metadata",reused.fpsTargetCaption==repeated.fpsTargetCaption);
 const unsigned captionOffset=48+10*268+136;memset(correct.data()+captionOffset,0,128);memcpy(correct.data()+captionOffset,"PresentMon [steamwebhelper.exe]",sizeof("PresentMon [steamwebhelper.exe]")-1);
 auto switched=parse(correct,&reuse);check("caption change under the same poll invalidates parse reuse",std::string(switched.fpsTargetCaption.data())=="PresentMon [steamwebhelper.exe]");
 check("known changed target fails the existing game's binding",!ymcc::monitorpresented::targetMatches(switched.fpsTargetCaption.data(),game.name));
 auto raced=targetBuffer("PresentMon [Cyberpunk2077.exe]");targetRaceView=&raced;targetRaceOffset=captionOffset;NativeMonitorHw racedHw;bool unstable=false;
 const bool accepted=monitorParseHWiNFOView(raced.data(),raced.size(),racedHw,unstable);targetRaceView=nullptr;
 check("target change during one read is unstable rather than misbound",!accepted&&unstable);
 bool ok=true;for(const auto& c:cases)ok=ok&&c["ok"].get<bool>();std::ofstream(argv[1])<<json({{"ok",ok},{"checks",cases.size()},{"cases",cases},{"actualParser",true},{"actualWorkerHardwareBranch",true},{"extraProcessQueries",0},{"physicalSteamXboxDualDisplayValidation",false}}).dump(2);return ok?0:1;
}
