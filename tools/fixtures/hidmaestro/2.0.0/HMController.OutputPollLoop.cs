	private void OutputPollLoop()
	{
		try
		{
			OutputPoll();
		}
		finally
		{
			CloseOutputCts();
		}
	}
