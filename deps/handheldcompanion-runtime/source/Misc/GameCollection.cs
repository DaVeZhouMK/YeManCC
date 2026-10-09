using System;

namespace HandheldCompanion.Misc
{
    [Serializable]
    public class GameCollection
    {
        // Well-known Guid that represents the built-in Favorites collection.
        // Stored in Profile.Collections just like any user collection, so no
        // special-casing is needed in persistence or serialization.
        public static readonly Guid FavoritesId = new("10000000-0000-0000-0000-000000000000");

        public Guid Id { get; set; } = Guid.NewGuid();
        public string Name { get; set; } = string.Empty;
        public bool IsBuiltIn { get; set; } = false;
    }
}
