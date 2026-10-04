// Coin Rush: a small multiplayer game that uses every easyforge library.
//
// One copy of the program hosts. Its server runs the arena's physics, owns the
// coins and the scores, and reads the rules from rules.script. Every player,
// the host's own included, is a client of that server: it sends what its
// controls say, draws the arena from what the server sends back, and plays
// sounds for what happens. So a game across a local network works the same as
// one on this computer.
//
//     core      vectors, colors, random numbers, logging
//     assets    the sounds, the icon, and the menu's image, read from files
//     window    the window and its frames
//     input     keys and gamepads, bound to Move and Dash
//     graphics  the arena, drawn with a Canvas
//     data      the scores, coins, and round, kept in a table
//     ui        the menu, the scoreboard, and the frame around the arena
//     script    the rules
//     sound     the coin and bump sounds, placed around the player
//     physics   the arena's bodies, on the host
//     network   the server and its clients, and the table shared with them
//
// Host a game and add bots to play against, or start the program twice and
// join from the second. Move with WASD, the arrow keys, or a gamepad's left
// stick, and dash with Space or the gamepad's A button.

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <easyforge/easyforge.h>

using namespace easyforge;

namespace
{
    constexpr std::uint16_t GamePort = 7777;
    constexpr float StepSeconds = 1.0f / 60.0f;

    // The arena in metres, centred on the origin, Y up.
    constexpr Vector2 ArenaSize { 20.0f, 12.0f };
    constexpr float PlayerRadius = 0.5f;
    constexpr float CoinRadius = 0.3f;
    constexpr float BumperRadius = 1.0f;
    constexpr std::array<Vector2, 3> Bumpers { Vector2 { -5.0f, -1.0f }, Vector2 { 5.0f, -1.0f }, Vector2 { 0.0f, 2.5f } };

    constexpr std::array<Color, 6> PlayerColors {
        Color::Hex("#6FC3FF"), Color::Hex("#FF8FB1"), Color::Hex("#7DD69B"),
        Color::Hex("#C9A2FF"), Color::Hex("#FFB86B"), Color::Hex("#5FE0D0"),
    };

    const Color Muted = Color::Hex("#8C95A8");
    const Color Gold = Color::Hex("#F2C14E");

    // ---- The host ----------------------------------------------------------

    // The rules, read once from rules.script when the host starts.
    struct Rules
    {
        float RoundSeconds = 45.0f;
        float BreakSeconds = 4.0f;
        int CoinsAtOnce = 6;
        float Push = 22.0f;
        float DashShove = 7.0f;
        float DashSeconds = 1.2f;
    };

    // The server, the arena's physics, and the rules. Players reach it only
    // through the network, the host's own player included.
    class GameHost
    {
    public:
        // Nothing, with `error` set, when the port is taken or the rules do not load.
        static std::unique_ptr<GameHost> Start(bool openToNetwork, std::string& error)
        {
            std::unique_ptr<GameHost> host(new GameHost());
            host->Link = Server::New({
                .Port = GamePort,
                .MaximumClients = static_cast<int>(PlayerColors.size()),
                .Name = "Coin Rush",
                .ThisComputerOnly = !openToNetwork,
            });
            if (!host->Link)
            {
                error = host->Link.Error();
                return nullptr;
            }
            if (Result<ScriptValue> loaded = host->Script.RunFile("rules.script"); !loaded)
            {
                error = loaded.Error();
                return nullptr;
            }
            host->ReadRules();
            host->Build();
            host->Listen();
            host->StartRound();
            return host;
        }

        // Steps the arena, keeps the round's clock, and sends where everyone is.
        void Update(float deltaSeconds)
        {
            Waiting = std::min(Waiting + deltaSeconds, 5.0f * StepSeconds);
            while (Waiting >= StepSeconds)
            {
                Step();
                Waiting -= StepSeconds;
            }

            Node round = Game.Find("Round");
            if (BreakLeft > 0.0f)
            {
                BreakLeft -= deltaSeconds;
                if (BreakLeft <= 0.0f)
                {
                    StartRound();
                }
            }
            else
            {
                TimeLeft -= deltaSeconds;
                if (TimeLeft <= 0.0f)
                {
                    EndRound();
                }
            }
            int shown = static_cast<int>(std::ceil(std::max(TimeLeft, 0.0f)));
            if (round["TimeLeft"].As<int>() != shown)
            {
                round["TimeLeft"] = shown;
            }

            // Places change every step, so they go unreliably: a lost one is
            // replaced by the next. Scores and coins change seldom and go
            // through the shared table.
            Message places;
            for (const auto& [index, racer] : Racers)
            {
                places[std::format("P{}", index)] = racer.Body.Position.Get();
            }
            Link.SendToAll("Places", places, Delivery::Unreliable);
            Link.Update();
        }

    private:
        struct Racer
        {
            Body2D Body;
            Node Entry;
            Vector2 Move;
            int Dashes = 0;
            float DashWait = 0.0f;
        };

        struct Coin
        {
            Body2D Body;
            Node Entry;
        };

        GameHost() = default;

        float Rule(std::string_view name, float fallback) const
        {
            ScriptValue value = Script.Get(name);
            return value.IsNothing() ? fallback : static_cast<float>(value.AsNumber());
        }

        void ReadRules()
        {
            Settings.RoundSeconds = Rule("RoundSeconds", Settings.RoundSeconds);
            Settings.BreakSeconds = Rule("BreakSeconds", Settings.BreakSeconds);
            Settings.CoinsAtOnce = static_cast<int>(Rule("CoinsAtOnce", static_cast<float>(Settings.CoinsAtOnce)));
            Settings.Push = Rule("Push", Settings.Push);
            Settings.DashShove = Rule("DashShove", Settings.DashShove);
            Settings.DashSeconds = Rule("DashSeconds", Settings.DashSeconds);
        }

        void Build()
        {
            // No gravity: the arena is seen from above.
            Vector2 half = ArenaSize * 0.5f;
            for (Vector2 place : { Vector2 { 0.0f, half.Y + 0.5f }, Vector2 { 0.0f, -half.Y - 0.5f } })
            {
                Arena.AddBox({ .Position = place, .Size = { ArenaSize.X + 2.0f, 1.0f }, .Type = BodyType::Static, .Restitution = 0.6f });
            }
            for (Vector2 place : { Vector2 { half.X + 0.5f, 0.0f }, Vector2 { -half.X - 0.5f, 0.0f } })
            {
                Arena.AddBox({ .Position = place, .Size = { 1.0f, ArenaSize.Y }, .Type = BodyType::Static, .Restitution = 0.6f });
            }
            for (Vector2 place : Bumpers)
            {
                Arena.AddCircle({ .Position = place, .Radius = BumperRadius, .Type = BodyType::Static, .Restitution = 0.9f });
            }

            Game.Add("Round", { { "TimeLeft", 0 }, { "Message", "" } });
            Game.Add("Players");
            Game.Add("Coins");
            Shared = Share(Game, Link, Sharing::OneWay);
        }

        void Listen()
        {
            // Joining is a request, so the player learns the index the server
            // gave it, and the name, made unique if someone has it already.
            Link.OnRequest("Join", [this](Connection from, const Message& request) -> Result<Message> {
                std::string name = request["Name"].AsText().substr(0, 12);
                if (name.empty())
                {
                    return Failure("a player needs a name");
                }
                std::string unique = name;
                for (int number = 2; Game.Find("Players/" + unique); ++number)
                {
                    unique = std::format("{} {}", name, number);
                }
                AddRacer(from.Index(), unique);
                return Message { { "Index", from.Index() }, { "Name", unique } };
            });

            Link.OnMessage("Controls", [this](Connection from, const Message& message) {
                auto racer = Racers.find(from.Index());
                if (racer == Racers.end())
                {
                    return;
                }
                Vector2 move = message["Move"];
                racer->second.Move = Length(move) > 1.0f ? Normalize(move) : move;
                // A count rather than a flag, so a dash in a lost message still
                // shows in the next one.
                int dashes = message["Dashes"];
                if (dashes > racer->second.Dashes)
                {
                    racer->second.Dashes = dashes;
                    Dash(racer->second);
                }
            });

            Link.OnDisconnected = [this](Connection client, std::string) {
                auto racer = Racers.find(client.Index());
                if (racer != Racers.end())
                {
                    racer->second.Body.Remove();
                    racer->second.Entry.Remove();
                    Racers.erase(racer);
                }
            };
        }

        void AddRacer(int index, const std::string& name)
        {
            if (Racers.contains(index))
            {
                return;
            }
            Racer racer;
            racer.Body = Arena.AddCircle({
                .Position = FreeSpot(1.5f),
                .Radius = PlayerRadius,
                .Friction = 0.0f,
                .Restitution = 0.5f,
                .LinearDamping = 2.5f,
                .FixedRotation = true,
            });
            racer.Entry = Game.Find("Players").Add(name, {
                { "Index", index },
                { "Color", PlayerColors[static_cast<std::size_t>(index) % PlayerColors.size()] },
                { "Score", 0 },
            });
            racer.Body.OnTouch = [this, index](const Contact2D& contact) {
                if (contact.Sensor)
                {
                    Touched.emplace_back(contact.Other, index);
                }
                else if (contact.Speed > 2.5f)
                {
                    Bumps.push_back({ contact.Point, contact.Speed });
                }
            };
            Racers[index] = racer;
        }

        void Dash(Racer& racer)
        {
            if (racer.DashWait > 0.0f)
            {
                return;
            }
            Vector2 direction = Length(racer.Move) > 0.1f ? Normalize(racer.Move) : Normalize(racer.Body.Velocity.Get());
            racer.Body.ApplyImpulse(direction * Settings.DashShove);
            racer.DashWait = Settings.DashSeconds;
        }

        void Step()
        {
            for (auto& [index, racer] : Racers)
            {
                racer.Body.ApplyForce(racer.Move * Settings.Push);
                racer.DashWait = std::max(0.0f, racer.DashWait - StepSeconds);
            }
            Arena.Step(StepSeconds);

            // Touches are reported after the step, and handled after it too, as
            // coins cannot be removed while the step reports them.
            for (const auto& [coinBody, index] : Touched)
            {
                PickUp(coinBody, index);
            }
            Touched.clear();

            // Both racers in a collision report it; nearby reports are one bump.
            std::vector<Vector2> sent;
            for (const auto& [point, speed] : Bumps)
            {
                if (std::ranges::none_of(sent, [&](Vector2 other) { return Distance(other, point) < 0.5f; }))
                {
                    Link.SendToAll("Bump", { { "Position", point }, { "Speed", speed } }, Delivery::Unreliable);
                    sent.push_back(point);
                }
            }
            Bumps.clear();
        }

        void PickUp(const Body2D& coinBody, int index)
        {
            auto coin = std::ranges::find_if(Coins, [&](const Coin& candidate) { return candidate.Body == coinBody; });
            auto racer = Racers.find(index);
            if (coin == Coins.end() || racer == Racers.end() || BreakLeft > 0.0f)
            {
                return;
            }
            int value = coin->Entry["Value"];
            racer->second.Entry["Score"] += value;
            Link.SendToAll("Coin", { { "Position", coinBody.Position.Get() }, { "Value", value } });
            coin->Body.Remove();
            coin->Entry.Remove();
            Coins.erase(coin);
            AddCoin();
        }

        void AddCoin()
        {
            Vector2 place = FreeSpot(1.0f);
            Result<ScriptValue> rolled = Script.Call("CoinValue", static_cast<double>(Chance.Fraction()));
            int value = rolled ? static_cast<int>(rolled->AsNumber()) : 1;
            Body2D body = Arena.AddCircle({ .Position = place, .Radius = CoinRadius, .Type = BodyType::Static, .Sensor = true });
            Node entry = Game.Find("Coins").Add(std::format("Coin {}", ++CoinsMade), { { "Position", place }, { "Value", value } });
            Coins.push_back({ body, entry });
        }

        // A random place clear of bumpers, racers, and coins.
        Vector2 FreeSpot(float clearance)
        {
            Vector2 half = ArenaSize * 0.5f - Vector2 { 1.0f, 1.0f };
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                Vector2 place { Chance.Between(-half.X, half.X), Chance.Between(-half.Y, half.Y) };
                bool clear = std::ranges::all_of(Bumpers, [&](Vector2 bumper) { return Distance(place, bumper) > BumperRadius + clearance; });
                for (const auto& [index, racer] : Racers)
                {
                    clear = clear && Distance(place, racer.Body.Position.Get()) > PlayerRadius + clearance;
                }
                for (const Coin& coin : Coins)
                {
                    clear = clear && Distance(place, coin.Body.Position.Get()) > clearance;
                }
                if (clear)
                {
                    return place;
                }
            }
            return { 0.0f, -4.0f };
        }

        void StartRound()
        {
            TimeLeft = Settings.RoundSeconds;
            BreakLeft = 0.0f;
            for (auto& [index, racer] : Racers)
            {
                racer.Entry["Score"] = 0;
            }
            Game.Find("Round")["Message"] = "";
            while (static_cast<int>(Coins.size()) < Settings.CoinsAtOnce)
            {
                AddCoin();
            }
        }

        // The script words the result, from the names with the best score.
        void EndRound()
        {
            int best = 0;
            for (const auto& [index, racer] : Racers)
            {
                best = std::max(best, racer.Entry["Score"].As<int>());
            }
            std::vector<ScriptValue> leaders;
            for (const auto& [index, racer] : Racers)
            {
                if (racer.Entry["Score"].As<int>() == best)
                {
                    leaders.push_back(racer.Entry.Name());
                }
            }
            Result<ScriptValue> text = Script.Call("WinnerText", Script.NewList(leaders), static_cast<double>(best));
            Game.Find("Round")["Message"] = text ? text->AsText() : std::string("Round over");
            BreakLeft = Settings.BreakSeconds;
            TimeLeft = 0.0f;
        }

        Server Link;
        Physics2D Arena = Physics2D::New({ .Gravity = { 0.0f, 0.0f } });
        Table Game = Table::New();
        TableShare Shared;
        ScriptEngine Script = ScriptEngine::New();
        Rules Settings;
        Random Chance;
        std::map<int, Racer> Racers;
        std::vector<Coin> Coins;
        std::vector<std::pair<Body2D, int>> Touched;
        std::vector<std::pair<Vector2, float>> Bumps;
        float Waiting = 0.0f;
        float TimeLeft = 0.0f;
        float BreakLeft = 0.0f;
        int CoinsMade = 0;
    };

    // ---- A player ----------------------------------------------------------

    // A client of the host: its copy of the host's table, and the racers'
    // places from the newest Places message.
    class Player
    {
    public:
        static std::unique_ptr<Player> Join(const std::string& address, const std::string& name)
        {
            std::unique_ptr<Player> player(new Player());
            Player* self = player.get();
            self->Link = Client::New({ .Address = address, .Port = GamePort });
            self->Shared = Share(self->Copy, self->Link);
            self->Link.OnMessage("Places", [self](const Message& message) {
                self->Places.clear();
                for (const std::string& key : message.Names())
                {
                    self->Places[std::stoi(key.substr(1))] = message[key].AsVector2();
                }
            });
            self->Link.OnDisconnected = [self](std::string why) { self->Problem = why; };
            self->Link.Request("Join", { { "Name", name } }, [self](const Reply& reply) {
                if (!reply)
                {
                    self->Problem = reply.Error;
                    return;
                }
                self->Index = reply.Message["Index"].As<int>();
                self->Name = reply.Message["Name"].AsText();
            });
            return player;
        }

        // Sends what the controls say; the server does the moving.
        void Steer(Vector2 move, bool dash)
        {
            if (dash)
            {
                ++Dashes;
            }
            Link.Send("Controls", { { "Move", move }, { "Dashes", Dashes } }, Delivery::Unreliable);
        }

        std::optional<Vector2> Place() const
        {
            auto found = Places.find(Index);
            return found == Places.end() ? std::nullopt : std::optional<Vector2>(found->second);
        }

        bool IsPlaying() const { return Index >= 0 && Shared.IsReady(); }

        Client Link;
        Table Copy = Table::New();
        TableShare Shared;
        std::map<int, Vector2> Places;
        std::string Name;
        std::string Problem;
        int Index = -1;
        int Dashes = 0;

    private:
        Player() = default;
    };

    // A bot heads for the nearest coin, and dashes now and then on a long way.
    void SteerBot(Player& bot, Random& chance)
    {
        std::optional<Vector2> place = bot.Place();
        Node coins = bot.Copy.Find("Coins");
        if (!place || !coins)
        {
            return;
        }
        std::optional<Vector2> target;
        for (const Node& coin : coins.Children())
        {
            Vector2 spot = coin["Position"];
            if (!target || Distance(*place, spot) < Distance(*place, *target))
            {
                target = spot;
            }
        }
        if (!target)
        {
            bot.Steer({}, false);
            return;
        }
        Vector2 way = *target - *place;
        bot.Steer(Normalize(way), Length(way) > 5.0f && chance.Chance(0.01f));
    }

    // ---- Drawing -----------------------------------------------------------

    // How the arena maps onto an area: metres to points, centred, Y up.
    struct ArenaView
    {
        Vector2 Center;
        float Scale = 1.0f;

        static ArenaView Of(Vector2 size)
        {
            return { size * 0.5f, std::min(size.X / (ArenaSize.X + 1.0f), size.Y / (ArenaSize.Y + 1.0f)) };
        }

        Vector2 ToPoints(Vector2 metres) const { return { Center.X + metres.X * Scale, Center.Y - metres.Y * Scale }; }
    };

    void DrawArena(Canvas& canvas, Vector2 size, const Player& player)
    {
        ArenaView view = ArenaView::Of(size);
        Vector2 corner = view.ToPoints({ -ArenaSize.X * 0.5f, ArenaSize.Y * 0.5f });
        canvas.Rectangle({ .Position = corner, .Size = ArenaSize * view.Scale, .Color = Color::Hex("#121620"), .CornerRadius = 10.0f,
            .BorderWidth = 2.0f, .BorderColor = Color::Hex("#2B3142") });
        for (Vector2 bumper : Bumpers)
        {
            canvas.Circle(view.ToPoints(bumper), BumperRadius * view.Scale,
                { .Color = Color::Hex("#232A3A"), .BorderWidth = 2.0f, .BorderColor = Color::Hex("#3A4256") });
        }
        if (Node coins = player.Copy.Find("Coins"))
        {
            for (const Node& coin : coins.Children())
            {
                bool golden = coin["Value"].As<int>() > 1;
                canvas.Circle(view.ToPoints(coin["Position"]), CoinRadius * view.Scale * (golden ? 1.4f : 1.0f),
                    { .Color = golden ? Color::Hex("#FFE27A") : Gold, .BorderWidth = golden ? 2.0f : 0.0f, .BorderColor = Color::White });
            }
        }
        if (Node racers = player.Copy.Find("Players"))
        {
            for (const Node& racer : racers.Children())
            {
                int index = racer["Index"];
                auto place = player.Places.find(index);
                if (place == player.Places.end())
                {
                    continue;
                }
                Vector2 center = view.ToPoints(place->second);
                bool own = index == player.Index;
                canvas.Circle(center, PlayerRadius * view.Scale,
                    { .Color = racer["Color"], .BorderWidth = own ? 3.0f : 0.0f, .BorderColor = Color::White });
            }
        }
    }

    // The scores in the player's copy of the table, highest first.
    std::string Scoreboard(const Table& copy)
    {
        Node players = copy.Find("Players");
        if (!players)
        {
            return {};
        }
        std::vector<Node> ranked = players.Children();
        std::ranges::stable_sort(ranked, [](const Node& first, const Node& second) {
            return first["Score"].As<int>() > second["Score"].As<int>();
        });
        std::string text;
        for (const Node& player : ranked)
        {
            text += std::format("{}   {}\n", player.Name(), player["Score"].As<int>());
        }
        return text;
    }
}

int main()
{
    Window window = Window::New({ .Title = "Coin Rush", .Icon = "icon.png", .Width = 1180, .Height = 720 });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }
    ui::Root::Of(window).Theme = ui::Theme::Dark();

    Controls controls = Controls::New(window);
    controls.Bind("Move", {
        Stick::Left,
        KeyAxis { .Left = Key::A, .Right = Key::D, .Down = Key::S, .Up = Key::W },
        KeyAxis { .Left = Key::Left, .Right = Key::Right, .Down = Key::Down, .Up = Key::Up },
    });
    controls.Bind("Dash", { Key::Space, GamepadButton::South });

    // The game plays on without a sound device.
    Mixer mixer = Mixer::New();
    Sound coinSound = Sound::Load("coin.wav");
    Sound bumpSound = Sound::Load("bump.wav");
    if (!coinSound || !bumpSound)
    {
        Log(LogLevel::Error, coinSound ? bumpSound.Error() : coinSound.Error());
        return 1;
    }

    std::unique_ptr<GameHost> host;
    std::unique_ptr<Player> me;
    std::vector<std::unique_ptr<Player>> bots;
    Random chance;

    // ---- The menu ----

    ui::TextField nameField({ .Width = ui::Fill, .Text = "Ari", .MaximumLength = 12 });
    ui::Toggle openToNetwork("Open to the local network");
    ui::Label menuStatus("", { .Color = Muted, .Wrap = true });
    ui::List found({ .Width = ui::Fill, .Height = 110 });
    std::vector<FoundServer> foundServers;

    // ---- The game ----

    ui::Label roundLabel("", { .FontSize = 22 });
    ui::Label message("", { .FontSize = 18, .Color = Gold, .Wrap = true });
    ui::Label scores("");
    ui::Label playerStatus("", { .Color = Muted, .Wrap = true });
    ui::Button addBot("Add a bot");
    ui::DrawingArea arena({
        .Width = ui::Fill,
        .Height = ui::Fill,
        .OnDraw = [&me](Canvas& canvas, Vector2 size) {
            if (me)
            {
                DrawArena(canvas, size, *me);
            }
        },
    });

    // Filled in below, once both displays are made.
    ui::Displays pages;

    auto play = [&](const std::string& address) {
        me = Player::Join(address, nameField.Text);
        Player* self = me.get();
        me->Link.OnMessage("Coin", [&mixer, &coinSound, self](const Message& coin) {
            Vector2 place = coin["Position"];
            mixer.Play(coinSound, { .Volume = coin["Value"].As<int>() > 1 ? 1.0f : 0.7f, .Position = Vector3 { place.X, 0.0f, -place.Y },
                .MinimumDistance = 4.0f, .MaximumDistance = 40.0f });
        });
        me->Link.OnMessage("Bump", [&mixer, &bumpSound](const Message& bump) {
            Vector2 place = bump["Position"];
            float speed = bump["Speed"];
            mixer.Play(bumpSound, { .Volume = std::min(1.0f, speed / 10.0f), .Position = Vector3 { place.X, 0.0f, -place.Y },
                .MinimumDistance = 4.0f, .MaximumDistance = 40.0f });
        });
        roundLabel.Binding = ui::Binding();
        message.Binding = ui::Binding();
        addBot.Visible = host != nullptr;
        pages.Show("Game", ui::Transition::Fade(0.2f));
    };

    auto leave = [&](const std::string& why) {
        bots.clear();
        me.reset();
        host.reset();
        menuStatus.Text = why;
        pages.Show("Menu", ui::Transition::Fade(0.2f));
    };

    auto hostGame = [&] {
        std::string error;
        host = GameHost::Start(openToNetwork.Checked, error);
        if (!host)
        {
            menuStatus.Text = "Could not host: " + error;
            return;
        }
        play("127.0.0.1");
    };

    addBot.OnClick = [&] {
        if (host && bots.size() < 4)
        {
            bots.push_back(Player::Join("127.0.0.1", std::format("Bot {}", bots.size() + 1)));
        }
    };

    ui::Display menu("Menu", {
        .Width = ui::Fill,
        .Height = ui::Fill,
        .Alignment = ui::Alignment::Center,
        .Distribution = ui::Distribution::Center,
        .Children = {
            ui::Column({
                .Width = 380,
                .Gap = 12,
                .Children = {
                    ui::Row({ .Gap = 14, .Alignment = ui::Alignment::Center, .Children = {
                        ui::Image("icon.png", { .Width = 56, .Height = 56 }),
                        ui::Label("Coin Rush", { .FontSize = 40 }),
                    } }),
                    ui::Label("Pick up more coins than everyone else before the clock runs out.", { .Color = Muted, .Wrap = true }),
                    ui::Label("Your name"),
                    nameField,
                    openToNetwork,
                    ui::Label("Off, only this computer can join, and Windows does not ask about the network.",
                        { .Color = Muted, .Wrap = true }),
                    ui::Button("Host a game", { .Width = ui::Fill, .Style = ui::ButtonStyle::Accent, .OnClick = hostGame }),
                    ui::Button("Join the game on this computer", { .Width = ui::Fill, .OnClick = [&] { play("127.0.0.1"); } }),
                    ui::Button("Find games on the local network", { .Width = ui::Fill, .OnClick = [&] {
                        foundServers = FindServers({ .Port = GamePort, .Seconds = 0.5f });
                        std::vector<std::string> items;
                        for (const FoundServer& server : foundServers)
                        {
                            items.push_back(std::format("{} at {}, {} of {} players", server.Name, server.Address, server.Clients, server.MaximumClients));
                        }
                        found.Items = items;
                        menuStatus.Text = items.empty() ? "No games found" : "";
                    } }),
                    found,
                    ui::Button("Join the chosen game", { .Width = ui::Fill, .OnClick = [&] {
                        int chosen = found.Selected;
                        if (chosen >= 0 && chosen < static_cast<int>(foundServers.size()))
                        {
                            play(foundServers[chosen].Address);
                        }
                    } }),
                    ui::Toggle("Sound", { .Checked = true, .OnChange = [&mixer](bool on) { mixer.Volume = on ? 1.0f : 0.0f; } }),
                    menuStatus,
                },
            }),
        },
    });

    ui::Display game("Game", {
        .Width = ui::Fill,
        .Height = ui::Fill,
        .Children = {
            ui::Row({
                .Width = ui::Fill,
                .Height = ui::Fill,
                .Padding = { 20 },
                .Gap = 20,
                .Children = {
                    arena,
                    ui::Column({
                        .Width = 240,
                        .Gap = 12,
                        .Children = {
                            roundLabel,
                            message,
                            ui::Label("Scores", { .FontSize = 18 }),
                            scores,
                            playerStatus,
                            ui::Spacer(),
                            addBot,
                            ui::Button("Leave", { .OnClick = [&] { leave(""); } }),
                        },
                    }),
                },
            }),
        },
    });

    pages = ui::Displays({ .Start = "Menu", .Children = { menu, game } });
    window.Content = pages;

    window.OnFrame = [&](float deltaSeconds) {
        if (me)
        {
            // What the controls say goes to the server every frame, unreliably.
            if (me->IsPlaying())
            {
                me->Steer(controls.Axis("Move"), controls.Pressed("Dash"));
            }
            me->Link.Update();
        }
        for (std::unique_ptr<Player>& bot : bots)
        {
            SteerBot(*bot, chance);
            bot->Link.Update();
        }
        if (host)
        {
            host->Update(deltaSeconds);
        }
        if (!me)
        {
            return;
        }
        if (!me->Problem.empty())
        {
            leave("Left the game: " + me->Problem);
            return;
        }

        // Once the host's table has arrived, the labels follow it by themselves.
        Node round = me->Copy.Find("Round");
        if (round && !roundLabel.Binding.Get())
        {
            roundLabel.Binding = ui::Bind(round["TimeLeft"], "{} seconds left");
            message.Binding = ui::Bind(round["Message"]);
        }
        scores.Text = Scoreboard(me->Copy);
        if (std::optional<Vector2> place = me->Place())
        {
            mixer.Listener.Position = Vector3 { place->X, 0.0f, -place->Y };
        }
        playerStatus.Text = me->IsPlaying()
            ? std::format("Playing as {}, {:.0f} ms to the host", me->Name, me->Link.RoundTrip() * 1000.0f)
            : std::string("Joining");
    };

    window.Run();
}
