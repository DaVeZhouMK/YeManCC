static bool fixtureRadial=false,fixtureChild=false,fixtureOverlay=false,fixtureOverlayEligible=true;
static bool fixtureValid=true,fixtureVisible=true,fixtureIconic=false,fixtureForeground=false,fixtureChildForeground=false;
static std::array<unsigned long long,8> fixtureQueries{};
static bool radialHasInputOwner(){++fixtureQueries[0];return fixtureRadial;}
static bool customSteamLibraryInputSuppressed(){++fixtureQueries[1];return fixtureChild;}
struct Overlay {bool active() const {++fixtureQueries[2];return fixtureOverlay;}};
static Overlay g_gameOverlay;
static bool gameOverlayInputEligible(){++fixtureQueries[3];return fixtureOverlayEligible;}
static BOOL fixtureIsWindow(HWND){++fixtureQueries[4];return fixtureValid?TRUE:FALSE;}
static BOOL fixtureIsWindowVisible(HWND){++fixtureQueries[5];return fixtureVisible?TRUE:FALSE;}
static BOOL fixtureIsIconic(HWND){++fixtureQueries[6];return fixtureIconic?TRUE:FALSE;}
static bool focusMainWindowIsForeground(){++fixtureQueries[7];return fixtureForeground;}
static bool customSteamLibraryChildForeground(){return fixtureChildForeground;}
static HWND g_hwnd=reinterpret_cast<HWND>(1);
static XINPUT_GAMEPAD g_curPad{};
static WORD g_prevW=0;
static bool g_gamepadAxisNeutralAdmissionRequired=false;
static std::atomic<bool> g_domainLogEnabled[2]={false,false};
static ymcc::logging::UiInputGateEvidence g_gamepadUiGateEvidence;
static json fixtureActions=json::array();
static void gamepadEmitUiAction(const char* action,bool repeat=false,ULONGLONG heldMs=0){fixtureActions.push_back({{"action",action},{"repeat",repeat},{"heldMs",heldMs}});}
// ACTUAL_DECLARATIONS
// ACTUAL_OBSERVE
// ACTUAL_EXPORT
// ACTUAL_ELIGIBLE
// ACTUAL_PROCESS
static json fixtureState(){
    return {{"queries",fixtureQueries},{"actions",fixtureActions},{"shoulderPending",g_uiShoulderPending},{"shoulderTick",g_uiShoulderPendingTick},
        {"summonHeld",g_uiSummonShoulderHeld},{"suppressUntil",g_uiShoulderSuppressUntil},{"navX",g_uiNavX},{"navY",g_uiNavY},
        {"navLast",g_uiNavLastEmitTick},{"triggerDirection",g_uiSliderTriggerDirection},{"triggerLast",g_uiSliderTriggerLastEmitTick},
        {"startPending",g_uiStartPending},{"startModifier",g_uiStartUsedAsModifier}};
}
