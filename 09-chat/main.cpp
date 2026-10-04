// A chat with one-way messages: a server and three people, all in this window.
// Each person's column is a client of the server. What they send goes to the
// server, which passes it on to everyone; while someone types, the others see
// it. The switch on the left makes the network poor on purpose, dropping a
// tenth of the packets and delaying the rest, and the chat still arrives whole
// and in order.

#include <array>
#include <format>
#include <map>
#include <string>

#include <easyforge/network.h>
#include <easyforge/ui.h>
#include <easyforge/window.h>

using namespace easyforge;

namespace
{
    const Color Muted = Color::Hex("#8C95A8");
    const Color Accent = Color::Hex("#6FC3FF");

    constexpr NetworkConditions PoorNetwork { .Loss = 0.1f, .Latency = 0.07f, .Jitter = 0.03f };

    // One person: a client of the server, and the column that shows it.
    struct Person
    {
        std::string Name;
        Client Link;
        ui::Label Status { "", { .Color = Muted } };
        ui::Scroll Log { { .Width = ui::Fill, .Height = ui::Fill, .Padding = { 10 }, .Gap = 6 } };
        ui::Label Typing { "", { .Color = Muted } };
        ui::TextField Entry { { .Width = ui::Fill, .Placeholder = "Say something" } };
        ui::Button Leave { "Leave" };
        float LastTypingSent = -10.0f;
        std::map<std::string, float> TypingUntil;
    };

    void AddLine(Person& person, const std::string& text, Color color)
    {
        ui::Label line(text, { .Color = color, .Wrap = true });
        person.Log.Add(line);
        person.Log.ScrollIntoView(line);
    }
}

int main()
{
    Window window = Window::New({ .Title = "Chat", .Icon = "icon.png", .Width = 1180, .Height = 680 });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }
    ui::Root::Of(window).Theme = ui::Theme::Dark();

    // Port 0 takes any free port. Only this computer can connect, so Windows
    // does not ask whether to let the program on the network.
    Server server = Server::New({ .Port = 0, .MaximumClients = 8, .Name = "Chat", .ThisComputerOnly = true });
    if (!server)
    {
        Log(LogLevel::Error, server.Error());
        return 1;
    }

    // The server keeps each person's name by their connection's index.
    std::array<std::string, 8> names;
    server.OnMessage("Hello", [&](Connection from, const Message& message) {
        names[from.Index()] = message["Name"].AsText();
        server.SendToAll("Notice", { { "Text", names[from.Index()] + " joined" } });
    });
    server.OnMessage("Chat", [&](Connection from, const Message& message) {
        server.SendToAll("Chat", { { "Name", names[from.Index()] }, { "Text", message["Text"] } });
    });
    // Typing is sent many times and only the newest matters, so it goes unreliably.
    server.OnMessage("Typing", [&](Connection from, const Message&) {
        for (const Connection& other : server.Connections())
        {
            if (!(other == from))
            {
                other.Send("Typing", { { "Name", names[from.Index()] } }, Delivery::Unreliable);
            }
        }
    });
    server.OnDisconnected = [&](Connection client, std::string why) {
        server.SendToAll("Notice", { { "Text", std::format("{} left ({})", names[client.Index()], why) } });
    };

    std::array<Person, 3> people;
    people[0].Name = "Ari";
    people[1].Name = "Bea";
    people[2].Name = "Cy";

    float time = 0.0f;
    bool poor = false;

    auto join = [&](Person& person) {
        person.Link = Client::New({ .Address = "127.0.0.1", .Port = server.Port(), .Conditions = poor ? PoorNetwork : NetworkConditions {} });
        person.Link.OnMessage("Chat", [&person](const Message& message) {
            std::string name = message["Name"];
            AddLine(person, name + ": " + message["Text"].AsText(), name == person.Name ? Accent : Color::White);
            person.TypingUntil.erase(name);
        });
        person.Link.OnMessage("Notice", [&person](const Message& message) {
            AddLine(person, message["Text"], Muted);
        });
        person.Link.OnMessage("Typing", [&person, &time](const Message& message) {
            person.TypingUntil[message["Name"]] = time + 1.5f;
        });
        person.Link.OnDisconnected = [&person](std::string why) {
            AddLine(person, "Disconnected: " + why, Muted);
        };
        // Sent at once, and delivered as soon as the server accepts.
        person.Link.Send("Hello", { { "Name", person.Name } });
    };

    ui::Row columns({ .Width = ui::Fill, .Height = ui::Fill, .Gap = 16 });
    for (Person& person : people)
    {
        auto send = [&person] {
            std::string text = person.Entry.Text;
            if (!text.empty() && person.Link.IsConnected())
            {
                person.Link.Send("Chat", { { "Text", text } });
                person.Entry.Text = "";
            }
        };
        person.Entry.OnSubmit = [send](const std::string&) { send(); };
        person.Entry.OnChange = [&person, &time](const std::string&) {
            if (time - person.LastTypingSent > 0.3f)
            {
                person.Link.Send("Typing", {}, Delivery::Unreliable);
                person.LastTypingSent = time;
            }
        };
        person.Leave.OnClick = [&person, &join] {
            if (person.Link.IsConnected())
            {
                person.Link.Disconnect();
            }
            else
            {
                join(person);
            }
        };

        columns.Add(ui::Panel({
            .Width = ui::Fill,
            .Height = ui::Fill,
            .Padding = { 14 },
            .Gap = 10,
            .CornerRadius = 12.0f,
            .Background = Color::Hex("#171B24"),
            .Children = {
                ui::Row({
                    .Gap = 10,
                    .Alignment = ui::Alignment::Center,
                    .Children = { ui::Label(person.Name, { .FontSize = 20 }), ui::Spacer(), person.Leave },
                }),
                person.Status,
                ui::Panel({
                    .Width = ui::Fill,
                    .Height = ui::Fill,
                    .CornerRadius = 8.0f,
                    .Background = Color::Hex("#10131A"),
                    .Children = { person.Log },
                }),
                person.Typing,
                ui::Row({ .Gap = 8, .Children = { person.Entry, ui::Button("Send", { .Style = ui::ButtonStyle::Accent, .OnClick = send }) } }),
            },
        }));
        join(person);
    }

    ui::Label serverStatus("", { .Color = Muted, .Wrap = true });
    window.Content = ui::Row({
        .Padding = { 20 },
        .Gap = 20,
        .Children = {
            ui::Column({
                .Width = 230,
                .Gap = 12,
                .Children = {
                    ui::Label("Server", { .FontSize = 20 }),
                    serverStatus,
                    ui::Toggle("Poor network", { .OnChange = [&](bool on) {
                        poor = on;
                        NetworkConditions conditions = on ? PoorNetwork : NetworkConditions {};
                        server.Conditions = conditions;
                        for (Person& person : people)
                        {
                            person.Link.Conditions = conditions;
                        }
                    } }),
                    ui::Label("Drops one packet in ten and holds the rest back by 40 to 100 ms, both ways. "
                              "Chat still arrives once and in order; only the typing hints, sent unreliably, "
                              "may go missing.",
                        { .Color = Muted, .Wrap = true }),
                },
            }),
            columns,
        },
    });

    window.OnFrame = [&](float deltaSeconds) {
        time += deltaSeconds;
        server.Update();
        for (Person& person : people)
        {
            person.Link.Update();
            bool connected = person.Link.IsConnected();
            person.Status.Text = connected
                ? std::format("Connected, {:.0f} ms there and back", person.Link.RoundTrip() * 1000.0f)
                : std::string("Not connected");
            person.Leave.Text = connected ? "Leave" : "Join";
            person.Entry.Enabled = connected;

            std::string typing;
            for (const auto& [name, until] : person.TypingUntil)
            {
                if (until > time)
                {
                    typing += (typing.empty() ? "" : ", ") + name;
                }
            }
            person.Typing.Text = typing.empty() ? std::string() : typing + " is typing...";
        }
        serverStatus.Text = std::format("Listening on port {}\n{} of 8 people connected", server.Port(), server.Connections().size());
    };

    window.Run();
}
