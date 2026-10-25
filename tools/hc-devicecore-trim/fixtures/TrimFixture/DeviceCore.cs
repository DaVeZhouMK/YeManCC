using System.Resources;
[assembly:NeutralResourcesLanguage("en")]
namespace TrimFixture;
public static class DeviceCore
{
    public static readonly byte[] Calibration = [1,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53];
    static int calls;
    public static int Transform(int value)
    {
        try { return value switch { < 0 => throw new ArgumentOutOfRangeException(nameof(value)), 0=>Calibration[2], 1=>17, _=>checked(value*3+Calibration[value%Calibration.Length]) }; }
        catch(ArgumentOutOfRangeException) { return -7; }
        finally { calls++; }
    }
    public static T Echo<T>(T value)=>value;
    public static int Calls=>calls;
}
public static class UiOnly { public static string ImageResourceName=>"ui/art.png"; }
