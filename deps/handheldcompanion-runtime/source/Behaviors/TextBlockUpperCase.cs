using System;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;

namespace HandheldCompanion.Behaviors;

public static class TextBlockUpperCase
{
    public static readonly DependencyProperty IsUpperCaseProperty =
        DependencyProperty.RegisterAttached("IsUpperCase", typeof(bool), typeof(TextBlockUpperCase),
            new PropertyMetadata(false, OnIsUpperCaseChanged));

    public static bool GetIsUpperCase(DependencyObject element) => (bool)element.GetValue(IsUpperCaseProperty);
    public static void SetIsUpperCase(DependencyObject element, bool value) => element.SetValue(IsUpperCaseProperty, value);

    private static void OnIsUpperCaseChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not TextBlock tb) return;

        var dpd = DependencyPropertyDescriptor.FromProperty(TextBlock.TextProperty, typeof(TextBlock));
        if ((bool)e.NewValue)
        {
            dpd.AddValueChanged(tb, OnTextValueChanged);
            ApplyUpperCase(tb);
        }
        else
        {
            dpd.RemoveValueChanged(tb, OnTextValueChanged);
        }
    }

    private static void OnTextValueChanged(object? sender, EventArgs e)
    {
        if (sender is TextBlock tb) ApplyUpperCase(tb);
    }

    private static void ApplyUpperCase(TextBlock tb)
    {
        if (tb.Text is string text && text != text.ToUpperInvariant())
            tb.SetCurrentValue(TextBlock.TextProperty, text.ToUpperInvariant());
    }
}
