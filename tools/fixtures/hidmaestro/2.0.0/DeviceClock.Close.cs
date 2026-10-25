	private void Close()
	{
		lock (_lifetime)
		{
			if (_closed)
			{
				return;
			}
			_closed = true;
		}
		if (_timer != IntPtr.Zero)
		{
			ClockNative.CloseHandle(_timer);
		}
		_wake.Dispose();
	}
