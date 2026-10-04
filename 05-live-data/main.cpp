// A game's data kept in a table and shown by the interface as it changes. The
// labels are bound to the player's name, health, and score, the buttons change
// the table in named edits that Undo reverses, and a 3D scene turns behind it
// all. Nothing tells the labels to update: they follow the table by themselves.

#include <algorithm>
#include <format>

#include <easyforge/ui.h>
#include <easyforge/window.h>

using namespace easyforge;

int main()
{
    Window window = Window::New({ .Title = "Live data", .Icon = "icon.png", .Width = 1100, .Height = 680 });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }
    ui::Root::Of(window).Theme = ui::Theme::Dark();

    Table game = Table::New();
    Node player = game.Add("Player", { { "Name", "Ari" }, { "Health", 100 }, { "Score", 0 } });

    Scene scene = Scene::New();
    SceneObject box = scene.Add(Model::Load("cube.obj"), { .Scale = { 1.6f, 1.6f, 1.6f } });
    scene.Camera = { .Position = { 0.0f, 1.5f, 4.5f } };
    scene.Background = Color::Hex("#10131C");

    // Every change to health goes through an edit, so Undo takes it back.
    auto changeHealth = [game, player](std::string name, int amount) {
        game.BeginEdit(name);
        player["Health"] = std::clamp(player["Health"].As<int>() + amount, 0, 100);
        game.EndEdit();
    };

    ui::ProgressBar healthBar({ .Width = ui::Fill, .Value = 100, .Maximum = 100, .Color = Color::Hex("#06D6A0") });
    ui::Label lastChange("Nothing has happened yet", { .FontSize = 13, .Wrap = true });
    ui::Button undo("Undo", { .Enabled = false, .OnClick = [game] { game.Undo(); } });

    window.Content = ui::Stack({
        .Children = {
            ui::SceneView(scene),
            ui::Column({
                .Padding = 24,
                .Alignment = ui::Alignment::Start,
                .Children = {
                    ui::Panel({
                        .Width = 300,
                        .Padding = 20,
                        .Gap = 10,
                        .Background = Color::Hex("#1B2030").WithAlpha(0.8f),
                        .Effects = { ui::BackgroundBlur { .Radius = 18 }, ui::Shadow { .Offset = { 0, 8 }, .Blur = 24 } },
                        .Children = {
                            ui::Label(ui::Bind(player["Name"]), { .FontSize = 26 }),
                            ui::Label(ui::Bind(player["Health"], "Health: {}")),
                            healthBar,
                            ui::Label(ui::Bind(player["Score"], "Score: {}")),
                            ui::Row({
                                .Gap = 8,
                                .Children = {
                                    ui::Button("Hit", { .OnClick = [changeHealth] { changeHealth("Hit", -15); } }),
                                    ui::Button("Heal", { .Style = ui::ButtonStyle::Accent,
                                                          .OnClick = [changeHealth] { changeHealth("Heal", 10); } }),
                                    undo,
                                },
                            }),
                            lastChange,
                        },
                    }),
                },
            }),
        },
    });

    std::uint64_t seen = game.Version();
    float angle = 0.0f;
    float sinceScore = 0.0f;
    window.OnFrame = [&](float deltaSeconds) {
        angle += deltaSeconds * 0.7f;
        box.Rotation = Quaternion::FromAngles(angle * 0.6f, angle, 0.0f);

        // The score rises while the player is alive.
        sinceScore += deltaSeconds;
        if (sinceScore >= 0.5f && player["Health"].As<int>() > 0)
        {
            sinceScore = 0.0f;
            player["Score"] += 10;
        }

        // The table lists every change; the bar and the note follow the health.
        for (const Change& change : game.ChangesSince(seen))
        {
            if (change.Kind == ChangeKind::PropertySet && change.Property == "Health")
            {
                healthBar.Value.AnimateTo(change.New.As<float>(), { .Duration = 0.35f });
                lastChange.Text = std::format("Health went from {} to {}", change.Old, change.New);
            }
        }
        seen = game.Version();
        undo.Enabled = game.CanUndo();
        undo.Text = game.CanUndo() ? "Undo " + game.UndoName() : "Undo";
    };

    window.Run();
}
