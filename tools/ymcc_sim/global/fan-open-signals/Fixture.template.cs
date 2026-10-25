#nullable enable
using System.Collections.Generic;
using System;
using Thread = FanFixture.InertThread;
using HandheldCompanion.Shared;
using HidLibrary;
using HandheldCompanion.Extensions;
namespace FanFixture {
 public static class Trace {
  public static readonly List<string> Calls=new();
  public static readonly List<(int Block,int Value)> Power=new();
  public static bool UseOpenLib,BaseFalse,BaseThrow,WmiBad,WmiThrow,ShiftReadFailure,NoHid,OverBoostEnabled,DisposeThrows,ReleaseThrows;
  public static Action? OnRead; public static int Firmware=0x126;
  public static void Reset(){Calls.Clear();Power.Clear();UseOpenLib=BaseFalse=BaseThrow=WmiBad=WmiThrow=ShiftReadFailure=NoHid=OverBoostEnabled=DisposeThrows=ReleaseThrows=false;OnRead=null;}
  public static void Add(string name)=>Calls.Add(name);
  public static int Forbidden=>Calls.Count(x=>x.StartsWith("Power:")||x.StartsWith("Shift:")||x.StartsWith("UEFI")||x.StartsWith("HID:")||x=="Watcher");
 }
 public static class InertThread { public static void Sleep(int ms)=>Trace.Add("Wait:"+ms); }
 public sealed class FakeOpenLib : IDisposable {public void Dispose(){Trace.Add("OpenLib.Dispose");if(Trace.DisposeThrows)throw new InvalidOperationException("dispose failed");}}
}
namespace HidLibrary {
 public class HidDeviceAttributes { public int Version=>FanFixture.Trace.Firmware; }
 public sealed class HidDevice { public HidDeviceAttributes Attributes=>new();public void Dispose()=>FanFixture.Trace.Add("HID.Dispose"); }
}
namespace HandheldCompanion.Extensions { public static class HidDeviceExtensions {public static bool Write(this HidDevice d,byte[] bytes,int a,int n){FanFixture.Trace.Add("HID:"+bytes[4]);return true;}} }
namespace HandheldCompanion.Shared {public static class LogManager { public static void LogInformation(string s,params object[] a){} public static void LogError(string s,params object[] a){}public static void LogWarning(string s,params object[] a){} } }
namespace HandheldCompanion {
 public sealed class WmiIoResult { public byte[] Data{get;set;}=new byte[32];public bool IsUsable{get;set;}=true; }
 public static class WMI {
  public static byte[] Get(string scope,string path,string name,byte block,int length,out bool success){FanFixture.Trace.Add("Read:"+name+":"+block);success=!FanFixture.Trace.ShiftReadFailure;var data=new byte[Math.Max(length,3)];data[1]=2;data[2]=192;return data;}
  public static WmiIoResult GetStructured(string scope,string path,string name,byte block,int length,int min){FanFixture.Trace.Add("Structured:"+name+":"+block);FanFixture.Trace.OnRead?.Invoke();if(FanFixture.Trace.WmiThrow)throw new InvalidOperationException("WMI exception");return new(){Data=new byte[]{0,2,1,40,50,60,70,80,90,100},IsUsable=!FanFixture.Trace.WmiBad};}
  public static int GetAPLength(byte block)=>32;
  public static void Set(string scope,string path,string name,byte[] data){var block=data[0];if(block==210)FanFixture.Trace.Add("Shift:"+data[1]);else{FanFixture.Trace.Add("Power:"+block+":"+data[1]);FanFixture.Trace.Power.Add((block,data[1]));}}
 }
}
namespace HandheldCompanion.Devices {
 public class IDevice {
  protected bool UseOpenLib=FanFixture.Trace.UseOpenLib,DeviceOpen;
  protected static FanFixture.FakeOpenLib? openLibSys;
  protected Dictionary<int,HidDevice> hidDevices=new();
  public virtual bool IsOpen=>DeviceOpen;public virtual bool IsSupported=>true;
  public IDevice(){if(!FanFixture.Trace.NoHid)hidDevices.Add(1,new());}
  private bool OpenLibSys(){FanFixture.Trace.Add("OpenLib.Open");if(FanFixture.Trace.BaseThrow)throw new InvalidOperationException("base throw");if(FanFixture.Trace.BaseFalse)return false;openLibSys=new();return true;}
  /*BASE_OPEN*/
  public virtual void set_long_limit(int limit){} public virtual void set_short_limit(int limit){}
  public virtual void OpenEvents()=>throw new InvalidOperationException("general events forbidden");
  public virtual void Close()=>throw new InvalidOperationException("general close forbidden");
  public (bool Device,bool Claw) Flags()=>(DeviceOpen,this is ClawA1M m&&m.ClawFlag());
  public void ForeignOpen()=>DeviceOpen=true;
 }
 public class FirmwareData {public byte[]? M1XInput,M2XInput,M1DInput,M2DInput;}
 public class ClawA1M:IDevice {
  protected string WmiScope{get;set;}="root\\WMI"; protected string WmiPath{get;set;}="MSI_ACPI";
  protected int WmiMajorVersion,WmiMinorVersion,Firmware; private bool ClawOpen;
  public override bool IsOpen=>DeviceOpen&&ClawOpen;
  public bool ClawFlag()=>ClawOpen;public int FirmwareValue()=>Firmware;
  protected const int INPUT_HID_ID=1;protected GamepadMode gamepadMode=GamepadMode.XInput;
  public FirmwareData? FirmwareDevice=>null;
  protected string MsIDCVarData="fixture-guid";
  protected byte[] GetMsiDCVarData(ref int n){n=8;var data=new byte[8];data[1]=(byte)(FanFixture.Trace.OverBoostEnabled?1:0);return data;}
  public static bool SetUEFIVariableEx(string name,string guid,byte[] data,int n){FanFixture.Trace.Add("UEFI:"+name);return true;}
  protected void StartWatching()=>FanFixture.Trace.Add("Watcher");
  public object ReleaseMsiFanControl(string cause){FanFixture.Trace.Add("Fan.Release:"+cause);if(FanFixture.Trace.ReleaseThrows)throw new InvalidOperationException("release failed");return new {Status="ok"};}
  /*MSI_METHODS*/
 }
 public class ClawA2VM:ClawA1M { /*ClawA2VM*/ }
 public class ClawBZ2EM:ClawA1M { /*ClawBZ2EM*/ }
 public class UnknownClaw:ClawA1M {}
 public class UnrelatedDevice:IDevice {public override bool Open(){FanFixture.Trace.Add("Unrelated.Open");return base.Open();}}
}
namespace HandheldCompanion.Devices.MSI { public class ClawCG3EM:HandheldCompanion.Devices.ClawA1M { /*ClawCG3EM*/ } }
