#define NOMINMAX
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
#include "controller_motion_bank_sessions.h"
using json=nlohmann::json;namespace cm=ymcc::controller_motion;
struct Library {
 HMODULE module=nullptr;cm::Ops ops;cm::ReadOps reads;void(*offset)(void*,float*,float*,float*)=nullptr;
 template<class T>T symbol(const char* name){auto fn=GetProcAddress(module,name);if(!fn)throw std::runtime_error(name);return reinterpret_cast<T>(fn);}
 explicit Library(const wchar_t* file){module=LoadLibraryExW(file,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw std::runtime_error("pinned GamepadMotion load");ops={symbol<decltype(ops.create)>("CreateGamepadMotion"),symbol<decltype(ops.destroy)>("DeleteGamepadMotion"),symbol<decltype(ops.setMode)>("SetCalibrationMode"),symbol<decltype(ops.setOffset)>("SetCalibrationOffset"),symbol<decltype(ops.process)>("ProcessMotion")};reads={symbol<decltype(reads.getCal)>("GetCalibratedGyro"),symbol<decltype(reads.getGravity)>("GetGravity"),symbol<decltype(reads.getPlayer)>("GetPlayerSpaceGyro"),symbol<decltype(reads.getWorld)>("GetWorldSpaceGyro")};offset=symbol<decltype(offset)>("GetCalibrationOffset");}
 ~Library(){if(module)FreeLibrary(module);}
};
static Library* lib=nullptr;static unsigned creates=0,destroys=0;
static void* create(){++creates;return lib->ops.create();}static void destroy(void* handle){++destroys;lib->ops.destroy(handle);}
static unsigned checks=0;static json failures=json::array();static void check(bool success,const std::string& what){++checks;if(!success)failures.push_back(what);}static bool closeEnough(float a,float b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=.0001f*(1+std::abs(b));}
static void compare(cm::MotionResult value,void* reference,const std::string& label){std::array<float,3> gyro{},gravity{};lib->reads.getCal(reference,&gyro[0],&gyro[1],&gyro[2]);lib->reads.getGravity(reference,&gravity[0],&gravity[1],&gravity[2]);const std::array<float,3> actualGyro{value.calibrated.x,value.calibrated.y,value.calibrated.z},actualGravity{value.gravity.x,value.gravity.y,value.gravity.z};for(unsigned i=0;i<3;++i){check(closeEnough(actualGyro[i],gyro[i]),label+" calibrated "+std::to_string(i));check(closeEnough(actualGravity[i],gravity[i]),label+" gravity "+std::to_string(i));}check(value.orientationFinite,label+" finite orientation");}
int main(int argc,char**argv){try{
 if(argc!=3)return 2;Library library(std::filesystem::path(argv[1]).wstring().c_str());lib=&library;
 auto ops=library.ops;ops.create=create;ops.destroy=destroy;const auto reads=library.reads;
 cm::BankSessions slots;std::array<void*,2> reference{},actual{};std::array<cm::Key,2> previous{};
 const std::array<cm::Calibration,2> offsets={cm::Calibration{1,-2,3,7},cm::Calibration{101,-102,103,9}};
 std::array<double,2> lastTime{};std::array<bool,2> paused{};
 for(unsigned bank=0;bank<2;++bank){reference[bank]=library.ops.create();check(reference[bank]!=nullptr,"reference native handle created");library.ops.setMode(reference[bank],0);library.ops.setOffset(reference[bank],offsets[bank].x,offsets[bank].y,offsets[bank].z,offsets[bank].weight);}
 for(unsigned step=0;step<160;++step){
  const unsigned selected=(step/8)%2;const auto epoch=100+step/8;const double now=20+step*.008;
  const cm::Key selectedKey{"fixture-physical",5,7,epoch,selected};
  check(slots.activate(selectedKey,11,now,1.5),"selected activation with background continuation");
  void* selectedHandle=nullptr;
  for(unsigned role=0;role<2;++role){const unsigned bank=role?1-selected:selected;
   const cm::Key key{"fixture-physical",5,7,epoch,bank};
   // Explicit bank pause simulates a source gap without destroying its CPU state.
   if(step>=70&&step<74&&role){slots.pauseUnselected(bank);paused[bank]=true;continue;}
   auto* session=role?slots.prepareUnselected(key,11,now,1.5):slots.operator->();
   check(session!=nullptr,"bank session available");if(!session)continue;
   check(session->select(key,ops,reads,offsets[bank],now),"source-owned real library selection");
   void* handle=session->handleFor(key);check(handle!=nullptr,"bank handle available");
   if(!actual[bank])actual[bank]=handle;else check(handle==actual[bank],"handle retained through continuous promotion");
   if(previous[bank].valid()&&!(previous[bank]==key))check(!session->handleFor(previous[bank]),"old authorization retired");
   if(!role)selectedHandle=handle;else check(slots->handleFor(selectedKey)==selectedHandle,"background did not replace selected pointer");
   const bool automatic=bank==1&&step>=20&&step<50;
   check(session->reconcileCalibrationMode(key,automatic),"per-bank calibration intent");library.ops.setMode(reference[bank],automatic?3:0);
   check(session->calibrationModeFor(key)==(automatic?3:0),"separate cached modes");
   const cm::RawSixAxes axes{float(step%9)+bank*110.f,float(step%13)-6.f,float(step%5)-2.f,.1f+bank*.3f,.2f,.9f};
   const double delta=step==0||paused[bank]?0:now-lastTime[bank];
   auto result=session->process(key,axes,now,{true,true});check(result.has_value(),"native bank result exists");
   library.ops.process(reference[bank],axes.gx,axes.gy,axes.gz,axes.ax,axes.ay,axes.az,float(delta));
   if(result){check(std::abs(result->deltaSeconds-delta)<.000001,"exact bank time origin including promotion and resume");compare(*result,reference[bank],"continuous native bank "+std::to_string(bank));float x=0,y=0;library.reads.getPlayer(reference[bank],&x,&y,1.41f);check(result->player&&closeEnough(result->player->x,x)&&closeEnough(result->player->y,y),"native player reference");library.reads.getWorld(reference[bank],&x,&y,.125f);check(result->world&&closeEnough(result->world->x,x)&&closeEnough(result->world->y,y),"native world reference");}
   std::array<float,3> actualOffset{},referenceOffset{};library.offset(handle,&actualOffset[0],&actualOffset[1],&actualOffset[2]);library.offset(reference[bank],&referenceOffset[0],&referenceOffset[1],&referenceOffset[2]);for(unsigned axis=0;axis<3;++axis)check(closeEnough(actualOffset[axis],referenceOffset[axis]),"native calibration learned independently");
   check(slots.noteProcessed(key,11,now,role!=0),"per-bank time receipt");lastTime[bank]=now;paused[bank]=false;previous[bank]=key;
  }
 }
 check(creates==2&&destroys==0&&slots.liveSlots()==2,"exactly two production processor handles");
 const cm::Key replacement{"fixture-new-physical",6,8,500,0};
 check(slots.activate(replacement,12,30,1.5),"real source replacement admitted");check(destroys==2,"source replacement retires both native handles");
 check(slots->select(replacement,ops,reads,offsets[0],30),"replacement created");auto fresh=slots->process(replacement,{1,2,3,0,0,1},30);check(fresh&&fresh->deltaSeconds==0,"replacement starts at zero delta");
 slots.reset();check(creates==3&&destroys==3&&slots.liveSlots()==0,"balanced cleanup before native library unload");for(auto handle:reference)library.ops.destroy(handle);
 const json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"actualPinnedGamepadMotionExecuted",true},{"independentRawAbiReference",true},{"continuousTicks",160},{"selectedRoleSwitches",19},{"twoBanksProcessed",true},{"newProductDlls",json::array()},{"hardwareIo",false},{"normalAppStarted",false}};
 std::ofstream(argv[2])<<report.dump(2);std::cout<<checks<<" checks / "<<failures.size()<<" failures\n";return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
