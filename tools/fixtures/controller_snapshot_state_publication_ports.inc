static std::vector<std::string> reads;
static std::uint64_t oem=0;
static int driverState=0,driverNotice=0;
static std::string driverResult="ready",driverDirectory="C:/own/driver",driverSession="session";
struct DriverPort {
 int state(){if(observing)reads.emplace_back("driver-state");return driverState;}
 int hidHideNotice(){if(observing)reads.emplace_back("driver-notice");return driverNotice;}
 std::string result(){if(observing)reads.emplace_back("driver-result");return driverResult;}
 std::wstring redistDirectory(){if(observing)reads.emplace_back("driver-directory");return std::wstring(driverDirectory.begin(),driverDirectory.end());}
 std::string sessionToken(){if(observing)reads.emplace_back("driver-session");return driverSession;}
};
static DriverPort g_gamepadDriverSetup;
static std::atomic<std::uint64_t> g_externalPriorityRevision{1};
static std::atomic<bool> g_backendInstallPrompt{false};
static unsigned g_xinputConnectedMask=1,g_xinputPrimarySlot=0;
static XINPUT_GAMEPAD g_xinputPads[4]{},g_publishedPad{};
static bool g_xinputSpecialButton=false,g_publishedPadConnected=false,g_gamepadStatePublished=false;
static constexpr bool kXInputPermissiveAnyConnectedInput=true;
static bool rumble=true,lighting=false;
static struct {YmccFamily family=YmccFamily::Unknown;int deviceClass=0;} g_machineIdentity;
static bool ymccFamilySupportsRumble(YmccFamily){if(observing)reads.emplace_back("rumble");return rumble;}
static bool ymccDeviceClassSupportsLighting(int){if(observing)reads.emplace_back("lighting");return lighting;}
static std::uint64_t nativeOemKeyMask(bool){if(observing)reads.emplace_back("oem");return oem;}
static json gamepadOemPressedKeys(std::uint64_t value){if(observing)reads.emplace_back("keys");json j=json::array();for(unsigned i=0;i<8;++i)if(value&(1ull<<i))j.push_back("key-"+std::to_string(i));return j;}
static json externalPriority=json::object();
static json externalControllerPrioritySnapshot(){if(observing)reads.emplace_back("external");return externalPriority;}
@@YMCC_W2U@@
static void portReset(int targets){reset(targets,false);reads.clear();g_gamepadStatePublished=false;g_publishedPadConnected=false;g_publishedPad={};oem=0;driverState=driverNotice=0;driverResult="ready";driverDirectory="C:/own/driver";driverSession="session";g_externalPriorityRevision=1;g_backendInstallPrompt=false;g_xinputConnectedMask=1;g_xinputPrimarySlot=0;g_xinputSpecialButton=false;rumble=true;lighting=false;externalPriority=json::object();for(auto& pad:g_xinputPads)pad={};}
