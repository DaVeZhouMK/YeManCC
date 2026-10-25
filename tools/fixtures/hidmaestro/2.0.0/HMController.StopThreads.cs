	internal void StopThreads()
	{
		lock (_submitLock)
		{
			lock (_pidStateLock)
			{
				_disposed = true;
			}
		}
		StopWorkers();
	}
