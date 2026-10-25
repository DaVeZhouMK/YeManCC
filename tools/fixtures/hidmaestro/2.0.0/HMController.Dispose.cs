	public void Dispose()
	{
		if (!_disposed)
		{
			try
			{
				SendReleaseFrames();
			}
			catch
			{
			}
			StopThreads();
			_context.OnControllerDisposing(this);
		}
	}
