using HandheldCompanion.Extensions;
using HandheldCompanion.Utils;

namespace HandheldCompanion.ViewModels
{
    public class MotionInputViewModel : BaseViewModel
    {
        public MotionInput Value { get; set; }
        public string? Glyph { get; set; }
        public string? Description { get; set; }
        public bool HasGlyph => !string.IsNullOrWhiteSpace(Glyph);

        public MotionInputViewModel() { }

        public MotionInputViewModel(MotionInput mode)
        {
            Value = mode;
            Glyph = mode.ToGlyph();
            Description = EnumUtils.GetDescriptionFromEnumValue(mode);
        }
    }
}
