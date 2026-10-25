// Only the external terminal/base is replaced. Both complete frozen CEc/CEii
// product classes are compiled unchanged by this existing fixture project.
using System.IO;
using System.Windows.Media;
using HandheldCompanion.Inputs;
using static HandheldCompanion.Utils.DeviceUtils;
namespace HandheldCompanion.Utils
{
 public static partial class DeviceUtils
 {
  [Flags] public enum LEDLevel { None=0,SolidColor=1,Rainbow=4 }
 }
}
namespace HandheldCompanion.Commands.Functions.HC { public sealed class MainWindowCommands{} public sealed class QuickToolsCommands{} }
namespace WindowsInput.Events { public enum KeyCode { RControlKey,LControl,LWin,F17,D,F15,F16 } }
namespace HandheldCompanion.Inputs
{
 public enum ButtonFlags { OEM1,OEM2,OEM3,OEM4 }
 public sealed class KeyboardChord { public KeyboardChord(string name,WindowsInput.Events.KeyCode[] down,WindowsInput.Events.KeyCode[] up,bool flag,ButtonFlags button){} }
}
namespace HandheldCompanion.Managers
{
 public static class ManagerFactory { public static readonly FixtureSettings settingsManager=new(); }
 public sealed class FixtureSettings
 {
  public string GetString(string key)=>throw new InvalidOperationException("Settings access forbidden in lighting fixture");
  public bool GetBoolean(string key)=>throw new InvalidOperationException("Settings access forbidden in lighting fixture");
  public int GetInt(string key)=>throw new InvalidOperationException("Settings access forbidden in lighting fixture");
 }
}
namespace Windows.System.Power
{
 public static class PowerManager
 {
  public static event EventHandler<object> RemainingChargePercentChanged { add{} remove{} }
  public static int RemainingChargePercent=>throw new InvalidOperationException("Power query forbidden in lighting fixture");
 }
}
namespace HandheldCompanion.Devices.AYANEO
{
 [Flags] public enum DeviceCapabilities { FanControl=1,DynamicLighting=2,DynamicLightingBrightness=4,DynamicLightingSecondLEDColor=8,BatteryChargeLimit=16,BatteryChargeLimitPercent=32 }
 public sealed class ECDetails { public ushort AddressStatusCommandPort,AddressDataPort,AddressFanControl,AddressFanDuty;public int FanValueMin,FanValueMax; }
 public sealed class FixtureMatrix { public System.Numerics.Vector3 Axis;public SortedDictionary<char,char> AxisSwap=[]; }
 public sealed class FixtureChord { public Dictionary<ButtonFlags,bool> ButtonState=[]; }
 public sealed class FixtureHotkey { public FixtureChord inputsChord=new(); }
 public record FixtureEcWrite(ushort Address,byte Value);
 public class AYANEODevice
 {
  protected enum LEDGroup { StickLeft=1,StickRight=2,StickBoth=3,AYA=4 }
  protected readonly object updateLock=new();
  protected bool UseOpenLib=true;public bool IsOpen=true;
  protected DeviceCapabilities Capabilities;
  protected string ProductIllustration="",ProductModel="";protected double[] nTDP=[],cTDP=[],GfxClock=[];protected int CpuClock;
  protected LEDLevel DynamicLightingCapabilities;
  protected ECDetails ECDetails=new();protected FixtureMatrix GyroMatrix=new(),AcceleroMatrix=new();
  protected List<KeyboardChord> OEMChords=[];
  protected Dictionary<Type,FixtureHotkey> DeviceHotkeys=new(){[typeof(HandheldCompanion.Commands.Functions.HC.MainWindowCommands)]=new(),[typeof(HandheldCompanion.Commands.Functions.HC.QuickToolsCommands)]=new()};
  public List<FixtureEcWrite> Writes {get;}=[];public ushort? FailAddress;public int BaseOpenCalls,FanWrites;
  public virtual bool Open(){BaseOpenCalls++;return true;}
  public virtual void OpenEvents(){} public virtual void Close(){FanWrites++;}
  protected virtual void QuerySettings(){}
  protected virtual void SettingsManager_SettingValueChanged(string name,object? value,bool temporary,bool initializing){}
  public virtual void SetFanDuty(double value){FanWrites++;}public virtual void SetFanControl(bool value,int mode=0){FanWrites++;}
  public virtual bool SetLedColor(Color first,Color second,LEDLevel level,int speed=100)=>true;
  public virtual bool SetLedBrightness(int value)=>true;public virtual bool SetLedStatus(bool value)=>true;
  protected virtual void EcWriteByte(byte register,byte value){Write(register,value);}
  protected virtual byte EcReadByte(byte register)=>0;
  protected virtual byte ECRamDirectReadByte(ushort register,ECDetails details)=>0;
  protected virtual bool ECRamDirectWriteByte(ushort register,ECDetails details,byte value){Write(register,value);return true;}
  private void Write(ushort address,byte value){Writes.Add(new(address,value));if(FailAddress==address)throw new IOException("Injected AYANEO EC write failure");}
 }
}

namespace HandheldCompanion.Devices.AYANEO
{
 public sealed class ChangedAyaneoLightingFixture : AYANEODeviceCEc
 { public override bool SetLedColor(Color first,Color second,LEDLevel level,int speed=100)=>base.SetLedColor(first,second,level,speed); }
}
