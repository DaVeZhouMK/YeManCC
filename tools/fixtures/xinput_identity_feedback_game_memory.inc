// Environment observation leaves only. Production command gates and Dispatch bodies remain verbatim.
// This fixture does not bind a game callback, populate game tickets or authorize a forwarding protocol.
static std::atomic<unsigned long long> g_inputHostGameMotorBindingGeneration{0},g_inputHostVirtualIntentRevision{1},g_msiMainSourceRevision{0};
static std::atomic<bool> g_inputReady{false},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0};
static unsigned long long fixtureInputHostEpoch=1;
static unsigned long long inputHostEpochToken() noexcept{return fixtureInputHostEpoch;}
