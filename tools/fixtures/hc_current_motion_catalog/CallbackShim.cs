using System.Numerics;
using HandheldCompanion.Devices;
using Windows.Devices.Sensors;
internal struct NumericReading { public Vector3 reading; public double timestamp; }
internal class NumericSensorBase {
    protected double[] readingAxis=new double[3];
    protected NumericReading reading;
    protected double threshold=double.MaxValue;
    protected void ReadingChanged() { }
}
internal class NumericGyro : NumericSensorBase {
    // @@GYRO_CALLBACK@@
    internal float[] Map(float[] raw) {
        ReadingChanged(new Gyrometer(),new GyrometerReadingChangedEventArgs {
            Reading=new GyrometerReading {AngularVelocityX=raw[0],AngularVelocityY=raw[1],AngularVelocityZ=raw[2],Timestamp=DateTimeOffset.UnixEpoch}});
        return [reading.reading.X,reading.reading.Y,reading.reading.Z];
    }
}
internal class NumericAccel : NumericSensorBase {
    // @@ACCEL_CALLBACK@@
    internal float[] Map(float[] raw) {
        ReadingChanged(new Accelerometer(),new AccelerometerReadingChangedEventArgs {
            Reading=new AccelerometerReading {AccelerationX=raw[0],AccelerationY=raw[1],AccelerationZ=raw[2],Timestamp=DateTimeOffset.UnixEpoch}});
        return [reading.reading.X,reading.reading.Y,reading.reading.Z];
    }
}
namespace Windows.Devices.Sensors {
    internal class Gyrometer { }
    internal class Accelerometer { }
    internal class GyrometerReading {public double AngularVelocityX,AngularVelocityY,AngularVelocityZ;public DateTimeOffset Timestamp;}
    internal class AccelerometerReading {public double AccelerationX,AccelerationY,AccelerationZ;public DateTimeOffset Timestamp;}
    internal class GyrometerReadingChangedEventArgs {public GyrometerReading Reading=new();}
    internal class AccelerometerReadingChangedEventArgs {public AccelerometerReading Reading=new();}
}
