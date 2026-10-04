// Screens that switch with transitions: a menu, settings, a game, and an about
// screen. Each menu button uses a different transition, and Back goes to the
// screen before with the transition reversed.

#include <easyforge/ui.h>
#include <easyforge/window.h>

using namespace easyforge;

namespace
{
    ui::Label Heading(std::string text)
    {
        return ui::Label(std::move(text), { .FontSize = 32 });
    }
}

int main()
{
    Window window = Window::New({ .Title = "Displays", .Icon = "icon.png", .Width = 900, .Height = 600 });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }

    // The buttons below refer to the displays, which are made after them.
    ui::Displays pages;

    auto choice = [&pages](std::string text, std::string display, ui::Transition transition,
                      ui::ButtonStyle style = ui::ButtonStyle::Normal) {
        return ui::Button(std::move(text), {
            .Width = 240,
            .Height = 40,
            .Style = style,
            .OnClick = [&pages, display, transition] { pages.Show(display, transition); },
        });
    };
    auto back = [&pages] {
        return ui::Button("Back", { .Width = 120, .OnClick = [&pages] { pages.Back(); } });
    };

    ui::Label volume("Volume: 70%");
    ui::Slider volumeSlider({
        .Width = 280,
        .Value = 70,
        .Maximum = 100,
        .Step = 5,
        .OnChange = [volume](float value) { volume.Text = "Volume: " + std::to_string(static_cast<int>(value)) + "%"; },
    });

    pages = ui::Displays({
        .Start = "Menu",
        .Children = {
            ui::Display("Menu", {
                .Padding = 48,
                .Gap = 14,
                .Alignment = ui::Alignment::Center,
                .Distribution = ui::Distribution::Center,
                .Children = {
                    ui::Label("Displays", { .FontSize = 44 }),
                    ui::Label("Each button uses another transition.", { .Margin = { 0, 0, 0, 18 } }),
                    choice("Play", "Game", ui::Transition::Fade(0.4f), ui::ButtonStyle::Accent),
                    choice("Settings", "Settings", ui::Transition::Slide(0.35f)),
                    choice("About", "About", ui::Transition::Scale(0.3f)),
                },
            }),
            ui::Display("Settings", {
                .Padding = 48,
                .Gap = 18,
                .Children = {
                    Heading("Settings"),
                    ui::Toggle("Full screen", { .OnChange = [window](bool on) { window.Fullscreen = on; } }),
                    ui::Toggle("Show hints", { .Checked = true }),
                    ui::Checkbox("Remember where I stopped", { .Checked = true }),
                    volume,
                    volumeSlider,
                    ui::Spacer(),
                    back(),
                },
            }),
            ui::Display("Game", {
                .Padding = 48,
                .Gap = 18,
                .Alignment = ui::Alignment::Center,
                .Distribution = ui::Distribution::Center,
                .Background = ui::Background::Gradient(Color::Hex("#1D2B64"), Color::Hex("#0B0F24"), 90),
                .Children = {
                    ui::Label("Playing", { .FontSize = 44, .Color = Color::White }),
                    ui::Label("The settings screen kept its values while you were here.", { .Color = Color::Hex("#C8D0F0") }),
                    back(),
                },
            }),
            ui::Display("About", {
                .Padding = 48,
                .Gap = 12,
                .Children = {
                    Heading("About"),
                    ui::Label("Every screen here is a ui::Display inside one ui::Displays. Showing one keeps the others "
                              "as they were, so text you typed and switches you flipped are still there when you come back.",
                        { .Width = 520, .Wrap = true }),
                    ui::Spacer(),
                    back(),
                },
            }),
        },
    });
    window.Content = pages;

    window.Run();
}
