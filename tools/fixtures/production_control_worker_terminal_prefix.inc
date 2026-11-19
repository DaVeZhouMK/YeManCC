#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <atomic>
#include <stdexcept>
static std::atomic<unsigned> wtMode{0},wtInvalidConfigReads{0};
static std::atomic<bool> wtTakeNextArmed{false};
static std::atomic<unsigned> wtTakeNextThrows{0};
static void wtBeforeTakeNext(){if(wtMode.load()==2&&wtTakeNextArmed.exchange(false)){++wtTakeNextThrows;throw std::runtime_error("fixture-take-next-terminal");}}
