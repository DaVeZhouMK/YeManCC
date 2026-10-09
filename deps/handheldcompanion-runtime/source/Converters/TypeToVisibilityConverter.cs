using System;
using System.Windows;
using System.Windows.Data;

namespace HandheldCompanion.Converters
{
    public class TypeToVisibilityConverter : IValueConverter
    {
        public Type? TargetType { get; set; }

        public object Convert(object value, Type targetType, object parameter, System.Globalization.CultureInfo culture)
        {
            if (value == null)
                return Visibility.Collapsed;

            TargetType = parameter as Type;
            if (TargetType is null)
                return Visibility.Collapsed;

            return value.GetType() == TargetType ? Visibility.Visible : Visibility.Collapsed;
        }

        public object ConvertBack(object value, Type targetType, object parameter, System.Globalization.CultureInfo culture)
        {
            throw new NotImplementedException();
        }
    }
}
