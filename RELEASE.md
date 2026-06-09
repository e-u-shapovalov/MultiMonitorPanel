# MultiMonitorPanel v1.0.0 - релиз, Assets и текст публикации

Этот файл нужен для оформления GitHub Release, страницы репозитория и будущих
публикаций. Главный язык проекта - русский; английский блок ниже.

## Фактическое состояние релиза v1.0.0

Проверено 2026-06-09: опубликован GitHub Release:

<https://github.com/e-u-shapovalov/MultiMonitorPanel/releases/tag/v1.0.0>

В блоке **Assets** сейчас загружен готовый файл:

```text
panel.exe
```

Размер: около 319 КБ. Это portable-приложение, установщик не нужен.

Важно для README и инструкций: обычному пользователю нужно скачивать
**`panel.exe` из Assets**, а не **Source code** и не **Code -> Download ZIP**.

## Рекомендация для следующего релиза

Для более понятного пользовательского скачивания лучше публиковать zip-архив:

```text
MultiMonitorPanel-v1.0.1-win64.zip
```

Рекомендуемый состав архива:

```text
panel.exe
ico\
README.txt или info.txt
```

`lang\`, `info.txt` и `ico\` программа умеет создать при первом запуске, но
готовый архив с понятной структурой выглядит лучше для новичков и поискового
трафика.

## Короткое описание для GitHub About

Отдельная portable-панель запуска на каждый монитор Windows 10/11. Real AppBar,
свои кнопки на каждом экране, запуск программ на нужном мониторе, GUI-настройка,
C++/WinAPI, без .NET и Electron.

Рекомендуемые topics:

```text
windows
windows-10
windows-11
multi-monitor
multiple-monitors
appbar
launcher
dock
taskbar
winapi
cpp
portable
desktop-utility
productivity
```

## Release Notes RU

**MultiMonitorPanel v1.0.0** - первая публичная версия небольшой нативной панели
запуска для Windows 10/11. Программа создаёт отдельный док на каждом мониторе:
один экран может хранить папки и задачи, второй - IDE, третий - терминалы,
админские утилиты и удалённые подключения.

Это не скин и не тяжёлый desktop suite. MultiMonitorPanel написан на C++/WinAPI
и использует настоящий Windows AppBar: система резервирует место под панель,
поэтому развёрнутые окна её не перекрывают. Кнопка запускает приложение на том
мониторе, где она нажата. Настройки открываются правым кликом по панели.

### Скачать и запустить

1. Откройте этот релиз на GitHub.
2. В блоке **Assets** скачайте **`panel.exe`**.
3. Положите файл в удобную папку, например:

   ```text
   C:\Tools\MultiMonitorPanel\panel.exe
   ```

4. Запустите `panel.exe` двойным кликом.
5. Для автозапуска нажмите `Win+R`, введите `shell:startup` и положите туда
   ярлык на `panel.exe`.

Не скачивайте **Source code (zip)** и **Source code (tar.gz)**, если хотите
просто запустить программу. Это исходники для разработчиков.

Если SmartScreen предупреждает о неизвестном приложении, проверьте, что файл
скачан из этого репозитория, затем нажмите **Подробнее -> Выполнить в любом
случае**.

### Что внутри

- Один док на каждый монитор Windows 10/11.
- Реальный AppBar: развёрнутые окна уважают зарезервированную полосу.
- Запуск приложения на том мониторе, где нажата кнопка.
- Встроенный редактор настроек: ПКМ по панели -> **Изменить настройки...**.
- Группы кнопок слева, по центру и справа.
- Вертикальные разделители.
- Свои иконки `.ico`, `.png`, `.jpg`, `.bmp`, `.gif`, `.tiff`.
- Флаги кнопки: запуск от администратора и режим консоли/conhost.
- Русский и английский интерфейс, внешние `lang\*.lang` для переводов.
- PerMonitorV2 DPI и пересоздание панелей при смене мониторов.
- Portable `.exe`, без .NET, Electron, Node.js, CMake, vcpkg и установщика.

### Ограничения

- Поддерживаются Windows 10/11.
- SVG-иконки не поддерживаются напрямую.
- Single-instance, UWP/Store и launcher-based приложения могут сами выбирать
  экран запуска; MultiMonitorPanel переносит окна только когда может найти окно
  запущенного процесса.
- Конфигурация хранится в `HKCU\Software\MultiMonitorPanel` и не переносится
  вместе с exe автоматически.

### Сборка из исходников

Нужны Visual Studio 2022 Build Tools с C++ workload и Windows SDK:

```bat
build.bat
```

## Release Notes EN

**MultiMonitorPanel v1.0.0** is the first public release of a small native
launcher panel for Windows 10/11. It creates a separate dock on each monitor, so
you can keep folders on one screen, IDEs on another, and terminals or admin
tools on a third one.

It is not a skin or a heavy desktop suite. MultiMonitorPanel is written in
C++/WinAPI and uses a real Windows AppBar: the system reserves screen space for
the panel, so maximized windows do not cover it. A button launches the app on
the monitor where the button was clicked. Configuration is available from the
right-click menu.

### Download and run

1. Open this GitHub Release.
2. In **Assets**, download **`panel.exe`**.
3. Put it into a permanent folder, for example:

   ```text
   C:\Tools\MultiMonitorPanel\panel.exe
   ```

4. Double-click `panel.exe`.
5. For autostart, press `Win+R`, enter `shell:startup`, and place a shortcut to
   `panel.exe` there.

Do not download **Source code (zip)** or **Source code (tar.gz)** if you only
want to run the app. Those files are for developers.

If SmartScreen warns about an unknown app, verify that the file came from this
repository, then use **More info -> Run anyway**.

### Highlights

- One dock per Windows 10/11 monitor.
- Real AppBar: maximized windows respect the reserved strip.
- Launch apps on the monitor where the button was clicked.
- Built-in settings editor: right-click a panel -> **Edit config...**.
- Left, center and right button groups.
- Visual separators.
- Custom icons: `.ico`, `.png`, `.jpg`, `.bmp`, `.gif`, `.tiff`.
- Per-button run-as-admin and console/conhost flags.
- Russian and English UI with external `lang\*.lang` translation files.
- PerMonitorV2 DPI and automatic rebuild on monitor changes.
- Portable native `.exe`, no .NET, Electron, Node.js, CMake, vcpkg or installer.

### Build from source

Requires Visual Studio 2022 Build Tools with the C++ workload and Windows SDK:

```bat
build.bat
```
