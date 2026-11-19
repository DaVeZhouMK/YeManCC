// Separately compiled without /GL or LTCG. The producer caller must expose
// every byte of its completed owned value before destruction in both arms.
static const void* volatile observedCapture=nullptr;
extern "C" __declspec(noinline) void observeWholeCapture(const void* value) noexcept {observedCapture=value;}
