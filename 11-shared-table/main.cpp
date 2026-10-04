// A data table shared between a server and two clients, all in this window.
// The server owns a field of coins; each client adds a marker of its own and
// moves it by clicking in its own copy of the field. The tables are kept the
// same by the data_network bridge, two ways: the markers belong to the clients
// that added them, the coins to the server. Picking up a coin is a request, so
// the server decides who got it and adds to that player's score.
//
// Click in the server's field to drop a coin, and in Ari's or Bea's field to
// walk there and pick up any coin close by. The switch on the left makes the
// network poor; the three tables still end up the same.

#include <array>
#include <format>
#include <string>
#include <vector>

#include <easyforge/bridges/data_network.h>
#include <easyforge/network.h>
#include <easyforge/ui.h>
#include <easyforge/window.h>

using namespace easyforge;

namespace
{
    const Color Muted = Color::Hex("#8C95A8");
    const Color Gold = Color::Hex("#F2C14E");

    constexpr NetworkConditions PoorNetwork { .Loss = 0.1f, .Latency = 0.07f, .Jitter = 0.03f };

    // Positions in a table run from 0 to 1 across the field, whatever its size.
    constexpr float Reach = 0.12f;

    // Draws one table's coins and markers into a field.
    void DrawField(Canvas& canvas, Vector2 size, const Table& table, const Node& own)
    {
        auto onScreen = [size](Vector2 place) { return Vector2 { place.X * size.X, place.Y * size.Y }; };
        if (Node coins = table.Find("Coins"))
        {
            for (const Node& coin : coins.Children())
            {
                canvas.Circle(onScreen(coin["Position"]), 7.0f, { .Color = Gold });
            }
        }
        if (Node players = table.Find("Players"))
        {
            for (const Node& player : players.Children())
            {
                Vector2 place = onScreen(player["Position"]);
                if (player == own)
                {
                    canvas.Circle(place, Reach * size.X,
                        { .Color = Color::Transparent, .BorderWidth = 1, .BorderColor = Color::White.WithAlpha(0.25f) });
                }
                canvas.Circle(place, 11.0f,
                    { .Color = player["Color"], .BorderWidth = player == own ? 2.0f : 0.0f, .BorderColor = Color::White });
            }
        }
    }

    // One of the three columns: a field, and the table's nodes as a tree.
    struct FieldView
    {
        ui::Label Status { "", { .Color = Muted, .Wrap = true } };
        ui::DrawingArea Field;
        ui::Tree Nodes;

        ui::Element Make(const std::string& title) const
        {
            return ui::Panel({
                .Width = ui::Fill,
                .Height = ui::Fill,
                .Padding = { 14 },
                .Gap = 10,
                .CornerRadius = 12.0f,
                .Background = Color::Hex("#171B24"),
                .Children = { ui::Label(title, { .FontSize = 20 }), Status, Field, Nodes },
            });
        }
    };

    // A client: its own copy of the table, and the marker it added.
    struct Player
    {
        std::string Name;
        Color Tint;
        Client Link;
        Table Copy = Table::New();
        TableShare Shared;
        Node Marker;
        std::string LastAnswer;
        FieldView View;
    };
}

int main()
{
    Window window = Window::New({ .Title = "Shared table", .Icon = "icon.png", .Width = 1220, .Height = 720 });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }
    ui::Root::Of(window).Theme = ui::Theme::Dark();

    Server server = Server::New({ .Port = 0, .MaximumClients = 8, .Name = "Shared table", .ThisComputerOnly = true });
    if (!server)
    {
        Log(LogLevel::Error, server.Error());
        return 1;
    }

    // The server's table, which every client gets a copy of.
    Table world = Table::New();
    world.DefineType("Coin", { { "Value", 1 } });
    world.Add("Coins");
    world.Add("Players");
    TableShare shared = Share(world, server, Sharing::TwoWay);

    Random random(7);
    int coinsDropped = 0;
    auto dropCoin = [&](Vector2 place) {
        world.Find("Coins").Add(std::format("Coin {}", ++coinsDropped), "Coin", { { "Position", place } });
    };
    for (int count = 0; count < 6; ++count)
    {
        dropCoin({ random.Between(0.1f, 0.9f), random.Between(0.1f, 0.9f) });
    }

    // Picking up is a request: the server checks the coin is still there and
    // close to the player's marker, removes it, and adds to the score. The
    // marker belongs to the client, but the server may change any node.
    server.OnRequest("PickUp", [&](Connection from, const Message& request) -> Result<Message> {
        std::string name = request["Coin"];
        Node coin = world.Find("Coins/" + name);
        if (!coin)
        {
            return Failure(name + " is gone");
        }
        std::vector<Node> owned = shared.NodesOwnedBy(from);
        if (owned.empty())
        {
            return Failure("there is no marker yet");
        }
        Node marker = owned[0];
        if (Distance(marker["Position"].As<Vector2>(), coin["Position"].As<Vector2>()) > Reach)
        {
            return Failure(name + " is out of reach");
        }
        marker["Score"] += coin["Value"].Get();
        coin.Remove();
        return Message { { "Score", marker["Score"].As<int>() } };
    });

    // A client's marker goes when the client does.
    server.OnDisconnected = [&](Connection client, std::string) {
        for (Node node : shared.NodesOwnedBy(client))
        {
            node.Remove();
        }
    };

    FieldView serverView;
    serverView.Field = ui::DrawingArea({
        .Width = ui::Fill,
        .Height = 280,
        .CornerRadius = 10.0f,
        .Background = Color::Hex("#10131A"),
        .OnDraw = [&world](Canvas& canvas, Vector2 size) { DrawField(canvas, size, world, Node()); },
    });
    serverView.Field.OnPress = [&](Vector2 point) {
        Vector2 size = serverView.Field.Frame().Size();
        dropCoin({ point.X / size.X, point.Y / size.Y });
    };
    serverView.Nodes = ui::Tree({ .Width = ui::Fill, .Height = ui::Fill, .Source = world });

    std::array<Player, 2> players;
    players[0].Name = "Ari";
    players[0].Tint = Color::Hex("#6FC3FF");
    players[1].Name = "Bea";
    players[1].Tint = Color::Hex("#FF8FB1");

    for (Player& player : players)
    {
        player.Link = Client::New({ .Address = "127.0.0.1", .Port = server.Port() });
        player.Shared = Share(player.Copy, player.Link);

        player.View.Field = ui::DrawingArea({
            .Width = ui::Fill,
            .Height = 280,
            .CornerRadius = 10.0f,
            .Background = Color::Hex("#10131A"),
            .OnDraw = [&player](Canvas& canvas, Vector2 size) { DrawField(canvas, size, player.Copy, player.Marker); },
        });

        // A click moves the marker, a change the client may make itself, and
        // asks for any coin within reach.
        player.View.Field.OnPress = [&player](Vector2 point) {
            if (!player.Marker)
            {
                return;
            }
            Vector2 size = player.View.Field.Frame().Size();
            Vector2 place { point.X / size.X, point.Y / size.Y };
            player.Marker["Position"] = place;

            // Sent now rather than during the next update, so the server has
            // the new position before the request below arrives.
            player.Shared.SendChanges();
            for (const Node& coin : player.Copy.Find("Coins").Children())
            {
                if (Distance(place, coin["Position"].As<Vector2>()) <= Reach)
                {
                    std::string name = coin.Name();
                    player.Link.Request("PickUp", { { "Coin", name } }, [&player, name](const Reply& reply) {
                        player.LastAnswer = reply ? std::format("Picked up {}", name) : reply.Error;
                    });
                }
            }
        };
        player.View.Nodes = ui::Tree({ .Width = ui::Fill, .Height = ui::Fill, .Source = player.Copy });
    }

    window.Content = ui::Row({
        .Padding = { 20 },
        .Gap = 16,
        .Children = {
            ui::Column({
                .Width = 220,
                .Gap = 12,
                .Children = {
                    ui::Label("Network", { .FontSize = 20 }),
                    ui::Toggle("Poor network", { .OnChange = [&](bool on) {
                        server.Conditions = on ? PoorNetwork : NetworkConditions {};
                        for (Player& player : players)
                        {
                            player.Link.Conditions = server.Conditions;
                        }
                    } }),
                    ui::Label("Drops one packet in ten and holds the rest back by 40 to 100 ms, both ways.",
                        { .Color = Muted, .Wrap = true }),
                    ui::Label("Click in the server's field to drop a coin. Click in Ari's or Bea's field to walk "
                              "there and pick up the coins within the ring.",
                        { .Color = Muted, .Wrap = true }),
                },
            }),
            serverView.Make("Server"),
            players[0].View.Make("Ari"),
            players[1].View.Make("Bea"),
        },
    });

    window.OnFrame = [&](float) {
        server.Update();
        for (Player& player : players)
        {
            player.Link.Update();

            // Once the server's table has arrived, the client adds its marker:
            // a node it owns, which reaches the server and the other client.
            if (player.Shared.IsReady() && !player.Marker)
            {
                player.Marker = player.Copy.Find("Players").Add(player.Name, {
                    { "Position", Vector2 { random.Between(0.2f, 0.8f), random.Between(0.2f, 0.8f) } },
                    { "Color", player.Tint },
                    { "Score", 0 },
                });
                player.View.Nodes.OpenNode(player.Copy.Find("Coins"));
                player.View.Nodes.OpenNode(player.Copy.Find("Players"));
            }

            std::string score = player.Marker ? std::format("score {}", player.Marker["Score"].As<int>()) : "waiting for the table";
            player.View.Status.Text = std::format("{}, {} nodes\n{}", score, player.Copy.NodeCount(), player.LastAnswer);
        }
        serverView.Status.Text = std::format("{} clients, {} nodes\n{} coins left", server.Connections().size(), world.NodeCount(),
            world.Find("Coins").ChildCount());
    };

    window.Run();
}
