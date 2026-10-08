using System.Windows.Media;

namespace HandheldCompanion.Misc
{
    public class GlyphIconInfo
    {
        public string? Name { get; set; }
        public string? Glyph { get; set; }
        public FontFamily? FontFamily { get; set; } = new FontFamily("Segoe UI");
        public double FontSize { get; set; }
        public Color? Color { get; set; }
    }
}
