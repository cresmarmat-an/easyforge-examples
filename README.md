# easyforge examples

Small programs that show how to use [easyforge](https://github.com/cresmarmat-an/easyforge),
one topic each. Every example is a single `main.cpp` with the files it needs,
short enough to read in one sitting.

The examples fetch easyforge with CMake exactly as your own project would, so
they only use what the public interface offers.

## Building

```bash
cmake -S . -B build
cmake --build build --config Debug
```

Each example becomes a program named after its folder. To build against a copy
of easyforge on your computer instead of the one on GitHub:

```bash
cmake -S . -B build -D FETCHCONTENT_SOURCE_DIR_EASYFORGE=../easyforge
```

## The examples

easyforge is at 0.0.1-alpha, and its libraries arrive one at a time. An example
whose libraries are not available yet is skipped when you configure, with a
message saying which library it is waiting for.

| Example | Shows | Needs |
|---|---|---|
| [01-empty-window](01-empty-window) | An empty window with a title and an icon | window |
| [02-custom-title-bar](02-custom-title-bar) | A title bar of its own with menus, a list to choose from, tabs, a text area, and a dialog | window, ui |
| [03-displays](03-displays) | Switching between screens with fades, slides, and scaling, and going back | window, ui |
| [04-effects-and-shaders](04-effects-and-shaders) | Every built-in effect, and a shader of your own | window, ui |
| [05-live-data](05-live-data) | Labels that follow a data table, edits that undo, and a 3D scene behind | window, ui, data |
| [06-scripted-interface](06-scripted-interface) | A menu, settings, and a game screen run by a script | window, ui, script |
| 07-sound | Music and positional sound | sound |
| 08-physics | A stack of boxes that settles | physics |
| 09-chat | One-way messages between programs | network |
| 10-lobby | Two-way requests and replies | network |
| 11-shared-table | A data table kept in step between machines | data, network |
| 12-small-multiplayer-game | Every library together | all |

Examples without a link are not written yet.

## License

The examples are released under the [MIT License](LICENSE). Copyright (c) 2026
Cresmar Mat-an. Copy them into your own projects freely.
