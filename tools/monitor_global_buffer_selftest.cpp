// Deterministic allocations in unchanged Global reads, NOT a CPU benchmark.
#include <windows.h>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <new>
static bool tracking=false;static size_t allocations=0;
void* operator new(size_t n){if(tracking&&n>=128)++allocations;auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
#include MONITOR_GLOBAL_HEADER
int main(int argc,char** argv){assert(argc==2);const auto path=std::filesystem::absolute(argv[1]).wstring();std::ofstream f(path);f<<"[Framerate]\nLimit=120\n";for(int i=0;i<256;i++)f<<";stable irrelevant row 0123456789\n";f.close();ymcc::monitorrtss::GlobalReader r;for(int i=0;i<4;i++)r.sample(path,1700000000000LL+i*1000,1000+i*1000);tracking=true;for(int i=4;i<104;i++){auto x=r.sample(path,1700000000000LL+i*1000,1000+i*1000);assert(x.valid&&x.fps==120);}tracking=false;std::cout<<"{\"samples\":100,\"steadyLargeAllocations\":"<<allocations<<",\"actualCpuMeasurement\":false}\n";}
