#pragma once
// Isolated test executables only. Never include in production.
#include <atomic>
#include <cstdlib>
#include <new>
namespace ymcc::testing::allocationFault {
inline std::atomic<int> countdown{-1};
inline void failAfter(int successfulAllocations)noexcept{countdown.store(successfulAllocations,std::memory_order_relaxed);}
inline bool failThis()noexcept{
    int remaining=countdown.load(std::memory_order_relaxed);
    while(remaining>=0){
        if(countdown.compare_exchange_weak(remaining,remaining==0?-1:remaining-1,std::memory_order_relaxed))return remaining==0;
    }
    return false;
}
}
void* operator new(std::size_t n){if(ymcc::testing::allocationFault::failThis())throw std::bad_alloc();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
