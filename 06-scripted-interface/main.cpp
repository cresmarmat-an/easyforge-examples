// An interface whose behavior is written in a script. This file lays the
// interface out and names its elements; menu.script finds them by name, says
// what the buttons do, and keeps the title moving with spawn.

#include <easyforge/bridges/ui_script.h>
#include <easyforge/script.h>
#include <easyforge/ui.h>
#include <easyforge/window.h>

using namespace easyforge;

namespace
{
    ui::Button MenuButton(std::string text, std::string name)
    {
        return ui::Button(text, { .Name = name, .Width = 260 });
    }

    // A page of the program: a column centered in the window.
    ui::Display Page(std::string name, std::vector<ui::Element> children)
    {
        return ui::Display(name, {
            .Padding = 48,
            .Gap = 16,
            .Alignment = ui::Alignment::Center,
            .Distribution = ui::Distribution::Center,
            .Children = std::move(children),
        });
    }
}

int main()
{
    Window window = Window::New({ .Title = "Scripted interface", .Icon = "icon.png", .Width = 900, .Height = 600 });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }

    window.Content = ui::Displays({
        .Name = "Pages",
        .Children = {
            Page("Menu", {
                ui::Label("Scripted", { .Name = "Title", .FontSize = 48 }),
                MenuButton("Play", "Play"),
                MenuButton("Settings", "Settings"),
                MenuButton("Quit", "Quit"),
            }),
            Page("Settings", {
                ui::Label("Settings", { .FontSize = 32 }),
                ui::Label("", { .Name = "VolumeText" }),
                ui::Slider({ .Name = "Volume", .Width = 260, .Value = 0.8f }),
                ui::Toggle("Full screen", { .Name = "FullScreen" }),
                MenuButton("Back", "SettingsBack"),
            }),
            Page("Game", {
                ui::Label("Score: 0", { .Name = "Score", .FontSize = 32 }),
                MenuButton("Add a point", "Point"),
                MenuButton("Back to the menu", "GameBack"),
            }),
        },
    });

    // The script reaches the interface, and two things the program allows.
    ScriptEngine scripts = ScriptEngine::New({ .MemoryLimit = 16 * 1024 * 1024 });
    ui::DefineInterface(scripts, ui::Root::Of(window));
    scripts.Define("Quit", [window] { window.Close(); });
    scripts.Define("SetFullScreen", [window](bool on) { window.Fullscreen = on; });

    Result<ScriptValue> ran = scripts.RunFile("menu.script");
    if (!ran)
    {
        Log(LogLevel::Error, ran.Error());
        return 1;
    }

    // Functions the script started with spawn carry on a little each frame.
    window.OnFrame = [&scripts](float deltaSeconds) {
        Result<> updated = scripts.Update(deltaSeconds);
        if (!updated)
        {
            Log(LogLevel::Error, updated.Error());
        }
    };
    window.Run();
}