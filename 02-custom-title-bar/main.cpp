// A window with a title bar of its own: the icon, the title, menus, and the
// window's buttons, which show the snap layouts on Windows 11. Below it, lists
// to choose from, each with items to check off and notes, in tabs.

#include <map>
#include <string>
#include <vector>

#include <easyforge/ui.h>
#include <easyforge/window.h>

using namespace easyforge;

int main()
{
    Window window = Window::New({
        .Title = "Notes",
        .Icon = "icon.png",
        .Width = 960,
        .Height = 600,
        .MinimumWidth = 560,
        .MinimumHeight = 360,
    });
    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }

    ui::Root root = ui::Root::Of(window);
    bool dark = window.SystemColorScheme() == ColorScheme::Dark;

    // What each list holds while the program runs.
    struct Notes
    {
        std::vector<std::string> Items;
        std::string Text;
    };
    std::vector<std::string> names = { "Shopping", "Ideas", "Reading" };
    std::map<std::string, Notes> lists;
    std::string current = names.front();

    ui::Label title(current, { .FontSize = 28 });
    ui::Column items({ .Gap = 6 });
    ui::Label count("Nothing added yet", { .FontSize = 13 });
    ui::TextField entry({ .Width = ui::Fill, .Placeholder = "Add an item and press Enter" });
    ui::TextArea notes({ .Width = ui::Fill, .Height = ui::Fill, .Placeholder = "Anything else to remember" });

    auto countText = [&] {
        std::size_t total = lists[current].Items.size();
        return total == 0 ? std::string("Nothing added yet") : std::to_string(total) + (total == 1 ? " item" : " items");
    };

    // Shows a list's items and notes.
    auto show = [&](const std::string& name) {
        current = name;
        title.Text = name;
        items.Clear();
        for (const std::string& item : lists[name].Items)
        {
            items.Add(ui::Checkbox(item));
        }
        count.Text = countText();
        notes.Text = lists[name].Text;
    };

    entry.OnSubmit = [&](const std::string& text) {
        if (text.empty())
        {
            return;
        }
        lists[current].Items.push_back(text);
        items.Add(ui::Checkbox(text));
        count.Text = countText();
        entry.Text = "";
    };
    notes.OnChange = [&](const std::string& text) { lists[current].Text = text; };

    // Asks before emptying a list.
    ui::Dialog confirm;
    confirm = ui::Dialog({
        .Title = "Clear the list?",
        .Children = { ui::Label("Every item in it will be removed.") },
        .Buttons = {
            ui::Button("Cancel", { .OnClick = [&] { confirm.Close(); } }),
            ui::Button("Clear", {
                .Style = ui::ButtonStyle::Accent,
                .OnClick = [&] {
                    lists[current].Items.clear();
                    show(current);
                    confirm.Close();
                },
            }),
        },
    });

    // Checks or unchecks every item of the list shown.
    auto checkAll = [&](bool checked) {
        for (const ui::Element& item : items.Children())
        {
            item.As<ui::Checkbox>().Checked = checked;
        }
    };

    // Switches between the light and dark themes, gradually.
    ui::Button themeButton(dark ? "Light" : "Dark", { .Style = ui::ButtonStyle::Subtle });
    themeButton.OnClick = [&] {
        dark = !dark;
        root.Theme.AnimateTo(dark ? ui::Theme::Dark() : ui::Theme::Light(), { .Duration = 0.3f });
        themeButton.Text = dark ? "Light" : "Dark";
    };

    window.TitleBar = ui::TitleBar({
        .Height = 40,
        .Children = {
            ui::Image("icon.png", { .Width = 18, .Height = 18 }),
            ui::Label("Notes"),
            ui::Menu("File", {
                .Items = {
                    { .Text = "Clear list", .OnClick = [&] { confirm.Open(root); } },
                    { .Separator = true },
                    { .Text = "Quit", .OnClick = [&] { window.Close(); } },
                },
            }),
            ui::Menu("Edit", {
                .Items = {
                    { .Text = "Check all", .OnClick = [&] { checkAll(true); } },
                    { .Text = "Uncheck all", .OnClick = [&] { checkAll(false); } },
                },
            }),
            ui::Spacer(),
            themeButton,
            ui::WindowButtons(),
        },
    });

    window.Content = ui::Row({
        .Children = {
            ui::Panel({
                .Width = 220,
                .Padding = 12,
                .Gap = 4,
                .CornerRadius = 0.0f,
                .Children = {
                    ui::Label("Lists", { .Margin = { 8, 4, 0, 4 }, .FontSize = 13 }),
                    ui::List({
                        .Width = ui::Fill,
                        .Height = ui::Fill,
                        .Items = names,
                        .Selected = 0,
                        .Background = Color::Transparent,
                        .BorderWidth = 0.0f,
                        .OnSelect = [&](int row) { show(names[static_cast<std::size_t>(row)]); },
                    }),
                },
            }),
            ui::Column({
                .Width = ui::Fill,
                .Padding = 28,
                .Gap = 8,
                .Children = {
                    title,
                    ui::Tabs({
                        .Children = {
                            ui::Display("Items", {
                                .Padding = { 0, 14, 0, 0 },
                                .Gap = 14,
                                .Children = { entry, ui::Scroll({ .Height = ui::Fill, .Children = { items } }), count },
                            }),
                            ui::Display("Notes", { .Padding = { 0, 14, 0, 0 }, .Children = { notes } }),
                        },
                    }),
                },
            }),
        },
    });
    entry.Focus();

    window.Run();
}
