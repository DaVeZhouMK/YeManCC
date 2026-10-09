using HandheldCompanion.Managers;
using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media.Imaging;

namespace HandheldCompanion.Converters;

/// <summary>
/// Converts a BitmapImage to Visibility based on whether it's valid artwork or a placeholder.
/// Returns Collapsed if the image is null, MissingCover, MissingArtwork or empty.
/// </summary>
public sealed class HasArtworkConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        if (value is not BitmapImage image)
            return Visibility.Collapsed;

        // Reject known placeholder instances
        if (image == LibraryResources.MissingCover || image == LibraryResources.MissingArtwork)
            return Visibility.Collapsed;

        // A decoded, frozen BitmapImage always has a positive pixel dimension.
        // This works regardless of whether the image was loaded via UriSource or StreamSource.
        if (image.PixelWidth <= 0 || image.PixelHeight <= 0)
            return Visibility.Collapsed;

        return Visibility.Visible;
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}
