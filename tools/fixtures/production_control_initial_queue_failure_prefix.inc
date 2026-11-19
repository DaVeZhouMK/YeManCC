#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <new>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
static thread_local bool qaArmAfterLaunch=false,qaArmed=false,qaInEnqueue=false;
static thread_local unsigned qaQueueEntries=0,qaDeniedAllocations=0;
static thread_local std::size_t qaDeniedBytes=0;
void* operator new(std::size_t count){
 if(qaArmed&&qaInEnqueue){qaArmed=false;++qaDeniedAllocations;qaDeniedBytes=count;throw std::bad_alloc();}
 if(void* p=std::malloc(count?count:1))return p;
 throw std::bad_alloc();
}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static std::uintptr_t qaAfterLaunch(std::uintptr_t handle)noexcept{if(handle&&qaArmAfterLaunch){qaArmAfterLaunch=false;qaArmed=true;}return handle;}
struct QaEnqueueObservation{bool prior;QaEnqueueObservation()noexcept:prior(qaInEnqueue){qaInEnqueue=true;++qaQueueEntries;}~QaEnqueueObservation(){qaInEnqueue=prior;}};
