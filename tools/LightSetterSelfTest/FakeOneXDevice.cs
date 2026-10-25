using System.IO;
using System.Collections.Concurrent;
using HandheldCompanion.Misc.Threading.Tasks;
namespace HandheldCompanion.Devices
{
 public class OneXAOKZOE
 {
  public int DriverInitializations; public List<(byte Address, byte Value)> EcWrites {get;}=[];
  public virtual bool Open(){DriverInitializations++;EcWrites.Add((0xf1,0x40));EcWrites.Add((0xf2,0x02));return true;}
 }
 public class OneXPlayerX1 : OneXAOKZOE
 {
  private readonly SerialQueue _queue=new();
  private System.IO.Ports.SerialPort? _serialPort=new();
  public bool EnableSerialPort=true;
  public string SerialPortDeviceName="CH340";
  public int SerialPortBaudRate=115200, SerialPortDataBits=8;
  public System.IO.Ports.Parity SerialPortParity=System.IO.Ports.Parity.Even;
  public System.IO.Ports.StopBits SerialPortStopBits=System.IO.Ports.StopBits.Two;
  public System.IO.Ports.SerialPort Port=>_serialPort!;
  public void ForgetPort()=>_serialPort=null;
  public bool HasPort=>_serialPort is not null;
  public List<FakePreset> LEDPresets {get;}=[new("preset-high-value",0x1e3),new("LEDPreset_OneXPlayerX1_Preset09",0x1e3)];
  private FakePreset? LEDPreset;
  private int LEDBrightness;
  private bool LEDEnabled;
  private System.Windows.Media.Color LEDControllerColor, LEDBackColor;
  private FakeLed LEDCurrentLevel;
  public OneXPlayerX1(){LEDPreset=LEDPresets[0];}
  public override bool Open(){if(!base.Open())return false;EcWrites.Add((0xeb,0x40));return true;}
  public void Write(string value){_queue.Enqueue(()=>_serialPort!.Write(value));}
  public virtual bool SetLedStatus(bool enable)
  {if(LEDEnabled!=enable){Write("status-left:"+enable);Write("status-right:"+enable);LEDEnabled=enable;}return true;}
  public virtual bool SetLedBrightness(int value)
  {if(LEDBrightness!=value){value=value==0?0:value<33?1:value>66?4:3;Write("brightness:"+value);SetLedStatus(value>0);LEDBrightness=value;}return true;}
  public virtual bool SetLedColor(System.Windows.Media.Color main,System.Windows.Media.Color second,FakeLed level,int speed)
  {if(LEDControllerColor!=main || LEDCurrentLevel!=level){Write("color-controller");LEDControllerColor=main;LEDCurrentLevel=level;}if(LEDBackColor!=second || LEDCurrentLevel!=level){Write("color-left");Write("color-right");LEDBackColor=second;LEDCurrentLevel=level;}return true;}
  public bool SetLEDPreset(FakePreset preset){if(preset!=LEDPreset)Write("preset:"+preset.Value);LEDPreset=preset;return true;}
 }
 public class OneXPlayerX1AMD : OneXPlayerX1 { }
 public sealed class OneXPlayerX1Intel : OneXPlayerX1 { }
 public sealed class OneXPlayerX1Pro : OneXPlayerX1AMD { }
 public class OneXPlayerOneXFly : OneXAOKZOE
 {
  protected HidLibrary.HidDevice? hidDevice=new();
  public HidLibrary.HidDevice Hid=>hidDevice!;
  public int HidWriteAttempts;
  public void ForgetHid()=>hidDevice=null;
  public bool SetLedStatus(bool status)=>true; // actual inherited IDevice no-op; off relies on the HID brightness/color writes
  public virtual bool SetLedBrightness(int value){if(hidDevice?.IsConnected is not true)return false;HidWriteAttempts++;return !hidDevice.FailWrite;}
  public virtual bool SetLedColor(System.Windows.Media.Color main,System.Windows.Media.Color second,FakeLed level,int speed)
  {if(hidDevice?.IsConnected is not true || (int)level is not (1 or 4))return false;HidWriteAttempts++;return !hidDevice.FailWrite;}
 }
 public class OneXPlayerOneXFlyF1Pro : OneXPlayerOneXFly { }
 public class OneXPlayerG1 : OneXAOKZOE
 {
  protected HidLibrary.HidDevice? hidDevice=new();
  public HidLibrary.HidDevice Hid=>hidDevice!;
  public int HidWriteAttempts;
  public void ForgetHid()=>hidDevice=null;
  public virtual bool SetLedBrightness(int value)=>WriteLighting();
  public virtual bool SetLedColor(System.Windows.Media.Color main,System.Windows.Media.Color second,FakeLed level,int speed)=>WriteLighting();
  private bool WriteLighting(){if(hidDevice?.IsConnected is not true)return false;HidWriteAttempts++;if(hidDevice.ThrowWrite)throw new IOException("Injected G1 HID failure");return !hidDevice.FailWrite;}
 }
 public class OneXPlayerG1AMD : OneXPlayerG1 { }
 public class OneXPlayerG1Intel : OneXPlayerG1 { }
 public class AOKZOEA1 : OneXAOKZOE
 {
  protected HidLibrary.HidDevice? hidDevice=new();
  public HidLibrary.HidDevice Hid=>hidDevice!;
  public int HidWriteAttempts;
  public void ForgetHid()=>hidDevice=null;
  public virtual bool SetLedBrightness(int value)=>WriteLighting();
  public virtual bool SetLedColor(System.Windows.Media.Color main,System.Windows.Media.Color second,FakeLed level,int speed)=>WriteLighting();
  private bool WriteLighting(){if(hidDevice?.IsConnected is not true)return false;HidWriteAttempts++;if(hidDevice.ThrowWrite)throw new IOException("Injected AOKZOE HID failure");return !hidDevice.FailWrite;}
 }
 public class AOKZOEA1Pro : AOKZOEA1 { }
 public class AOKZOEA1X : AOKZOEA1Pro { }
 public class AOKZOEA2 : AOKZOEA1Pro { }
 public class OneXPlayerX1Mini : OneXPlayerX1
 {
  protected HidLibrary.HidDevice hidDevice=new();
  public HidLibrary.HidDevice Hid=>hidDevice;
  public int HidWrites;
  public override bool SetLedBrightness(int value){if(!hidDevice.IsConnected || hidDevice.FailWrite)return false;HidWrites++;return true;}
  public OneXPlayerX1Mini(){EnableSerialPort=false;}
 }
}
namespace HandheldCompanion.Utils
{
 public static partial class DeviceUtils
 {
  public static List<SerialRow> Rows {get;set;}=[new("USB-SERIAL CH340 (COM3)")];
  public static int Discoveries;
  public static List<SerialRow> GetSerialDevices(){Discoveries++;return Rows;}
 }
 public sealed record SerialRow(string Name);
}
namespace HidLibrary { public sealed class HidDevice { public bool IsConnected {get;set;}=true; public bool FailWrite,ThrowWrite; public int DisposeCalls; public void Dispose()=>DisposeCalls++; } }
namespace System.IO.Ports
{
 // Entirely in memory. No framework SerialPort assembly or native call exists.
 public enum Parity { Even=2 } public enum StopBits { Two=2 }
 public class SerialPort : IDisposable
 {
  public static SerialPort? LastCreated;public static bool FailOpen,FalseOpen,FailClose;
  public SerialPort(){}
  public SerialPort(string name,int rate,Parity parity,int bits,StopBits stop)
  {PortName=name;BaudRate=rate;Parity=parity;DataBits=bits;StopBits=stop;IsOpen=false;LastCreated=this;}
  public string PortName="COM3"; public int BaudRate,DataBits; public Parity Parity; public StopBits StopBits;
  public bool IsOpen {get;set;}=true; public int WriteTimeout {get;set;}=-1;
  public int OpenCalls,CloseCalls,DisposeCalls;
  public string? FailValue;public ManualResetEventSlim? Release;
  public ConcurrentQueue<string> Writes {get;}=new();
  public void Open(){OpenCalls++;if(FailOpen)throw new IOException("Injected port busy");IsOpen=!FalseOpen;}
  public void Close(){CloseCalls++;IsOpen=false;if(FailClose)throw new IOException("Injected close failure");}
  public void Dispose(){DisposeCalls++;IsOpen=false;}
  public void Write(string value){Release?.Wait();Writes.Enqueue(value);if(value==FailValue)throw new IOException("Injected serial write failure");}
 }
}

namespace HandheldCompanion.Devices.OneXPlayer { public sealed class OneXPlayerApex : global::HandheldCompanion.Devices.OneXPlayerX1AMD { } }
