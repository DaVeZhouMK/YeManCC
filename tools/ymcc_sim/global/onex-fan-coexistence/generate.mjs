import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { block } from '../fan-open-signals/extract.mjs';
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
export function generateFixture(repo, out, template) {
  const dev = 'deps/handheldcompanion-runtime/source/Devices/';
  const specs = [
    ['MANAGER_STATUS', 'deps/handheldcompanion-runtime/source/Managers/IManager.cs', 'public enum ManagerStatus'],
    ['BASE_EVENTS', dev + 'IDevice.cs', 'public virtual void OpenEvents()'],
    ['QUERY_DEVICES', dev + 'IDevice.cs', 'private void QueryDevices()'],
    ['HID_ARRIVED', dev + 'IDevice.cs', 'private void DeviceManager_HidDeviceArrived(PnPDetails device, Guid InterfaceGuid)'],
    ['HID_REMOVED', dev + 'IDevice.cs', 'private void DeviceManager_HidDeviceRemoved(PnPDetails device, Guid InterfaceGuid)'],
    ['DEVICE_INIT_EVENT', dev + 'IDevice.cs', 'private void DeviceManager_Initialized()'],
    ['QUERY_SETTINGS', dev + 'IDevice.cs', 'protected virtual void QuerySettings()'],
    ['BASE_SETTING_EVENT', dev + 'IDevice.cs', 'protected virtual void SettingsManager_SettingValueChanged(string name, object? value, bool temporary, bool initializing)'],
    ['SETTINGS_INIT_EVENT', dev + 'IDevice.cs', 'protected virtual void SettingsManager_Initialized()'],
    ['QUERY_PROFILE', dev + 'IDevice.cs', 'protected virtual void QueryPowerProfile()'],
    ['PROFILE_INIT_EVENT', dev + 'IDevice.cs', 'protected virtual void PowerProfileManager_Initialized()'],
    ['AYA_EVENTS', dev + 'AYANEO/AYANEODevice.cs', 'public override void OpenEvents()'],
    ['AYA_SYSTEM_INIT', dev + 'AYANEO/AYANEODevice.cs', 'private void SystemManager_Initialized()'],
    ['AYA_POWER_LINE', dev + 'AYANEO/AYANEODevice.cs', 'private void SystemManager_PowerLineStatusChanged_Handler(PowerLineStatus prevPowerLineStatus, PowerLineStatus powerLineStatus)'],
    ['AYA_SESSION_LOCK', dev + 'AYANEO/AYANEODevice.cs', 'private void SystemManager_SessionLockChanged(bool isLocked)'],
    ['CEC_EVENTS', dev + 'AYANEO/AYANEODeviceCEc.cs', 'public override void OpenEvents()'],
    ['CEC_QUERY_SETTINGS', dev + 'AYANEO/AYANEODeviceCEc.cs', 'protected override void QuerySettings()'],
    ['CEC_SETTING_EVENT', dev + 'AYANEO/AYANEODeviceCEc.cs', 'protected override void SettingsManager_SettingValueChanged(string name, object? value, bool temporary, bool initializing)'],
    ['CEC_POWER_EVENT', dev + 'AYANEO/AYANEODeviceCEc.cs', 'private void PowerManager_RemainingChargePercentChanged(object? sender, object e)'],
    ['FLIP_EVENTS', dev + 'AYANEO/AYANEOFlipDS.cs', 'public override void OpenEvents()'],
    ['FLIP_QUERY_SETTINGS', dev + 'AYANEO/AYANEOFlipDS.cs', 'protected override void QuerySettings()'],
    ['FLIP_SETTING_EVENT', dev + 'AYANEO/AYANEOFlipDS.cs', 'protected override void SettingsManager_SettingValueChanged(string name, object? value, bool temporary, bool initializing)'],
    ['FLIP_INPUT_EVENT', dev + 'AYANEO/AYANEOFlipDS.cs', 'private void ControllerManager_InputsUpdated(ControllerState Inputs, bool IsMapped)'],
    ['PROFILE_CALLBACK', dev + 'IDevice.cs', 'public virtual void PowerProfileManager_Applied(PowerProfile profile, UpdateSource source)'],
    ['CLONE_PROFILE', 'deps/handheldcompanion-runtime/source/Managers/PowerProfileManager.cs', 'public PowerProfile CloneProfile(PowerProfile source)'],
    ['UPDATE_SOURCE', 'deps/handheldcompanion-runtime/source/Misc/Profile.cs', 'public enum UpdateSource'],
    ['FAN_MODE', 'deps/handheldcompanion-runtime/source/Misc/FanProfile.cs', 'public enum FanMode'],
    ['DEVICE_INITIALIZE', dev + 'IDevice.cs', 'public virtual void Initialize(bool FirstStart, bool NewUpdate)'],
    ['FLIP_INITIALIZE', dev + 'AYANEO/AYANEOFlipDS.cs', 'public override void Initialize(bool FirstStart, bool NewUpdate)'],
    ['FLIP1S_INITIALIZE', dev + 'AYANEO/AYANEOFlip1SDS.cs', 'public override void Initialize(bool FirstStart, bool NewUpdate)'],
    ['BASE_OPEN', dev + 'IDevice.cs', 'public virtual bool Open()'],
    ['BASE_CLOSE', dev + 'IDevice.cs', 'public virtual void Close()'],
    ['CEC_CLOSE', dev + 'AYANEO/AYANEODeviceCEc.cs', 'public override void Close()'],
    ['CEC_OPEN', dev + 'AYANEO/AYANEODeviceCEc.cs', 'public override bool Open()'],
    ['AYA_CLOSE', dev + 'AYANEO/AYANEODevice.cs', 'public override void Close()'],
    ['FLIP_CLOSE', dev + 'AYANEO/AYANEOFlipDS.cs', 'public override void Close()'],
    ['CEC_CONSTRUCTOR_EC', dev + 'AYANEO/AYANEODeviceCEc.cs', 'ECDetails = new ECDetails'],
    ['BASE_INIT', dev + 'IDevice.cs', 'private bool OpenLibSys()'],
    ['PARENT_OPEN', dev + 'OneXAOKZOE.cs', 'public override bool Open()'],
    ['X1_OPEN', dev + 'OneXPlayer/OneXPlayerX1.cs', 'public override bool Open()'],
    ['EC_WRITE', dev + 'IDevice.cs', 'public virtual bool ECRamDirectWriteByte(ushort register, ECDetails details, byte data)'],
    ['BASE_FAN_CONTROL', dev + 'IDevice.cs', 'public virtual void SetFanControl(bool enable, int mode = 0)'],
    ['BASE_FAN_DUTY', dev + 'IDevice.cs', 'public virtual void SetFanDuty(double percent)'],
    ['FAN_CONTROL', dev + 'OneXAOKZOE.cs', 'public override void SetFanControl(bool enable, int mode = 0)'],
    ['FAN_DUTY', dev + 'OneXAOKZOE.cs', 'public override void SetFanDuty(double percent)'],
    ['EC_DETAILS', dev + 'IDevice.cs', 'public struct ECDetails'],
    ['CAPABILITIES', dev + 'IDevice.cs', 'public enum DeviceCapabilities : ushort'],
    ['CONSTRUCTOR_EC', dev + 'OneXPlayer/OneXPlayerX1.cs', 'ECDetails = new ECDetails'],
  ];
  const sourceHashes = {}, methods = [];
  let text = fs.readFileSync(template, 'utf8');
  for (const [key, relative, signature] of specs) {
    const bytes = fs.readFileSync(path.join(repo, relative));
    const source = block(bytes.toString('utf8'), signature);
    const marker = `/*${key}*/`;
    if (text.split(marker).length !== 2) throw Error('Fixture marker is not unique: ' + key);
    text = text.replace(marker, () => source);
    sourceHashes[relative] = hash(bytes);
    methods.push({ relative, signature, sha256: hash(source) });
  }
  const managerSource=fs.readFileSync(path.join(repo,'deps/handheldcompanion-runtime/source/Managers/IManager.cs'),'utf8');
  if(!/\[Flags\]\s+public enum ManagerStatus/.test(managerSource))throw Error('Original ManagerStatus Flags declaration changed');
  methods.push({relative:'deps/handheldcompanion-runtime/source/Managers/IManager.cs',signature:'Original ManagerStatus Flags attribute',sha256:hash('[Flags]')});
  const cecFile=dev+'AYANEO/AYANEODeviceCEc.cs';
  const cecBytes=fs.readFileSync(path.join(repo,cecFile));
  const fanFlag='this.Capabilities |= DeviceCapabilities.FanControl;';
  if(cecBytes.toString('utf8').split(fanFlag).length!==2||text.split('/*CEC_FAN_CAPACITY*/').length!==2)throw Error('Exact CEc fan capability source changed');
  text=text.replace('/*CEC_FAN_CAPACITY*/',fanFlag);sourceHashes[cecFile]=hash(cecBytes);
  methods.push({relative:cecFile,signature:'constructor fan-capability statement',sha256:hash(fanFlag)});
  const rogFile=dev+'ASUS/ROGAlly.cs',rogBytes=fs.readFileSync(path.join(repo,rogFile));
  const rogFan='Capabilities |= DeviceCapabilities.FanControl;';
  if(rogBytes.toString('utf8').split(rogFan).length!==2||text.split('/*ROG_FAN_CAPACITY*/').length!==2)throw Error('Exact ROG fan capability source changed');
  text=text.replace('/*ROG_FAN_CAPACITY*/',rogFan);sourceHashes[rogFile]=hash(rogBytes);
  methods.push({relative:rogFile,signature:'constructor fan-capability statement only; original ROG lifecycle body not executed',sha256:hash(rogFan)});
  fs.mkdirSync(out, { recursive: true });
  fs.writeFileSync(path.join(out, 'Fixture.g.cs'), text);
  const result = { sourceHashes, methods, generatedSha256: hash(text), hardwareIo: false,
    scope: 'Complete extracted Open/OpenLibSys/fan/IDevice-AYANEO-CEc-FlipDS Close methods and exact EC data. Original mixed OpenEvents/settings/device/power/input callbacks execute with explicit settings/PnP/power/button/LED/screen/OLS terminals. Constructors, real manager implementations and hardware bodies are not executed.' };
  fs.writeFileSync(path.join(out, 'fixture-provenance.json'), JSON.stringify(result, null, 2));
  return result;
}
