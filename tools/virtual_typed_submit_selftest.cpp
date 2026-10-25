#include "production.inc"
#include "typed_submit_candidate.inc"
struct AllocationCount{std::uint64_t calls=0,bytes=0;};
static thread_local bool countAllocations=false;
static thread_local AllocationCount allocationCount;
void* operator new(std::size_t n){if(countAllocations){++allocationCount.calls;allocationCount.bytes+=n;}void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
static std::uint64_t checks=0,sink=0;
static void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
static const std::string id="0123456789abcdef0123456789abcdef",hashValue(64,'a');
static std::string before(const FocusFrameContent& c,std::uint64_t n){const auto frame=focusIsolationAssembleFrame(c);return inputHostBuildSubmitFrameText(frame,id,n,n,n+100,hashValue,c.imuAdmitted?"proof-live":"safe-zero");}
static std::string after(const FocusFrameContent& c,std::uint64_t n){return focusIsolationBuildTypedSubmitText(c,id,n,n,n+100,hashValue,c.imuAdmitted?"proof-live":"safe-zero");}
static FocusFrameContent sample(std::uint64_t i,bool idle,bool gyro,int persona){
 FocusFrameContent c;
 if(!idle){c.buttons=static_cast<int>(i%65536);c.lxShort=static_cast<short>(i*37);c.lyShort=static_cast<short>(i*71);c.rxShort=static_cast<short>(i*97);c.ryShort=static_cast<short>(i*137);c.rawLtByte=static_cast<unsigned char>(i%256);c.rawRtByte=static_cast<unsigned char>(i*3);c.lt=c.rawLtByte/255.f;c.rt=c.rawRtByte/255.f;c.ltRaw=c.rawLtByte;c.rtRaw=c.rawRtByte;c.backButtons=i%4;c.screenButtons=i%17==0?1:0;}
 // Elite may use motion-to-stick upstream but does not admit raw IMU on wire.
 c.imuAdmitted=gyro&&persona!=2;
 if(c.imuAdmitted){double f=idle?.0125:static_cast<double>(static_cast<int>(i%2001)-1000)/32;c.gyroX=f;c.gyroY=-f/3;c.gyroZ=f*1.2;c.accelX=f/90;c.accelY=-f/71;c.accelZ=1+f/93;c.qw=.997;c.qx=f/700;c.qy=-f/800;c.qz=f/1200;}
 if(gyro&&persona==2&&!idle){c.rxShort=static_cast<short>(i*43);c.ryShort=static_cast<short>(i*59);}
 if(!idle&&persona!=2&&i%5==0){c.touchpads.present=true;c.touchpads.left={true,i%7==0,static_cast<short>(i%32768),static_cast<short>((i*11)%32768),static_cast<unsigned short>(i%1024)};c.touchpads.right={i%3==0,i%13==0,static_cast<short>((i*3)%32768),static_cast<short>((i*7)%32768),static_cast<unsigned short>((i*9)%1024)};}
 return c;
}
static void setPersona(int i){fixturePersona=i==0?"steamdeck":i==1?"dualsense-edge":"elite";fixtureProfile=i==0?"steam-deck-composite":i==1?"dualsense-edge":"xbox-elite-v2";}
static std::uint64_t cycles(){ULONG64 n=0;if(!QueryThreadCycleTime(GetCurrentThread(),&n))throw std::runtime_error("thread cycles");return n;}
static std::uint64_t cpu100ns(){FILETIME a,b,k,u;if(!GetThreadTimes(GetCurrentThread(),&a,&b,&k,&u))throw std::runtime_error("thread time");ULARGE_INTEGER x,y;x.LowPart=k.dwLowDateTime;x.HighPart=k.dwHighDateTime;y.LowPart=u.dwLowDateTime;y.HighPart=u.dwHighDateTime;return x.QuadPart+y.QuadPart;}
static json leg(bool candidate,const std::vector<FocusFrameContent>& data,int count,int hz=0){
 std::uint64_t sum=0;const auto c0=cycles(),cpu0=cpu100ns();const auto t0=std::chrono::steady_clock::now();
 for(int i=0;i<count;++i){if(hz)std::this_thread::sleep_until(t0+std::chrono::microseconds(1000000LL*i/hz));const auto text=candidate?after(data[i%data.size()],i+1):before(data[i%data.size()],i+1);sum+=text.size()+static_cast<unsigned char>(text[(i*7)%text.size()]);}
 const auto t1=std::chrono::steady_clock::now();const auto cpu1=cpu100ns(),c1=cycles();sink^=sum;
 return json{{"candidate",candidate},{"frames",count},{"cycles",c1-c0},{"cpu100ns",cpu1-cpu0},{"wallNs",std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()},{"checksum",sum}};
}
static int correctness(){
 std::mt19937_64 rng(0x892408a7);
 for(int p=0;p<3;p++){setPersona(p);for(bool idle:{false,true})for(bool gyro:{false,true})for(std::uint64_t i=0;i<2048;i++){const auto c=sample(i,idle,gyro,p);const auto a=before(c,i+1),b=after(c,i+1);check(a==b,"sample exact bytes");check(json::parse(a)==json::parse(b),"sample parsed content");}}
 for(int p=0;p<3;p++){setPersona(p);for(int i=0;i<2048;i++){auto c=sample(rng(),false,true,p);c.imuAdmitted=(i&1)!=0;
  const auto axis=[&]{return std::bit_cast<double>(rng());};c.gyroX=axis();c.gyroY=axis();c.gyroZ=axis();c.accelX=axis();c.accelY=axis();c.accelZ=axis();c.qw=axis();c.qx=axis();c.qy=axis();c.qz=axis();
  c.lt=std::bit_cast<float>(static_cast<std::uint32_t>(rng()));c.rt=std::bit_cast<float>(static_cast<std::uint32_t>(rng()));c.ltRaw=std::bit_cast<float>(static_cast<std::uint32_t>(rng()));c.rtRaw=std::bit_cast<float>(static_cast<std::uint32_t>(rng()));
  c.touchpads.present=i%3!=0;c.touchpads.left.down=i%2;c.touchpads.right.down=i%5==0;c.touchpads.left.click=i%7==0;c.touchpads.right.click=i%13==0;c.screenButtons=static_cast<unsigned>(rng());
  const auto n=rng();check(before(c,n)==after(c,n),"random exact scalar/optional bytes");}}
 for(const auto& weird:{std::string("quote\"slash\\tab\tline\n"),std::string("\xE5\xBE\xAE\xE6\x98\x9F"),std::string(8192,'x')}){g_inputHostRunId=weird;g_inputHostStartupNonce=weird;fixturePersona=weird;fixtureProfile=weird;check(before(sample(37,false,true,0),~0ULL)==after(sample(37,false,true,0),~0ULL),"escaped identity/max sequence");}
 // Independent field oracle, including signed zero and inactive sensors/contacts.
 setPersona(1);g_inputHostRunId="run-fixed";g_inputHostStartupNonce="nonce-fixed";auto c=sample(50,false,true,1);c.imuAdmitted=false;c.gyroX=999;c.qw=17;c.lt=-0.0f;c.touchpads.present=true;c.touchpads.left.down=false;c.touchpads.left.x=333;c.touchpads.left.pressure=77;
 const auto doc=json::parse(after(c,19));check(doc.at("imu").at("admitted")==false,"imu gate");check(doc.at("imu").at("gyroDps").at("x")==0.0,"safe gyro");check(doc.at("imu").at("quaternion").at("w")==1.0,"safe quaternion");check(std::signbit(doc.at("lt").get<double>()),"signed zero preserved");check(doc.at("touchpads").at("left").at("x")==0,"inactive contact coordinate");check(doc.at("touchpads").at("left").at("pressure")==0,"inactive pressure");check(doc.at("profileIdentity")=="dualsense-edge","profile oracle");check(doc.at("sequence")==19,"sequence oracle");
 std::cout<<json{{"pass",true},{"assertions",checks},{"productOrDeviceCalls",0},{"scope","Exact current pure constructor+wire vs typed candidate, random IEEE bits, signed-zero, inactive sensors, pads, escaped identities"}}.dump()<<'\n';return 0;
}
int main(int argc,char**argv){try{
 if(argc<2||std::string(argv[1])=="correctness")return correctness();
 const std::string mode=argv[1];const int campaign=argc>2?std::stoi(argv[2]):0;const int rounds=mode=="paced"?3:11;
 for(int p=0;p<3;p++){setPersona(p);for(bool idle:{false,true})for(bool gyro:{false,true}){
  std::vector<FocusFrameContent> data;for(int i=0;i<2048;i++)data.push_back(sample(i,idle,gyro,p));
  for(int i=0;i<4096;i++){sink^=before(data[i%data.size()],i).size();sink^=after(data[i%data.size()],i).size();}
  if(mode=="allocations"){
   for(bool candidate:{false,true}){allocationCount={};countAllocations=true;for(int i=0;i<1024;++i){const auto text=candidate?after(data[i%data.size()],i):before(data[i%data.size()],i);sink^=text.size();}countAllocations=false;std::cout<<json{{"persona",fixturePersona},{"idle",idle},{"gyroEnabled",gyro},{"candidate",candidate},{"frames",1024},{"allocationCalls",allocationCount.calls},{"allocationBytes",allocationCount.bytes}}.dump()<<std::endl;}continue;
  }

  for(int hz:(mode=="paced"?std::vector<int>{10,125}:std::vector<int>{0}))for(int round=0;round<rounds;round++){
   const bool reverse=(round+campaign)&1;json legs=json::array();const int count=hz?hz*2:4096;
   for(bool candidate:{reverse,!reverse,!reverse,reverse})legs.push_back(leg(candidate,data,count,hz));
   std::cout<<json{{"campaign",campaign},{"persona",fixturePersona},{"idle",idle},{"gyroEnabled",gyro},{"hz",hz},{"round",round},{"order",reverse?"BAAB":"ABBA"},{"legs",legs}}.dump()<<std::endl;
  }
 }}return sink==~0ULL?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
