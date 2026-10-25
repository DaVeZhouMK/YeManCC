#nullable enable
using HandheldCompanion.Misc;
using Newtonsoft.Json;
using HandheldCompanion.Managers;using HandheldCompanion.Controllers;using Nefarius.Utilities.DeviceManagement.PnP;using Windows.System.Power;
using System.Windows.Forms;using System.Collections.Concurrent;using System.Windows;using HandheldCompanion.Views.Windows;
using System.Text.RegularExpressions;using HandheldCompanion.Shared;using HandheldCompanion.Inputs;using HandheldCompanion.Misc.Threading.Tasks;using HandheldCompanion.Sensors;using System.IO.Ports;using static HandheldCompanion.Utils.DeviceUtils;
namespace ConsumerFixture {
 public static class Trace {public static List<string> Calls=new();public static int DriverInitializations,SerialOpenAttempts;public static bool FailInit,InitThrows,DisposeThrows,ReleaseThrows,ExitThrows,RgbReleaseThrows;public static Action? OnInit;public static Action<ushort,byte>? OnPortWrite;public static HashSet<string> Ports=new(StringComparer.OrdinalIgnoreCase);}
 public sealed class FakeOpenLib:IDisposable {public void Dispose(){Trace.Calls.Add("OpenLib.Dispose");}}
}
namespace HandheldCompanion {
 public class OpenLibSys:IDisposable {private byte selector,target,upper,lower;public bool Initialized;public int PortWrites,DisposeCalls;
 public void EnterSuperIoConfig(ushort a,ushort b){ConsumerFixture.Trace.Calls.Add($"IO.Enter:{a:x2}:{b:x2}");}
 public void ExitSuperIoConfig(ushort a,ushort b){ConsumerFixture.Trace.Calls.Add($"IO.Exit:{a:x2}:{b:x2}");if(ConsumerFixture.Trace.ExitThrows)throw new IOException("IO_EXIT_THROW");}
 public void WriteIoPortByte(ushort port,byte value){PortWrites++;ConsumerFixture.Trace.OnPortWrite?.Invoke(port,value);if(ConsumerFixture.Trace.ReleaseThrows)throw new IOException("PORT_WRITE_THROW");if(port==0x4e){selector=value;return;}if(selector==0x2e)target=value;else if(selector==0x2f){if(target==0x11)upper=value;else if(target==0x10)lower=value;else if(target==0x12)ConsumerFixture.Trace.Calls.Add($"FAN:{((upper<<8)|lower):x4}:{value}");}}
 public bool InitializeOls(){ConsumerFixture.Trace.DriverInitializations++;ConsumerFixture.Trace.OnInit?.Invoke();if(ConsumerFixture.Trace.InitThrows)throw new IOException("OLS_INIT_THROW");Initialized=!ConsumerFixture.Trace.FailInit;return Initialized;}public void Dispose(){DisposeCalls++;ConsumerFixture.Trace.Calls.Add("OpenLib.Dispose");if(ConsumerFixture.Trace.DisposeThrows)throw new IOException("OLS_DISPOSE_THROW");Initialized=false;}}
 public sealed class USBDeviceInfo {public string Name {get;set;}="USB-SERIAL CH340 (COM3)";}
}
namespace HandheldCompanion.Shared {public static class LogManager {public static void LogInformation(string x,params object[] a){}public static void LogError(string x,params object[] a){ConsumerFixture.Trace.Calls.Add("Error:"+string.Format(x,a));}public static void LogWarning(string x,params object[] a){ConsumerFixture.Trace.Calls.Add("Warning:"+x);}}}
namespace HandheldCompanion.Inputs {public enum ButtonFlags:byte {OEM1=30,OEM5=34}}
namespace HandheldCompanion.Utils {public static class DeviceUtils {public static List<HandheldCompanion.USBDeviceInfo> GetSerialDevices()=>[new()];}}
namespace HandheldCompanion.Sensors {public static class SerialUSBIMU {public static List<string> SerialPortNamesInUse {get;set;} = new();}}
namespace HandheldCompanion.Misc.Threading.Tasks {public sealed class SerialQueue {}}
namespace HandheldCompanion.Devices { /*EC_DETAILS*/ /*CAPABILITIES*/
 public class IDevice {
  public static IDevice GetCurrent()=>ConsumerFixture.FactoryPorts.GetCurrent();
  public void PullSensors()=>ConsumerFixture.FactoryPorts.Check("device.pull-sensors");
  protected bool DeviceOpen,UseOpenLib=true;protected static HandheldCompanion.OpenLibSys? openLibSys;public static bool LibraryInitialized=>openLibSys?.Initialized is true;public static void ResetLibrary(){openLibSys?.Dispose();openLibSys=null;}
  public DeviceCapabilities Capabilities=DeviceCapabilities.DynamicLighting|DeviceCapabilities.DynamicLightingBrightness|DeviceCapabilities.DynamicLightingSecondLEDColor;public ECDetails ECDetails;public void ForeignMarker()=>DeviceOpen=true;public void LoseMarker()=>DeviceOpen=false;public void DisableOpenLib()=>UseOpenLib=false;public void EnableOpenLib()=>UseOpenLib=true;public static void ForeignTransport()=>openLibSys=new();public virtual bool IsOpen=>DeviceOpen; /*BASE_FAN_CONTROL*/ /*BASE_FAN_DUTY*//*EC_WRITE*/
  /*BASE_INIT*/
  protected virtual void EcWriteByte(byte a,byte v){ConsumerFixture.Trace.Calls.Add($"EC:{a:x2}:{v:x2}");}
  protected virtual byte EcReadByte(byte a)=>a==0xf2?(byte)2:(byte)0x40;
  protected object updateLock=new();
  public delegate void ClosedEventHandler(IDevice sender);public event ClosedEventHandler? Closed;
  public delegate void OpenedEventHandler(IDevice sender);public event OpenedEventHandler? Opened;
  public IDevice(){Closed+=_=>ConsumerFixture.Trace.Calls.Add("Base.Close");Opened+=_=>ConsumerFixture.Trace.Calls.Add("Base.Events");}
  protected int vendorId=100;protected int[] productIds=[200];
  public void SetFixtureDeviceIdentity(int vendor,int product){vendorId=vendor;productIds=[product];}
  protected virtual void Device_Inserted(bool reScan=false){ConsumerFixture.Trace.Calls.Add("Device.Inserted:"+reScan);}
  protected virtual void Device_Removed(){ConsumerFixture.Trace.Calls.Add("Device.Removed");}
  /*DEVICE_INIT_EVENT*/
  private void GenericDeviceUpdated(PnPDevice? device,Guid guid){ConsumerFixture.Trace.Calls.Add("Devices.GenericUpdated");}
  /*HID_ARRIVED*/ /*HID_REMOVED*/ /*QUERY_DEVICES*/
  /*QUERY_SETTINGS*/ /*BASE_SETTING_EVENT*/ /*SETTINGS_INIT_EVENT*/ /*QUERY_PROFILE*/ /*PROFILE_INIT_EVENT*/
  // Historical Close fixtures add only handlers not already installed by the
  // complete original OpenEvents. Constructors and hardware leaves stay inert.
  public virtual void SeedFixtureCloseSubscriptions(){
   if(ManagerFactory.settingsManager.InitializedSubscribers==0)ManagerFactory.settingsManager.Initialized+=SettingsManager_Initialized;if(ManagerFactory.settingsManager.SettingSubscribers==0)ManagerFactory.settingsManager.SettingValueChanged+=SettingsManager_SettingValueChanged;
   if(ManagerFactory.powerProfileManager.InitializedSubscribers==0)ManagerFactory.powerProfileManager.Initialized+=PowerProfileManager_Initialized;if(ManagerFactory.deviceManager.InitializedSubscribers==0)ManagerFactory.deviceManager.Initialized+=DeviceManager_Initialized;
   if(ManagerFactory.deviceManager.DeviceSubscribers==0){ManagerFactory.deviceManager.UsbDeviceArrived+=GenericDeviceUpdated;ManagerFactory.deviceManager.UsbDeviceRemoved+=GenericDeviceUpdated;
   ManagerFactory.deviceManager.HidDeviceArrived+=DeviceManager_HidDeviceArrived;ManagerFactory.deviceManager.HidDeviceRemoved+=DeviceManager_HidDeviceRemoved;}
  }
  /*BASE_CLOSE*/
  /*DEVICE_INITIALIZE*/
  /*BASE_EVENTS*/
  /*BASE_OPEN*/
  /*PROFILE_CALLBACK*/
 }
 public class OneXAOKZOE:IDevice {public override void Close(){ConsumerFixture.Trace.Calls.Add("GeneralParent.Close");throw new InvalidOperationException("GENERIC_PARENT_CLOSE_FORBIDDEN");} /*PARENT_OPEN*/ /*FAN_CONTROL*/ /*FAN_DUTY*/}
 public class OneXPlayerX1:OneXAOKZOE {
  private SerialPort? _serialPort;private readonly SerialQueue _queue=new();private object? _vendorHidMonitor=null;public void ForeignSerial()=>_serialPort=new("COM9",115200,Parity.Even,8,StopBits.Two);public void ForeignHid()=>_vendorHidMonitor=new();
  public bool EnableSerialPort=true;public string SerialPortDeviceName="CH340";public int SerialPortBaudRate=115200,SerialPortDataBits=8;
  public Parity SerialPortParity=Parity.Even;public StopBits SerialPortStopBits=StopBits.Two;
  public override void Close(){ConsumerFixture.Trace.Calls.Add("GeneralX1.Close");throw new InvalidOperationException("GENERIC_X1_CLOSE_FORBIDDEN");} public OneXPlayerX1(){/*CONSTRUCTOR_EC*/;} public bool HasPort=>_serialPort?.IsOpen is true;
  /*X1_OPEN*/
 }
 public class OneXPlayerX1AMD:OneXPlayerX1 {}
 public class OneXPlayerX1Intel:OneXPlayerX1 {}
 public class OneXPlayerX1Pro:OneXPlayerX1AMD {}
 public class UnknownX1:OneXPlayerX1AMD {}
 public class UnknownOverride:OneXPlayerX1AMD {public override bool Open()=>base.Open();}
 // Only the existing Host insertion/wait coordination is under test here.
 // This inert terminal does not execute or model the original ROG HID methods.
 public class ROGAlly:IDevice {
  public ROGAlly(){ConsumerFixture.FactoryPorts.Check("device.construct");/*ROG_FAN_CAPACITY*/}
  public override void Initialize(bool firstStart,bool newUpdate){ConsumerFixture.FactoryPorts.Check("device.initialize");}
  public string Version=>ConsumerFixture.FactoryPorts.Identity();
  public object ReadMsiFanStateReadOnly()=>ConsumerFixture.FactoryPorts.Sample();
  public bool Inserted;public Action? OnInsert;
  public override bool IsOpen=>Inserted&&base.IsOpen;
  protected override void Device_Inserted(bool reScan){ConsumerFixture.Trace.Calls.Add("Insertion.Terminal:"+reScan);Inserted=true;OnInsert?.Invoke();}
 }
 public class InheritedEcDevice:IDevice {public InheritedEcDevice(){ECDetails=new ECDetails{AddressFanControl=0x44a,AddressFanDuty=0x44b,AddressStatusCommandPort=0x4e,AddressDataPort=0x4f,FanValueMin=0,FanValueMax=100};}}
 public class AYANEOFlipKB:AYANEO.AYANEODeviceCEc {}
 public class AYANEOFlipDS:AYANEOFlipKB {
  private ButtonState prevState=new();
  /*FLIP_INPUT_EVENT*/ /*FLIP_QUERY_SETTINGS*/ /*FLIP_SETTING_EVENT*/
  public void CEcControl_SetSecDispBrightness(short value){ConsumerFixture.Trace.Calls.Add("Screen.Brightness:"+value);}
  public override void SeedFixtureCloseSubscriptions(){base.SeedFixtureCloseSubscriptions();if(ControllerManager.Subscribers==0)ControllerManager.InputsUpdated+=ControllerManager_InputsUpdated;}
  /*FLIP_CLOSE*/ /*FLIP_EVENTS*/
  /*FLIP_INITIALIZE*/
 }
 public class VendorEcOverride:InheritedEcDevice {public override void SetFanControl(bool enabled,int mode=0){ConsumerFixture.Trace.Calls.Add("VendorControl:"+enabled);}public override void SetFanDuty(double value){ConsumerFixture.Trace.Calls.Add("VendorDuty:"+value);}}
 public class UnrelatedDevice:IDevice {public override bool Open(){ConsumerFixture.Trace.Calls.Add("Unrelated.Open");return base.Open();}}
}
namespace HandheldCompanion.Devices.OneXPlayer {public sealed class OneXPlayerApex:global::HandheldCompanion.Devices.OneXPlayerX1AMD {}}
namespace System.IO.Ports {
 public enum Parity{Even=2}public enum StopBits{Two=2}
 public sealed class SerialPort:System.ComponentModel.Component {
  public string PortName {get;}public int WriteTimeout{get;set;}public bool IsOpen{get;private set;}
  public SerialPort(string name,int rate,Parity p,int bits,StopBits stop){PortName=name;}
  public void Open(){ConsumerFixture.Trace.SerialOpenAttempts++;if(!ConsumerFixture.Trace.Ports.Add(PortName))throw new IOException("SERIAL_PORT_BUSY:"+PortName);IsOpen=true;ConsumerFixture.Trace.Calls.Add("Serial.Open:"+PortName);}
  public void Close(){if(IsOpen){ConsumerFixture.Trace.Ports.Remove(PortName);IsOpen=false;ConsumerFixture.Trace.Calls.Add("Serial.Close:"+PortName);}}
  protected override void Dispose(bool disposing){Close();base.Dispose(disposing);}
 }
}
namespace HandheldCompanion.Managers {
 [Flags] /*MANAGER_STATUS*/
 public class IManager {
  public delegate void InitializedEventHandler();public event InitializedEventHandler? Initialized;
  public ManagerStatus Status {get;set;}=ManagerStatus.None;public int InitializedSubscribers=>Initialized?.GetInvocationList().Length??0;
  public void RaiseInitialized(){Status=ManagerStatus.Initialized;Initialized?.Invoke();}
 }
 public sealed class SettingsManager:IManager {
  public readonly Dictionary<string,object?> Values=new();
  public void SetProperty(string name,object? value,bool force=false,bool temporary=false){Values[name]=value;ConsumerFixture.Trace.Calls.Add("Settings.Set:"+name+"="+value);SettingValueChanged?.Invoke(name,value,temporary,false);}
  public void Publish(string name,object? value,bool initializing=false){Values[name]=value;SettingValueChanged?.Invoke(name,value,false,initializing);}
  public string GetString(string name,bool temporary=false)=>Convert.ToString(Values.GetValueOrDefault(name))??"False";
  public bool GetBoolean(string name,bool temporary=false)=>Convert.ToBoolean(Values.GetValueOrDefault(name));
  public int GetInt(string name,bool temporary=false)=>Convert.ToInt32(Values.GetValueOrDefault(name));
  public double GetDouble(string name,bool temporary=false)=>Convert.ToDouble(Values.GetValueOrDefault(name));
  public delegate void SettingValueChangedEventHandler(string name,object? value,bool temporary,bool initializing);public event SettingValueChangedEventHandler? SettingValueChanged;
  public int SettingSubscribers=>SettingValueChanged?.GetInvocationList().Length??0;
 }
 public sealed class DeviceManager:IManager {
  public readonly ConcurrentDictionary<string,HandheldCompanion.PnPDetails> PnPDevices=new();
  public void PublishArrived(HandheldCompanion.PnPDetails d)=>HidDeviceArrived?.Invoke(d,Guid.Empty);public void PublishRemoved(HandheldCompanion.PnPDetails d)=>HidDeviceRemoved?.Invoke(d,Guid.Empty);
  public delegate void GenericDeviceArrivedEventHandler(PnPDevice? device,Guid guid);public event GenericDeviceArrivedEventHandler? UsbDeviceArrived;
  public delegate void GenericDeviceRemovedEventHandler(PnPDevice? device,Guid guid);public event GenericDeviceRemovedEventHandler? UsbDeviceRemoved;
  public delegate void DInputDeviceArrivedEventHandler(HandheldCompanion.PnPDetails device,Guid guid);public event DInputDeviceArrivedEventHandler? HidDeviceArrived;
  public delegate void DInputDeviceRemovedEventHandler(HandheldCompanion.PnPDetails device,Guid guid);public event DInputDeviceRemovedEventHandler? HidDeviceRemoved;
  public int DeviceSubscribers=>(UsbDeviceArrived?.GetInvocationList().Length??0)+(UsbDeviceRemoved?.GetInvocationList().Length??0)+(HidDeviceArrived?.GetInvocationList().Length??0)+(HidDeviceRemoved?.GetInvocationList().Length??0);
 }
 public sealed class PowerProfileManager:IManager {
  public PowerProfile Current=new();public PowerProfile GetCurrent()=>Current;
  /*CLONE_PROFILE*/
  private Action<object?,object?>? applied,discarded;
  public Action<string>? OnAdd,OnRemove;
  public void PublishApplied(object profile)=>applied?.Invoke(profile,"Background");
  public event Action<object?,object?> Applied {add{applied+=value;OnAdd?.Invoke("Applied");}remove{OnRemove?.Invoke("Applied");applied-=value;}}
  public event Action<object?,object?> Discarded {add{discarded+=value;OnAdd?.Invoke("Discarded");}remove{OnRemove?.Invoke("Discarded");discarded-=value;}}
  public int Subscribers=>(applied?.GetInvocationList().Length??0)+(discarded?.GetInvocationList().Length??0);
 }
 public static class ManagerFactory {
  public static readonly SettingsManager settingsManager=new();public static readonly DeviceManager deviceManager=new();public static readonly PowerProfileManager powerProfileManager=new();
  public static object[] Managers=>[settingsManager,deviceManager,powerProfileManager];
 }
}

namespace HandheldCompanion { /*UPDATE_SOURCE*/ }
namespace HandheldCompanion.Misc {
 /*FAN_MODE*/
 // Fixed curve-output leaf for temperature consumer tests; not a model of HC curve computation.
 public sealed class FanProfile {public FanMode fanMode;public double[] fanSpeeds=[];public double LastTemperature;public void SetTemperature(double value){LastTemperature=value;}public double GetFanSpeed()=>50.0;}
 public sealed class PowerProfile {public FanProfile FanProfile {get;set;}=new();public int OEMPowerMode {get;set;}=0xff;}
}

namespace Nefarius.Utilities.DeviceManagement.PnP {public sealed class PnPDevice {}}
namespace HandheldCompanion {public sealed class PnPDetails {public ushort VendorID,ProductID;}}
namespace HandheldCompanion.Inputs {public sealed class ButtonState {private readonly HashSet<ButtonFlags> buttons=new();public bool this[ButtonFlags button]{get=>buttons.Contains(button);set{if(value)buttons.Add(button);else buttons.Remove(button);}}public static void Overwrite(ButtonState origin,ButtonState target){target.buttons.Clear();target.buttons.UnionWith(origin.buttons);}public override bool Equals(object? other)=>other is ButtonState b&&buttons.SetEquals(b.buttons);public override int GetHashCode()=>buttons.Count;}}
namespace HandheldCompanion.Controllers {public sealed class ControllerState {public ButtonState ButtonState=new();}}
namespace HandheldCompanion.Managers {
 public static class ControllerManager {
  public delegate void InputsUpdatedEventHandler(HandheldCompanion.Controllers.ControllerState inputs,bool mapped);private static InputsUpdatedEventHandler? inputs;
  public static event InputsUpdatedEventHandler InputsUpdated{add{inputs+=value;}remove{ConsumerFixture.Trace.Calls.Add("Controller.Unsubscribe");inputs-=value;}}
  public static int Subscribers=>inputs?.GetInvocationList().Length??0;public static void Publish(ControllerState state,bool mapped)=>inputs?.Invoke(state,mapped);
 }
}
namespace Windows.System.Power {
 public static class PowerManager {
  private static EventHandler<object>? charge;
  public static event EventHandler<object> RemainingChargePercentChanged{add{charge+=value;}remove{ConsumerFixture.Trace.Calls.Add("Power.Unsubscribe");charge-=value;}}
  public static int Subscribers=>charge?.GetInvocationList().Length??0;public static int RemainingChargePercent{get;set;}=50;public static void Publish(int percent){RemainingChargePercent=percent;charge?.Invoke(null,new object());}
 }
}
namespace HandheldCompanion.Devices.AYANEO {
 public class AYANEOFlip1SDS:HandheldCompanion.Devices.AYANEOFlipDS { /*FLIP1S_INITIALIZE*/ }
 public class AYANEODevice:HandheldCompanion.Devices.IDevice {
  /*AYA_SYSTEM_INIT*/ /*AYA_POWER_LINE*/ /*AYA_SESSION_LOCK*/ /*AYA_EVENTS*/
  private void UpdateLED(){ConsumerFixture.Trace.Calls.Add("System.UpdateLED");}
  public override void SeedFixtureCloseSubscriptions(){base.SeedFixtureCloseSubscriptions();if(SystemManager.Subscribers==0)SystemManager.Initialized+=SystemManager_Initialized;if(SystemManager.PowerSubscribers==0)SystemManager.PowerLineStatusChanged+=SystemManager_PowerLineStatusChanged_Handler;if(SystemManager.LockSubscribers==0)SystemManager.SessionLockChanged+=SystemManager_SessionLockChanged;}
  /*AYA_CLOSE*/
 }
 public class AYANEODeviceCEc:AYANEODevice {
  public AYANEODeviceCEc(){/*CEC_FAN_CAPACITY*/ this./*CEC_CONSTRUCTOR_EC*/;}
  /*CEC_OPEN*/
  protected virtual void CEcControl_RgbHoldControl(){ConsumerFixture.Trace.Calls.Add("RGB.Hold");}
  protected virtual void CEcControl_RgbReleaseControl(){ConsumerFixture.Trace.Calls.Add("RGB.Release");if(ConsumerFixture.Trace.RgbReleaseThrows)throw new IOException("RGB_RELEASE_THROW");}
  /*CEC_POWER_EVENT*/ /*CEC_EVENTS*/ /*CEC_QUERY_SETTINGS*/ /*CEC_SETTING_EVENT*/
  protected virtual void CEcControl_BypassChargeOpen(){ConsumerFixture.Trace.Calls.Add("Battery.Bypass:open");}
  protected virtual void CEcControl_BypassChargeClose(){ConsumerFixture.Trace.Calls.Add("Battery.Bypass:close");}
  public override void SeedFixtureCloseSubscriptions(){base.SeedFixtureCloseSubscriptions();if(PowerManager.Subscribers==0)PowerManager.RemainingChargePercentChanged+=PowerManager_RemainingChargePercentChanged;}
  /*CEC_CLOSE*/
 }
}

namespace ConsumerFixture {public static class CloseTerminals {public static int PowerSubscribers=>global::Windows.System.Power.PowerManager.Subscribers;public static void PublishBattery(int percent)=>global::Windows.System.Power.PowerManager.Publish(percent);}}

namespace System.Windows.Forms {public enum PowerLineStatus {Offline,Online,Unknown=255}}
namespace HandheldCompanion.Managers {
 public static class SystemManager {
  public delegate void InitializedEventHandler();public static event InitializedEventHandler? Initialized;
  public delegate void PowerLineStatusChangedEventHandler(global::System.Windows.Forms.PowerLineStatus previous,global::System.Windows.Forms.PowerLineStatus current);public static event PowerLineStatusChangedEventHandler? PowerLineStatusChanged;
  public delegate void SessionLockChangedEventHandler(bool locked);public static event SessionLockChangedEventHandler? SessionLockChanged;
  public static int Subscribers=>(Initialized?.GetInvocationList().Length??0)+(PowerLineStatusChanged?.GetInvocationList().Length??0)+(SessionLockChanged?.GetInvocationList().Length??0);
  public static int PowerSubscribers=>PowerLineStatusChanged?.GetInvocationList().Length??0;public static int LockSubscribers=>SessionLockChanged?.GetInvocationList().Length??0;public static bool IsInitialized;
  public static void RaiseInitialized(){IsInitialized=true;Initialized?.Invoke();}public static void PublishLine(global::System.Windows.Forms.PowerLineStatus current)=>PowerLineStatusChanged?.Invoke(global::System.Windows.Forms.PowerLineStatus.Unknown,current);public static void PublishLock(bool locked)=>SessionLockChanged?.Invoke(locked);
 }
}

namespace System.Windows {public enum Visibility {Visible=0,Hidden=1,Collapsed=2}}
namespace HandheldCompanion.Views.Windows {public sealed class OverlayQuickTools {private static readonly OverlayQuickTools current=new();public static OverlayQuickTools GetCurrent()=>current;public void SetVisibility(System.Windows.Visibility value){ConsumerFixture.Trace.Calls.Add("Screen.Visibility:"+value);}}}
namespace ConsumerFixture {
 public static class FactoryPorts {
  public static bool Enabled;public static string Failure="";public static int Failures;public static List<string> Calls=new();public static HandheldCompanion.Devices.IDevice? Current;
  public static string BootIdentity="fixture-boot";public static Exception? LastFailure;public static Action<string>? OnStage;
  public static Dictionary<object,int> ResolverCalls=new(ReferenceEqualityComparer.Instance);public static string ResolverProbe="";
  public static void Reset(string failure="",int count=0){Enabled=true;Failure=failure;Failures=count;Calls.Clear();Current=null;LastFailure=null;OnStage=null;ResolverProbe="";ResolverCalls.Clear();}
  public static void Check(string stage){if(!Enabled)return;Calls.Add(stage);OnStage?.Invoke(stage);if(stage==Failure&&Failures>0){Failures--;throw LastFailure=new IOException("FACTORY_FAULT:"+stage);}}
  public static HandheldCompanion.Devices.IDevice GetCurrent(){Check("device.get-current");return Current??=new HandheldCompanion.Devices.ROGAlly();}
  public static void Resolve(object owner,string? name){if(Enabled&&name==ResolverProbe)ResolverCalls[owner]=ResolverCalls.GetValueOrDefault(owner)+1;}
  public static int Count(string stage)=>Calls.Count(x=>x==stage);
  public static string Identity(){Check("device.identity");return "fixture-identity";}
  public static object Sample(){Check("sample.read-only");return new {CoreReadsEffective=true,BootIdentity};}
 }
}
namespace HandheldCompanion {public static class MotherboardInfo {public static void Collect()=>ConsumerFixture.FactoryPorts.Check("motherboard.collect");}}

namespace ConsumerFixture {
 public static class StaPorts {
  public static readonly ConcurrentDictionary<int,Thread> Threads=new();
  public static readonly ConcurrentQueue<(Thread Thread,bool GateHeld)> Starts=new();
  public static Action? IdentityHook;public static Action<Thread,object>? StartHook;public static int OsIdentityReads;
  [global::System.Runtime.InteropServices.DllImport("kernel32.dll",EntryPoint="GetCurrentThreadId",ExactSpelling=true)]
  private static extern uint NativeThreadId();
  public static uint ReadOsThreadId(){Threads[Environment.CurrentManagedThreadId]=Thread.CurrentThread;Interlocked.Increment(ref OsIdentityReads);IdentityHook?.Invoke();return NativeThreadId();}
  public static void BeforeThreadStart(Thread thread,object backend){var gate=backend.GetType().GetField("staGate",global::System.Reflection.BindingFlags.Instance|global::System.Reflection.BindingFlags.NonPublic)!.GetValue(backend)!;Starts.Enqueue((thread,Monitor.IsEntered(gate)));StartHook?.Invoke(thread,backend);}
  public static void Reset(){Threads.Clear();while(Starts.TryDequeue(out _)){}IdentityHook=null;StartHook=null;OsIdentityReads=0;}
 }
}

namespace ConsumerFixture {public static class ValidationPorts {public static string HostRoot="";public static string BaseDirectory()=>HostRoot.Length==0?throw new InvalidOperationException("Validation fixture root is not configured"):HostRoot;}}

namespace ConsumerFixture {public static class CapabilityAclPorts {public static string LocalAppData="";public static string GetFolderPath(Environment.SpecialFolder folder){if(folder!=Environment.SpecialFolder.LocalApplicationData||LocalAppData.Length==0)throw new InvalidOperationException("Owned ACL fixture root is not configured");return LocalAppData;}}}
