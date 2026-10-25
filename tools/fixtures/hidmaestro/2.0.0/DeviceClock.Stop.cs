	public void Stop(int joinMs)
	{
		_stop = true;
		Wake();
		if ((_thread.ThreadState & System.Threading.ThreadState.Unstarted) != System.Threading.ThreadState.Running)
		{
			Close();
		}
		else if (System.Threading.Thread.CurrentThread != _thread)
		{
			try
			{
				_thread.Join(joinMs);
			}
			catch (ThreadStateException)
			{
			}
		}
	}
