# MultiMonitorPanel — Manual

*(Русская версия: [MANUAL.ru.md](MANUAL.ru.md))*

---

## What it is

**MultiMonitorPanel** is a portable launcher panel for Windows 10/11: one dock on
**every** monitor, each with its own buttons. Unlike the Windows taskbar,
different monitors do not have to show the same pinned icons. It is a real system
bar (**AppBar**): maximized windows do not cover it — Windows reserves a strip
for it out of the monitor's work area.

A single native `.exe` under 400 KB with no runtime dependencies (GDI+, comctl32 and
shcore ship with every Windows 10/11).

## Download the release

1. Open
   [Releases / Latest](https://github.com/e-u-shapovalov/MultiMonitorPanel/releases/latest).
2. In **Assets**, download the ready-to-run program. In the current `v1.0.0`
   release, this is `panel.exe`.
3. Put `panel.exe` into a permanent folder, for example
   `C:\Tools\MultiMonitorPanel`.
4. Run `panel.exe`. The `ico` folder is created next to it on first start; place
   custom icons there if you need them.

If a future release ships as `MultiMonitorPanel-*-win64.zip`, download that
archive from **Assets** and extract it before running.

If you are a regular user, do not use **Code → Download ZIP** and do not download
**Source code**. Those files are for developers, not for running the app.

If Windows SmartScreen warns about an unknown app, verify that the file came from
this repository, then use **More info → Run anyway**.

## Why it exists

With two or three monitors, identical pinned icons on every taskbar get in the
way: one screen may need folders and task lists, another an IDE, another
terminals, admin tools or remote connections. MultiMonitorPanel was created as a
small native alternative to heavy desktop customization suites: a separate panel
per screen, without .NET, Electron, skins or a large background footprint.

## How it works

On startup the program enumerates monitors from left to right, creates one system
AppBar panel on each screen, and reads settings from
`HKCU\Software\MultiMonitorPanel`. When you click a button, the target is launched
through Windows Shell and the new window is moved to the work area of the monitor
where the button was clicked. For `cmd`, PowerShell and other consoles, the
**Console** flag forces classic `conhost` so the window does not drift to the
monitor chosen by Windows Terminal.

## Starting

- Double-click `panel.exe`.
- A second launch silently exits (only one copy runs).
- **Autostart:** put a shortcut to `panel.exe` into the startup folder
  (`Win+R` → `shell:startup`).
- Panels rebuild themselves when a monitor is connected or disconnected.

## Menu (right-click a panel)

| Item | What it does |
|------|--------------|
| **Edit config…** | open the settings editor window (see below) |
| **Reload config** | re-read settings and redraw the panels |
| **Set default config** | write the standard sample set (for 3 monitors) — it demonstrates every feature |
| **Erase config** | remove all buttons (panels become empty) |
| **Язык / Language** | switch the interface language |
| **Open app folder (ico)** | open the folder with `panel.exe` (and the `ico` folder) |
| **About** | version, release date, author, repository link |
| **Exit** | quit the program |

## Settings editor

Right-click → **Edit config…**. The window edits settings in place — no `regedit`
or text files. It is **resizable** (drag an edge), so long arguments are fully
visible.

- **Top:** monitor picker (the **+/−** buttons add and remove a monitor — you can
  set up a screen that is not connected right now), bar height, icon folder.
- **Left:** the selected monitor's button list, grouped by edge — “left”,
  “center”, “right” sections.
- **Right:** the selected button's fields — label, what to run, arguments, icon
  (the `…` button picks a file), **edge** (which side of the bar to pin it to) and
  the **admin** / **console** / **separator** check boxes.
- **Add** / **+ Separator** / **Delete** / **▲** / **▼** — change the set and
  order. **▲/▼** at a section edge move the entry into the neighbouring group.
- A separator takes its edge from where it sits — move it with **▲/▼** into the
  right section (its edge radio is disabled).
- **OK** or **Apply** — write and redraw the panels at once (Apply keeps the
  window open). **Cancel** — leave without saving.

## Where settings live

In the registry: `HKCU\Software\MultiMonitorPanel` (not a file). Edit them through
the window. The registry is the source of truth; on OK/Apply the editor rewrites
the whole branch.

## How a button is built

| Field | Purpose |
|-------|---------|
| `label` | caption (shown as a tooltip) |
| `target` | what to run |
| `args` | command-line arguments (optional) |
| `icon` | a custom icon (optional) |
| `flags` | extra flags (optional) |

`target` can be:

- a program by name: `notepad.exe`, `calc.exe`;
- a full path: `C:\Program Files\App\app.exe` (**no quotes** — arguments go in the
  separate `args` field);
- a folder (opens in Explorer): `C:\Users\Name\Downloads`;
- a Store app: `shell:AppsFolder\<AUMID>`.

## Button flags

In the editor these are check boxes and the block choice:

- **admin** — run as administrator (UAC prompt).
- **left / center / right** — alignment block: left (default), center, right edge.
- **sep** — not a button, but a vertical separator.
- **console** — for `cmd`/`powershell` and other consoles: open on the **same**
  monitor as the button (via the classic `conhost`). Without it a console may
  drift to another monitor — its window is owned by Windows Terminal, not the cmd.

## Blocks (left / center / right)

The bar can be split into 2–3 blocks. For example: folders on the left (no flag),
programs on the right (the `right` flag). Some icons sit at the left edge, some at
the right.

## Separators

To visually split buttons into groups — insert a separator: press **“+ Separator”**
in the editor (or tick the “Separator” box on a selected button). It is a thin
vertical line; it also obeys the edge (left/center/right).

## Icons

By default icons come from the `ico` subfolder **next to** `panel.exe` (created
automatically on start). Wherever you move the `.exe`, `ico` is looked up next to
it. In the `icon` field just write a file name (`foo.png`) — it is looked up in
that folder; or give a full path. Change the folder via the `icon_dir` value
(relative — next to the exe; absolute — anywhere).

Formats: `.ico .png .jpg .bmp .gif .tiff` (**SVG is not supported** — convert SVG
to PNG beforehand). Any size is shrunk to 32 px; a 48–256 px square PNG with
transparency works best.

## Environment variables

In `target` / `args` / `icon` you can use `%USERNAME%`, `%USERPROFILE%`,
`%APPDATA%` and so on — they are expanded automatically.

## Different buttons on different monitors

`monitor_1` is the leftmost monitor, `monitor_2` is to its right, and so on. Each
has its own button set. On a single-monitor machine only `monitor_1` is used.

## Interface language

The UI defaults to **Russian**; the second built-in language is **English**.
Switch it from the **Язык / Language** menu (the choice is stored in the registry;
on first run it is auto-detected from the system locale).

Strings live in `lang\<code>.lang` next to `panel.exe` (INI, UTF-8). The `ru.lang`
and `en.lang` files are created from the built-in defaults on first run (if
missing) and act as templates; your edits to them are kept on later starts. To
restore the original template, delete the file and restart. **To add a language:**

1. Copy `lang\en.lang` to `lang\<code>.lang` (e.g. `de.lang`).
2. Translate the right-hand side of each `key = value` line; set `lang.name` to
   your language's name (shown in the menu).
3. Restart `panel.exe` — the language appears in the **Язык / Language** menu.

Multi-line values (dialog texts) are written on one line with `\n` where a line
break should go.

## Build

Requirements: **Visual Studio 2022 Build Tools** + the C++ workload (MSVC +
Windows SDK). No CMake, no vcpkg, no .NET.

```bat
build.bat
```

It calls `vcvars64`, compiles `res\panel.rc` (`rc`) and `src\panel.cpp`
(`cl /std:c++17 /O2 /utf-8`), links against the standard Windows libraries,
closes any running copy and launches the fresh `build\panel.exe`.

## Portability

Works out of the box on another Win10/11: the `.exe` itself, the `ico` folder,
bare names of system utilities, and monitor enumeration. The **config does not
travel** (it lives in the `HKCU` registry) — on a new machine the panels start
with the sample set; move it with a `.reg` export/import or set it up again in the
editor. Absolute `target` paths and Store AUMIDs may also need adjusting.

## License

[MIT](../LICENSE) © 2026 Evgenii Shapovalov
