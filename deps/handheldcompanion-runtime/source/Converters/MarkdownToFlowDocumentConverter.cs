using System;
using System.Globalization;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Media;

namespace HandheldCompanion.Converters;

[ValueConversion(typeof(string), typeof(FlowDocument))]
public sealed class MarkdownToFlowDocumentConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        var markdown = value as string ?? string.Empty;
        return ParseMarkdown(markdown);
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();

    private static FlowDocument ParseMarkdown(string markdown)
    {
        var doc = new FlowDocument
        {
            PagePadding = new Thickness(4),
            FontSize = 13
        };

        var lines = markdown.Split('\n');
        var bulletList = (List?)null;

        foreach (var rawLine in lines)
        {
            var line = rawLine.TrimEnd('\r');

            // Heading 3: ### text
            if (line.StartsWith("### ", StringComparison.Ordinal))
            {
                bulletList = null;
                var p = new Paragraph { Margin = new Thickness(0, 6, 0, 2) };
                p.Inlines.Add(new Run(line[4..]) { FontSize = 14, FontWeight = FontWeights.Bold });
                doc.Blocks.Add(p);
                continue;
            }

            // Heading 2: ## text
            if (line.StartsWith("## ", StringComparison.Ordinal))
            {
                bulletList = null;
                var p = new Paragraph { Margin = new Thickness(0, 8, 0, 2) };
                p.Inlines.Add(new Run(line[3..]) { FontSize = 16, FontWeight = FontWeights.Bold });
                doc.Blocks.Add(p);
                continue;
            }

            // Heading 1: # text
            if (line.StartsWith("# ", StringComparison.Ordinal))
            {
                bulletList = null;
                var p = new Paragraph { Margin = new Thickness(0, 10, 0, 2) };
                p.Inlines.Add(new Run(line[2..]) { FontSize = 18, FontWeight = FontWeights.Bold });
                doc.Blocks.Add(p);
                continue;
            }

            // Bullet list: - text  or  * text
            if (line.StartsWith("- ", StringComparison.Ordinal) || line.StartsWith("* ", StringComparison.Ordinal))
            {
                if (bulletList is null)
                {
                    bulletList = new List
                    {
                        MarkerStyle = TextMarkerStyle.Disc,
                        Margin = new Thickness(16, 2, 0, 2),
                        Padding = new Thickness(0)
                    };
                    doc.Blocks.Add(bulletList);
                }
                var item = new ListItem(new Paragraph(BuildInlines(line[2..])));
                bulletList.ListItems.Add(item);
                continue;
            }

            // Empty line — end bullet list, add spacing paragraph
            if (string.IsNullOrWhiteSpace(line))
            {
                bulletList = null;
                doc.Blocks.Add(new Paragraph { Margin = new Thickness(0, 2, 0, 2) });
                continue;
            }

            // Normal paragraph
            bulletList = null;
            doc.Blocks.Add(new Paragraph(BuildInlines(line)) { Margin = new Thickness(0, 1, 0, 1) });
        }

        return doc;
    }

    /// <summary>
    /// Parses inline markdown: **bold**, *italic*, __bold__, _italic_, `code`
    /// </summary>
    private static Span BuildInlines(string text)
    {
        var span = new Span();

        // Regex matches: **bold**, __bold__, *italic*, _italic_, `code`
        var pattern = @"(\*\*|__|\*|_|`)(.+?)\1";
        var lastIndex = 0;

        foreach (Match match in Regex.Matches(text, pattern, RegexOptions.Singleline))
        {
            if (match.Index > lastIndex)
                span.Inlines.Add(new Run(text[lastIndex..match.Index]));

            var marker = match.Groups[1].Value;
            var content = match.Groups[2].Value;

            Run run = new(content);

            if (marker is "**" or "__")
                run.FontWeight = FontWeights.Bold;
            else if (marker is "*" or "_")
                run.FontStyle = FontStyles.Italic;
            else if (marker == "`")
            {
                run.FontFamily = new FontFamily("Consolas, Courier New");
                run.Background = new SolidColorBrush(Color.FromArgb(40, 128, 128, 128));
            }

            span.Inlines.Add(run);
            lastIndex = match.Index + match.Length;
        }

        if (lastIndex < text.Length)
            span.Inlines.Add(new Run(text[lastIndex..]));

        return span;
    }
}
