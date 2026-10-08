using System;
using System.Globalization;
using System.Windows.Data;

namespace HandheldCompanion.Converters;

public sealed class IsVerticalOrientationConverter : IMultiValueConverter
{
    public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture)
    {
        if (values.Length < 2)
            return false;

        if (values[0] is not double width || values[1] is not double height)
            return false;

        if (width <= 0 || height <= 0)
            return false;

        return height > width;
    }

    public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}
