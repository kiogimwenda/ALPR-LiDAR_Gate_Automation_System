// TitleLabel.cs — a page title whose words reveal one after another
// (spec A2: each word fades and rises, ~90 ms apart). Each word is its own
// Label tagged for Reveal, so the page entrance staggers them; the words
// wrap like text.

using GateControl.Core.Motion;
using GateControl.Motion;

namespace GateControl.Controls;

public sealed class TitleLabel : ContentView
{
    public static readonly BindableProperty TextProperty = BindableProperty.Create(
        nameof(Text), typeof(string), typeof(TitleLabel), null,
        propertyChanged: (b, _, _) => ((TitleLabel)b).Build());

    private readonly FlexLayout _words = new() { Wrap = Microsoft.Maui.Layouts.FlexWrap.Wrap };

    public TitleLabel()
    {
        Content = _words;
    }

    public string? Text
    {
        get => (string?)GetValue(TextProperty);
        set => SetValue(TextProperty, value);
    }

    // Seconds before the first word (to sit inside a page's stagger).
    public double StartDelay { get; set; }

    private void Build()
    {
        _words.Children.Clear();
        var words = (Text ?? "").Split(' ', StringSplitOptions.RemoveEmptyEntries);
        SemanticProperties.SetHeadingLevel(this, SemanticHeadingLevel.Level1);
        SemanticProperties.SetDescription(this, Text);
        for (var i = 0; i < words.Length; i++)
        {
            var word = new Label
            {
                Text = words[i],
                Margin = new Thickness(0, 0, i < words.Length - 1 ? 7 : 0, 0),
                Style = (Style)Application.Current!.Resources["LargeTitle"],
            };
            Reveal.SetKind(word, RevealKind.Word);
            Reveal.SetDelay(word, StartDelay + Stagger.Delay(i, Stagger.Words));
            _words.Children.Add(word);
        }
    }
}
