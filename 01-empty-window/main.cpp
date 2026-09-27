// An empty window with a title and an icon. It stays open until it is closed.

#include <easyforge/window.h>

using namespace easyforge;

int main()
{
    Window window = Window::New({
        .Title = "Empty window",
        .Icon = "icon.png",
        .Width = 1280,
        .Height = 720,
    });

    if (!window)
    {
        Log(LogLevel::Error, window.Error());
        return 1;
    }

    window.Run();
}
