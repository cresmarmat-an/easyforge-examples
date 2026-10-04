// A lobby built on two-way requests: signing in, listing rooms, creating them,
// and joining them are all requests that the server answers or refuses with a
// reason. Two people use the same server from this window, and each keeps a log
// of what they asked and what came back. The switches on the left make the
// network poor, or stop the server answering so the requests time out.

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <functional>
#include <string>
#include <vector>

#include <easyforge/network.h>
#include <easyforge/ui.h>
#include <easyforge/window.h>

using namespace easyforge;

namespace
{
    const Color Muted = Color::Hex("#8C95A8");
    const Color Good = Color::Hex("#7DD69B");
    const Color Bad = Color::Hex("#FF8A7A");

    constexpr NetworkConditions PoorNetwork { .Loss = 0.1f, .Latency = 0.07f, .Jitter = 0.03f };
    constexpr float RequestSeconds = 2.0f;

    struct Room
    {
        std::string Name;
        int Limit = 4;
        std::vector<int> Players;
    };

    // One person: a client of the server, and the column that shows it.
    struct Person
    {
        Client Link;
        ui::TextField NameField;
        ui::Label Status { "Not signed in", { .Color = Muted } };
        ui::List Rooms { { .Width = ui::Fill, .Height = 150 } };
        ui::TextField NewRoom { { .Width = ui::Fill, .Placeholder = "New room" } };
        ui::Scroll Log { { .Width = ui::Fill, .Height = ui::Fill, .Padding = { 10 }, .Gap = 4 } };
        std::vector<std::string> RoomNames;
        std::string Name;
        std::string InRoom;
        float NextRefresh = 0.0f;
    };

    void AddLine(Person& person, const std::string& text, Color color)
    {
        ui::Label line(text, { .Color = color, .Wrap = true });
        person.Log.Add(line);
        person.Log.ScrollIntoView(line);
    }

    std::string Trimmed(std::string text)
    {
        text.erase(0, text.find_first_not_of(' '));
        text.erase(text.find_last_not_of(' ') + 1);
        return text;
    }
}

int main()
{
    Window window = Window::New({ .Title = "Lobby", .Icon = "icon.png", .Width = 1180, .Height = 720 });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }
    ui::Root::Of(window).Theme = ui::Theme::Dark();

    Server server = Server::New({ .Port = 0, .MaximumClients = 8, .Name = "Lobby", .ThisComputerOnly = true });
    if (!server)
    {
        Log(LogLevel::Error, server.Error());
        return 1;
    }

    // The server's side: who is signed in, by connection index, and the rooms.
    std::array<std::string, 8> names;
    std::vector<Room> rooms = { { "Meadow", 4, {} }, { "Cave", 1, {} } };

    auto findRoom = [&](const std::string& name) {
        return std::ranges::find_if(rooms, [&](const Room& room) { return room.Name == name; });
    };
    auto leaveRooms = [&](int player) {
        for (Room& room : rooms)
        {
            std::erase(room.Players, player);
        }
    };

    server.OnRequest("SignIn", [&](Connection from, const Message& request) -> Result<Message> {
        std::string name = Trimmed(request["Name"]);
        if (name.size() < 2 || name.size() > 12)
        {
            return Failure("a name needs 2 to 12 characters");
        }
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            if (names[index] == name && static_cast<int>(index) != from.Index())
            {
                return Failure(name + " is already signed in");
            }
        }
        names[from.Index()] = name;
        return Message { { "Name", name } };
    });

    server.OnRequest("Rooms", [&](Connection, const Message&) {
        Message answer { { "Count", rooms.size() } };
        for (std::size_t index = 0; index < rooms.size(); ++index)
        {
            const Room& room = rooms[index];
            std::string players;
            for (int player : room.Players)
            {
                players += (players.empty() ? "" : ", ") + names[player];
            }
            answer[std::format("Name {}", index)] = room.Name;
            answer[std::format("Limit {}", index)] = room.Limit;
            answer[std::format("Players {}", index)] = players;
        }
        return answer;
    });

    server.OnRequest("CreateRoom", [&](Connection from, const Message& request) -> Result<Message> {
        std::string name = Trimmed(request["Name"]);
        if (names[from.Index()].empty())
        {
            return Failure("sign in first");
        }
        if (name.size() < 2 || name.size() > 16)
        {
            return Failure("a room name needs 2 to 16 characters");
        }
        if (findRoom(name) != rooms.end())
        {
            return Failure("there is already a room called " + name);
        }
        rooms.push_back({ name, 4, {} });
        return Message { { "Room", name } };
    });

    server.OnRequest("JoinRoom", [&](Connection from, const Message& request) -> Result<Message> {
        std::string name = request["Name"];
        auto room = findRoom(name);
        if (names[from.Index()].empty())
        {
            return Failure("sign in first");
        }
        if (room == rooms.end())
        {
            return Failure("there is no room called " + name);
        }
        if (std::ranges::find(room->Players, from.Index()) != room->Players.end())
        {
            return Failure("you are in " + name + " already");
        }
        if (static_cast<int>(room->Players.size()) >= room->Limit)
        {
            return Failure(name + " is full");
        }
        leaveRooms(from.Index());
        room->Players.push_back(from.Index());
        return Message { { "Room", name }, { "Players", room->Players.size() } };
    });

    server.OnRequest("LeaveRoom", [&](Connection from, const Message&) {
        leaveRooms(from.Index());
        return Message {};
    });

    server.OnDisconnected = [&](Connection client, std::string) {
        leaveRooms(client.Index());
        names[client.Index()].clear();
    };

    // Both start as Ari, so whoever signs in second is refused until they pick
    // another name.
    std::array<Person, 2> people;
    people[0].NameField = ui::TextField({ .Width = ui::Fill, .Text = "Ari" });
    people[1].NameField = ui::TextField({ .Width = ui::Fill, .Text = "Ari" });

    float time = 0.0f;
    bool answering = true;

    // Sends a request, notes it in the log, and notes the reply when it comes.
    auto ask = [&](Person& person, const std::string& name, const Message& message, std::string what,
                   std::function<void(const Message&)> onAnswer) {
        AddLine(person, "asked to " + what, Muted);
        auto start = std::chrono::steady_clock::now();
        person.Link.Request(name, message, [&person, start, onAnswer](const Reply& reply) {
            auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (reply)
            {
                AddLine(person, std::format("   done in {} ms", milliseconds), Good);
                onAnswer(reply.Message);
            }
            else
            {
                AddLine(person, std::format("   failed after {} ms: {}", milliseconds, reply.Error), Bad);
            }
        }, { .Timeout = RequestSeconds });
    };

    // The room list is asked for once a second, without a note in the log.
    auto refresh = [&](Person& person) {
        person.NextRefresh = time + 1.0f;
        person.Link.Request("Rooms", {}, [&person](const Reply& reply) {
            if (!reply)
            {
                return;
            }
            std::vector<std::string> items;
            person.RoomNames.clear();
            int count = reply.Message["Count"];
            for (int index = 0; index < count; ++index)
            {
                std::string name = reply.Message[std::format("Name {}", index)];
                std::string players = reply.Message[std::format("Players {}", index)];
                int limit = reply.Message[std::format("Limit {}", index)];
                person.RoomNames.push_back(name);
                items.push_back(std::format("{}  ({} of {}){}{}", name, players.empty() ? 0 : std::ranges::count(players, ',') + 1,
                    limit, players.empty() ? "" : ": ", players));
            }
            int selected = person.Rooms.Selected;
            person.Rooms.Items = items;
            person.Rooms.Selected = std::min(selected, static_cast<int>(items.size()) - 1);
        }, { .Timeout = RequestSeconds });
    };

    ui::Row columns({ .Width = ui::Fill, .Height = ui::Fill, .Gap = 16 });
    for (Person& person : people)
    {
        person.Link = Client::New({ .Address = "127.0.0.1", .Port = server.Port(), .Timeout = 30.0f });

        ui::Button signIn("Sign in", { .Style = ui::ButtonStyle::Accent });
        signIn.OnClick = [&] {
            ask(person, "SignIn", { { "Name", person.NameField.Text.Get() } }, "sign in as " + person.NameField.Text.Get(),
                [&person](const Message& answer) { person.Name = answer["Name"].AsText(); });
        };
        ui::Button join("Join");
        join.OnClick = [&] {
            int selected = person.Rooms.Selected;
            if (selected < 0 || selected >= static_cast<int>(person.RoomNames.size()))
            {
                AddLine(person, "choose a room first", Muted);
                return;
            }
            std::string room = person.RoomNames[selected];
            ask(person, "JoinRoom", { { "Name", room } }, "join " + room, [&person, &refresh](const Message& answer) {
                person.InRoom = answer["Room"].AsText();
                refresh(person);
            });
        };
        ui::Button leave("Leave");
        leave.OnClick = [&] {
            ask(person, "LeaveRoom", {}, "leave the room", [&person, &refresh](const Message&) {
                person.InRoom.clear();
                refresh(person);
            });
        };
        ui::Button create("Create");
        create.OnClick = [&] {
            std::string room = person.NewRoom.Text;
            ask(person, "CreateRoom", { { "Name", room } }, "create " + room, [&person, &refresh](const Message&) {
                person.NewRoom.Text = "";
                refresh(person);
            });
        };

        columns.Add(ui::Panel({
            .Width = ui::Fill,
            .Height = ui::Fill,
            .Padding = { 14 },
            .Gap = 10,
            .CornerRadius = 12.0f,
            .Background = Color::Hex("#171B24"),
            .Children = {
                ui::Row({ .Gap = 8, .Children = { person.NameField, signIn } }),
                person.Status,
                ui::Label("Rooms", { .FontSize = 17 }),
                person.Rooms,
                ui::Row({ .Gap = 8, .Children = { join, leave } }),
                ui::Row({ .Gap = 8, .Children = { person.NewRoom, create } }),
                ui::Label("Requests", { .FontSize = 17 }),
                ui::Panel({
                    .Width = ui::Fill,
                    .Height = ui::Fill,
                    .CornerRadius = 8.0f,
                    .Background = Color::Hex("#10131A"),
                    .Children = { person.Log },
                }),
            },
        }));
    }

    ui::Label serverStatus("", { .Color = Muted, .Wrap = true });
    window.Content = ui::Row({
        .Padding = { 20 },
        .Gap = 20,
        .Children = {
            ui::Column({
                .Width = 250,
                .Gap = 12,
                .Children = {
                    ui::Label("Server", { .FontSize = 20 }),
                    serverStatus,
                    ui::Toggle("Poor network", { .OnChange = [&](bool on) {
                        server.Conditions = on ? PoorNetwork : NetworkConditions {};
                        for (Person& person : people)
                        {
                            person.Link.Conditions = server.Conditions;
                        }
                    } }),
                    ui::Label("Drops one packet in ten and holds the rest back by 40 to 100 ms, both ways. "
                              "Every request still gets its answer.",
                        { .Color = Muted, .Wrap = true }),
                    ui::Toggle("Server answers", { .Checked = true, .OnChange = [&](bool on) { answering = on; } }),
                    ui::Label(std::format("Off, the server stops updating, and requests give up after {:.0f} seconds.", RequestSeconds),
                        { .Color = Muted, .Wrap = true }),
                },
            }),
            columns,
        },
    });

    window.OnFrame = [&](float deltaSeconds) {
        time += deltaSeconds;
        if (answering)
        {
            server.Update();
        }
        for (Person& person : people)
        {
            person.Link.Update();
            if (person.Link.IsConnected() && time >= person.NextRefresh)
            {
                refresh(person);
            }
            if (!person.Link.IsConnected())
            {
                person.Status.Text = "Connecting";
            }
            else if (person.Name.empty())
            {
                person.Status.Text = "Connected, not signed in";
            }
            else
            {
                person.Status.Text = person.InRoom.empty() ? std::format("Signed in as {}", person.Name)
                                                           : std::format("Signed in as {}, in {}", person.Name, person.InRoom);
            }
        }

        int signedIn = static_cast<int>(std::ranges::count_if(names, [](const std::string& name) { return !name.empty(); }));
        std::string text = std::format("Port {}\n{} connected, {} signed in\n", server.Port(), server.Connections().size(), signedIn);
        for (const Room& room : rooms)
        {
            text += std::format("\n{}: {} of {}", room.Name, room.Players.size(), room.Limit);
        }
        serverStatus.Text = text;
    };

    window.Run();
}
