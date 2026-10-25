// Explicit memory-only seams. No Windows headers, registry API, sensor API or BUS.
static std::map<std::wstring,std::string> mockRegistry;
static unsigned mockRegistryReads=0;
static constexpr int HKEY_LOCAL_MACHINE=1;
static std::string sgRegSZ(int,const wchar_t*,const wchar_t* value) {
    ++mockRegistryReads;
    return mockRegistry[value];
}
static std::mutex g_sensorMetaMx;
struct MockBindingSink { std::atomic<UINT64> activeBindingGeneration{73}; };
static MockBindingSink mockSink;
static MockBindingSink* g_inputCaptureSink=&mockSink;
struct MockCaptureState { bool enabled=true; };
static MockCaptureState g_realStickTest;

struct MockRogState {bool identityMatched=false;};static MockRogState g_rog;
static unsigned mockProcessCalls=0;
static std::array<float,6> mockProcessAxes{};
static void recordMotionEffect(void*,float gx,float gy,float gz,float ax,float ay,float az,float) {
    ++mockProcessCalls;mockProcessAxes={gx,gy,gz,ax,ay,az};
}
static int mockGamepadHandle=0;
static void* g_gm=&mockGamepadHandle;
static auto g_gmProcess=&recordMotionEffect;
static bool g_gmGetCal=true;
static bool g_gmProcessedThisTick=false;

// The lease reader is a memory seam for the capability log caller only. It
// neither decodes reports nor validates the real power/raw/tick lease context.
struct MockControllerMotionObservation {};
static std::optional<MockControllerMotionObservation> mockControllerObservation;
static unsigned mockControllerSnapshotReads=0;
static std::optional<MockControllerMotionObservation> legionMotionSnapshot(UINT64) {
    ++mockControllerSnapshotReads;return mockControllerObservation;
}

static uint64_t mockCapabilityPower=1;
static uint64_t currentPowerGeneration() {return mockCapabilityPower;}
static std::vector<json> mockCapabilityEvents;
static void inputCaptureAppend(const json& event) {mockCapabilityEvents.push_back(event);}
