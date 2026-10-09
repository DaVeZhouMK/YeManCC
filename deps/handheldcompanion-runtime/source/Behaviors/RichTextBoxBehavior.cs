using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;

namespace HandheldCompanion.Behaviors;

public static class RichTextBoxBehavior
{
    public static readonly DependencyProperty DocumentProperty =
        DependencyProperty.RegisterAttached(
            "Document",
            typeof(object),
            typeof(RichTextBoxBehavior),
            new FrameworkPropertyMetadata(null, OnDocumentChanged));

    public static object GetDocument(RichTextBox target)
        => target.GetValue(DocumentProperty);

    public static void SetDocument(RichTextBox target, object value)
        => target.SetValue(DocumentProperty, value);

    private static void OnDocumentChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not RichTextBox rtb)
            return;

        var doc = (e.NewValue as FlowDocument) ?? new FlowDocument();

        // FlowDocument has its own font defaults that override the RichTextBox's
        // FontFamily/FontSize settings, so we propagate them explicitly.
        doc.FontFamily = rtb.FontFamily;
        doc.FontSize = rtb.FontSize;
        doc.FontWeight = rtb.FontWeight;
        doc.FontStyle = rtb.FontStyle;

        rtb.Document = doc;
    }
}
