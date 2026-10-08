using System;
using System.Windows.Data;
using System.Windows.Markup;

namespace HandheldCompanion.Localization;

[MarkupExtensionReturnType(typeof(object))]
public class StaticExtension : MarkupExtension
{
    public StaticExtension()
    {
    }

    public StaticExtension(string name) : this()
    {
        Name = name;
    }

    [ConstructorArgument("name")]
    public string Name
    {
        get;
        set;
    } = string.Empty;

    public override object ProvideValue(IServiceProvider serviceProvider)
    {
        Binding binding = new("[" + Name + "]")
        {
            Mode = BindingMode.Default,
            Source = TranslationSource.Instance,
        };

        return binding.ProvideValue(serviceProvider);
    }
}

