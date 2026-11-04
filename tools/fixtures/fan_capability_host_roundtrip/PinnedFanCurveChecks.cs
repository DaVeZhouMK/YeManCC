using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using YeManFanBackend.Capabilities;

internal static class PinnedFanCurveChecks
{
    public enum ReferenceFanMode { Hardware=0, Software=1 }
    private static DateTime ClockUtcNow;
    private sealed class IDevice
    {
        internal static int Calls;
        internal uint Tjmax=95;
        internal static IDevice GetCurrent(){Calls++;return new();}
    }
    private static class LogManager { internal static void LogCritical(string message, double value) { } }
    private static void Reject(Action action,Action<bool,string> check,string name)
    {bool rejected=false;try{action();}catch(ArgumentException){rejected=true;}check(rejected,name);}
    internal static void Run(Action<bool,string> check)
    {
        double[] points={0,0,10,20,30,40,50,60,70,80,100};
        foreach(var source in FanCapabilityCatalog.PinnedSourceDeclarations)
        {
            ClockUtcNow=new DateTime(2026,11,2,0,0,0,DateTimeKind.Utc);
            var actual=new PinnedSourceFanCurve(source,points,clock:()=>ClockUtcNow);
            var oracle=new ReferenceFanProfile((double[])points.Clone(),0);
            check(source.ThermalTripTemperatureC==95,"pinned-curve:source-ancestry-tj95-"+source.DeviceId);
            bool equal=true;
            for(int half=0;half<190;half++)
                equal &= actual.GetFanSpeed(half/2d)==oracle.GetFanSpeed(half/2d);
            check(equal,"pinned-curve:all-half-degree-below-trip-matches-whole-source-"+source.DeviceId);
            check(actual.GetFanSpeed(95)==oracle.GetFanSpeed(95)&&actual.GetFanSpeed(95)==100,"pinned-curve:exact-source-trip-latches100-"+source.DeviceId);
            ClockUtcNow=ClockUtcNow.AddSeconds(9.999);
            for(int i=0;i<5;i++) equal &= actual.GetFanSpeed(84)==oracle.GetFanSpeed(84);
            check(equal&&actual.GetFanSpeed(84)==100,"pinned-curve:cooldown-not-released-early-"+source.DeviceId);
            ClockUtcNow=ClockUtcNow.AddSeconds(0.001);
            for(int i=0;i<4;i++) equal &= actual.GetFanSpeed(85)==oracle.GetFanSpeed(85);
            check(equal&&actual.GetFanSpeed(85)==oracle.GetFanSpeed(85)&&actual.GetFanSpeed(85)<100,"pinned-curve:five-postcooldown-safe-samples-release-"+source.DeviceId);
            check(actual.GetFanSpeed(100)==oracle.GetFanSpeed(100)&&actual.GetFanSpeed(90)==oracle.GetFanSpeed(90),"pinned-curve:trip-rearms-and-hysteresis-holds-"+source.DeviceId);
        }
        {
            ClockUtcNow=new DateTime(2026,11,2,2,0,0,DateTimeKind.Utc);
            var actual=new PinnedSourceFanCurve(FanCapabilityCatalog.PinnedSourceDeclarations[0],points,clock:()=>ClockUtcNow);
            var oracle=new ReferenceFanProfile((double[])points.Clone(),0);
            _=actual.GetFanSpeed(95);_=oracle.GetFanSpeed(95);ClockUtcNow=ClockUtcNow.AddSeconds(5);
            double[] changed={0,5,15,25,35,45,55,65,75,90,100};
            var clone=actual.WithCurve(changed);oracle.SetFanSpeed(changed);
            check(clone.GetFanSpeed(85)==oracle.GetFanSpeed(85)&&clone.GetFanSpeed(85)==100,"pinned-curve:editing-preserves-original-thermal-latch");
            check(actual.fanSpeeds.SequenceEqual(points),"pinned-curve:edited-curve-does-not-mutate-old-owner-array");
            ClockUtcNow=ClockUtcNow.AddSeconds(10);bool equal=true;
            for(int i=0;i<5;i++)equal&=clone.GetFanSpeed(85)==oracle.GetFanSpeed(85);
            check(equal&&clone.GetFanSpeed(85)<100,"pinned-curve:edited-curve-preserves-original-release-counter-and-time");
        }
        var descriptor=FanCapabilityCatalog.PinnedSourceDeclarations[0];
        for(int aggression=0;aggression<=4;aggression++)
        {
            ClockUtcNow=new DateTime(2026,11,2,1,0,0,DateTimeKind.Utc);
            var actual=new PinnedSourceFanCurve(descriptor,points,aggression,()=>ClockUtcNow);
            var oracle=new ReferenceFanProfile((double[])points.Clone(),aggression);bool equal=true;
            foreach(var sample in new double[]{20,40,50,60,80,70,30,90,10})
            {actual.SetTemperature(sample);oracle.SetTemperature(sample);equal &= actual.GetAverageTemperature()==oracle.GetAverageTemperature()&&actual.GetFanSpeed()==oracle.GetFanSpeed();}
            check(equal,"pinned-curve:original-smoothing-window-"+aggression);
        }
        {
            var actual=new PinnedSourceFanCurve(descriptor,points);
            Reject(()=>actual.SetFanSpeed(new double[10]),check,"pinned-curve:wrong-point-count-refused");
            Reject(()=>actual.SetFanSpeed(new double[]{0,1,2,3,4,5,6,7,9,8,100}),check,"pinned-curve:descending-point-refused");
            Reject(()=>actual.SetFanSpeed(new double[]{0,1,2,3,4,5,6,7,8,9,101}),check,"pinned-curve:over100-refused");
            Reject(()=>actual.SetFanSpeed(Enumerable.Repeat(double.NaN,11).ToArray()),check,"pinned-curve:nonfinite-never-reaches-duty");
            Reject(()=>actual.SetAggressivity(5),check,"pinned-curve:over4-smoothing-refused");
            check(IDevice.Calls>0,"pinned-curve:source-oracle-used-explicit-factory-seam");
        }
    }

    [Serializable]
    public class ReferenceFanProfile
    {
        public ReferenceFanMode fanMode = ReferenceFanMode.Hardware;

        // y-axis: fan speed (%) at 0,10,...,100°C
        public double[] fanSpeeds = { 20, 20, 20, 30, 40, 50, 70, 80, 90, 100, 100 };

        // smoothing buffer
        private ConcurrentList<double> avgTemp;

        // user-tunable
        private int aggressivity;

        // --- Tjmax handling (latch) ---
        private bool tjLatch;                     // latched at Tjmax
        private DateTime tjLastHitUtc;            // last time we hit Tjmax
        public double TjHysteresisC { get; set; } = 10.0;           // release when <= (Tjmax - this)
        public TimeSpan TjCooldown { get; set; } = TimeSpan.FromSeconds(10);
        public int TjReleaseSamples { get; set; } = 5;              // need N consecutive safe samples
        private int belowCount;                                     // counter while below threshold

        public ReferenceFanProfile()
        {
            this.avgTemp = [];
        }

        public ReferenceFanProfile(double[] fanSpeeds, int aggressivity) : this()
        {
            SetFanSpeed(fanSpeeds);
            this.aggressivity = aggressivity;
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
            double tjmax = IDevice.GetCurrent().Tjmax;
            if (temp >= tjmax)
            {
                if (!tjLatch)
                {
                    tjLatch = true;
                    LogManager.LogCritical("Cpu temperature has reached TJmax ({0})", tjmax);
                }

                tjLastHitUtc = ClockUtcNow;
                belowCount = 0;
                return 100.0;
            }

            // If latched, require: (a) cooldown elapsed AND (b) temp <= Tjmax - hysteresis for N consecutive samples
            if (tjLatch)
            {
                bool cooldownOk = (ClockUtcNow - tjLastHitUtc) >= TjCooldown;
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

public class ConcurrentList<T> : IList<T>, IDisposable
{
    private T[] _arr;
    private int _count;
    private readonly ReaderWriterLockSlim _lock = new(LockRecursionPolicy.SupportsRecursion);

    public ConcurrentList(int initialCapacity)
    {
        _arr = new T[initialCapacity];
    }

    public ConcurrentList() : this(4) { }

    ~ConcurrentList()
    {
        Dispose();
    }

    public ConcurrentList(IEnumerable<T> items)
    {
        _arr = items.ToArray();
        _count = _arr.Length;
    }

    public int InternalArrayLength
    {
        get
        {
            _lock.EnterReadLock();
            try
            {
                return _arr.Length;
            }
            finally
            {
                _lock.ExitReadLock();
            }
        }
    }

    public void Dispose()
    {
        _lock.Dispose();
    }

    public int Count
    {
        get
        {
            _lock.EnterReadLock();
            try
            {
                return _count;
            }
            finally
            {
                _lock.ExitReadLock();
            }
        }
    }

    public void Add(T item)
    {
        _lock.EnterWriteLock();
        try
        {
            var newCount = _count + 1;
            EnsureCapacity(newCount);
            _arr[_count] = item;
            _count = newCount;
        }
        finally
        {
            _lock.ExitWriteLock();
        }
    }

    public bool Remove(T item)
    {
        _lock.EnterUpgradeableReadLock();

        try
        {
            var i = IndexOfInternal(item);

            if (i == -1)
                return false;

            _lock.EnterWriteLock();
            try
            {
                RemoveAtInternal(i);
                return true;
            }
            finally
            {
                _lock.ExitWriteLock();
            }
        }
        finally
        {
            _lock.ExitUpgradeableReadLock();
        }
    }

    public IEnumerator<T> GetEnumerator()
    {
        _lock.EnterReadLock();

        try
        {
            for (var i = 0; i < _count; i++)
                // deadlocking potential mitigated by lock recursion enforcement
                yield return _arr[i];
        }
        finally
        {
            _lock.ExitReadLock();
        }
    }

    IEnumerator IEnumerable.GetEnumerator()
    {
        return GetEnumerator();
    }

    public int IndexOf(T item)
    {
        _lock.EnterReadLock();
        try
        {
            return IndexOfInternal(item);
        }
        finally
        {
            _lock.ExitReadLock();
        }
    }

    public void Insert(int index, T item)
    {
        _lock.EnterUpgradeableReadLock();

        try
        {
            if (index > _count)
                throw new ArgumentOutOfRangeException("index");

            _lock.EnterWriteLock();
            try
            {
                var newCount = _count + 1;
                EnsureCapacity(newCount);

                // shift everything right by one, starting at index
                Array.Copy(_arr, index, _arr, index + 1, _count - index);

                // insert
                _arr[index] = item;
                _count = newCount;
            }
            finally
            {
                _lock.ExitWriteLock();
            }
        }
        finally
        {
            _lock.ExitUpgradeableReadLock();
        }
    }

    public void RemoveAt(int index)
    {
        _lock.EnterUpgradeableReadLock();
        try
        {
            if (index >= _count)
                throw new ArgumentOutOfRangeException("index");

            _lock.EnterWriteLock();
            try
            {
                RemoveAtInternal(index);
            }
            finally
            {
                _lock.ExitWriteLock();
            }
        }
        finally
        {
            _lock.ExitUpgradeableReadLock();
        }
    }

    public void Clear()
    {
        _lock.EnterWriteLock();
        try
        {
            Array.Clear(_arr, 0, _count);
            _count = 0;
        }
        finally
        {
            _lock.ExitWriteLock();
        }
    }

    public bool Contains(T item)
    {
        _lock.EnterReadLock();
        try
        {
            return IndexOfInternal(item) != -1;
        }
        finally
        {
            _lock.ExitReadLock();
        }
    }

    public void CopyTo(T[] array, int arrayIndex)
    {
        _lock.EnterReadLock();
        try
        {
            if (_count > array.Length - arrayIndex)
                throw new ArgumentException("Destination array was not long enough.");

            Array.Copy(_arr, 0, array, arrayIndex, _count);
        }
        finally
        {
            _lock.ExitReadLock();
        }
    }

    public bool IsReadOnly => false;

    public T this[int index]
    {
        get
        {
            _lock.EnterReadLock();
            try
            {
                if (index >= _count)
                    throw new ArgumentOutOfRangeException("index");

                return _arr[index];
            }
            finally
            {
                _lock.ExitReadLock();
            }
        }
        set
        {
            _lock.EnterUpgradeableReadLock();
            try
            {
                if (index >= _count)
                    throw new ArgumentOutOfRangeException("index");

                _lock.EnterWriteLock();
                try
                {
                    _arr[index] = value;
                }
                finally
                {
                    _lock.ExitWriteLock();
                }
            }
            finally
            {
                _lock.ExitUpgradeableReadLock();
            }
        }
    }

    public void AddRange(IEnumerable<T> items)
    {
        if (items == null)
            throw new ArgumentNullException("items");

        _lock.EnterWriteLock();

        try
        {
            var arr = items as T[] ?? items.ToArray();
            var newCount = _count + arr.Length;
            EnsureCapacity(newCount);
            Array.Copy(arr, 0, _arr, _count, arr.Length);
            _count = newCount;
        }
        finally
        {
            _lock.ExitWriteLock();
        }
    }

    private void EnsureCapacity(int capacity)
    {
        if (_arr.Length >= capacity)
            return;

        int doubled;
        checked
        {
            try
            {
                doubled = _arr.Length * 2;
            }
            catch (OverflowException)
            {
                doubled = int.MaxValue;
            }
        }

        var newLength = Math.Max(doubled, capacity);
        Array.Resize(ref _arr, newLength);
    }

    private int IndexOfInternal(T item)
    {
        return Array.FindIndex(_arr, 0, _count, x => x is not null && x.Equals(item));
    }

    private void RemoveAtInternal(int index)
    {
        Array.Copy(_arr, index + 1, _arr, index, _count - index - 1);
        _count--;

        // release last element
        Array.Clear(_arr, _count, 1);
    }

    public void DoSync(Action<ConcurrentList<T>> action)
    {
        GetSync(l =>
        {
            action(l);
            return 0;
        });
    }

    public TResult GetSync<TResult>(Func<ConcurrentList<T>, TResult> func)
    {
        _lock.EnterWriteLock();
        try
        {
            return func(this);
        }
        finally
        {
            _lock.ExitWriteLock();
        }
    }
}
}
