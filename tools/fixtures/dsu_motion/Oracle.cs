using System;
using System.Runtime.CompilerServices;
using System.IO;
using System.Text.Json;
using System.Collections.Generic;
using System.Numerics;
using System.Buffers.Binary;
using HandheldCompanion.DSU;
public enum ButtonFlags {DPadLeft,DPadDown,DPadRight,DPadUp,Start,RightStickClick,LeftStickClick,Back,B1,B2,B3,B4,R1,L1,Special,LeftPadClick,RightPadClick}
public enum AxisFlags {R2,L2,LeftStickX,LeftStickY,RightStickX,RightStickY}
public class ButtonState {public Dictionary<ButtonFlags,bool> Values=new();public bool this[ButtonFlags b]=>Values.GetValueOrDefault(b);}
public class AxisState {public Dictionary<AxisFlags,float> Values=new();public float this[AxisFlags a]=>Values.GetValueOrDefault(a);}
public class GyroState {public enum SensorState {DSU} public Vector3 G,A;public Vector3 GetGyroscope(SensorState s)=>G;public Vector3 GetAccelerometer(SensorState s)=>A;}
public class State {public ButtonState ButtonState=new();public AxisState AxisState=new();public GyroState GyroState=new();}
public class Touch {public bool IsActive=false;public int RawTrackingNum=0,X=0,Y=0;}
public static class DS4Touch {public static Touch LeftPadTouch=new(),RightPadTouch=new();}
public class GamepadMotion {public Vector3 G,A;public void GetRawGyro(out float x,out float y,out float z){x=G.X;y=G.Y;z=G.Z;}public void GetRawAcceleration(out float x,out float y,out float z){x=A.X;y=A.Y;z=A.Z;}}
public static class TimerManager {public static long GetElapsedMilliseconds()=>1234;/*@@TIME@@*/}
public static class Crc32Algorithm {public static uint Compute(byte[] bytes){uint v=0xffffffff;foreach(byte b in bytes){v^=b;for(int i=0;i<8;i++)v=(v&1)!=0?(v>>1)^0xedb88320:v>>1;}return v^0xffffffff;}}
public static class InputUtils {private const float SHORT_MAX_F=32767f,INV_USHORT_MAX=1f/65535f,BYTE_MAX_F=255f;/*@@NORMALIZE@@*/}
public class Oracle {
    private const ushort MaxProtocolVersion=1001;private static uint serverId=0;
    private static State Inputs=new();private static Dictionary<byte,GamepadMotion> GamepadMotions=new();
    /*@@REPORT@@*/
    /*@@BEGIN@@*/
    /*@@FINISH@@*/
    /*@@SWAP@@*/
    /*@@INVERT@@*/
    private enum Axis {X,Y,Z}
    private static Vector3 Matrix(JsonElement definition,float[] input){
        float[] output=new float[3];for(int i=0;i<3;i++)output[definition.GetProperty("swap")[i].GetInt32()]=input[i];
        for(int i=0;i<3;i++)output[i]*=definition.GetProperty("axis")[i].GetSingle();return new(output[0],output[1],output[2]);
    }
    private static string Packet(JsonElement row,byte slot){
        if(slot==3)return "";
        bool pad=!row.GetProperty("neutral").GetBoolean()&&!row.GetProperty("retirePhysical").GetBoolean();
        bool motion=pad&&row.GetProperty("motion").GetBoolean()&&!row.GetProperty("retireMotion").GetBoolean();
        var raw=row.GetProperty("raw");float[] g=new float[3],a=new float[3];for(int i=0;i<3;i++){g[i]=raw[i].GetSingle();a[i]=raw[i+3].GetSingle();}
        Vector3 rawG=Matrix(row.GetProperty("gyro"),g),rawA=Matrix(row.GetProperty("accel"),a);
        Vector3 dsG=rawG,dsA=rawA;
        if(row.GetProperty("yaw").GetBoolean()){SwapYawRoll(ref dsG);SwapYawRoll(ref dsA);}
        if(row.GetProperty("invertH").GetBoolean()){InvertAxis(ref dsG,Axis.Y,Axis.Z);InvertAxis(ref dsA,Axis.Y,Axis.Z);}
        if(row.GetProperty("invertV").GetBoolean()){InvertAxis(ref dsG,Axis.X,Axis.Y);InvertAxis(ref dsA,Axis.X,Axis.Y);}
        if(!motion){rawG=rawA=dsG=dsA=Vector3.Zero;}
        Inputs=new();Inputs.GyroState.G=dsG;Inputs.GyroState.A=dsA;GamepadMotions=new(){{0,new GamepadMotion{G=rawG,A=rawA}}};
        ushort b=pad?row.GetProperty("buttons").GetUInt16():(ushort)0;
        var flags=new[]{ButtonFlags.DPadLeft,ButtonFlags.DPadDown,ButtonFlags.DPadRight,ButtonFlags.DPadUp,ButtonFlags.Start,ButtonFlags.RightStickClick,ButtonFlags.LeftStickClick,ButtonFlags.Back,ButtonFlags.B1,ButtonFlags.B2,ButtonFlags.B3,ButtonFlags.B4,ButtonFlags.R1,ButtonFlags.L1,ButtonFlags.Special};
        ushort[] masks={4,2,8,1,16,128,64,32,0x1000,0x2000,0x4000,0x8000,512,256,1024};
        for(int i=0;i<flags.Length;i++)Inputs.ButtonState.Values[flags[i]]=(b&masks[i])!=0;
        var sticks=row.GetProperty("sticks");var axes=new[]{AxisFlags.LeftStickX,AxisFlags.LeftStickY,AxisFlags.RightStickX,AxisFlags.RightStickY};
        for(int i=0;i<4;i++)Inputs.AxisState.Values[axes[i]]=pad?sticks[i].GetSingle():0;
        Inputs.AxisState.Values[AxisFlags.L2]=pad?row.GetProperty("lt").GetSingle():0;Inputs.AxisState.Values[AxisFlags.R2]=pad?row.GetProperty("rt").GetSingle():0;
        byte[] output=new byte[100];int at=BeginPacket(output);
        BinaryPrimitives.WriteUInt32LittleEndian(output.AsSpan(at,4),(uint)MessageType.DSUS_PadDataRsp);at+=4;
        output[at++]=slot;output[at++]=2;output[at++]=2;output[at++]=1;for(int i=0;i<6;i++)output[at++]=(byte)(0x10+slot);output[at++]=5;output[at++]=1;
        BinaryPrimitives.WriteUInt32LittleEndian(output.AsSpan(at,4),0);at+=4;
        if(!ReportToBuffer(output,ref at,slot))return "";
        if(at!=100)throw new Exception("Fixed HC report extent changed: "+at);
        FinishPacket(output);return Convert.ToHexString(output).ToLowerInvariant();
    }
    public static void Main(string[] args){
        using var json=JsonDocument.Parse(File.ReadAllText(args[0]));var results=new List<object>();
        foreach(var row in json.RootElement.EnumerateArray()){var packets=new string[4];for(byte i=0;i<4;i++)packets[i]=Packet(row,i);results.Add(new{input=row.Clone(),packets});}
        File.WriteAllText(args[1],JsonSerializer.Serialize(results));Console.WriteLine("Fixed HC C# oracle cases: "+results.Count);
    }
}
