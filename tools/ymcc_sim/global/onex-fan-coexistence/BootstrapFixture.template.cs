#nullable enable
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Management;
using Newtonsoft.Json;
using HandheldCompanion.Devices;
using HandheldCompanion.Sensors;
using HandheldCompanion.Utils;
using Windows.Devices.Sensors;
using Sentry;
using HandheldCompanion.Shared;
using static HandheldCompanion.Utils.DeviceUtils;

namespace BootstrapFixture {
 public static class Ports {
  public static string Root="";public static string Failure="";
  public static readonly List<string> Calls=new();public static readonly List<string> Constructors=new();public static readonly List<string> Queries=new();
  public static readonly Dictionary<string,Dictionary<string,object?>> Wmi=new();public static int DisposedCollections,SensorEvents;public static string? LookupId;public static string? UsbName;
  public static Windows.Devices.Sensors.Gyrometer? Gyro;public static Windows.Devices.Sensors.Accelerometer? Accel;public static HandheldCompanion.Sensors.SerialUSBIMU? External;
  public static Sentry.Scope LastScope=new();
  public static void Check(string stage){Calls.Add(stage);if(Failure==stage)throw new IOException("BOOTSTRAP_IO:"+stage);if(Failure==stage+":unsupported")throw new PlatformNotSupportedException("BOOTSTRAP_UNSUPPORTED:"+stage);}
  public static void Constructor(string name){if(name.EndsWith(".SuiPlay0X1",StringComparison.Ordinal))throw new InvalidOperationException("INACTIVE_DEVICE_NOT_ADMITTED");Constructors.Add(name);Check("constructor");}
  public static void Reset(string root){
   if(Root.Length==0){Root=Path.GetFullPath(root);if(!Root.Split(Path.DirectorySeparatorChar).Any(part=>part.Equals("Build",StringComparison.OrdinalIgnoreCase)))throw new InvalidOperationException("Owned Build root required");HandheldCompanion.App.SettingsPath=Root;}
   else if(Root!=Path.GetFullPath(root))throw new InvalidOperationException("One owned cache root per fixture process");
   Directory.CreateDirectory(Root);Failure="";Calls.Clear();Constructors.Clear();Queries.Clear();Wmi.Clear();DisposedCollections=SensorEvents=0;LookupId=UsbName=null;Gyro=null;Accel=null;External=null;LastScope=new();
   typeof(HandheldCompanion.MotherboardInfo).GetField("cache",BindingFlags.Static|BindingFlags.NonPublic)!.SetValue(null,new Dictionary<string,object>());
   typeof(HandheldCompanion.Devices.IDevice).GetField("device",BindingFlags.Static|BindingFlags.NonPublic)!.SetValue(null,null);
   if(File.Exists(CachePath))File.Delete(CachePath);Identity("ASUSTeK COMPUTER INC.","RC71L","fixture-machine","ROG Ally","1","AMD Ryzen",8);
  }
  public static string CachePath=>Path.Combine(Root,"cache","motherboard.json");
  public static void Identity(string manufacturer,string product,string systemName,string model,string version,string processor,int cores){
   Wmi["Win32_BaseBoard"]=new(){{"Manufacturer",manufacturer},{"Product",product},{"Version",version}};
   Wmi["Win32_MotherboardDevice"]=new(){{"SystemName",systemName}};
   Wmi["Win32_Processor"]=new(){{"Name",processor},{"NumberOfCores",cores}};
   Wmi["Win32_ComputerSystem"]=new(){{"Model",model}};
  }
  public static void SeedCache(string json)=>File.WriteAllText(CachePath,json);
  public static HandheldCompanion.Devices.IDevice Current()=>HandheldCompanion.Devices.IDevice.GetCurrent();
 }
}
namespace HandheldCompanion {public static class App {public static string SettingsPath="";}public class USBDeviceInfo {public string Name {get;set;}="";}}
namespace System.Management {
 public class ManagementBaseObject {internal Dictionary<string,object?> Values=new();public object? this[string field]{get{BootstrapFixture.Ports.Check("wmi-read:"+field);return Values.GetValueOrDefault(field);}}}
 public class ManagementObject:ManagementBaseObject {public ManagementObject(Dictionary<string,object?> values){Values=values;}}
 public sealed class ManagementObjectCollection:IEnumerable,IDisposable {
  readonly IEnumerable<ManagementObject> values;public ManagementObjectCollection(IEnumerable<ManagementObject> values){this.values=values;}
  public IEnumerator GetEnumerator()=>values.GetEnumerator();public void Dispose(){BootstrapFixture.Ports.DisposedCollections++;}
 }
 public class ManagementObjectSearcher {readonly string table;public ManagementObjectSearcher(string scope,string query){if(scope!="root\\CIMV2"||!query.StartsWith("SELECT * FROM ",StringComparison.Ordinal))throw new InvalidOperationException("Unknown WMI fixture query");table=query[14..];}
  public ManagementObjectCollection Get(){BootstrapFixture.Ports.Queries.Add(table);BootstrapFixture.Ports.Check("wmi:"+table);return new(BootstrapFixture.Ports.Wmi.TryGetValue(table,out var value)?new[]{new ManagementObject(value)}:Array.Empty<ManagementObject>());}
 }
}
namespace Sentry {
 public sealed class Scope {public SentryContexts Contexts{get;}=new();}public sealed class SentryContexts {public Protocol.Device Device{get;}=new();}
 public static class SentrySdk {public static void ConfigureScope(Action<Scope> action){BootstrapFixture.Ports.Check("telemetry");action(BootstrapFixture.Ports.LastScope);}}
}
namespace Sentry.Protocol {public class Device {public string Model{get;set;}="";public string Manufacturer{get;set;}="";public string Name{get;set;}="";public string CpuDescription{get;set;}="";public string DeviceType{get;set;}="";}}
namespace HandheldCompanion.Shared {public static class LogManager {public static void LogInformation(string text,params object[] args){BootstrapFixture.Ports.Calls.Add("log-info");}public static void LogWarning(string text,params object[] args){BootstrapFixture.Ports.Calls.Add("log-warning");}}}
namespace Windows.Devices.Sensors {
 public sealed class Gyrometer {public static bool operator ==(Gyrometer? a,Gyrometer? b)=>ReferenceEquals(a,b);public static bool operator !=(Gyrometer? a,Gyrometer? b)=>!ReferenceEquals(a,b);public override bool Equals(object? b)=>ReferenceEquals(this,b);public override int GetHashCode()=>System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this);public string DeviceId{get;set;}="";public static Gyrometer? GetDefault(){BootstrapFixture.Ports.Check("gyro");return BootstrapFixture.Ports.Gyro;}}
 public sealed class Accelerometer {public static bool operator ==(Accelerometer? a,Accelerometer? b)=>ReferenceEquals(a,b);public static bool operator !=(Accelerometer? a,Accelerometer? b)=>!ReferenceEquals(a,b);public override bool Equals(object? b)=>ReferenceEquals(this,b);public override int GetHashCode()=>System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this);public string DeviceId{get;set;}="";public static Accelerometer? GetDefault(){BootstrapFixture.Ports.Check("accel");return BootstrapFixture.Ports.Accel;}}
}
namespace HandheldCompanion.Sensors {public class SerialUSBIMU {public string Name="";public static SerialUSBIMU? GetCurrent(){BootstrapFixture.Ports.Check("serial");return BootstrapFixture.Ports.External;}public string GetName(){BootstrapFixture.Ports.Check("serial-name");return Name;}}}
namespace HandheldCompanion.Utils {
 public static class DeviceUtils {public static HandheldCompanion.USBDeviceInfo? GetUSBDevice(string DeviceId){BootstrapFixture.Ports.Check("usb-lookup");BootstrapFixture.Ports.LookupId=DeviceId;return BootstrapFixture.Ports.UsbName is null?null:new HandheldCompanion.USBDeviceInfo{Name=BootstrapFixture.Ports.UsbName};}}
 public static class CommonUtils { /*BETWEEN*/ }
}
namespace HandheldCompanion { /*MOTHERBOARD*/ }
namespace HandheldCompanion.Devices {
 [Flags] /*CAPABILITIES*/
 public class IDevice {
  static IDevice? device;
  public string ManufacturerName="",ProductName="",SystemName="",SystemModel="",Version="",Processor="",DeviceType="Handheld";
  public int NumberOfCores;public uint CpuClock=6000;public DeviceCapabilities Capabilities;public string InternalSensorName="",ExternalSensorName="";
  public delegate void CapabilitiesChangedEventHandler(IDevice sender,DeviceCapabilities capabilities);public event CapabilitiesChangedEventHandler? CapabilitiesChanged;
  public void CountFixtureSensorEvents(){CapabilitiesChanged+=(_,_)=>BootstrapFixture.Ports.SensorEvents++;}
  /*GET_CURRENT*/
  /*PULL_SENSORS*/
  /*INITIALIZE*/
 }
}
/*MODEL_CONSTRUCTORS*/
