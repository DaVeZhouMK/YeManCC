# T22-P03/P04 raw static evidence

capturedUtc: 2026-09-04T15:05:26.9686838Z
cwd: G:\YeManCC-Work
runtimeOperation: false

## P03 — InputHost metadata / persona boundary
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\YeManInputHost.csproj
exitCode: 0
stdout:
`
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <AssemblyName>YeManInputHost</AssemblyName>
    <RootNamespace>YeManInputHost</RootNamespace>
    <PlatformTarget>x64</PlatformTarget>
  </PropertyGroup>
  <ItemGroup>
    <Reference Include="HIDMaestro.Core">
      <HintPath>$(MSBuildThisFileDirectory)..\..\..\..\Archives\Migration-Backup\20260831-152113\YMCC-Workspace\Build\External\HIDMaestro\v1.7.0\HIDMaestro.Core.dll</HintPath>
      <Private>true</Private>
    </Reference>
  </ItemGroup>
</Project>

`
stderr:

argv: Get-FileHash -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\bin\Release\net10.0-windows\HIDMaestro.Core.dll -Algorithm SHA256
exitCode: 0
stdout:
`

Algorithm : SHA256
Hash      : BD42A99BCB260435CE25796C54A4B792F8A2CED6AB78659C0CF926011663938E
Path      : G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\bin\Release\net10.0-windows\HIDMaestro.Core.dll
`
stderr:

argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\Program.cs, lines 10-18 and 106-153
exitCode: 0
stdout:
`
10:    static int Main(string[] args)
11:    {
12:        try
13:        {
14:            if (args.Length >= 2 && args[0] == "--state")
15:            {
16:                if (args.Length >= 4 && args[2] == "--persona")
17:                    persona = args[3] == "dualshock4" ? "dualshock-4-v2" : "xbox-360-wired";
18:                RunFile(args[1]);
106:            {
107:                Reply(false, ex.GetType().Name + ": " + ex.Message);
108:            }
109:        }
110:    }
111:
112:    static void EnsureController()
113:    {
114:        if (controller is not null) return;
115:        ctx = new HMContext();
116:        ctx.LoadDefaultProfiles();
117:        try { ctx.InstallDriver(); } catch { }
118:        var profile = ctx.GetProfile(persona) ?? throw new InvalidOperationException("profile-missing:" + persona);
119:        controller = ctx.CreateController(profile);
120:    }
121:
122:    static void Submit(JsonElement root)
123:    {
124:        if (controller is null) throw new InvalidOperationException("not-ready");
125:        var buttons = root.TryGetProperty("buttons", out var b) && b.ValueKind == JsonValueKind.Number ? b.GetInt32() : 0;
126:        var profile = ctx!.GetProfile(persona)!;
127:        var state = new HMGamepadState
128:        {
129:            Buttons = MapButtons(buttons),
130:            Hat = MapHat(buttons),
131:            Axes = HMGamepadStateHelpers.StandardAxes(profile, GetF(root, "lx"), GetF(root, "ly"), GetF(root, "rx"), GetF(root, "ry"), GetF(root, "lt"), GetF(root, "rt")),
132:            GyroDpsX = GetF(root, "gx"),
133:            GyroDpsY = GetF(root, "gy"),
134:            GyroDpsZ = GetF(root, "gz"),
135:        };
136:        controller.SubmitState(in state);
137:    }
138:
139:    static void SubmitNeutral()
140:    {
141:        if (controller is null) return;
142:        var profile = ctx!.GetProfile(persona)!;
143:        var state = new HMGamepadState
144:        {
145:            Buttons = HMButton.None,
146:            Hat = HMHat.None,
147:            Axes = HMGamepadStateHelpers.StandardAxes(profile, 0, 0, 0, 0, 0, 0),
148:        };
149:        controller.SubmitState(in state);
150:    }
151:
152:    static void Release()
153:    {
`
stderr:

## P04 — ROG direct feature writer and dormant constructor
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp, lines 14301-14313 and 14672-14694
exitCode: 0
stdout:
`
14301:// HC's XboxAdaptiveController.Disable() sends 0x02 while it is managing its
14302:// own virtual X360 slot. That action is not a general ROG input-isolation
14303:// primitive: it removes the physical Xbox face from both Steam and YMCC.
14304:// YMCC therefore exposes only HC's lifecycle safety-net direction (0x01).
14305:static bool rogWriteXboxFaceEnabled() {
14306:    if (g_rog.control == INVALID_HANDLE_VALUE || g_rog.featureLen < 5) return false;
14307:    std::vector<BYTE> report(g_rog.featureLen, 0);
14308:    report[0] = 0x5A;
14309:    report[1] = 0xD1;
14310:    report[2] = 0x0B;
14311:    report[3] = 0x01;
14312:    report[4] = 0x01;
14313:    return HidD_SetFeature(g_rog.control, report.data(), static_cast<ULONG>(report.size())) != FALSE;
14672:static bool rogEnsureXboxFaceEnabled(const char* reason) {
14673:    g_rog.suppressed = false;
14674:    g_rog.restoreRequired = false;
14675:    g_rog.hidSeenAfterSuppress = false;
14676:    g_rog.suppressTick = 0;
14677:    rogCloseSession(true);
14678:    rogBindInputHid();
14679:    bool wrote = false;
14680:    for (int attempt = 0; attempt < 5; ++attempt) {
14681:        if (!g_rog.hasControl) {
14682:            rogCloseSession(true);
14683:            rogBindInputHid();
14684:        }
14685:        if (g_rog.hasControl) wrote = rogWriteXboxFaceEnabled();
14686:        if (wrote) break;
14687:        rogCloseSession(true);
14688:        Sleep(80);
14689:        rogBindInputHid();
14690:    }
14691:    appendNativeLifecycleLog("rog.xbox-face-enabled", {
14692:        {"reason", reason ? reason : ""},
14693:        {"wrote", wrote},
14694:        {"hasControl", g_rog.hasControl},
`
stderr:

argv: rg -n buildRogXboxModeReport|rogInputAdapter <rogInputAdapter.ts> <main.cpp> <Program.cs>
exitCode: 0
stdout:
`
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\rogInputAdapter.ts:43:export function buildRogXboxModeReport(disabled: boolean): Uint8Array {
`
stderr:

argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\rogInputAdapter.ts, lines 1-50
exitCode: 0
stdout:
`
1:/** ROG Xbox Ally controller contract derived from the locked HC source.
2: *
3: * Native transport lives in main.cpp and is gated on VID/PID 0B05:1ABE/1B4C,
4: * feature report 0x5A, and a stable HID gamepad source.  Xbox 360 and GPD
5: * never match this identity.  The public owner remains PhysicalInputOwnership.v1.
6: */
7:export const ROG_VENDOR_ID = 0x0b05;
8:export const ROG_XBOX_ALLY_PRODUCT_IDS = [0x1abe, 0x1b4c] as const;
9:export const ROG_INPUT_HID_ID = 0x5a;
10:
11:export type RogInputFace = 'rog-vendor-hid' | 'xbox-xinput';
12:export type RogControllerPhase = 'observed' | 'armed' | 'suppressed' | 'restoring' | 'restored' | 'failed';
13:export const ROG_SHARED_OWNER_POLICY = 'PhysicalInputOwnership.v1';
14:
15:export interface RogDeviceIdentity {
16:  vendorId: number;
17:  productId: number;
18:  instanceId?: string;
19:  xInputUserIndex?: number;
20:}
21:
22:export interface RogControllerSession {
23:  identity: RogDeviceIdentity;
24:  phase: RogControllerPhase;
25:  restoreRequired: boolean;
26:  epoch: number;
27:}
28:
29:export function isRogXboxAlly(identity: RogDeviceIdentity): boolean {
30:  return identity.vendorId === ROG_VENDOR_ID &&
31:    (ROG_XBOX_ALLY_PRODUCT_IDS as readonly number[]).includes(identity.productId);
32:}
33:
34:export function classifyRogFace(instanceId: string): RogInputFace | null {
35:  const value = instanceId.toUpperCase();
36:  if (value.includes(`VID_${ROG_VENDOR_ID.toString(16).padStart(4, '0').toUpperCase()}`) &&
37:      (value.includes('PID_1ABE') || value.includes('PID_1B4C'))) return 'rog-vendor-hid';
38:  if (value.includes('IG_00') && (value.includes('XINPUT') || value.includes('XUSB') || value.includes('VID_045E'))) return 'xbox-xinput';
39:  return null;
40:}
41:
42:/** HC ROGAlly.XBoxController(false/true) report payload, kept explicit for audit. */
43:export function buildRogXboxModeReport(disabled: boolean): Uint8Array {
44:  return Uint8Array.from([0x5a, 0xd1, 0x0b, 0x01, disabled ? 0x02 : 0x01]);
45:}
46:
47:export function armRogController(identity: RogDeviceIdentity, epoch = 1): RogControllerSession {
48:  if (!isRogXboxAlly(identity)) throw new Error('rog-identity-mismatch');
49:  return { identity, phase: 'armed', restoreRequired: false, epoch };
50:}
`
stderr:

argv: rg -n hidHideIsolatePhysicalXbox360\\( G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp
exitCode: 0
stdout:
`
5136:static void hidHideIsolatePhysicalXbox360() {
`
stderr:
