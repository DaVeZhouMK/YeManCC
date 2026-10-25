	private void CloseOutputCts()
	{
		lock (_outputCtsLock)
		{
			if (_outputCtsClosed)
			{
				return;
			}
			_outputCtsClosed = true;
		}
		_outputCts.Dispose();
	}
