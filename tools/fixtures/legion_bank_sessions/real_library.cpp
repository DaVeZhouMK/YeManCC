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
int main(int argc,char**argv){try{if(argc!=3)return 2;Library library(std::filesystem::path(argv[1]).wstring().c_str());lib=&library;auto ops=library.ops;ops.create=create;ops.destroy=destroy;const auto reads=library.reads;cm::BankSessions slots;std::array<void*,2> reference{},actual{};std::array<cm::Key,2> prior{};const std::array<cm::Calibration,2> offsets={cm::Calibration{1,-2,3,7},cm::Calibration{101,-102,103,9}};
 for(unsigned bank=0;bank<2;++bank){reference[bank]=library.ops.create();check(reference[bank]!=nullptr,"independent reference created");library.ops.setMode(reference[bank],0);library.ops.setOffset(reference[bank],offsets[bank].x,offsets[bank].y,offsets[bank].z,offsets[bank].weight);}
 for(unsigned step=0;step<120;++step){const unsigned bank=step%2;const cm::Key key{"fixture-physical",5,7,100+step,bank};const double now=20+step;check(slots.activate(key,11,now),"activate selected bank");check(slots->select(key,ops,reads,offsets[bank],now),"bind exact real library callbacks");void* handle=slots->handleFor(key);check(handle!=nullptr,"selected native handle exists");if(step<2)actual[bank]=handle;else{check(handle==actual[bank],"native handle retained across left/right toggles");check(!slots->handleFor(prior[bank]),"old output authorization cannot borrow retained handle");}
  std::array<float,3> offset{};library.offset(handle,&offset[0],&offset[1],&offset[2]);for(unsigned axis=0;axis<3;++axis){const std::array<float,3> expected{offsets[bank].x,offsets[bank].y,offsets[bank].z};check(closeEnough(offset[axis],expected[axis]),"bank offset isolated");}
  const cm::RawSixAxes axes{float(step%9)-4,float(step%13)-6,float(step%5)-2,.1f,.2f,.97f};auto first=slots->process(key,axes,now,{true,true});check(first.has_value()&&first->deltaSeconds==0,"no time spent unselected integrated");library.ops.process(reference[bank],axes.gx,axes.gy,axes.gz,axes.ax,axes.ay,axes.az,0);if(first)compare(*first,reference[bank],"first post-switch frame");
  auto next=slots->process(key,axes,now+.008,{true,true});library.ops.process(reference[bank],axes.gx,axes.gy,axes.gz,axes.ax,axes.ay,axes.az,.008f);check(next.has_value()&&std::abs(next->deltaSeconds-.008)<.000001,"active-bank delta advances independently");if(next)compare(*next,reference[bank],"active frame");check(slots->reconcileCalibrationMode(key,false),"manual intent preserved");prior[bank]=key;
 }
 check(creates==2&&destroys==0&&slots.liveSlots()==2,"two real processing handles retained");slots.reset();check(destroys==2&&slots.liveSlots()==0,"both handles retired before library unload");for(auto handle:reference)library.ops.destroy(handle);reference={};const json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"actualPinnedGamepadMotionExecuted",true},{"independentRawAbiReference",true},{"selectedBankSwitches",120},{"newProductDlls",json::array()},{"hardwareIo",false},{"normalAppStarted",false}};std::ofstream(argv[2])<<report.dump(2);std::cout<<checks<<" checks / "<<failures.size()<<" failures\n";return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
