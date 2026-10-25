using System.Numerics;
using System.Text.Json;
using HandheldCompanion.Devices;
using HandheldCompanion.Helpers;
using HandheldCompanion.Models;
internal static class CatalogFixture {
    static float[][] Samples = [[1,2,3],[1,0,0],[0,1,0],[0,0,1],[-7.25f,2.5f,-0.125f],[0,0,0]];
    static object Matrix(IMUMatrix m,bool gyro) {
        var gyroSensor=new NumericGyro();var accelSensor=new NumericAccel();
        var vectors=Samples.Select(raw=>gyro?gyroSensor.Map(raw):accelSensor.Map(raw)).ToArray();
        return new{axis=new[]{m.Axis.X,m.Axis.Y,m.Axis.Z},swap=m.AxisRemapIndices,vectors};
    }
    static int Main(string[] args) {
        try {
            var rows=new List<object>();
            foreach(var name in JsonSerializer.Deserialize<string[]>(File.ReadAllText(args[0]))!) {
                HandheldCompanion.Shared.LogManager.Messages.Clear();
                var type=typeof(IDevice).Assembly.GetType(name,true)!;
                var device=(IDevice)Activator.CreateInstance(type)!;
                IDevice.NumericCurrent=device;
                rows.Add(new{name,gyro=Matrix(device.GyroMatrix,true),accel=Matrix(device.AcceleroMatrix,false),messages=HandheldCompanion.Shared.LogManager.Messages.ToArray()});
            }
            File.WriteAllText(args[1],JsonSerializer.Serialize(rows,new JsonSerializerOptions{WriteIndented=true}));
            Console.WriteLine(JsonSerializer.Serialize(new{status="pass",types=rows.Count,actualConfigurationHelper=true,actualMatrixConversion=true,realDeviceConstructors=false,hardwareIo=false}));return 0;
        } catch(Exception e) {Console.Error.WriteLine(e);return 2;}
    }
}
namespace HandheldCompanion.Shared {
    public static class LogManager {
        public static List<string> Messages=new();
        public static void LogDebug(string text,params object[] values)=>Messages.Add(text);
        public static void LogError(string text,params object[] values)=>Messages.Add(text);
    }
}
