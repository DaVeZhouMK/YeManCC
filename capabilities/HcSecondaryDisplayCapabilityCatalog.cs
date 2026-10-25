// AI-NAV[inactive-secondary-display]: This retained source is indexed for gap review only; check ACTIVE-INTEGRATION-SCOPE.json first.
// SECONDARY-DISPLAY-CONSUMPTION.json and the existing consumer retain the disabled-entry boundary.
// Do not activate UI, protocol or hardware writes from a registered method or compiled helper.
using System;
using System.Collections.Generic;

namespace YeManFanBackend.Capabilities;

// Actual consumer registration is separate from runtime EC readiness and ownership.
public static class HcSecondaryDisplayCapabilityCatalog
{
    public const string Domain = "model.secondary-display";
    public const string ModelId = "hc.device.AYANEOFlipDS";
    public const string DeviceClass = "HandheldCompanion.Devices.AYANEOFlipDS";
    public const string Model1SId = "hc.device.AYANEOFlip1SDS";
    public const string Model1SFactoryType = "HandheldCompanion.Devices.AYANEO.AYANEOFlip1SDS";
    public const string Model1SSourceSha256 = "78068b98db582805b83ab9d134221e0edbfb0921e5b737fe416dd704aa3773d0";
    public const string Capability1SId = "model.secondary-display.ayaneo-flip1s-ds.ec-brightness";
    public const string CapabilityId = "model.secondary-display.ayaneo-flip-ds.ec-brightness";
    public const string FrozenSourceSha256 = "307fa78aac6f823ecb44ef8797dfcc8c19d9f451bb1a7707b6a70ed5dd69a5b6";
    public const string DefaultSettingsSha256 = "7d2a4f92e798b1984dc08cadfa2940104799aeb7913b3879a0b0a6d64d99d4f3";
    public const ushort BrightnessRegister = 0x4E, LatchRegister = 0x4F;
    public const byte LatchValue = 0, DefaultRaw = 255;
    public static byte Encode(short brightness) => (byte)(Math.Clamp((int)brightness, 0, 100) * 255 / 100);
    public static int DescribePercent(byte raw) => (raw * 100 + 127) / 255;
    public static HcCapabilityDispatcher<T> Bind<T>(T consumer) where T : class =>
        HcDeviceCapabilityCatalog.Bind(Domain, new Dictionary<string,T>(StringComparer.Ordinal) { [ModelId] = consumer, [Model1SId] = consumer });
    public static HcCapabilityBinding<T>? Resolve<T>(HcCapabilityDispatcher<T> dispatcher,
        string manufacturer, string boardProduct) where T : class
    {
        var maker = manufacturer.Trim().ToUpperInvariant();
        if(maker != "AYADEVICE" && maker != "AYANEO")return null;
        string board=boardProduct.Trim().ToUpperInvariant();
        string? model=board=="FLIP DS"?ModelId:board=="FLIP 1S DS"?Model1SId:null;
        if(model is null)return null;
        var baseDevice=HcDeviceCapabilityCatalog.Find(ModelId);
        if(baseDevice?.SourceSha256!=FrozenSourceSha256||baseDevice.FactoryType!=DeviceClass)return null;
        var binding=dispatcher.Resolve(model);
        return binding is not null && binding.Device.DeviceId==model &&
            binding.Device.FactoryType==(model==ModelId?DeviceClass:Model1SFactoryType) &&
            binding.Device.SourceSha256==(model==ModelId?FrozenSourceSha256:Model1SSourceSha256)?binding:null;
    }
    public static string CapabilityFor(HcDeviceDescriptor device)=>device.DeviceId==ModelId?CapabilityId:
        device.DeviceId==Model1SId?Capability1SId:throw new InvalidOperationException("Unregistered secondary display");
}
