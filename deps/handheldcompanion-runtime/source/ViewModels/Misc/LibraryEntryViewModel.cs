using craftersmine.SteamGridDBNet;
using HandheldCompanion.Libraries;
using HandheldCompanion.ViewModels.Misc;
using IGDB.Models;
using System.Collections.ObjectModel;
using System.IO;
using System.Windows.Data;
using static HandheldCompanion.Libraries.LibraryEntry;
using static HandheldCompanion.Managers.LibraryManager;

namespace HandheldCompanion.ViewModels
{
    public class LibraryEntryViewModel : BaseViewModel
    {
        public ObservableCollection<LibraryVisualViewModel> LibraryCovers { get; } = [];
        public ObservableCollection<LibraryVisualViewModel> LibraryArtworks { get; } = [];
        public ObservableCollection<LibraryVisualViewModel> LibraryLogos { get; } = [];

        private LibraryEntry _LibEntry = null!;
        public LibraryEntry LibEntry
        {
            get => _LibEntry;
            set
            {
                _LibEntry = value;

                // refresh all properties
                OnPropertyChanged(string.Empty);
            }
        }

        public long Id => LibEntry.Id;
        public string Name => LibEntry.Name;
        public string Description => LibEntry.Description;
        public int ReleaseDateYear => LibEntry.ReleaseDate.Year;
        public LibraryFamily Family => LibEntry.Family;

        public bool IsManualEntry => LibEntry is ManualEntry;

        public LibraryEntryViewModel(LibraryEntry libraryEntry)
        {
            // Enable thread-safe access to the collection
            BindingOperations.EnableCollectionSynchronization(LibraryCovers, _collectionLock);
            BindingOperations.EnableCollectionSynchronization(LibraryArtworks, _collectionLock2);
            BindingOperations.EnableCollectionSynchronization(LibraryLogos, _collectionLock3);

            LibEntry = libraryEntry;

            if (LibEntry is ManualEntry manualEntry)
            {
                // Full-res extension comes from the cached source file; thumbnail is always a resized PNG.
                string coverExt = Path.GetExtension(manualEntry.ManualCoverPath);
                string artworkExt = Path.GetExtension(manualEntry.ManualArtworkPath);
                string logoExt = Path.GetExtension(manualEntry.ManualLogoPath);
                LibraryCovers.Add(new(this, ManualEntry.ManualCoverId, coverExt, ".png"));
                LibraryArtworks.Add(new(this, ManualEntry.ManualArtworkId, artworkExt, ".png"));
                LibraryLogos.Add(new(this, ManualEntry.ManualLogoId, logoExt, ".png"));
            }
            else if (LibEntry is SteamGridEntry steamEntry)
            {
                foreach (SteamGridDbGrid grid in steamEntry.Grids)
                    LibraryCovers.Add(new(this, grid.Id, Path.GetExtension(grid.FullImageUrl), Path.GetExtension(grid.ThumbnailImageUrl)));
                foreach (SteamGridDbHero hero in steamEntry.Heroes)
                    LibraryArtworks.Add(new(this, hero.Id, Path.GetExtension(hero.FullImageUrl), Path.GetExtension(hero.ThumbnailImageUrl)));
                foreach (SteamGridDbLogo logo in steamEntry.Logos)
                    LibraryLogos.Add(new(this, logo.Id, Path.GetExtension(logo.FullImageUrl), Path.GetExtension(logo.ThumbnailImageUrl)));
            }
            else if (LibEntry is IGDBEntry IGDB)
            {
                if (IGDB.Cover is not null)
                    LibraryCovers.Add(new(this, IGDB.Cover.Id.HasValue ? IGDB.Cover.Id.Value : 0, Path.GetExtension(IGDB.Cover.Url)));
                foreach (Artwork artwork in IGDB.Artworks)
                    LibraryArtworks.Add(new(this, artwork.Id.Value, Path.GetExtension(artwork.Url)));
            }
        }

        /// <summary>
        /// Refreshes the single manual visual entry for the given art type after the user browses a new file.
        /// </summary>
        public void RefreshManualVisual(LibraryType libraryType, string newExtension, string thumbnailExtension = "")
        {
            if (LibEntry is not ManualEntry)
                return;

            ObservableCollection<LibraryVisualViewModel> target;
            long imageId;
            if (libraryType.HasFlag(LibraryType.cover))
            {
                target = LibraryCovers;
                imageId = ManualEntry.ManualCoverId;
            }
            else if (libraryType.HasFlag(LibraryType.artwork))
            {
                target = LibraryArtworks;
                imageId = ManualEntry.ManualArtworkId;
            }
            else
            {
                target = LibraryLogos;
                imageId = ManualEntry.ManualLogoId;
            }

            target.Clear();
            target.Add(new(this, imageId, newExtension, string.IsNullOrEmpty(thumbnailExtension) ? newExtension : thumbnailExtension));
        }

        public override string ToString()
        {
            return Name;
        }

        public override void Dispose()
        {
            base.Dispose();
        }
    }
}
