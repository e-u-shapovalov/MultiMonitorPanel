# MultiMonitorPanel - One Launcher Dock Per Windows Monitor

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-blue)
![Language](https://img.shields.io/badge/C%2B%2B-WinAPI-00599C)

**MultiMonitorPanel** is a small portable launcher for Windows 10/11. It creates
one dock on each monitor, and every dock can have its own buttons.

Use it when the Windows taskbar repeats the same pinned apps everywhere, but your
real workflow is split by screen: folders on the left monitor, IDEs in the
center, terminals, admin tools or remote sessions on the right.

Russian README: [README.md](README.md)  
Detailed manual: [docs/MANUAL.en.md](docs/MANUAL.en.md)  
Download guide: [DOWNLOAD.md](DOWNLOAD.md)

## Download

Open the latest GitHub Release:

<https://github.com/e-u-shapovalov/MultiMonitorPanel/releases/latest>

In **Assets**, download the ready-to-run program:

- current `v1.0.0` release: **`panel.exe`**;
- future releases may provide a zip archive or installer in the same **Assets** block.

If you only want to run the app, do **not** download **Source code** and do **not**
use **Code -> Download ZIP**. Those are for developers.

## Install / Run

MultiMonitorPanel is portable and does not need installation.

1. Put `panel.exe` into a permanent folder, for example:

   ```text
   C:\Tools\MultiMonitorPanel\panel.exe
   ```

2. Double-click `panel.exe`.
3. A dock appears at the bottom of every monitor.
4. Right-click a panel and choose **Edit config...** to customize buttons.

For autostart, press `Win+R`, type `shell:startup`, and place a shortcut to
`panel.exe` into that folder.

If Windows SmartScreen warns about an unknown app, verify that it was downloaded
from this repository, then use **More info -> Run anyway**.

## Screenshot

![MultiMonitorPanel launcher dock on Windows](docs/screenshots/multi-monitor-panel-windows-ru.png)

The screenshot shows MultiMonitorPanel as a separate launch strip above the
regular Windows taskbar.

## Features

- One launcher dock per monitor.
- Real Windows AppBar: maximized windows respect the reserved strip.
- Launch placement: new windows are moved to the monitor where the button was clicked.
- Left, center and right alignment groups.
- Visual separators between button groups.
- Built-in GUI settings editor, no manual registry editing required.
- Custom icons: `.ico`, `.png`, `.jpg`, `.bmp`, `.gif`, `.tiff`.
- Per-button **run as administrator** and **console/conhost** flags.
- Russian and English UI, with external `lang\*.lang` files for translations.
- PerMonitorV2 DPI awareness and automatic panel rebuild on monitor changes.
- Native C++/WinAPI executable, no .NET, Electron or Node.js runtime.

## How It Works

On startup, MultiMonitorPanel enumerates monitors from left to right, creates a
real AppBar window on each screen, and reads settings from:

```text
HKCU\Software\MultiMonitorPanel
```

When you click a button, the app launches the target through Windows Shell and
tries to move the new window to the clicked monitor. This works best for regular
Win32 applications. Some single-instance, UWP/Store or launcher-based apps may
decide their own placement.

## Build From Source

Requirements:

- Windows 10/11;
- Visual Studio 2022 Build Tools;
- Desktop development with C++ workload;
- Windows SDK.

```bat
git clone https://github.com/e-u-shapovalov/MultiMonitorPanel.git
cd MultiMonitorPanel
build.bat
```

The script compiles `src\panel.cpp` and `res\panel.rc` with MSVC and produces
`build\panel.exe`.

## License

[MIT](LICENSE) © 2026 Evgenii Shapovalov
