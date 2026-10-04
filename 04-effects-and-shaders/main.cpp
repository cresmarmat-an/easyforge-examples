// Every built-in effect, and a shader of your own, each on a card over a moving
// background: shadows, glows, outlines, gradients, frosted glass, color
// changes, masks, and an image frame that stretches without distorting.

#include <cmath>

#include <easyforge/ui.h>
#include <easyforge/window.h>

using namespace easyforge;

namespace
{
    // Soft colored lights drifting behind the cards, so the frosted card has
    // something to blur.
    void DrawBackground(Canvas& canvas, Vector2 size, float time)
    {
        canvas.Rectangle({ .Size = size,
            .Gradient = LinearGradient { .From = Color::Hex("#141827"), .To = Color::Hex("#0A0C14"), .Angle = 90 } });
        const Color colors[] = { Color::Hex("#FF5F6D"), Color::Hex("#4DD0FF"), Color::Hex("#FFD166"),
            Color::Hex("#06D6A0"), Color::Hex("#B388FF") };
        for (int index = 0; index < 5; ++index)
        {
            float phase = static_cast<float>(index) * 1.3f;
            Vector2 center { size.X * (0.5f + 0.4f * std::sin(time * 0.3f + phase)),
                size.Y * (0.5f + 0.35f * std::cos(time * 0.23f + phase * 1.7f)) };
            canvas.Circle(center, 150, { .Color = colors[index].WithAlpha(0.55f), .Blur = 120 });
        }
    }

    // A few colored dots, to show what the color and mask effects do.
    ui::DrawingArea Dots()
    {
        return ui::DrawingArea({
            .Width = ui::Fill,
            .Height = ui::Fill,
            .OnDraw = [](Canvas& canvas, Vector2 size) {
                const Color colors[] = { Color::Hex("#FF5F6D"), Color::Hex("#FFD166"), Color::Hex("#06D6A0"),
                    Color::Hex("#4DD0FF") };
                for (int index = 0; index < 4; ++index)
                {
                    float x = size.X * (0.2f + 0.2f * static_cast<float>(index));
                    canvas.Circle({ x, size.Y * 0.5f }, 14, { .Color = colors[index] });
                }
            },
        });
    }

    ui::Element Card(std::string title, std::string note, ui::ContainerSettings settings, std::vector<ui::Element> extra = {})
    {
        settings.Width = 220;
        settings.Height = 150;
        settings.Padding = 16;
        settings.Gap = 4;
        settings.Children = { ui::Label(std::move(title), { .FontSize = 19 }),
            ui::Label(std::move(note), { .FontSize = 13, .Wrap = true }) };
        for (ui::Element& element : extra)
        {
            settings.Children.push_back(element);
        }
        return ui::Panel(settings);
    }
}

int main()
{
    Window window = Window::New({ .Title = "Effects and shaders", .Icon = "icon.png", .Width = 1280, .Height = 720 });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }
    ui::Root::Of(window).Theme = ui::Theme::Dark();

    float time = 0.0f;
    window.OnFrame = [&time](float deltaSeconds) { time += deltaSeconds; };

    Shader ripple = Shader::Load("ripple.shader");
    if (!ripple)
    {
        Log(LogLevel::Error, ripple.Error());
    }

    std::vector<ui::Element> cards = {
        Card("Shadow", "A soft shadow below the box.",
            { .Effects = { ui::Shadow { .Offset = { 0, 12 }, .Blur = 28, .Color = Color::Black.WithAlpha(0.55f) } } }),
        Card("Glow", "Light around the box, in any color.",
            { .Effects = { ui::Glow { .Blur = 30, .Color = Color::Hex("#4DD0FF").WithAlpha(0.8f) } } }),
        Card("Outline", "A line around the box, with a gap.",
            { .Effects = { ui::Outline { .Width = 2, .Gap = 4, .Color = Color::Hex("#FFD166") } } }),
        Card("Gradient", "A background that changes color.",
            { .Background = ui::Background::Gradient(Color::Hex("#FF5F6D"), Color::Hex("#FFC371"), 45) }),
        Card("Frosted glass", "Blurs what is behind it.",
            { .Background = Color::White.WithAlpha(0.1f),
                .Effects = { ui::BackgroundBlur { .Radius = 24 }, ui::Outline { .Width = 1, .Color = Color::White.WithAlpha(0.25f) } } }),
        Card("Sheen", "A gradient over the background.",
            { .Effects = { ui::Gradient { .From = Color::White.WithAlpha(0.22f), .To = Color::Transparent, .Angle = 60 } } }),
        Card("Color adjust", "The same dots, without color.",
            { .Effects = { ui::ColorAdjust { .Saturation = 0.0f } } }, { Dots() }),
        Card("Mask", "Everything inside cut to a pill.",
            { .Background = Color::Hex("#3A3F5C"), .Effects = { ui::Mask { .CornerRadius = 75 } } }, { Dots() }),
        Card("Image frame", "A 48 pixel image, stretched without distorting its corners.",
            { .Background = ui::Background::Image("frame.png", { .Slice = 16 }) }),
        Card("Your shader", "ripple.shader moves the card in waves.",
            { .Background = Color::Hex("#2A9D8F"), .Shader = ripple, .ShaderValues = { { "Speed", 1.5f } } }),
    };

    window.Content = ui::Stack({
        .Children = {
            ui::DrawingArea({
                .Width = ui::Fill,
                .Height = ui::Fill,
                .OnDraw = [&time](Canvas& canvas, Vector2 size) { DrawBackground(canvas, size, time); },
            }),
            ui::Column({
                .Padding = 48,
                .Gap = 28,
                .Alignment = ui::Alignment::Center,
                .Distribution = ui::Distribution::Center,
                .Children = {
                    ui::Label("Effects and shaders", { .FontSize = 32 }),
                    ui::Grid({ .Width = 5 * 220 + 4 * 28, .Columns = 5, .ColumnGap = 28, .RowGap = 28, .Children = cards }),
                },
            }),
        },
    });

    window.Run();
}
