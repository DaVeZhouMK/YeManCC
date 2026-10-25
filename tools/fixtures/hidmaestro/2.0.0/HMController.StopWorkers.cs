	private void StopWorkers()
	{
		try
		{
			_idleClock?.Stop(TimeoutScale.Apply(500));
		}
		catch
		{
		}
		try
		{
			_host?.Dispose();
		}
		catch
		{
		}
		lock (_outputCtsLock)
		{
			if (!_outputCtsClosed)
			{
				_outputCts.Cancel();
			}
		}
		Thread outputThread = _outputThread;
		if (outputThread != null)
		{
			if (Thread.CurrentThread != outputThread)
			{
				try
				{
					outputThread.Join(TimeoutScale.Apply(500));
				}
				catch
				{
				}
			}
		}
		else
		{
			CloseOutputCts();
		}
		try
		{
			SharedMemoryIO.UnregisterOutputPump(Index);
		}
		catch
		{
		}
	}
