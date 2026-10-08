using System;

namespace HandheldCompanion.Managers;

public class UpdateFile
{
    public bool debug;
    public bool isGameControllerDb;
    public string filename = string.Empty;
    public int filesize;
    public short idx;
    public Uri uri = null!;
}