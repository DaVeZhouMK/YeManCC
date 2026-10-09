// Compile the real native parser/evaluator/action against in-memory boundaries.
// No installed settings, hardware reads, process operations or device creation.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.resolve(process.env.YMCC_SHORTCUT_OUT || path.join(root, '../../Build/Validation/input-shortcut-toggles-20261006'));
fs.mkdirSync(out, { recursive: true });
const mainPath = process.env.YMCC_SHORTCUT_SOURCE || path.join(root, 'native/main.cpp');
const source = fs.readFileSync(mainPath, 'utf8').replace('#include "oem_vendor_stack.h"', () => fs.readFileSync(path.join(path.dirname(mainPath), 'oem_vendor_stack.h'), 'utf8'));
const fieldInput = process.env.YMCC_SHORTCUT_FIELD_SETTINGS ? JSON.parse(fs.readFileSync(process.env.YMCC_SHORTCUT_FIELD_SETTINGS, 'utf8').replace(/^\uFEFF/, '')).input : null;
function extract(name) {
  const marker = new RegExp(String.raw`^static [^\n]*\b` + name + String.raw`\([^;]*?\)\s*\{`, 'm');
  const definition = marker.exec(source); assert(definition, 'Missing function ' + name);
  const start = definition.index, bodyStart = start + definition[0].length - 1;
  let opened = false, depth = 0, state = 'code';
  for (let i = bodyStart; i < source.length; i++) {
    const c = source[i], next = source[i + 1];
    if (state === 'line') { if (c === '\n') state = 'code'; continue; }
    if (state === 'block') { if (c === '*' && next === '/') { state = 'code'; i++; } continue; }
    if (state === 'string' || state === 'char') { if (c === '\\') i++; else if (c === (state === 'string' ? '"' : "'")) state = 'code'; continue; }
    if (c === '/' && next === '/') { state = 'line'; i++; continue; }
    if (c === '/' && next === '*') { state = 'block'; i++; continue; }
    if (c === '"' || c === "'") { state = c === '"' ? 'string' : 'char'; continue; }
    if (c === '{') { opened = true; depth++; }
    if (c === '}' && --depth === 0 && opened) return source.slice(start, i + 1);
  }
  throw Error('Unterminated function ' + name);
}
const structStart = source.indexOf('struct NativeKeyboardShortcutRule {');
const structEnd = source.indexOf('static bool nativeKeyboardShortcutMaskBit(', structStart);
assert(structStart >= 0 && structEnd > structStart);
const hasWmi = source.includes("struct MsiWmiClickReceipt {");
const hasProducer = source.includes("struct OemProducerEdgeReceipt {");
const hasRogClick = source.includes("static void nativeRogClickDispatch(");
const hasReadSlots = source.includes("struct OemHidReadSession {");
const functions = [...(hasReadSlots ? ['oemHidReadTakeCompletion','oemHidReadFinalizeClose','oemHidReadServiceClosing','oemHidReadRequestClose','oemHidReadCanBind','oemHidReadCanBegin','legionBackStartRead','legionSBackStartRead','gpdBackStartRead','rogOemKeyStartRead','oemVendorHidShutdown','legionBackBind','legionBackSetEnabled','legionBackPump','legionSBackBind','legionSBackSetEnabled','legionSBackPump','gpdBackBind','gpdBackSetEnabled','gpdBackPump','rogOemKeyBind','rogOemKeySetEnabled','rogOemKeyPump','legionBackIsPid','legionSBackIsPid','gpdBackIsPid'] : []),'effectiveRuntimeInputSettings', 'inputShortcutRuntimeSnapshot', 'clearInputShortcutRuntime',
  'nativeKeyboardShortcutMaskBit', 'nativeKeyboardShortcutVirtualKey',
  'nativeKeyboardShortcutDuration', 'nativeKeyboardShortcutRequiredMask',
  'oemBackHookVkBit', 'oemChordEvalRules', 'oemBackHookRecompute', 'oemRawPathReset', ...(source.includes('static uint64_t oemShortcutEdgeToken(') ? ['oemShortcutEdgeToken', 'oemShortcutMessageTick', 'oemShortcutEdgeCurrent', 'oemShortcutPostPhysicalEdge'] : []),
  ...(source.includes('struct OemKeyboardConsumeState {') ? ['oemKeyboardConsumeSlot', 'oemKeyboardPhysicalMask', 'oemKeyboardConsumeSeedPassedKeys', 'oemKeyboardConsumeBusy', 'oemKeyboardConsumeEligibleMask', 'oemKeyboardConsumeCandidateBits', 'oemKeyboardConsumeReplay', 'oemKeyboardConsumeMaintain', 'oemKeyboardConsumeBuffer', 'oemKeyboardConsumeEvent'] : []),
  ...(source.includes('struct OemMouseConsumeState {') ? ['oemMouseConsumeSeedPassedKeys','oemMouseConsumeEligibleMask','oemMouseConsumeBusy','oemMouseConsumeReplay','oemMouseConsumeMaintain','oemMouseConsumeBuffer','oemMouseConsumeEvent'] : []),
  'oemBackHookProc', 'oemBackMouseHookProc', ...(source.includes('struct OemKeyboardConsumeState {') ? ['oemBackMappingReconcileOnUi', 'oemBackMappingSetEnabled'] : []),
  'nativeInputShortcutDesktopKey', 'nativeInputShortcutDesktopKeys',
  ...(hasWmi ? ['msiWmiGenerationCurrent','msiWmiShortcutAuthoritative','msiWmiWithdrawGeneration','msiWmiTryPopClick','msiWmiHandleEvent','msiWmiSubscriptionComplete','msiWmiReapThreadLocked','msiWmiStartLocked','msiWmiStart','msiWmiSetEnabled','msiWmiPump','nativeOemKeyMask','nativeKeyboardShortcutDispatch','nativeMsiWmiShortcutDrain'] : []),
  ...(hasProducer ? ['oemProducerName','oemProducerCurrent','oemProducerAuthorityMask','oemProducerTokensCurrent','oemProducerTokensFor','oemProducerBegin','oemProducerCancel','oemProducerActionMask','oemProducerPhysicalMask','oemProducerPhysicalBackState','oemProducerPublish',...(hasRogClick ? ['oemProducerPublishRogClick','nativeRogClickDispatch','rogOemKeyParse','rogOemKeyClose','rogOemKeyExpirePulse'] : []),'oemProducerTryPop','nativeOemKeyboardActionMask','nativeOemProducerDrain','oemBackMergedKeyMask','oemBackMergedDisplayState','oemLiveBackBits','oemHidClear','legionBackParse','legionSBackParse','gpdBackParse','onexBackParse','legionBackClose','legionSBackClose','gpdBackClose','onexBackClose','onexBackFinalizeClose','onexBackRetireStuck','onexBackRetireReadStuck','onexBackRequestReinit','msiDinputShortcutCancel','msiDinputShortcutBeginRead','msiDinputShortcutPublish'] : []),
  'nativeInputShortcutToggle', ...(source.includes('static bool nativeYmccShortcutAction(') ? ['nativeYmccShortcutAction', 'nativeYmccShortcutDelta', 'nativeYmccShortcutSubmit', 'nativeYmccShortcutSummon', 'nativeFrontendButtonTargetEpoch', 'nativeFrontendButtonReceipt', 'nativeFrontendButtonSendGamebar', 'nativeYmccShortcutEmit'] : []), ...(source.includes('static int nativeShortcutLegacyDefaultSlot(') ? ['nativeShortcutLegacyDefaultSlot','nativeShortcutLegacyTemplateMatches','nativeShortcutLegacyDefaultAllowed'] : []),
  ...(source.includes('static std::vector<std::wstring> oemVendorServiceNamesForFamily(') ? ['oemVendorServiceNamesForFamily','oemVendorStackFamilySupported','oemVendorStackFrontMask','oemVendorStackRuleCanTakeover'] : []), 'nativeKeyboardShortcutsRefresh', ...(source.includes('static void nativeOemKeyboardConsumePublish(') ? ['nativeOemKeyboardConsumePublish', 'nativeOemKeyboardConsumeMaintain'] : []), 'nativeKeyboardShortcutsEvaluate', ...(source.includes('static void nativeKeyboardShortcutHandleOemEdge(') ? ['nativeKeyboardShortcutHandleOemEdge'] : [])];
const familyStart = source.indexOf('enum class YmccFamily');
const familyEnum = source.slice(familyStart, source.indexOf('};',familyStart)+2);
const catalogDeclarations = source.slice(source.indexOf('enum class OemKeyForm'),source.indexOf('static OemCatalogRef oemCatalogForFamily('));
functions.push('legionHcInputReportView','oemCatalogForFamily');
const prelude = `${hasReadSlots ? "#define YMCC_HID_READ_SLOT_SOURCE\n" : ""}${hasRogClick ? "#define YMCC_ROG_CLICK_SOURCE\n" : ""}${hasProducer ? "#define YMCC_PRODUCER_EDGE_SOURCE\n" : ""}${hasWmi ? "#define YMCC_WMI_CLICK_SOURCE\n" : ""}${source.includes("static void nativeKeyboardShortcutHandleOemEdge(") ? "#define YMCC_SHORTCUT_EDGE_SOURCE\n" : ""}${source.includes("bool controllerAllowed = true") ? "#define YMCC_SHORTCUT_SOURCE_SCOPES\n" : ""}#define YMCC_SHORTCUT_FIXTURE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <algorithm>
#include <array>
#include <wbemidl.h>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "input_shortcut_toggle.h"
#include "shortcut_window_toggle.h"
#include "shortcut_frontend_button.h"
${source.match(/enum OemVkBit : uint32_t \{[\s\S]*?\};/)[0]}
${source.slice(source.indexOf("static constexpr uint64_t kOemBitM1"), source.indexOf("// ── GP-FAMILY-CAPABILITY-4 C4-D2"))}
${source.slice(source.indexOf("struct OemChordRule {"), source.indexOf("static uint32_t oemBackHookVkBit("))}
using json = nlohmann::json;
${familyEnum}
${catalogDeclarations}
static json fixtureInput;
static const json fixtureFieldInput = json::parse(R"FIELD(${JSON.stringify(fieldInput)})FIELD");
static std::vector<json> fixtureLogs;
static std::vector<std::pair<WORD, bool>> fixtureInjections;
static std::set<WORD> fixtureDesktopDown;
static std::vector<std::string> fixtureOwnActions;
static std::vector<int> fixtureDeltas;
static std::mutex g_gamepadSettingsMx;
static std::string g_mouseBackend = "native";
static std::atomic<bool> g_inputReady{true};
static HWND g_hwnd = reinterpret_cast<HWND>(1);
static std::atomic<bool> g_fullHeight{false};
static bool fixtureWindowVisible = true, fixtureWindowIconic = false, fixtureWindowZoomed = false, fixtureWindowForeground = false;
static BOOL fixtureIsWindow(HWND h) { return h != nullptr; }
static BOOL fixtureIsWindowVisible(HWND) { return fixtureWindowVisible; }
static BOOL fixtureIsIconic(HWND) { return fixtureWindowIconic; }
static BOOL fixtureIsZoomed(HWND) { return fixtureWindowZoomed; }
#define IsWindow fixtureIsWindow
#define IsWindowVisible fixtureIsWindowVisible
#define IsIconic fixtureIsIconic
#define IsZoomed fixtureIsZoomed
static bool focusMainWindowIsForeground() { return fixtureWindowForeground; }
static ymcc::FrontendButtonPulse g_frontendButtonPulse;
static std::atomic<unsigned long long> g_inputHostEpochMirror{1};
static std::string fixtureFrontendPersona = "disabled";
static std::mutex g_inputHostMx;
static HANDLE g_inputHostProcess = reinterpret_cast<HANDLE>(1);
static bool g_inputHostPrepared = true, g_inputHostNeutralized = true, g_inputHostTransportFault = false;
static std::atomic<bool> g_inputHostFaultHoldsStart{false};
static std::string g_inputHostBoundPersona = "disabled";
static const char* inputHostPersona() { return fixtureFrontendPersona.c_str(); }
static std::string inputHostBoundPersonaValue() { return g_inputHostBoundPersona; }
static bool fixtureWriterAllowed = true;
static bool sgVirtualWriterAllowedNow() { return fixtureWriterAllowed; }
static UINT fixtureSendCount = UINT_MAX;
static std::vector<std::vector<INPUT>> fixtureSendInputs;
static unsigned long long g_summonGeneration = 0;
struct FocusTargetSnapshot { bool valid = true; };
static FocusTargetSnapshot g_pendingSummonTarget;
static FocusTargetSnapshot focusCaptureForegroundTarget() { return {}; }
static void bringToFront(HWND, bool = false) { fixtureOwnActions.push_back("window.summon"); }
static json nativeSummonCandidateSnapshot(const FocusTargetSnapshot&, unsigned long long) { return {}; }
static void queueSummonGameRegistration(const FocusTargetSnapshot&, unsigned long long) {}
static void focusBeginReturnToPreviousWindow(ULONGLONG) { fixtureOwnActions.push_back("window.hideToTray"); }
static void returnToDesktop() { fixtureOwnActions.push_back("os.returnDesktop"); }
static void runKillBat() { fixtureOwnActions.push_back("game.killCurrent"); }
static void openTouchKeyboard() { fixtureOwnActions.push_back("os.openTouchKeyboard"); }
static void toggleMouseMode(const std::string&) { fixtureOwnActions.push_back("mouse.toggle"); }
static bool hardwareWriteAllowed() { return true; }
static void gamepadSerialPostEvent(const char* event, const json& payload) { fixtureOwnActions.push_back(event); fixtureDeltas.push_back(payload.value("delta", 0)); }
static void nativeApplyBrightness(int delta, bool, int) { fixtureOwnActions.push_back("display.brightnessAdjust"); fixtureDeltas.push_back(delta); }
static uint64_t fixtureOemMask = 0;
static bool fixtureAssets = true;
static bool fixtureJobAccept = true;
static int fixtureRefreshes = 0, fixtureJoyxoffCloses = 0;
static std::atomic<bool> g_exitRequested{false}, g_shortcutRecordingActive{false};
static std::mutex g_inputShortcutRuntimeMx;
static ymcc::InputShortcutRuntime g_inputShortcutRuntime;
static const std::string& gameInputOwnerSession() { static const std::string s = "memory-session"; return s; }
static json ymSettingsSection(const char* section) { if (std::string(section) != "input") throw std::runtime_error("Non-input storage access"); return fixtureInput; }
static bool inputCaptureAssetsPresent() { return fixtureAssets; }
static void inputCaptureRefreshRealStickSettings(bool) { ++fixtureRefreshes; }
static void closeJoyXoffAsync(const char*) { ++fixtureJoyxoffCloses; }
static void appendNativeLifecycleLog(const char* event, const json& payload) { auto row = payload; row["event"] = event; fixtureLogs.push_back(row); }
static void ipc_emit(const char*, const json&) {}
static void nativeKeyboardShortcutSend(WORD key, bool shift) { fixtureInjections.emplace_back(key, shift); }
${hasWmi ? "static bool msiWmiShortcutAuthoritative();\nstatic uint64_t nativeOemKeyMask(bool forActions = false);" + (hasProducer ? "\nstatic uint64_t nativeOemKeyboardActionMask();\nstatic uint64_t oemProducerAuthorityMask();" : "") : "static uint64_t nativeOemKeyMask();"}
static constexpr uint64_t kOemShortcutUnboundEpoch = UINT64_MAX;
${source.includes("struct OemKeyboardConsumeState {") ? `#define YMCC_OEM_CONSUME_SOURCE
${source.slice(source.indexOf("struct OemKeyboardConsumeState {"), source.indexOf("static unsigned oemKeyboardConsumeSlot("))}
${source.includes("struct OemMouseConsumeState {") ? "#define YMCC_OEM_MOUSE_CONSUME_SOURCE" : ""}
static std::atomic<bool> g_oemHooksWanted{true};
static void oemBackMappingReconcileOnUi();
static HHOOK g_oemBackHook = nullptr, g_oemMouseHook = nullptr;
static DWORD fixtureThreadId = 100, fixtureWindowThreadId = 100;
static DWORD fixtureCurrentThreadId() { return fixtureThreadId; }
static DWORD fixtureWindowThread(HWND, LPDWORD) { return fixtureWindowThreadId; }
#define GetCurrentThreadId fixtureCurrentThreadId
#define GetWindowThreadProcessId fixtureWindowThread
static int fixtureHookInstalls = 0, fixtureHookRemoves = 0;
static bool fixtureHookInstallOk = true;
static HHOOK fixtureSetHook(int, HOOKPROC, HINSTANCE, DWORD) { ++fixtureHookInstalls; return fixtureHookInstallOk ? reinterpret_cast<HHOOK>(1) : nullptr; }
static BOOL fixtureUnhook(HHOOK) { ++fixtureHookRemoves; return TRUE; }
#define SetWindowsHookExW fixtureSetHook
#define UnhookWindowsHookEx fixtureUnhook
static void writeLog(const char*, const std::string&) {}
static int fixtureReconcilePosts = 0;
#define WM_OEM_HOOK_RECONCILE (WM_USER + 27)
static std::vector<INPUT> fixtureReplayed;
static UINT fixtureSendLimit = UINT_MAX;
static UINT fixtureSendInput(UINT count, LPINPUT input, int) {
    fixtureSendInputs.emplace_back(input, input + count);
    const UINT accepted = std::min(count, std::min(fixtureSendLimit, fixtureSendCount));
    for (UINT i = 0; i < accepted; ++i) fixtureReplayed.push_back(input[i]);
    return accepted;
}
#define SendInput fixtureSendInput
static int fixtureSystemMetrics(int code) { return code==SM_CXVIRTUALSCREEN?1920:code==SM_CYVIRTUALSCREEN?1080:0; }
#define GetSystemMetrics fixtureSystemMetrics
` : ""}
static uint64_t gamepadShortcutRecordingMask(WORD buttons, const XINPUT_GAMEPAD& pad) { return static_cast<uint64_t>(buttons) | (pad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 1ull << 16 : 0) | (pad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 1ull << 17 : 0); }
static SHORT fixtureAsyncKeyState(WORD key) { return fixtureDesktopDown.count(key) ? static_cast<SHORT>(0x8000) : 0; }
#define GetAsyncKeyState fixtureAsyncKeyState
${hasWmi ? `
static ULONGLONG fixtureTick = 10000;
static ULONGLONG fixtureTickCount64() { return fixtureTick; }
#define GetTickCount64 fixtureTickCount64
#define WM_MSI_WMI_CLICK (WM_USER + 28)
` : ""}
#define WM_OEM_SHORTCUT_EDGE (WM_USER + 26)
static std::atomic<uint32_t> g_oemShortcutEdgeEpoch{1}, g_oemShortcutEdgePostFailures{0};
static std::atomic<uint32_t> g_gpdBackIfaceGen{1};
static std::atomic<bool> g_gpdBackIfaceReady{true}, g_oemKeyHooksActive{true};
static std::atomic<int> g_oemBackProfileId{4};
static std::atomic<uint64_t> g_oemKeyMask{0}, g_oemKeyMaskHid{0}, g_oemKeyMaskRog{0}, g_oemKeyMaskMsi{0}, g_oemKeyMaskMsiDinput{0};
${hasWmi ? source.slice(source.indexOf("static constexpr int kMsiWmiEventClaw"), source.indexOf("static void physicalReleaseWindowToggleRequest(const char* source);", source.indexOf("static constexpr int kMsiWmiEventClaw"))).replace("static std::atomic<uint64_t> g_oemKeyMaskMsi{0};", "") + `
static int fixtureWmiCreates = 0, fixtureWmiCloses = 0, fixtureWmiWakePosts = 0, fixtureLegacyClaw = 0;
static bool fixtureWmiTerminal = false, fixtureWmiCreateOk = true;
static DWORD fixtureLastWaitMs = UINT_MAX;
static DWORD WINAPI msiWmiThreadProc(LPVOID) { return 0; } // only external WMI subscription is mocked
static DWORD fixtureWmiWait(HANDLE, DWORD ms) { fixtureLastWaitMs = ms; return fixtureWmiTerminal ? WAIT_OBJECT_0 : WAIT_TIMEOUT; }
static BOOL fixtureWmiClose(HANDLE) { ++fixtureWmiCloses; return TRUE; }
static HANDLE fixtureWmiCreate(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID context, DWORD, LPDWORD) {
    ++fixtureWmiCreates;
    if (!fixtureWmiCreateOk) return nullptr;
    delete static_cast<uint64_t*>(context); fixtureWmiTerminal = false; // no real thread is started
    return reinterpret_cast<HANDLE>(0x101);
}
static int fixtureLegacyRog = 0;
static void physicalReleaseWindowToggleRequest(const char* from) { if (std::string(from)=="oem-claw") ++fixtureLegacyClaw; else ++fixtureLegacyRog; }
static void nativeMsiWmiShortcutDrain();
#define WaitForSingleObject fixtureWmiWait
#define CloseHandle fixtureWmiClose
#define CreateThread fixtureWmiCreate
` : ""}
${hasReadSlots ? source.slice(source.indexOf("struct OemHidReadSession {"),source.indexOf("static OemHidReadCompletion oemHidReadTakeCompletion(")) : ""}
${hasRogClick ? source.slice(source.indexOf("struct RogOemKeySession "),source.indexOf("static std::atomic<uint64_t> g_oemKeyMaskRog")) + source.slice(source.indexOf("static constexpr BYTE kRogKeyEventAc"),source.indexOf("static void rogOemKeyClose() {",source.indexOf("static constexpr BYTE kRogKeyEventAc"))) : ""}
${hasProducer ? source.slice(source.indexOf("enum class OemProducerId"), source.indexOf("static const char* oemProducerName(")) + `
#define WM_OEM_PRODUCER_EDGE (WM_USER + 29)
static int fixtureProducerPosts = 0, fixtureCancelCalls = 0;
static bool fixtureIoCancelOk=true; static DWORD fixtureIoCancelError=ERROR_SUCCESS;
static BOOL fixtureCancelIoEx(HANDLE, LPOVERLAPPED) { ++fixtureCancelCalls; if(!fixtureIoCancelOk) SetLastError(fixtureIoCancelError); return fixtureIoCancelOk; }
#define CancelIoEx fixtureCancelIoEx
static std::atomic<int> g_oemBackState{0}, g_msiDinputBackBits{0};
static std::atomic<uint64_t> g_oemRearMapToL5{0}, g_oemRearMapToR5{0};
static uint64_t fixtureRealOemBackMergedKeyMask();
static void nativeOemProducerDrain();
` + source.slice(source.indexOf("struct LegionBackSession "), source.indexOf("static bool legionBackIsPid(")) +
source.slice(source.indexOf("struct LegionSBackSession "), source.indexOf("static bool legionSBackIsPid(")) +
source.slice(source.indexOf("struct GpdBackSession "), source.indexOf("static bool gpdBackIsPid(")) +
source.match(/static constexpr BYTE kLegionSBackBitLeft[^\n]*\n/)[0] +
source.match(/static constexpr BYTE kLegionSBackBitRight[^\n]*\n/)[0] +
source.match(/static constexpr BYTE kGpdBackMask[^\n]*\n/)[0] +
source.slice(source.indexOf("static constexpr BYTE kOnexBackFrame"), source.indexOf("static OnexCapDescriptor onexCapDescriptorFor(")) +
source.slice(source.indexOf("struct OnexBackSession {"), source.indexOf("static OnexBackSession g_onexBack;")+"static OnexBackSession g_onexBack;".length) + `
static void onexBackFinalizeClose();
static void rogOemKeyClose();
static void legionBackClose();
static void legionSBackClose();
static void gpdBackClose();
static std::atomic<bool> g_gpdBackEnabledSnap{false};
struct FixtureMsiDinput { uint64_t actionGeneration = 0; };
static FixtureMsiDinput g_msiDinput;
` : ""}
${hasReadSlots ? `
static const GUID kYeManGuidDeviceInterfaceHid={};
${source.slice(source.indexOf('static const USHORT kLegionBackPids[]'),source.indexOf('struct LegionBackSession '))}
${source.match(/static const USHORT kLegionSPids\[\][^\n]*\n/)[0]}
${source.match(/static const USHORT kGpdBackPids\[\][^\n]*\n/)[0]}
static std::string W2U(const std::wstring&) { return "fixture-hid"; }
struct FixtureRogControl { std::wstring controlPath=L"fixture-rog";ULONG featureLen=64; };
static FixtureRogControl g_rog;
static int fixtureIoEnumCalls=0,fixtureIoOpenCalls=0,fixtureIoEventCreates=0;
static bool fixtureIoEnumAvailable=false,fixtureIoOpenOk=true,fixtureIoEventOk=true;
static USHORT fixtureIoVendor=0x17EF,fixtureIoProduct=0x6182,fixtureIoUsagePage=0xFFA0;
static HDEVINFO fixtureClassDevs(const GUID*,PCWSTR,HWND,DWORD){++fixtureIoEnumCalls;return fixtureIoEnumAvailable ? reinterpret_cast<HDEVINFO>(0x701) : INVALID_HANDLE_VALUE;}
static BOOL fixtureEnumInterfaces(HDEVINFO,PSP_DEVINFO_DATA,const GUID*,DWORD i,PSP_DEVICE_INTERFACE_DATA){return i==0;}
static BOOL fixtureInterfaceDetail(HDEVINFO,PSP_DEVICE_INTERFACE_DATA,PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail,DWORD,PDWORD bytes,PSP_DEVINFO_DATA){if(bytes)*bytes=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)+128*sizeof(wchar_t);if(!detail)return FALSE;wcscpy_s(detail->DevicePath,128,L"fixture-hid");return TRUE;}
static BOOL fixtureDestroyList(HDEVINFO){return TRUE;}
static HANDLE fixtureCreateFile(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE){++fixtureIoOpenCalls;return fixtureIoOpenOk ? reinterpret_cast<HANDLE>(0x702) : INVALID_HANDLE_VALUE;}
static HANDLE fixtureCreateEvent(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR){++fixtureIoEventCreates;return fixtureIoEventOk ? reinterpret_cast<HANDLE>(0x703) : nullptr;}
static BOOLEAN fixtureHidAttributes(HANDLE,PHIDD_ATTRIBUTES attrs){attrs->VendorID=fixtureIoVendor;attrs->ProductID=fixtureIoProduct;return TRUE;}
static BOOLEAN fixtureHidPrep(HANDLE,PHIDP_PREPARSED_DATA* prep){*prep=reinterpret_cast<PHIDP_PREPARSED_DATA>(1);return TRUE;}
static NTSTATUS fixtureHidCaps(PHIDP_PREPARSED_DATA,PHIDP_CAPS caps){caps->UsagePage=fixtureIoUsagePage;caps->Usage=1;caps->InputReportByteLength=64;return HIDP_STATUS_SUCCESS;}
static BOOLEAN fixtureHidFree(PHIDP_PREPARSED_DATA){return TRUE;}
#define SetupDiGetClassDevsW fixtureClassDevs
#define SetupDiEnumDeviceInterfaces fixtureEnumInterfaces
#define SetupDiGetDeviceInterfaceDetailW fixtureInterfaceDetail
#define SetupDiDestroyDeviceInfoList fixtureDestroyList
#define CreateFileW fixtureCreateFile
#define CreateEventW fixtureCreateEvent
#define HidD_GetAttributes fixtureHidAttributes
#define HidD_GetPreparsedData fixtureHidPrep
#define HidP_GetCaps fixtureHidCaps
#define HidD_FreePreparsedData fixtureHidFree
static DWORD fixtureIoWait=WAIT_TIMEOUT, fixtureIoResultError=ERROR_IO_INCOMPLETE, fixtureIoReadError=ERROR_IO_PENDING;
static bool fixtureIoResultOk=false, fixtureIoReadOk=false;
static int fixtureIoResultCalls=0, fixtureIoReadCalls=0;
static std::vector<HANDLE> fixtureIoClosed;
static std::vector<DWORD> fixtureIoWaitBudgets;
static std::vector<BOOL> fixtureIoResultWaitFlags;
static DWORD fixtureIoBytes=64;
static BYTE fixtureIoData[64]{};
static DWORD fixtureBoundaryWait(HANDLE h,DWORD ms) {
 if(h==reinterpret_cast<HANDLE>(0x101)) return fixtureWmiWait(h,ms);
 fixtureIoWaitBudgets.push_back(ms);return fixtureIoWait;
}
static BOOL fixtureBoundaryClose(HANDLE h) {
 if(h==reinterpret_cast<HANDLE>(0x101)) return fixtureWmiClose(h);
 fixtureIoClosed.push_back(h);return TRUE;
}
static BOOL fixtureGetOverlappedResult(HANDLE,LPOVERLAPPED,DWORD* bytes,BOOL wait) {
 ++fixtureIoResultCalls;fixtureIoResultWaitFlags.push_back(wait);*bytes=fixtureIoBytes;
 SetLastError(fixtureIoResultError);return fixtureIoResultOk;
}
static BOOL fixtureReadFile(HANDLE,LPVOID buffer,DWORD len,LPDWORD got,LPOVERLAPPED ov) {
 ++fixtureIoReadCalls;std::copy(fixtureIoData,fixtureIoData+std::min<DWORD>(len,64),static_cast<BYTE*>(buffer));
 *got=fixtureIoReadOk ? fixtureIoBytes : 0;ov->Internal=fixtureIoReadOk ? 0 : fixtureIoReadError==ERROR_IO_PENDING ? 0x103 : 0;
 SetLastError(fixtureIoReadError);return fixtureIoReadOk;
}
static BOOL fixtureResetEvent(HANDLE) { return TRUE; }
#undef WaitForSingleObject
#undef CloseHandle
#define WaitForSingleObject fixtureBoundaryWait
#define CloseHandle fixtureBoundaryClose
#define GetOverlappedResult fixtureGetOverlappedResult
#define ReadFile fixtureReadFile
#define ResetEvent fixtureResetEvent
` : ""}
static int fixtureMsiAuthority = 0;
static int msiDinputBackAuthoritativeMask() { return fixtureMsiAuthority; }
static WORD g_curW = 0; static XINPUT_GAMEPAD g_curPad{};
static uint32_t g_oemKbDown = 0; static bool g_oemMouseLeft = false, g_oemMouseX2 = false;
static std::vector<std::pair<WPARAM, LPARAM>> fixturePhysicalReceipts;
static bool fixturePostOk = true;
static LONG fixtureMessageTime = 0;
static std::vector<LONG> fixtureReceiptTimes;
static LONG fixtureGetMessageTime() { return fixtureMessageTime; }
#define GetMessageTime fixtureGetMessageTime
static BOOL fixturePostMessage(HWND, UINT message, WPARAM w, LPARAM l) {
#if defined(YMCC_OEM_CONSUME_SOURCE)
    if (message == WM_OEM_HOOK_RECONCILE) { ++fixtureReconcilePosts; return fixturePostOk; }
#endif
#if defined(YMCC_WMI_CLICK_SOURCE)
    if (message == WM_MSI_WMI_CLICK) { ++fixtureWmiWakePosts; return fixturePostOk; }
#if defined(YMCC_PRODUCER_EDGE_SOURCE)
    if (message == WM_OEM_PRODUCER_EDGE) { ++fixtureProducerPosts; return fixturePostOk; }
#endif
#endif
    if (!fixturePostOk) return FALSE;
    fixturePhysicalReceipts.emplace_back(w, l); fixtureReceiptTimes.push_back(static_cast<LONG>(GetTickCount64())); return TRUE;
}
static LRESULT fixtureCallNextHook(HHOOK, int, WPARAM, LPARAM) { return 0; }
#define PostMessageW fixturePostMessage
#define CallNextHookEx fixtureCallNextHook
// Windows API, current source identity and non-keyboard channels are fake;
// real profile/evaluator/recompute/hook/receipt/shortcut functions run below.
static std::atomic<uint32_t> g_oemKbDownRaw{0}, g_gpdChordSnapBaseKey{0}, g_gpdChordSnapPrefix{0}, g_gpdChordPubGen{0};
static std::atomic<int> g_oemBackStateKb{0}, g_gpdChordSnapPhysical{0};
${hasProducer ? "static uint64_t oemBackMergedKeyMask() { return fixtureOemMask | fixtureRealOemBackMergedKeyMask(); }" : hasWmi ? "static uint64_t oemBackMergedKeyMask() { return fixtureOemMask | (g_oemKeyMask.load() & ~static_cast<uint64_t>(fixtureMsiAuthority)) | g_oemKeyMaskMsiDinput.load(); }" : "static uint64_t nativeOemKeyMask() { return fixtureOemMask | g_oemKeyMask.load(); }"}
 enum class SerialJobKind { YmccOwn, ExternalInjection };
static std::vector<std::pair<std::function<void()>, SerialJobKind>> fixtureJobs;
static bool gamepadSerialSubmit(std::function<void()> job, SerialJobKind kind = SerialJobKind::YmccOwn) { if (!fixtureJobAccept) return false; fixtureJobs.emplace_back(std::move(job), kind); return true; }
`;
const hasDefaultOwner = source.includes('static int nativeShortcutLegacyDefaultSlot(');
const legacyStart = source.indexOf('    const bool shoulderDown = ', source.indexOf('static void gamepadEval() {'));
const legacyEnd = source.indexOf('    gamepadProcessUiInput(w, now);', legacyStart);
assert(legacyStart > 0 && legacyEnd > legacyStart);
// Copy the complete production legacy dispatch region. Only boundary calls and
// the physical snapshot are mocked; none of its guards or branches are rewritten.
const legacyFixture = hasDefaultOwner ? `
#define YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE
static bool g_summonEnabled=true,g_bDoubleMinimize=true,g_tdpShortcut=true,g_fpsShortcut=true,g_killGame=true,g_openKeyboard=true,g_returnDesktop=true,g_mouseToggle=true;
static bool gpArmed=true,gpPrevB=false,gpKbArmed=true,gpKxArmed=true,gpKyArmed=true,g_bClosePending=false;
static ULONGLONG gpHoldStart=0,gpLastBPress=0,gpKbHoldStart=0,gpKxHoldStart=0,gpKyHoldStart=0,g_bClosePendingSince=0,g_bCloseReadyAt=0,s_dpHeldStart=0,s_dpLastEmit=0,s_brightnessHeldStart=0,s_brightnessLastEmit=0;
static WORD g_prevW=0;

static void nativeAdjustTdp(int delta) { fixtureOwnActions.push_back("legacy.tdp"); fixtureDeltas.push_back(delta); }
static void nativeAdjustBrightness(int delta) { fixtureOwnActions.push_back("legacy.brightness"); fixtureDeltas.push_back(delta); }
static void fixtureLegacyReset() {
 gpArmed=gpKbArmed=gpKxArmed=gpKyArmed=true;gpPrevB=g_bClosePending=false;
 gpHoldStart=gpLastBPress=gpKbHoldStart=gpKxHoldStart=gpKyHoldStart=g_bClosePendingSince=g_bCloseReadyAt=s_dpHeldStart=s_dpLastEmit=s_brightnessHeldStart=s_brightnessLastEmit=0;g_prevW=0;
}
static void fixtureLegacyEval(WORD w, ULONGLONG now) {
 fixtureTick=now;
 const ULONGLONG HOLD_MS=500,B_DOUBLE_MS=500;
 const WORD shoulderBits=XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER;
 const bool both=(w&shoulderBits)==shoulderBits;
 const bool bPressed=(w&XINPUT_GAMEPAD_B)!=0,aPressed=(w&XINPUT_GAMEPAD_A)!=0,startHeld=(w&XINPUT_GAMEPAD_START)!=0,selectHeld=(w&XINPUT_GAMEPAD_BACK)!=0,xPressed=(w&XINPUT_GAMEPAD_X)!=0,yPressed=(w&XINPUT_GAMEPAD_Y)!=0;
 const bool dpUp=(w&XINPUT_GAMEPAD_DPAD_UP)!=0,dpDown=(w&XINPUT_GAMEPAD_DPAD_DOWN)!=0,dpLeft=(w&XINPUT_GAMEPAD_DPAD_LEFT)!=0,dpRight=(w&XINPUT_GAMEPAD_DPAD_RIGHT)!=0;
 ${source.slice(legacyStart,legacyEnd)}
 g_prevW=w;
}
` : '';
const generated = path.join(out, 'input-shortcut-real-route.fixture.cpp');
fs.writeFileSync(generated, prelude + source.slice(structStart, structEnd) + '\n' + functions.map(extract).sort((a,b) => source.indexOf(a)-source.indexOf(b)).map(text => hasProducer ? text.replace(/^static uint64_t oemBackMergedKeyMask\(/, 'static uint64_t fixtureRealOemBackMergedKeyMask(') : text).join('\n') + (hasWmi ? '\n' + source.slice(source.indexOf('class MsiWmiSink final'), source.indexOf('static DWORD WINAPI msiWmiThreadProc(')) : '') + legacyFixture + '\n#include "' + path.join(root, 'tools/input_shortcut_toggle_selftest.cpp').replaceAll('\\', '/') + '"\n');
const exe = path.join(out, 'input-shortcut-real-route.fixture.exe');
const batch = path.join(out, 'compile-native-fixture.cmd');
fs.writeFileSync(batch, '@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"' + path.join(root, 'native') + '" /I"' + path.join(root, 'deps/json') + '" "' + generated + '" /Fo"' + path.join(out, 'input-shortcut-real-route.fixture.obj') + '" /Fe"' + exe + '"' + (hasWmi ? ' /link oleaut32.lib wbemuuid.lib' : '') + '\r\nexit /b %errorlevel%\r\n');
const compile = spawnSync('cmd.exe', ['/d', '/c', batch], { encoding: 'utf8', cwd: out });
fs.writeFileSync(path.join(out, 'native-fixture-build.log'), (compile.stdout || '') + (compile.stderr || ''));
process.stdout.write(compile.stdout || ''); process.stderr.write(compile.stderr || '');
assert.equal(compile.status, 0, 'Native fixture compilation failed');
const run = spawnSync(exe, [], { encoding: 'utf8', cwd: out });
fs.writeFileSync(path.join(out, 'native-fixture-results.log'), (run.stdout || '') + (run.stderr || ''));
process.stdout.write(run.stdout || ''); process.stderr.write(run.stderr || '');
assert.equal(run.status, 0, 'Native route/model regression failed');
assert.equal((source.match(/ymcc\.effectiveGameInput\(ymSettingsSection/g) || []).length, 0, 'An effective reader bypasses the runtime switch');
assert(source.includes('"input.shortcutRuntime.clear"'));
const action = extract('nativeInputShortcutToggle');
assert(!/ymSettingsWrite|SettingsFileGuard|input\["revision"\]/.test(action), 'Toggle must never write settings or remembered state');
console.log('PASS all effective consumers use the runtime overlay; toggle action has no durable-write boundary');

const frontendRoute = extract('nativeYmccShortcutEmit');
assert(!/ymSettingsWrite|SettingsFileGuard|input\["revision"\]/.test(frontendRoute), 'Frontend buttons must not persist intent');
const frame = extract('inputHostSubmitPad');
const hostLock = frame.indexOf('std::lock_guard<std::mutex> lock(g_inputHostMx)');
const pulseSample = frame.indexOf('g_frontendButtonPulse.sample');
assert(hostLock >= 0 && pulseSample > hostLock, 'Guide sample must be generation-bound under live target lock');
assert(frame.includes('hostFrameReady && !semanticNeutral && physicalSourceAdmitted'), 'Guide must respect frame/source admission');
assert(frame.includes('g_frontendButtonPulse.sample(GetTickCount64(), g_inputHostEpoch,'), 'Guide must use locked epoch, not a pre-lock mirror');
assert(!/SetTimer|std::thread|sleep_for/.test(fs.readFileSync(path.join(root,'native/shortcut_frontend_button.h'),'utf8')), 'Frontend pulse cannot add a timer or polling thread');
console.log('PASS Guide integration shares existing frame/target lock, source/neutral gates and no new timer or durable writes');
