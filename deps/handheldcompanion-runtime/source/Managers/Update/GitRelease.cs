using System;
using System.Collections.Generic;

namespace HandheldCompanion.Managers;

public class Asset
{
    public string url { get; set; } = string.Empty;
    public int id { get; set; }
    public string node_id { get; set; } = string.Empty;
    public string name { get; set; } = string.Empty;
    public object? label { get; set; }
    public Uploader uploader { get; set; } = new();
    public string content_type { get; set; } = string.Empty;
    public string state { get; set; } = string.Empty;
    public int size { get; set; }
    public int download_count { get; set; }
    public DateTime created_at { get; set; }
    public DateTime updated_at { get; set; }
    public string browser_download_url { get; set; } = string.Empty;
}

public class Author
{
    public string login { get; set; } = string.Empty;
    public int id { get; set; }
    public string node_id { get; set; } = string.Empty;
    public string avatar_url { get; set; } = string.Empty;
    public string gravatar_id { get; set; } = string.Empty;
    public string url { get; set; } = string.Empty;
    public string html_url { get; set; } = string.Empty;
    public string followers_url { get; set; } = string.Empty;
    public string following_url { get; set; } = string.Empty;
    public string gists_url { get; set; } = string.Empty;
    public string starred_url { get; set; } = string.Empty;
    public string subscriptions_url { get; set; } = string.Empty;
    public string organizations_url { get; set; } = string.Empty;
    public string repos_url { get; set; } = string.Empty;
    public string events_url { get; set; } = string.Empty;
    public string received_events_url { get; set; } = string.Empty;
    public string type { get; set; } = string.Empty;
    public bool site_admin { get; set; }
}

public class GitRelease
{
    public string url { get; set; } = string.Empty;
    public string assets_url { get; set; } = string.Empty;
    public string upload_url { get; set; } = string.Empty;
    public string html_url { get; set; } = string.Empty;
    public int id { get; set; }
    public Author author { get; set; } = new();
    public string node_id { get; set; } = string.Empty;
    public string tag_name { get; set; } = string.Empty;
    public string target_commitish { get; set; } = string.Empty;
    public string name { get; set; } = string.Empty;
    public bool draft { get; set; }
    public bool prerelease { get; set; }
    public DateTime created_at { get; set; }
    public DateTime published_at { get; set; }
    public List<Asset> assets { get; set; } = [];
    public string tarball_url { get; set; } = string.Empty;
    public string zipball_url { get; set; } = string.Empty;
    public string body { get; set; } = string.Empty;
}

public class Uploader
{
    public string login { get; set; } = string.Empty;
    public int id { get; set; }
    public string node_id { get; set; } = string.Empty;
    public string avatar_url { get; set; } = string.Empty;
    public string gravatar_id { get; set; } = string.Empty;
    public string url { get; set; } = string.Empty;
    public string html_url { get; set; } = string.Empty;
    public string followers_url { get; set; } = string.Empty;
    public string following_url { get; set; } = string.Empty;
    public string gists_url { get; set; } = string.Empty;
    public string starred_url { get; set; } = string.Empty;
    public string subscriptions_url { get; set; } = string.Empty;
    public string organizations_url { get; set; } = string.Empty;
    public string repos_url { get; set; } = string.Empty;
    public string events_url { get; set; } = string.Empty;
    public string received_events_url { get; set; } = string.Empty;
    public string type { get; set; } = string.Empty;
    public bool site_admin { get; set; }
}