# MultiMonitorPanel

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-blue)
![Language](https://img.shields.io/badge/C%2B%2B-WinAPI-00599C)

A tiny launcher dock — **one per monitor** — for Windows 10/11. Unlike the
built-in taskbar, each monitor can show its **own** set of buttons. Each panel
is a real system **AppBar** with a reserved area: maximized windows don't cover
it — Windows actually carves a strip out of the monitor's work area for it.

A single ~200 KB native `.exe`, no runtime dependencies (uses GDI+, comctl32 and
shcore that ship with Windows).

---

## Why

The Windows 10/11 taskbar **can't** pin different apps to different monitors —
the same chain of icons is duplicated everywhere. MultiMonitorPanel gives you a
separate strip of launch buttons on each screen, for free, without DisplayFusion
or Rainmeter skins.

It's written in plain C++/WinAPI because an AppBar is a shell API, and going
native keeps the layers to a minimum: correct DPI handling, monitor
connect/disconnect, and full-screen apps all just work.

## Features

- **One dock per monitor**, each with its own buttons.
- **Real AppBar** — reserves screen space; maximized windows respect it.
- **Per-monitor launch** — the app you click opens centered on *that* monitor.
- **Alignment groups** — pin buttons to the left, center or right of a strip
  (e.g. folders left, programs right), with optional vertical separators.
- **Built-in settings editor** — a GUI window to edit everything in place
  (right-click a panel → *Edit config…*); no text files, no registry digging.
- **Custom icons** — `.ico` / `.png` / `.jpg` / `.bmp` / `.gif` / `.tiff`,
  auto-resampled.
- **Per-monitor DPI aware** (PerMonitorV2), handles resolution/rotation changes
  and monitor (dis)connect by rebuilding the panels automatically.
- **Run-as-admin** and **console** launch flags per button.
- Single instance, friendly for `shell:startup` autostart.

> The settings UI and the bundled manual (`info.txt`) are in Russian.

## Install / Run

1. Grab the latest build from the [Releases](../../releases) page (the `build`
   folder: `panel.exe` + the `ico` folder), or build it yourself (below).
2. Double-click **`panel.exe`**. A dock appears at the bottom of every monitor.
3. Autostart: drop a shortcut to `panel.exe` into the startup folder
   (`Win+R` → `shell:startup`).

A second launch exits silently — only one instance runs.

## Configure

Right-click any panel for the menu:

- **Edit config…** — opens the settings editor window. Pick a monitor (add/remove
  monitors with **+/−**, so you can pre-configure a screen that's currently
  unplugged), edit each button's label / target / arguments / icon, choose its
  edge (left / center / right), and add separators. **Apply** / **OK** redraw the
  panels instantly.
- **Reload config**, **Set default config** (a self-documenting sample),
  **Erase config**, **Open app folder**.

Settings live in the registry under `HKCU\Software\MultiMonitorPanel`. A button's
`target` can be an executable, a full path, a folder (opens in Explorer), or a
`shell:AppsFolder\<AUMID>` URI for Store apps.

## Build

Requirements: **Visual Studio 2022 Build Tools** with the C++ workload (MSVC +
Windows SDK). No CMake, no vcpkg, no .NET.

```bat
build.bat
```

It calls `vcvars64`, compiles the resources (`rc`) and the single source file
(`cl /std:c++17 /O2 /utf-8`), and links against the standard Windows libraries.
The result is `build\panel.exe`.

## Project layout

```
MultiMonitorPanel/
├── build.bat          ← build script (vcvars64 + cl + rc)
├── svg2png.ps1        ← optional SVG→PNG icon converter (headless Chrome/Edge)
├── src/panel.cpp      ← all the code, one file
├── res/               ← app manifest (PerMonitorV2 DPI + ComCtl v6) and .rc
└── build/             ← portable kit: panel.exe + ico/ icons
```

## License

[MIT](LICENSE) © 2026 Evgenii Shapovalov

---

## По-русски

**MultiMonitorPanel** — отдельная панель-лаунчер на каждый монитор Windows 10/11.
В отличие от штатного таскбара, на разных мониторах могут быть разные кнопки.
Это настоящая системная панель (AppBar) с зарезервированной областью — развёрнутые
окна её не перекрывают. Один нативный `.exe` ~200 КБ без зависимостей.

- **Запуск:** двойной клик по `build\panel.exe`. Автозапуск — ярлык в
  `shell:startup`.
- **Настройка:** правый клик по панели → **Edit config…** — окно-редактор:
  выбор монитора (кнопки **+/−** добавляют/убирают мониторы), правка кнопок
  (подпись, что запускать, аргументы, иконка), край (слева/центр/справа),
  разделители. **Применить/OK** сразу перерисовывают панели.
- **Сборка:** `build.bat` (нужны VS 2022 Build Tools + C++ workload).

Интерфейс и встроенная справка (`info.txt`) — на русском.

Лицензия: [MIT](LICENSE) © 2026 Евгений Шаповалов
