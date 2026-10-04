// Music streamed from a file, and sounds placed around a listener. Click
// anywhere in the dark area: a sound plays at that spot, louder near the
// listener in the middle and from the side it is on. The controls on the left
// turn the music down, put it under water, and give the clicks an echo.

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

#include <easyforge/sound.h>
#include <easyforge/ui.h>
#include <easyforge/window.h>

using namespace easyforge;

namespace
{
    // How many points on screen make one metre in the world.
    constexpr float PointsPerMetre = 36.0f;

    // A ripple drawn where a sound was played.
    struct Ripple
    {
        Vector2 Place;
        float Age = 0.0f;
    };

    ui::Label Heading(std::string text)
    {
        return ui::Label(text, { .FontSize = 18 });
    }
}

int main()
{
    Window window = Window::New({ .Title = "Sound", .Icon = "icon.png", .Width = 1000, .Height = 640 });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }
    ui::Root::Of(window).Theme = ui::Theme::Dark();

    // Without a sound device the program still runs, silently.
    Mixer mixer = Mixer::New();
    if (!mixer)
    {
        Log(LogLevel::Warning, mixer.Error());
    }

    Sound music = Sound::Load("music.qoa", { .Stream = true });
    Sound blip = Sound::Load("blip.wav");
    Sound drop = Sound::Load("drop.wav");
    for (const Sound* sound : { &music, &blip, &drop })
    {
        if (!*sound)
        {
            Log(LogLevel::Error, sound->Error());
            return 1;
        }
    }

    MixerBus musicBus = mixer.Bus("Music");
    MixerBus effects = mixer.Bus("Effects");
    PlayingSound song = mixer.Play(music, { .Volume = 0.7f, .Loop = true, .Bus = "Music", .FadeIn = 2.0f });

    std::vector<Ripple> ripples;
    Random random;

    ui::Label status("", { .Color = Color::Hex("#9AA3B5"), .Wrap = true });
    ui::Button pause("Pause the music", { .Width = ui::Fill });
    pause.OnClick = [song, pause] {
        if (song.IsPaused())
        {
            song.Resume();
            pause.Text = "Pause the music";
        }
        else
        {
            song.Pause();
            pause.Text = "Play the music";
        }
    };

    ui::DrawingArea field({
        .Width = ui::Fill,
        .Height = ui::Fill,
        .CornerRadius = 14.0f,
        .Background = Color::Hex("#121620"),
        .OnDraw = [&ripples](Canvas& canvas, Vector2 size) {
            Vector2 middle = size * 0.5f;
            // Rings a metre apart around the listener.
            for (int metre = 2; metre <= 14; metre += 2)
            {
                canvas.Circle(middle, static_cast<float>(metre) * PointsPerMetre,
                    { .Color = Color::Transparent, .BorderWidth = 1, .BorderColor = Color::Hex("#232A3A") });
            }
            for (const Ripple& ripple : ripples)
            {
                float fade = 1.0f - ripple.Age;
                canvas.Circle(middle + ripple.Place, 8.0f + ripple.Age * 60.0f,
                    { .Color = Color::Transparent, .BorderWidth = 2, .BorderColor = Color::Hex("#6FC3FF").WithAlpha(fade) });
                canvas.Circle(middle + ripple.Place, 5.0f, { .Color = Color::Hex("#6FC3FF").WithAlpha(fade) });
            }
            // The listener, facing up the screen.
            canvas.Circle(middle, 11.0f, { .Color = Color::Hex("#F2C14E") });
            canvas.Line(middle, middle - Vector2 { 0, 22 }, { .Color = Color::Hex("#F2C14E"), .Width = 3 });
        },
    });

    window.Content = ui::Row({
        .Padding = 20,
        .Gap = 20,
        .Children = {
            ui::Column({
                .Width = 260,
                .Gap = 12,
                .Children = {
                    Heading("Music"),
                    ui::Label("Volume"),
                    ui::Slider({ .Width = ui::Fill, .Value = 0.7f, .OnChange = [song](float volume) { song.Volume = volume; } }),
                    ui::Toggle("Under water", { .OnChange = [musicBus](bool on) {
                        musicBus.LowPass = on ? 450.0f : 0.0f;
                        musicBus.Volume = on ? 1.4f : 1.0f;
                    } }),
                    pause,
                    ui::Label(""),
                    Heading("Clicks"),
                    ui::Toggle("Echo", { .OnChange = [effects](bool on) {
                        effects.Echo = { .Delay = 0.28f, .Feedback = 0.45f, .Mix = on ? 0.5f : 0.0f };
                    } }),
                    ui::Label("Pitch"),
                    ui::Slider({ .Name = "Pitch", .Width = ui::Fill, .Value = 1.0f, .Minimum = 0.5f, .Maximum = 2.0f }),
                    ui::Label(""),
                    ui::Label("Click in the dark area to play a sound there. The listener is the yellow dot.", { .Wrap = true }),
                    status,
                },
            }),
            field,
        },
    });

    // A press in the area plays a sound at that spot.
    field.OnPress = [&](Vector2 point) {
        Vector2 offset = point - field.Frame().Size() * 0.5f;
        // Right on the screen is right of the listener, and up is ahead of it,
        // which is -Z.
        Vector3 place { offset.X / PointsPerMetre, 0.0f, offset.Y / PointsPerMetre };
        float pitch = ui::Root::Of(window).Find<ui::Slider>("Pitch").Value;
        mixer.Play(random.Chance(0.5f) ? blip : drop, {
            .Volume = 0.9f,
            .Pitch = pitch * random.Between(0.9f, 1.1f),
            .Bus = "Effects",
            .Position = place,
            .MinimumDistance = 1.5f,
            .MaximumDistance = 15.0f,
        });
        ripples.push_back({ offset });
    };

    window.OnFrame = [&](float deltaSeconds) {
        for (Ripple& ripple : ripples)
        {
            ripple.Age += deltaSeconds / 1.2f;
        }
        std::erase_if(ripples, [](const Ripple& ripple) { return ripple.Age >= 1.0f; });
        std::string device = mixer ? mixer.DeviceName() : std::string("no sound device");
        status.Text = std::format("{}\nSounds playing: {}\nMusic at {:.1f} s", device, mixer.PlayingCount(), song.Time());
    };

    window.Run();
}
