#include <condition_variable>
#include <future>
#include <fstream>
#include <filesystem>
static void fixtureBeforeRead();static void fixtureWriteSubmitted(bool);
// ORIGINAL_PORTS
// ACTUAL_PROVIDER
static constexpr const char* kMouseProviderCurrentnessCases[] = {
 "stale-lock",
 "stale-read",
 "already-submitted-control",
 "valid-off-control",
 "explicit-off-control"
};
static std::string scenario;
static std::mutex barrierMx,foregroundMx;
static std::condition_variable barrierCv;
static bool entered=false,released=false;
static std::atomic<bool> armed{false},successorRequested{false};
static thread_local bool offCall=false;
static unsigned expiredOffSubmissions=0,allOffSubmissions=0;
static std::vector<std::string> submitted;
static ymcc::mousePolicy::Context foreground{1,1,1,true,false,false};
static void pauseAtLeaf(){std::unique_lock lock(barrierMx);entered=true;barrierCv.notify_all();if(!barrierCv.wait_for(lock,std::chrono::seconds(2),[]{return released;}))throw std::runtime_error("owned barrier timed out");}
static bool waitEntered(){std::unique_lock lock(barrierMx);return barrierCv.wait_for(lock,std::chrono::seconds(2),[]{return entered;});}
static void resumeLeaf(){{std::lock_guard lock(barrierMx);released=true;}barrierCv.notify_all();}
static void fixtureBeforeRead(){if(offCall&&scenario=="stale-read"&&armed.exchange(false))pauseAtLeaf();}
static void fixtureWriteSubmitted(bool on){
 if(!on){++allOffSubmissions;if(successorRequested.load())++expiredOffSubmissions;}
 submitted.push_back(on?"on":"off");
 if(!on&&offCall&&scenario=="already-submitted-control"&&armed.exchange(false))pauseAtLeaf();
}
int main(int argc,char**argv){if(argc!=3)return 2;scenario=argv[1];if(std::find(std::begin(kMouseProviderCurrentnessCases),std::end(kMouseProviderCurrentnessCases),scenario)==std::end(kMouseProviderCurrentnessCases))return 2;ymcc::mousePolicy::Runtime runtime;bool warm=false,barrier=false,joined=false,currentOn=false,validOff=false;std::string error;json receipt;
 try{
  runtime.start({[&]{std::lock_guard lock(foregroundMx);return runtime.current()==ymcc::mousePolicy::Mode::Off?ymcc::mousePolicy::Context{}:foreground;},
   [&](bool on,const std::function<bool()>& valid){offCall=!on;if(!on&&scenario=="stale-lock"&&armed.exchange(false)){{std::lock_guard lock(barrierMx);entered=true;}barrierCv.notify_all();}return mouseModeApplyState(on,std::string{},valid);},
   []{return mouseModeApplyState(false);},{},[](const json& r){return r.value("backend",std::string{})=="joyxoff";}},ymcc::mousePolicy::Mode::Off);
  warm=runtime.set(ymcc::mousePolicy::Mode::Auto).value("on",false)&&joyRunning;
  if(!warm)throw std::runtime_error("actual Runtime/provider failed desktop warmup");
  submitted.clear();allOffSubmissions=expiredOffSubmissions=0;
  std::unique_lock<std::mutex> held;
  if(scenario=="stale-lock")held=std::unique_lock<std::mutex>(g_mouseModeMx);
  {std::lock_guard lock(foregroundMx);foreground={2,20,200,false,false,false};foreground.game=true;foreground.identityKnown=true;foreground.foregroundImage="c:/fixture/game.exe";}
  armed=true;
  const auto requested=scenario=="explicit-off-control"?ymcc::mousePolicy::Mode::Off:ymcc::mousePolicy::Mode::Auto;
  const auto oldTicket=runtime.request(requested);
  if(scenario=="valid-off-control"||scenario=="explicit-off-control"){
   receipt=runtime.wait(requested,oldTicket);validOff=receipt.value("ok",false)&&receipt.value("stateKnown",false)&&!receipt.value("on",true)&&!joyRunning;
  }else{
   barrier=waitEntered();if(!barrier)throw std::runtime_error("requested original provider boundary did not enter");
   const auto currentTicket=runtime.request(ymcc::mousePolicy::Mode::On);successorRequested=true;
   if(held.owns_lock())held.unlock();resumeLeaf();
   receipt=runtime.wait(ymcc::mousePolicy::Mode::On,currentTicket);
   currentOn=receipt.value("ok",false)&&receipt.value("stateKnown",false)&&receipt.value("on",false)&&!receipt.value("pending",true)&&joyRunning;
  }
  runtime.stop();joined=true;
 }catch(const std::exception& e){error=e.what();resumeLeaf();runtime.stop();joined=true;}
 const bool control=scenario=="already-submitted-control"||scenario=="valid-off-control"||scenario=="explicit-off-control";
 const bool setup=warm&&error.empty()&&joined&&((scenario=="valid-off-control"||scenario=="explicit-off-control")?validOff:barrier&&currentOn);
 const bool safe=expiredOffSubmissions==0;
 const json result={{"scenario",scenario},{"status",setup&&safe?"PASS_SELECTED":setup?"REPRODUCED_EXPIRED_UNSUBMITTED_OFF":"FAIL_SETUP"},{"setupPassed",setup},{"expiredOffSubmissions",expiredOffSubmissions},{"allOffSubmissions",allOffSubmissions},{"submissionOrder",submitted},{"latestOnConfirmed",currentOn},{"validOffConfirmed",validOff},{"runtimeWorkerJoined",joined},{"receipt",receipt},{"error",error},{"control",control},{"noClaimOfOldOffAfterCompletedNewOn",true},{"realProviderOrProcessCalls",0}};
 const auto text=result.dump(2);const auto target=std::filesystem::path(argv[2]);
 const auto file=CreateFileW(target.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)return 2;DWORD wrote=0;const bool saved=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&wrote,nullptr)&&wrote==text.size();CloseHandle(file);
 if(!saved)return 2;std::cout<<text;return !setup?2:safe?0:1;
}
