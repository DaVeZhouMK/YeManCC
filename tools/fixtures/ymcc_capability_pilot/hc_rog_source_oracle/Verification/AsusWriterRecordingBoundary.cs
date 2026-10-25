using Ymcc.CapabilityPilot.HcReference;

namespace YeManFanBackend.Asus;

// Recording-only terminal boundary for the linked actual writer; never include this file in a product.
public enum AsusFan { CPU = 0, GPU = 1, Mid = 2 }

// Compile-only receipt token for the actual writer's observed overloads; no hardware success semantics.
public sealed class AsusAcpiCallReceipt { }

internal static class AsusACPI
{
    internal static int SetFanCurveObserved(AsusFan fan, byte[] curve, out AsusAcpiCallReceipt receipt)
    {
        receipt = new AsusAcpiCallReceipt();
        return CurveWriteCapture.Record(fan.ToString(), curve);
    }
}