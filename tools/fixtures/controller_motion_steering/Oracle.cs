using System;
using System.Globalization;
using System.IO;
using System.Numerics;
using System.Text.Json;
using System.Collections.Generic;

// These are minimal value stubs; the decision, Default-plane statements and
// LocalSpace statements are separately generated verbatim from fixed HC source.
internal enum SensorFamily { Windows, SerialUSBIMU, Controller }
internal enum SensorState { Default, GamepadMotion, DSU }
internal enum SteeringAxis { Roll, Yaw, Auto, Unknown = 999 }
internal sealed class Profile
{
    public SteeringAxis SteeringAxis;
    public float AccelerometerMultiplier;
    public float GyrometerMultiplier;
}
internal sealed class GyroState
{
    private readonly Vector3[] gyro = new Vector3[3];
    private readonly Vector3[] accel = new Vector3[3];
    public Vector3 GetGyroscope(SensorState state) => gyro[(int)state];
    public Vector3 GetAccelerometer(SensorState state) => accel[(int)state];
    public ref Vector3 GetGyroscopeRef(SensorState state) => ref gyro[(int)state];
    public ref Vector3 GetAccelerometerRef(SensorState state) => ref accel[(int)state];
}
internal sealed class ControllerState { public GyroState GyroState = new GyroState(); }
internal sealed class OracleRow
{
    public string id {get;set;}
    public bool yaw {get;set;}
    public string resolved {get;set;}
    public bool windowsDefaultYaw {get;set;}
    public int[] windowsShortContribution {get;set;}
    public float[] defaultGyro {get;set;}
    public float[] defaultAccel {get;set;}
    public float[] local {get;set;}
    public int[] shortContribution {get;set;}
}
internal static partial class MotionOracle
{
    private static SensorFamily sensorSelection;
    private static float Parse(string s) => float.Parse(s, CultureInfo.InvariantCulture);
    private static SteeringAxis Axis(string value) => value switch {
        "roll" => SteeringAxis.Roll, "yaw" => SteeringAxis.Yaw,
        "auto" => SteeringAxis.Auto, _ => SteeringAxis.Unknown
    };
    public static int Main(string[] args)
    {
        var rows = new List<OracleRow>();
        foreach (var line in File.ReadLines(args[0]))
        {
            var p = line.Split('\t');
            sensorSelection = Enum.Parse<SensorFamily>(p[1]);
            var current = new Profile { SteeringAxis = Axis(p[2]),
                AccelerometerMultiplier = Parse(p[6]), GyrometerMultiplier = Parse(p[7]) };
            var state = new ControllerState();
            state.GyroState.GetAccelerometerRef(SensorState.GamepadMotion) = new Vector3(Parse(p[3]), Parse(p[4]), Parse(p[5]));
            state.GyroState.GetGyroscopeRef(SensorState.GamepadMotion) = new Vector3(Parse(p[8]), Parse(p[9]), Parse(p[10]));
            var row = RunExactPlane(current, state);
            var saved = sensorSelection; sensorSelection = SensorFamily.Windows;
            var legacy = RunExactPlane(current, state);
            sensorSelection = saved;
            row.windowsDefaultYaw = legacy.yaw;
            row.windowsShortContribution = legacy.shortContribution;
            row.id = p[0]; rows.Add(row);
        }
        Console.WriteLine(JsonSerializer.Serialize(rows));
        return 0;
    }
}
