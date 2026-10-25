	private void Loop()
	{
		nint[] handles = new nint[2]
		{
			_timer,
			_wake.SafeWaitHandle.DangerousGetHandle()
		};
		long frequency = Stopwatch.Frequency;
		bool flag = false;
		long num = 0L;
		long num2 = 0L;
		long num3 = 0L;
		try
		{
			while (!_stop)
			{
				try
				{
					if (!_tick.Running(out var origin))
					{
						if (flag)
						{
							flag = false;
							_tick.RunEnded();
						}
						_wake.WaitOne(500);
						continue;
					}
					long num4 = Math.Max(1L, _tick.Period);
					long timestamp = Stopwatch.GetTimestamp();
					if (!flag || origin != num || num4 != num2)
					{
						flag = true;
						num = origin;
						num2 = num4;
						num3 = FirstDue(_missed, num, timestamp, num2);
					}
					long notBefore = _tick.NotBefore;
					if (notBefore > num3)
					{
						num3 = notBefore;
					}
					if (timestamp >= num3)
					{
						_tick.Tick(num3);
						num3 = NextDue(_missed, num3, timestamp, num2);
						continue;
					}
					long num5 = num3 - timestamp;
					if (_timer == IntPtr.Zero)
					{
						goto IL_017c;
					}
					long dueTime = -Math.Max(1L, num5 * 10000000 / frequency);
					bool num6 = ClockNative.SetWaitableTimer(_timer, ref dueTime, 0, IntPtr.Zero, IntPtr.Zero, resume: false);
					if (num6)
					{
						Volatile.Write(ref _armedDue, num3);
					}
					if (!num6 || ClockNative.WaitForMultipleObjects(2u, handles, waitAll: false, _waitCapMs) == uint.MaxValue)
					{
						goto IL_017c;
					}
					goto end_IL_003c;
					IL_017c:
					_wake.WaitOne((int)Math.Max(1L, num5 * 1000 / frequency));
					end_IL_003c:;
				}
				catch
				{
				}
			}
		}
		finally
		{
			Close();
		}
	}
