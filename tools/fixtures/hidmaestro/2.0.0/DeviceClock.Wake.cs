	public void Wake()
	{
		lock (_lifetime)
		{
			if (!_closed)
			{
				_wake.Set();
			}
		}
	}
