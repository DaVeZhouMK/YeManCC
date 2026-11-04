// Adapted from the complete frozen HC FanProfile numerical algorithm.
// Source SHA256: a8c8921b324020940fac42d4284c94f74139844b676d6d6beb1abb5fd1cce8a5
// Device Tjmax comes from the unique fixed-source catalog. No IDevice.GetCurrent,
// runtime assembly load, temperature reader, driver, timer or product entry point.
using System.Collections.Generic;

using System;
using System.Linq;

namespace YeManFanBackend.Capabilities
{
    public enum PinnedSourceFanMode
    {
        Hardware = 0,
        Software = 1,
    }

    [Serializable]
    public sealed class PinnedSourceFanCurve
    {
        public PinnedSourceFanMode fanMode = PinnedSourceFanMode.Hardware;

        // y-axis: fan speed (%) at 0,10,...,100°C
        public double[] fanSpeeds = { 20, 20, 20, 30, 40, 50, 70, 80, 90, 100, 100 };

        // smoothing buffer
        private List<double> avgTemp;

        // user-tunable
        private int aggressivity;

        // --- Tjmax handling (latch) ---
        private bool tjLatch;                     // latched at Tjmax
        private DateTime tjLastHitUtc;            // last time we hit Tjmax
        public double TjHysteresisC { get; set; } = 10.0;           // release when <= (Tjmax - this)
        public TimeSpan TjCooldown { get; set; } = TimeSpan.FromSeconds(10);
        public int TjReleaseSamples { get; set; } = 5;              // need N consecutive safe samples
        private int belowCount;                                     // counter while below threshold

        private readonly double sourceTjmax;
        private readonly Func<DateTime> utcNow;
        public PinnedSourceFanCurve(PinnedFanDescriptor source, Func<DateTime>? clock = null)
        {
            ArgumentNullException.ThrowIfNull(source);
            if (!ReferenceEquals(FanCapabilityCatalog.FindPinnedFanSource(source.ModelClass), source))
                throw new ArgumentException("Fan source is not registered", nameof(source));
            sourceTjmax = source.ThermalTripTemperatureC;
            utcNow = clock ?? (() => DateTime.UtcNow);
            this.avgTemp = [];
        }

        public PinnedSourceFanCurve(PinnedFanDescriptor source, double[] fanSpeeds,
            int aggressivity = 0, Func<DateTime>? clock = null) : this(source, clock)
        {
            SetFanSpeed(fanSpeeds);
            if (aggressivity != 0) SetAggressivity(aggressivity);
        }

        public PinnedSourceFanCurve WithCurve(double[] values)
        {
            var next = (PinnedSourceFanCurve)MemberwiseClone();
            next.avgTemp = new List<double>(avgTemp);
            next.SetFanSpeed((double[])values.Clone());
            return next;
        }

        public void SetAggressivity(int aggressivity)
        {
            if (aggressivity < 1 || aggressivity > 4)
                throw new ArgumentException("Aggressivity level must be within the range of 1 to 4");

            this.aggressivity = aggressivity;
        }

        public double GetFanSpeed()
        {
            double average = GetAverageTemperature();
            return GetFanSpeed(average);
        }

        public double GetFanSpeed(double temp)
        {
            // Clamp temp to our curve range
            if (double.IsNaN(temp)) return 0.0;
            temp = Math.Clamp(temp, 0.0, 100.0);

            // Trip latch if we hit or exceed Tjmax
            double tjmax = sourceTjmax;
            if (temp >= tjmax)
            {
                if (!tjLatch)
                {
                    tjLatch = true;
                    // Caller owns diagnostics; the pure curve never enters HC managers.
                }

                tjLastHitUtc = utcNow();
                belowCount = 0;
                return 100.0;
            }

            // If latched, require: (a) cooldown elapsed AND (b) temp <= Tjmax - hysteresis for N consecutive samples
            if (tjLatch)
            {
                bool cooldownOk = (utcNow() - tjLastHitUtc) >= TjCooldown;
                bool belowThreshold = temp <= (tjmax - TjHysteresisC);

                if (cooldownOk && belowThreshold)
                {
                    belowCount++;
                    if (belowCount >= TjReleaseSamples)
                    {
                        tjLatch = false;      // release
                        belowCount = 0;
                    }
                }
                else
                {
                    belowCount = 0;          // reset streak
                }

                if (tjLatch)
                    return 100.0;            // stay at max until released
            }

            // --- Correct interpolation on 0..100°C grid in 10°C steps ---
            // X-axis (temperatures): 0,10,20,...,100  => indices 0..10
            int lastIdx = fanSpeeds.Length - 1;

            // Bracket indices
            int i0 = Math.Min((int)Math.Floor(temp / 10.0), lastIdx);
            int i1 = Math.Min(i0 + 1, lastIdx);

            // Degenerate case (at 100°C or array of length 1)
            if (i0 == i1) return fanSpeeds[i0];

            double t0 = i0 * 10.0;
            double t1 = i1 * 10.0;
            double y0 = fanSpeeds[i0];
            double y1 = fanSpeeds[i1];

            double y = y0 + (temp - t0) * (y1 - y0) / (t1 - t0);
            return Math.Clamp(y, 0.0, 100.0);
        }

        public void SetFanSpeed(double[] fanSpeeds)
        {
            ArgumentNullException.ThrowIfNull(fanSpeeds);
            if (fanSpeeds.Any(value => !double.IsFinite(value)))
                throw new ArgumentException("Fan curve must contain finite percentages", nameof(fanSpeeds));
            if (fanSpeeds.Length != this.fanSpeeds.Length || fanSpeeds.Length == 0)
                throw new ArgumentException("Invalid input array");

            for (int i = 0; i < fanSpeeds.Length - 1; i++)
            {
                if (fanSpeeds[i] > fanSpeeds[i + 1])
                    throw new ArgumentException("Input array must be sorted in ascending order");
            }

            if (fanSpeeds[0] < 0 || fanSpeeds[^1] > 100)
                throw new ArgumentException("Fan speed values must be within the range of 0 to 100");

            this.fanSpeeds = fanSpeeds;
        }

        public void SetTemperature(double temp)
        {
            if (temp < 0 || temp > 100)
                return;

            // Keep at most `aggressivity` samples (1..4)
            if (this.avgTemp.Count >= aggressivity && this.avgTemp.Count > 0)
                this.avgTemp.RemoveAt(0); // drop oldest

            this.avgTemp.Add(temp);
        }

        public double GetAverageTemperature()
        {
            return this.avgTemp.Count != 0 ? this.avgTemp.Average() : 50.0;
        }
    }
}