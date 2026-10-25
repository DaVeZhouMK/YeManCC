#nullable enable
// Offline seam only: inputs are ALREADY DECODED dictionaries. No HID packet
// decoder, transport, device API, HC assembly, manager or Windows API is loaded.
using System;
using System.Collections.Generic;
using System.Text.Json;
using HandheldCompanion.Inputs;

public sealed class Values<K,V> where K:notnull {
    readonly Dictionary<K,V> data=new();
    public V this[K k] { get=>data.TryGetValue(k,out var v)?v:default!; set=>data[k]=value; }
    public void Clear()=>data.Clear();
    public void Copy(Values<K,V> other){Clear();foreach(var p in other.data)data[p.Key]=p.Value;}
}
public sealed class ButtonState:ValuesBase {
    readonly Values<ButtonFlags,bool> values=new();
    public bool this[ButtonFlags k]{get=>values[k];set=>values[k]=value;}
    public static void Overwrite(ButtonState from,ButtonState to)=>to.values.Copy(from.values);
}
public class ValuesBase { }
public sealed class GyroState {
    public float[] Gyro=new float[3],Accel=new float[3];
    public void SetGyroscope(float x,float y,float z)=>Gyro=new[]{x,y,z};
    public void SetAccelerometer(float x,float y,float z)=>Accel=new[]{x,y,z};
}
public sealed class Output {
    public ButtonState ButtonState=new();
    public Values<AxisFlags,short> AxisState=new();
    public GyroState GyroState=new();
}
public sealed class GamepadMotion { public int Count;public void ProcessMotion(float gx,float gy,float gz,float ax,float ay,float az,float dt)=>Count++; }
public static class Gamepad { public const byte TriggerThreshold=30; }
public sealed class DecodedState {
    public Values<NeptuneControllerButton,bool> ButtonState=new();
    public Values<NeptuneControllerAxis,short> AxesState=new();
}
public sealed class NeptuneControllerInputEventArgs { public DecodedState State=new(); }
public sealed class FakeController {
    public event Action<NeptuneControllerInputEventArgs>? OnControllerInputReceived;
    public int HandlerCount=>OnControllerInputReceived?.GetInvocationList().Length??0;
    public List<string> Trace=new();
    public void Open()=>Trace.Add("open");
    public void Close()=>Trace.Add("close");
    public void EndRead()=>Trace.Add("end-read");
    public void RequestLizardMode(bool enabled)=>Trace.Add(enabled?"lizard:true":"lizard:false");
    public void Emit(NeptuneControllerInputEventArgs e)=>OnControllerInputReceived?.Invoke(e);
}
public class BaseController {
    public Output? Inputs=new();public bool IsBusy,IsPlugged=true,_disposing,_disposed;
    public ButtonState InjectedButtons=new();public float aX,aY,aZ,gX,gY,gZ;
    public Dictionary<int,GamepadMotion> gamepadMotions=new(){{0,new()}};public int gamepadIndex;
    public int Commits;public virtual void Tick(long ticks,float delta,bool commit=true)=>Commits++;
    public virtual void Gone() { }
}
public partial class Subject:BaseController {
    public FakeController? Controller=new();private NeptuneControllerInputEventArgs? input;
    private const short TrackPadInner=21844;private readonly object hidLock=new();
    // @@PRODUCTION_METHODS@@
    public void OpenSeam()=>Open();public void CloseSeam()=>Close();
}
public static class Entry {
    static readonly List<object> checks=new();
    static void Check(string name,bool ok){checks.Add(new{name,pass=ok});if(!ok)throw new Exception(name);}
    static void Near(string name,float got,float expected)=>Check(name,Math.Abs(got-expected)<0.0003f);
    static Subject Ready(NeptuneControllerInputEventArgs e){var s=new Subject();s.OpenSeam();s.Controller!.Emit(e);return s;}
    public static int Main(){try{
        var empty=new Subject();empty.Tick(0,.004f,true);Check("no decoded event blocks publication",empty.Commits==0);
        var e=new NeptuneControllerInputEventArgs();
        e.State.ButtonState[NeptuneControllerButton.BtnSteam]=true;
        var s=Ready(e);s.Tick(0,.004f,true);
        Check("Steam maps Special",s.Inputs!.ButtonState[ButtonFlags.Special]);
        Check("Steam does not map OEM1/OEM2 or Special2",!s.Inputs.ButtonState[ButtonFlags.OEM1]&&!s.Inputs.ButtonState[ButtonFlags.OEM2]&&!s.Inputs.ButtonState[ButtonFlags.Special2]);
        e=new();e.State.ButtonState[NeptuneControllerButton.BtnQuickAccess]=true;s.Controller!.Emit(e);s.Tick(1,.004f,true);
        Check("QuickAccess maps Special2 independently",s.Inputs.ButtonState[ButtonFlags.Special2]&&!s.Inputs.ButtonState[ButtonFlags.Special]);
        var pairs=new[]{(NeptuneControllerButton.BtnL4,ButtonFlags.L4),(NeptuneControllerButton.BtnR4,ButtonFlags.R4),(NeptuneControllerButton.BtnL5,ButtonFlags.L5),(NeptuneControllerButton.BtnR5,ButtonFlags.R5)};
        foreach(var pair in pairs){e=new();e.State.ButtonState[pair.Item1]=true;s.Controller.Emit(e);s.Tick(2,.004f,true);foreach(var other in pairs)Check($"rear {pair.Item2} isolate {other.Item2}",s.Inputs.ButtonState[other.Item2]==(pair.Item2==other.Item2));}
        e=new();e.State.AxesState[NeptuneControllerAxis.LeftPadX]=1234;e.State.AxesState[NeptuneControllerAxis.RightPadY]=-2345;s.Controller.Emit(e);s.Tick(3,.004f,true);
        Check("untouched pads zero despite nonzero decoded axes",s.Inputs.AxisState[AxisFlags.LeftPadX]==0&&s.Inputs.AxisState[AxisFlags.RightPadY]==0);
        e.State.ButtonState[NeptuneControllerButton.BtnLPadTouch]=true;e.State.ButtonState[NeptuneControllerButton.BtnLPadPress]=true;
        e.State.ButtonState[NeptuneControllerButton.BtnRPadTouch]=true;e.State.ButtonState[NeptuneControllerButton.BtnRPadPress]=true;
        e.State.AxesState[NeptuneControllerAxis.LeftPadX]=21844;e.State.AxesState[NeptuneControllerAxis.LeftPadY]=-21844;
        e.State.AxesState[NeptuneControllerAxis.RightPadX]=-21844;e.State.AxesState[NeptuneControllerAxis.RightPadY]=21844;s.Tick(4,.004f,true);
        Check("touch axes preserved",s.Inputs.AxisState[AxisFlags.LeftPadX]==21844&&s.Inputs.AxisState[AxisFlags.RightPadY]==21844);
        Check("left threshold includes right/down edge",s.Inputs.ButtonState[ButtonFlags.LeftPadClickRight]&&s.Inputs.ButtonState[ButtonFlags.LeftPadClickDown]);
        Check("right threshold includes left/up edge",s.Inputs.ButtonState[ButtonFlags.RightPadClickLeft]&&s.Inputs.ButtonState[ButtonFlags.RightPadClickUp]);
        e.State.AxesState[NeptuneControllerAxis.LeftPadX]=21843;s.Tick(5,.004f,true);Check("below directional edge does not click right",!s.Inputs.ButtonState[ButtonFlags.LeftPadClickRight]);
        e=new();s.Controller.Emit(e);s.Tick(6,.004f,true);Check("pad release clears axes and directions",s.Inputs.AxisState[AxisFlags.LeftPadX]==0&&!s.Inputs.ButtonState[ButtonFlags.LeftPadClickDown]&&!s.Inputs.ButtonState[ButtonFlags.RightPadClickUp]);
        e.State.AxesState[NeptuneControllerAxis.GyroAccelX]=16384;e.State.AxesState[NeptuneControllerAxis.GyroAccelY]=32767;e.State.AxesState[NeptuneControllerAxis.GyroAccelZ]=-8192;
        e.State.AxesState[NeptuneControllerAxis.GyroPitch]=8192;e.State.AxesState[NeptuneControllerAxis.GyroRoll]=-16384;e.State.AxesState[NeptuneControllerAxis.GyroYaw]=4096;s.Tick(7,.004f,true);
        Near("accel X in g",s.Inputs.GyroState.Accel[0],1.0000305f);Near("accel Z becomes Y",s.Inputs.GyroState.Accel[1],-.50001526f);Near("accel negative Y becomes Z",s.Inputs.GyroState.Accel[2],-2f);
        Near("pitch becomes gyro X dps",s.Inputs.GyroState.Gyro[0],500.01526f);Near("roll becomes gyro Y dps",s.Inputs.GyroState.Gyro[1],-1000.0305f);Near("negative yaw becomes gyro Z dps",s.Inputs.GyroState.Gyro[2],-250.00763f);
        e.State.AxesState[NeptuneControllerAxis.GyroAccelX]=short.MinValue;s.Tick(8,.004f,true);Near("int16 minimum HC scale exceeds -2g slightly",s.Inputs.GyroState.Accel[0],-2.000061f);
        int commits=s.Commits;s.IsBusy=true;s.Tick(9,.004f,true);Check("busy guard",s.Commits==commits);s.IsBusy=false;s.IsPlugged=false;s.Tick(9,.004f,true);Check("unplugged guard",s.Commits==commits);s.IsPlugged=true;s._disposing=true;s.Tick(9,.004f,true);Check("disposing guard",s.Commits==commits);
        var lifecycle=new Subject();var controller=lifecycle.Controller!;lifecycle.OpenSeam();Check("subscribe before fake input",controller.HandlerCount==1);Check("Open requests Lizard off",string.Join(",",controller.Trace)=="open,lizard:false");lifecycle.CloseSeam();Check("Close unsubscribes",controller.HandlerCount==0);Check("Close restores Lizard before transport close",string.Join(",",controller.Trace)=="open,lizard:false,lizard:true,close");
        var gone=new Subject();var gc=gone.Controller!;gone.OpenSeam();gc.Trace.Clear();gone.Gone();Check("Gone EndRead only; no restore claim",string.Join(",",gc.Trace)=="end-read"&&gone.Controller==null&&gc.HandlerCount==0);
        var duplicate=new Subject();duplicate.OpenSeam();duplicate.OpenSeam();int subscriptions=duplicate.Controller!.HandlerCount;duplicate.CloseSeam();int remaining=duplicate.Controller!.HandlerCount;
        Console.WriteLine(JsonSerializer.Serialize(new{pass=true,checks=checks.Count,results=checks,scope="Exact fixed HC wrapper methods; already-decoded state and fake transport only; no packet/HID/hardware proof",observations=new{repeatedOpenSubscriptions=subscriptions,remainingAfterOneClose=remaining,productionIdempotenceNotProven=true}}));return 0;
    }catch(Exception ex){Console.WriteLine(JsonSerializer.Serialize(new{pass=false,checks=checks.Count,results=checks,error=ex.Message}));return 1;}}
}
