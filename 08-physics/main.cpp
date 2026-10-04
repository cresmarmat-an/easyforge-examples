// A stack of boxes that settles the same way every run. The same world runs
// twice, side by side; whatever you do happens to both, and the label says
// whether they still match to the last bit. The fingerprint after 600 steps
// is the same number every time the program starts, as long as nothing is
// dropped before then.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

#include <easyforge/physics.h>
#include <easyforge/ui.h>
#include <easyforge/window.h>

using namespace easyforge;

namespace
{
    constexpr float StepSeconds = 1.0f / 60.0f;

    // The ground, two walls, a stack of ten boxes, and a small pyramid.
    Physics2D Build()
    {
        Physics2D physics = Physics2D::New({ .Gravity = { 0.0f, -9.8f } });
        physics.AddBox({ .Position = { 0.0f, -0.5f }, .Size = { 20.0f, 1.0f }, .Type = BodyType::Static });
        physics.AddBox({ .Position = { -10.5f, 6.0f }, .Size = { 1.0f, 14.0f }, .Type = BodyType::Static });
        physics.AddBox({ .Position = { 10.5f, 6.0f }, .Size = { 1.0f, 14.0f }, .Type = BodyType::Static });
        for (int level = 0; level < 10; ++level)
        {
            physics.AddBox({ .Position = { -4.0f, 0.5f + static_cast<float>(level) * 1.02f }, .Size = { 1.0f, 1.0f } });
        }
        for (int row = 0; row < 5; ++row)
        {
            for (int column = 0; column < 5 - row; ++column)
            {
                float x = 4.0f + (static_cast<float>(column) - static_cast<float>(4 - row) * 0.5f) * 1.05f;
                physics.AddBox({ .Position = { x, 0.5f + static_cast<float>(row) * 1.02f }, .Size = { 1.0f, 1.0f } });
            }
        }
        return physics;
    }

    // Every body's place and angle, as bits.
    std::uint64_t Fingerprint(const Physics2D& physics)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const Body2D& body : physics.Bodies())
        {
            Vector2 position = body.Position;
            for (float value : { position.X, position.Y, body.Rotation.Get() })
            {
                hash = (hash ^ std::bit_cast<std::uint32_t>(value)) * 1099511628211ull;
            }
        }
        return hash;
    }

    bool Same(const Physics2D& first, const Physics2D& second)
    {
        return Fingerprint(first) == Fingerprint(second) && first.BodyCount() == second.BodyCount();
    }

    // How a world maps onto an area: metres to points, with the origin at the
    // bottom middle and Y up.
    struct WorldView
    {
        Vector2 Origin;
        float Scale = 1.0f;

        static WorldView Of(Vector2 size) { return { { size.X * 0.5f, size.Y - 12.0f }, std::min(size.X / 22.0f, (size.Y - 12.0f) / 14.0f) }; }
        Vector2 ToPoints(Vector2 metres) const { return { Origin.X + metres.X * Scale, Origin.Y - metres.Y * Scale }; }
        Vector2 ToMetres(Vector2 points) const { return { (points.X - Origin.X) / Scale, (Origin.Y - points.Y) / Scale }; }
    };

    void Draw(Canvas& canvas, Vector2 size, const Physics2D& physics, Color tint)
    {
        WorldView view = WorldView::Of(size);
        for (const Body2D& body : physics.Bodies())
        {
            bool fixed = body.Type.Get() == BodyType::Static;
            Color fill = fixed ? Color::Hex("#2B3142") : tint;
            Color edge = fixed ? Color::Hex("#3A4256") : Color::Hex("#11141C").WithAlpha(0.6f);
            Vector2 center = view.ToPoints(body.Position);
            switch (body.Shape())
            {
            case ShapeKind2D::Circle:
                canvas.Circle(center, body.Radius() * view.Scale, { .Color = fill, .BorderWidth = 1.5f, .BorderColor = edge });
                // A spoke, so turning shows.
                canvas.Line(center, center + Vector2 { std::cos(-body.Rotation.Get()), std::sin(-body.Rotation.Get()) } * body.Radius() * view.Scale,
                    { .Color = edge, .Width = 1.5f });
                break;
            case ShapeKind2D::Box:
            {
                Vector2 extent = body.Size() * view.Scale;
                canvas.Rectangle({ .Position = center - extent * 0.5f, .Size = extent, .Rotation = -body.Rotation.Get(), .Color = fill,
                    .CornerRadius = 2.0f, .BorderWidth = 1.5f, .BorderColor = edge });
                break;
            }
            case ShapeKind2D::Capsule:
            {
                // A pill: a rectangle as tall as the capsule with fully rounded ends.
                Vector2 extent = body.Size() * view.Scale;
                canvas.Rectangle({ .Position = center - extent * 0.5f, .Size = extent, .Rotation = -body.Rotation.Get(), .Color = fill,
                    .CornerRadius = extent.Y * 0.5f, .BorderWidth = 1.5f, .BorderColor = edge });
                break;
            }
            default: break;
            }
        }
    }
}

int main()
{
    Window window = Window::New({ .Title = "Physics", .Icon = "icon.png", .Width = 1180, .Height = 640 });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }
    ui::Root::Of(window).Theme = ui::Theme::Dark();

    Physics2D left = Build();
    Physics2D right = Build();
    int steps = 0;
    int drops = 0;
    float waiting = 0.0f;
    std::uint64_t settled = 0;

    // Whatever happens to one world happens to the other, the same way.
    auto drop = [&](Vector2 place) {
        for (Physics2D* physics : { &left, &right })
        {
            switch (drops % 3)
            {
            case 0: physics->AddBox({ .Position = place, .Rotation = 0.3f, .Size = { 1.2f, 0.8f } }); break;
            case 1: physics->AddCircle({ .Position = place, .Radius = 0.45f, .Restitution = 0.3f }); break;
            default: physics->AddCapsule({ .Position = place, .Rotation = 1.0f, .Length = 1.0f, .Radius = 0.3f }); break;
            }
        }
        ++drops;
    };

    ui::Label status("", { .Color = Color::Hex("#9AA3B5"), .Wrap = true });
    auto area = [&](Physics2D& physics, Color tint) {
        ui::DrawingArea field({
            .Width = ui::Fill,
            .Height = ui::Fill,
            .CornerRadius = 12.0f,
            .Background = Color::Hex("#121620"),
            .OnDraw = [&physics, tint](Canvas& canvas, Vector2 size) { Draw(canvas, size, physics, tint); },
        });
        field.OnPress = [&, field](Vector2 point) {
            Vector2 place = WorldView::Of(field.Frame().Size()).ToMetres(point);
            if (place.Y > 0.5f && place.X > -9.5f && place.X < 9.5f)
            {
                drop(place);
            }
        };
        return field;
    };

    window.Content = ui::Row({
        .Padding = 20,
        .Gap = 16,
        .Children = {
            ui::Column({
                .Width = 230,
                .Gap = 12,
                .Children = {
                    ui::Label("Physics", { .FontSize = 22 }),
                    ui::Label("Two copies of one world. Click either to drop a box, a ball, or a capsule into both.", { .Wrap = true }),
                    ui::Button("Shake", { .Width = ui::Fill, .OnClick = [&] {
                        for (Physics2D* physics : { &left, &right })
                        {
                            for (const Body2D& body : physics->Bodies())
                            {
                                body.ApplyImpulse(Vector2 { 0.0f, 4.0f } * body.Mass());
                            }
                        }
                    } }),
                    ui::Button("Start again", { .Width = ui::Fill, .OnClick = [&] {
                        left = Build();
                        right = Build();
                        steps = 0;
                        drops = 0;
                        settled = 0;
                    } }),
                    status,
                },
            }),
            area(left, Color::Hex("#5FA8FF")),
            area(right, Color::Hex("#F2A65A")),
        },
    });

    window.OnFrame = [&](float deltaSeconds) {
        // Steps of the same length, however long the frames are.
        waiting = std::min(waiting + deltaSeconds, 0.25f);
        while (waiting >= StepSeconds)
        {
            left.Step(StepSeconds);
            right.Step(StepSeconds);
            waiting -= StepSeconds;
            if (++steps == 600)
            {
                settled = Fingerprint(left);
            }
        }
        std::string fingerprint = settled != 0 ? std::format("{:016X}", settled) : std::string("after 600 steps");
        status.Text = std::format("Step {}\nBodies {}\nThe worlds {}\nFingerprint {}", steps, left.BodyCount(),
            Same(left, right) ? "match exactly" : "differ", fingerprint);
    };

    window.Run();
}
