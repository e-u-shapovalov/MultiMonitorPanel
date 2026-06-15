// =====================================================================
// MultiMonitorPanel
//
// One .exe, one process, one AppBar window per monitor. Buttons launch
// apps via ShellExecute. Config lives in HKCU\Software\MultiMonitorPanel.
//
// Build with: build.bat  (invokes vcvars64.bat + cl.exe + rc.exe)
// =====================================================================

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00       // Windows 10 — TaskDialog, GetDpiForWindow, stock icons
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <initguid.h>              // must come before commoncontrols.h to emit IID_IImageList
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>             // process-tree walk (a console window is owned by a child conhost)
#include <shlwapi.h>
#include <shellscalingapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <commdlg.h>               // GetOpenFileNameW for the config editor's file pickers
#include <objbase.h>
#include <shobjidl.h>              // IFileOpenDialog (folder picker for icon_dir)
#include <gdiplus.h>

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <utility>
#include <cwchar>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comdlg32.lib")

// ------------------------------ constants ----------------------------
namespace {
constexpr wchar_t kPanelClass[]   = L"MMPanel.AppBarWindow";
constexpr wchar_t kCtrlClass[]    = L"MMPanel.Controller";
constexpr wchar_t kEditorClass[]  = L"MMPanel.Editor";
constexpr wchar_t kAppTitle[]     = L"MultiMonitorPanel";
constexpr wchar_t kMutexName[]    = L"Local\\MultiMonitorPanel.SingleInstance.B1C3F8A4";
constexpr wchar_t kRegSubkey[]    = L"Software\\MultiMonitorPanel";

// Release metadata — shown in the About dialog and the editor title bar.
constexpr wchar_t kVersion[]      = L"1.1.0";
constexpr wchar_t kAuthor[]       = L"Evgenii Shapovalov";
constexpr wchar_t kReleaseDate[]  = L"2026-06-10";
constexpr wchar_t kRepoUrl[]      = L"https://github.com/e-u-shapovalov/MultiMonitorPanel";

constexpr UINT WM_APPBAR_CB       = WM_APP + 1;
constexpr UINT WM_MMP_REBUILD     = WM_APP + 10;
constexpr UINT WM_MMP_RELOAD      = WM_APP + 11;
constexpr UINT WM_MMP_RELANG      = WM_APP + 12;   // editor: re-apply UI language in place

constexpr int  BTN_ID_BASE        = 1000;
constexpr int  MENU_RELOAD        = 2001;
constexpr int  MENU_GUIEDIT       = 2002;   // built-in config editor window
constexpr int  MENU_EXIT          = 2003;
constexpr int  MENU_FOLDER        = 2006;
constexpr int  MENU_SETDEFAULT    = 2007;
constexpr int  MENU_ERASE         = 2008;
constexpr int  MENU_ABOUT         = 2009;   // version / author / repo dialog
constexpr int  MENU_LANG_BASE     = 2100;   // language submenu items: BASE + index

// Config-editor control IDs. OK/Cancel reuse IDOK(1)/IDCANCEL(2) so that
// IsDialogMessage routes Enter/Esc to them automatically.
constexpr int  IDC_MON_COMBO      = 3001;
constexpr int  IDC_HEIGHT_EDIT    = 3002;
constexpr int  IDC_ICONDIR_EDIT   = 3003;
constexpr int  IDC_BTN_LIST       = 3004;
constexpr int  IDC_LABEL_EDIT     = 3005;
constexpr int  IDC_TARGET_EDIT    = 3006;
constexpr int  IDC_ARGS_EDIT      = 3007;
constexpr int  IDC_ICON_EDIT      = 3008;
constexpr int  IDC_ICON_BROWSE    = 3009;
constexpr int  IDC_TARGET_BROWSE  = 3010;
constexpr int  IDC_ALIGN_L        = 3011;
constexpr int  IDC_ALIGN_C        = 3012;
constexpr int  IDC_ALIGN_R        = 3013;
constexpr int  IDC_CHK_ADMIN      = 3014;
constexpr int  IDC_CHK_CONSOLE    = 3015;
constexpr int  IDC_CHK_SEP        = 3016;
constexpr int  IDC_BTN_ADD        = 3017;
constexpr int  IDC_BTN_DEL        = 3018;
constexpr int  IDC_BTN_UP         = 3019;
constexpr int  IDC_BTN_DOWN       = 3020;
constexpr int  IDC_APPLY          = 3021;
constexpr int  IDC_BTN_ADDSEP     = 3022;
constexpr int  IDC_MON_ADD        = 3023;
constexpr int  IDC_MON_DEL        = 3024;
constexpr int  IDC_ICONDIR_BROWSE = 3025;

constexpr int  DEFAULT_HEIGHT_DIP = 48;
constexpr int  MIN_HEIGHT_DIP     = 24;
constexpr int  MAX_HEIGHT_DIP     = 200;

constexpr int  ICON_SIZE_DIP      = 32;  // rendered icon inside each 48-dip button
constexpr int  BTN_GAP_DIP        = 4;
constexpr int  SEP_WIDTH_DIP      = 9;   // width of a separator slot
constexpr int  SEP_LINE_DIP       = 1;   // thickness of the drawn separator line

constexpr COLORREF COL_PANEL_BG   = RGB(24, 24, 24);
constexpr COLORREF COL_BTN_BG     = RGB(24, 24, 24);
constexpr COLORREF COL_BTN_HOVER  = RGB(46, 46, 46);
constexpr COLORREF COL_BTN_PRESS  = RGB(66, 66, 66);
constexpr COLORREF COL_SEP        = RGB(70, 70, 70);
} // namespace

// -------------------------------- types ------------------------------
// Which alignment block a button sits in. Buttons are partitioned into up to
// three blocks per panel: Left hugs the left edge, Right hugs the right edge,
// Center is centred. Within a block, config order is left-to-right.
enum class BtnAlign { Left, Center, Right };

struct ButtonCfg {
    std::wstring label;
    std::wstring target;     // clean path / URI (no args)
    std::wstring args;       // command-line args, if any
    std::wstring iconPath;
    bool         runAsAdmin  = false;
    BtnAlign     align       = BtnAlign::Left;
    bool         isSeparator = false;   // vertical divider, not a launchable button
    bool         console     = false;   // wrap launch in classic conhost (a movable window)
};

struct GlobalCfg {
    int          heightDip = DEFAULT_HEIGHT_DIP;
    std::wstring iconDir;   // resolved, ends with '\\'
};

struct PanelWindow {
    HWND     hwnd      = nullptr;
    HMONITOR hMonitor  = nullptr;
    RECT     monRect   {};
    int      index     = 0;
    UINT     dpi       = 96;
    HWND     tooltip   = nullptr;

    std::vector<ButtonCfg> buttons;
    std::vector<HICON>     icons;
    std::vector<HWND>      btnHwnds;
};

// ------------------------------ globals ------------------------------
namespace {
HINSTANCE     g_hInst   = nullptr;
HWND          g_hCtrl   = nullptr;
HWND          g_hEditor = nullptr;   // built-in config editor (single instance)
GlobalCfg     g_global;
std::vector<std::unique_ptr<PanelWindow>> g_panels;
HANDLE        g_mutex   = nullptr;
std::wstring  g_lang    = L"ru";     // active UI language code (see localization)
} // namespace

// -------------------------- forward declarations ---------------------
namespace {
std::wstring GetExeDir();
std::wstring ExpandEnv(const std::wstring& s);
std::wstring TrimW(std::wstring s);

void LoadGlobalCfg();
std::vector<ButtonCfg> LoadButtonsForMonitor(int idx);
void EnsureRegistryConfig();
void EnsureInfoFile();

void EnsureLangFiles();
void LoadLanguage(const std::wstring& code);
std::wstring DetectLanguageCode();

HICON LoadFileIcon(const std::wstring& iconFile, int px);
HICON LoadShellIcon(const std::wstring& path, int px);

void RegisterPanelClass();
void RegisterCtrlClass();
void RegisterEditorClass();
void OpenConfigEditor(HWND owner);

void CreateAllPanels();
void DestroyAllPanels();

void LayoutPanelButtons(PanelWindow* p);
void SetupAppBar(PanelWindow* p);
void TeardownAppBar(PanelWindow* p);
void RepositionAppBar(PanelWindow* p);

void RunButton(PanelWindow* p, int idx);
void ShowContextMenu(HWND hwnd);

LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK CtrlProc (HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK BtnSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

constexpr wchar_t kPropHover[]    = L"MMP.Hover";
constexpr wchar_t kPropTracking[] = L"MMP.Tracking";
} // namespace

// ================================= main ==============================
int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    g_mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_mutex) CloseHandle(g_mutex);
        return 0;
    }

    // PerMonitorV2 is also set via the manifest; this is belt-and-braces.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr);

    INITCOMMONCONTROLSEX icc{sizeof(icc),
        ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    g_hInst = hInst;
    EnsureRegistryConfig();
    LoadGlobalCfg();

    // Localization: write the editable lang templates, then load the chosen
    // language (registry value "language", else auto-detected on first run).
    EnsureLangFiles();
    LoadLanguage(DetectLanguageCode());

    // Auto-create the icon directory so `icon_dir = ico` works out of the box
    // on a fresh unpack, even before the user drops any PNGs inside.
    if (!g_global.iconDir.empty())
        CreateDirectoryW(g_global.iconDir.c_str(), nullptr);

    // Drop/refresh the plain-language manual next to the exe.
    EnsureInfoFile();

    RegisterPanelClass();
    RegisterCtrlClass();
    RegisterEditorClass();

    // Invisible top-level controller — receives WM_DISPLAYCHANGE.
    g_hCtrl = CreateWindowExW(0, kCtrlClass, kAppTitle, WS_POPUP,
                              0, 0, 0, 0, nullptr, nullptr, g_hInst, nullptr);

    CreateAllPanels();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // The config editor is a modeless top-level window — route Tab/Enter/Esc
        // and arrow-key group navigation through it before normal dispatch.
        if (g_hEditor && IsDialogMessageW(g_hEditor, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyAllPanels();
    if (gdipToken) Gdiplus::GdiplusShutdown(gdipToken);
    if (SUCCEEDED(coHr)) CoUninitialize();
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
    }
    return static_cast<int>(msg.wParam);
}

// ============================ implementation =========================
namespace {

// ----------------------------- util ----------------------------------
std::wstring GetExeDir() {
    wchar_t p[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, p, static_cast<DWORD>(_countof(p) - 1));
    PathRemoveFileSpecW(p);
    return std::wstring(p);
}

std::wstring ExpandEnv(const std::wstring& s) {
    wchar_t buf[MAX_PATH * 4]{};
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, static_cast<DWORD>(_countof(buf)));
    if (n == 0 || n > _countof(buf)) return s;
    return std::wstring(buf);
}

std::wstring TrimW(std::wstring s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) ++i;
    s.erase(0, i);
    while (!s.empty()) {
        wchar_t c = s.back();
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') s.pop_back();
        else break;
    }
    return s;
}

// Split a target string into an executable and its command-line arguments.
//   "C:\path with spaces\app.exe" arg1 arg2   → exe="C:\path with spaces\app.exe", args="arg1 arg2"
//   notepad.exe                                → exe="notepad.exe", args=""
//   C:\no-space\app.exe arg                    → exe="C:\no-space\app.exe", args="arg"
// Rule: if the target starts with '"', the exe ends at the matching '"'.
// Otherwise, split at the first whitespace. (Put quotes around paths with spaces
// whenever you pass arguments — the usual Windows convention.)
void SplitTargetAndArgs(const std::wstring& raw, std::wstring& exe, std::wstring& args) {
    exe.clear();
    args.clear();
    std::wstring s = TrimW(raw);
    if (s.empty()) return;

    if (s.front() == L'"') {
        size_t end = s.find(L'"', 1);
        if (end == std::wstring::npos) { exe = s.substr(1); return; }
        exe  = s.substr(1, end - 1);
        args = TrimW(s.substr(end + 1));
    } else {
        size_t sp = s.find_first_of(L" \t");
        if (sp == std::wstring::npos) {
            exe = s;
        } else {
            exe  = s.substr(0, sp);
            args = TrimW(s.substr(sp + 1));
        }
    }
}

// --------------------------- registry I/O ----------------------------
// Config lives in HKCU\Software\MultiMonitorPanel (kRegSubkey). Layout:
//   (DWORD) height            panel height in DIP
//   (SZ)    icon_dir          icon folder, absolute or relative to the exe
//   monitor_<N>\button_<M>\   one sub-key per button, string values:
//                               label, target, args, icon, flags
// Monitors are 1-based left-to-right (monitor_1 = leftmost); buttons are
// ordered numerically by <M>. target/args are stored separately, so paths
// with spaces need no quoting and the old '|' field-splitting is gone.

// Path of the optional panel.ini next to the exe — used for legacy migration
// on first run and for the manual Export/Import config commands.
std::wstring IniPath() {
    std::wstring p = GetExeDir();
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    p += L"panel.ini";
    return p;
}

HKEY OpenRoot(REGSAM access) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubkey, 0, access, &h) != ERROR_SUCCESS)
        return nullptr;
    return h;
}

std::wstring RegReadStr(HKEY key, const wchar_t* name) {
    if (!key) return L"";
    DWORD type = 0, cb = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || cb < sizeof(wchar_t))
        return L"";
    std::wstring s(cb / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&s[0]), &cb) != ERROR_SUCCESS)
        return L"";
    s.resize(wcsnlen(s.c_str(), s.size()));   // drop trailing NUL terminator(s)
    return s;
}

DWORD RegReadDword(HKEY key, const wchar_t* name, DWORD def) {
    if (!key) return def;
    DWORD type = 0, val = 0, cb = sizeof(val);
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<LPBYTE>(&val), &cb) == ERROR_SUCCESS &&
        type == REG_DWORD)
        return val;
    return def;
}

void RegWriteStr(HKEY key, const wchar_t* name, const std::wstring& val) {
    RegSetValueExW(key, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(val.c_str()),
                   static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
}

void RegWriteDword(HKEY key, const wchar_t* name, DWORD val) {
    RegSetValueExW(key, name, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&val), sizeof(val));
}

// --------------------------- flags parsing ---------------------------
// `flags` is a space/comma/tab-separated token list. Recognized tokens:
//   admin              → launch elevated (runas verb, UAC prompt)
//   left/center/right  → which alignment block the button lives in
//   sep/separator      → this entry is a vertical separator, not a button
// Unknown tokens are ignored, so the field stays forward-compatible.
void ParseFlags(const std::wstring& flagsRaw, ButtonCfg& b) {
    std::wstring f = flagsRaw;
    for (auto& c : f) c = static_cast<wchar_t>(towlower(c));

    size_t pos = 0;
    while (pos < f.size()) {
        size_t e = f.find_first_of(L" ,\t", pos);
        std::wstring tok = (e == std::wstring::npos) ? f.substr(pos)
                                                     : f.substr(pos, e - pos);
        if (!tok.empty()) {
            if      (tok == L"admin")                      b.runAsAdmin  = true;
            else if (tok == L"right")                      b.align       = BtnAlign::Right;
            else if (tok == L"center" || tok == L"centre") b.align       = BtnAlign::Center;
            else if (tok == L"left")                       b.align       = BtnAlign::Left;
            else if (tok == L"sep"    || tok == L"separator") b.isSeparator = true;
            else if (tok == L"console" || tok == L"conhost")  b.console     = true;
        }
        if (e == std::wstring::npos) break;
        pos = e + 1;
    }
}

// Inverse of ParseFlags — serialize the structured flags back to one string,
// so a parse→store round-trip (ini import, default seeding) keeps alignment
// and separators intact.
std::wstring BuildFlags(const ButtonCfg& b) {
    std::wstring s;
    auto add = [&](const wchar_t* t) { if (!s.empty()) s += L' '; s += t; };
    if (b.isSeparator)               add(L"sep");
    if (b.align == BtnAlign::Right)  add(L"right");
    if (b.align == BtnAlign::Center) add(L"center");
    if (b.console)                   add(L"console");
    if (b.runAsAdmin)                add(L"admin");
    return s;
}

// --------------------------- config writing --------------------------
void WriteGlobal(HKEY root, int heightDip, const std::wstring& iconDir) {
    RegWriteDword(root, L"height", static_cast<DWORD>(heightDip));
    RegWriteStr(root, L"icon_dir", iconDir);
}

// Re-persist the active UI language. Button config and language share one root
// key, and every "rewrite the branch" path (editor save, set-default, erase)
// calls RegDeleteTree first — without this the `language` value would be wiped,
// silently resetting the user's choice on the next start.
void WriteLanguageValue(HKEY root) {
    RegSetValueExW(root, L"language", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(g_lang.c_str()),
                   static_cast<DWORD>((g_lang.size() + 1) * sizeof(wchar_t)));
}

void WriteButton(HKEY root, int mon, int btn, const ButtonCfg& b) {
    wchar_t sub[64]{};
    _snwprintf_s(sub, _countof(sub), _TRUNCATE, L"monitor_%d\\button_%d", mon, btn);
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr)
            != ERROR_SUCCESS)
        return;
    RegWriteStr(k, L"label",  b.label);
    RegWriteStr(k, L"target", b.target);
    RegWriteStr(k, L"args",   b.args);
    RegWriteStr(k, L"icon",   b.iconPath);
    RegWriteStr(k, L"flags",  BuildFlags(b));
    RegCloseKey(k);
}

// --------------------------- default config ---------------------------
// Seeded only on a fresh install (no panel.ini to migrate). A few standard
// apps, the same set on each monitor — mirrors the old default INI.
// A self-documenting sample config: only native apps present on every Win10/11,
// arranged to show off every feature at a glance — left/center/right blocks,
// separators, run-as-admin, an icon override, command-line args, a folder shortcut.
std::vector<ButtonCfg> DefaultButtons() {
    bool ru = (g_lang == L"ru");
    // Pick the label in the active language (sample content, not part of the
    // string table — the user replaces these anyway).
    auto L = [ru](const wchar_t* rus, const wchar_t* eng) { return ru ? rus : eng; };
    auto mk = [](const wchar_t* label, const wchar_t* target, const wchar_t* args,
                 const wchar_t* icon, bool admin, BtnAlign align, bool console = false) {
        ButtonCfg b;
        b.label = label; b.target = target; b.args = args;
        b.iconPath = icon; b.runAsAdmin = admin; b.align = align; b.console = console;
        return b;
    };
    auto sep = [](BtnAlign align) {
        ButtonCfg b; b.isSeparator = true; b.align = align; b.label = L"---";
        return b;
    };
    using A = BtnAlign;
    return {
        // — LEFT block: explorer & folders —
        mk(L(L"Проводник", L"Explorer"),    L"explorer.exe",             L"",  L"",            false, A::Left),
        mk(L(L"Загрузки", L"Downloads"),    L"%USERPROFILE%\\Downloads", L"",  L"download.png",false, A::Left),
        sep(A::Left),                                  // separator inside the left block
        mk(L(L"Блокнот", L"Notepad"),       L"notepad.exe",              L"",  L"",            false, A::Left),
        mk(L(L"Калькулятор", L"Calculator"),L"calc.exe",                 L"",  L"",            false, A::Left),
        // — CENTER block —
        mk(L"Paint",                        L"mspaint.exe",              L"",  L"",            false, A::Center),
        mk(L"Ping",                         L"cmd.exe", L"/k ping 127.0.0.1", L"",            false, A::Center, true), // args + console
        // — RIGHT block: system, some as admin —
        mk(L(L"Панель управления", L"Control Panel"), L"control.exe",    L"",  L"",            false, A::Right),
        mk(L(L"Диспетчер задач", L"Task Manager"),    L"taskmgr.exe",    L"",  L"",            false, A::Right),
        sep(A::Right),                                 // separator inside the right block
        mk(L(L"CMD (админ)", L"CMD (admin)"),         L"cmd.exe",        L"",  L"",            true,  A::Right, true),  // console
        mk(L(L"Реестр (админ)", L"Registry (admin)"), L"regedit.exe",    L"",  L"",            true,  A::Right),
    };
}

void WriteDefaultConfig(HKEY root) {
    WriteGlobal(root, DEFAULT_HEIGHT_DIP, L"ico");
    auto def = DefaultButtons();
    for (int mon = 1; mon <= 3; ++mon)
        for (size_t i = 0; i < def.size(); ++i)
            WriteButton(root, mon, static_cast<int>(i + 1), def[i]);
}

// Menu command: wipe the whole config (panels go empty on the next Reload).
// Leaves the root key present-but-empty, so the next launch does NOT treat it
// as a fresh install (no auto-seed, no panel.ini auto-import) — the user stays
// in control of when to re-import.
void EraseConfig() {
    HKEY root = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubkey, 0, nullptr, 0,
                        KEY_READ | KEY_WRITE | DELETE, nullptr, &root, nullptr) == ERROR_SUCCESS) {
        RegDeleteTree(root, nullptr);   // remove monitor_* + values, keep the empty root
        WriteLanguageValue(root);       // language is a UI pref — survive the wipe
        RegCloseKey(root);
    }
}

// Menu command: replace whatever is there with the sample config on 3 monitors.
// Wipes first so the sample isn't merged with leftover buttons.
void SetDefaultConfig() {
    HKEY root = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubkey, 0, nullptr, 0,
                        KEY_READ | KEY_WRITE | DELETE, nullptr, &root, nullptr) == ERROR_SUCCESS) {
        RegDeleteTree(root, nullptr);
        WriteDefaultConfig(root);
        WriteLanguageValue(root);       // preserve the chosen language across the wipe
        RegCloseKey(root);
    }
}

// ----------------------- one-time INI migration -----------------------
// If a legacy panel.ini sits next to the exe, pull it into the registry on
// first run. Mirrors the old INI parser exactly (it produced today's working
// panels), so the migrated config is identical down to target/args splitting.
// Returns true if at least one button was imported.
bool ImportIniToRegistry(HKEY root, const std::wstring& iniPath) {
    wchar_t hbuf[64]{};
    GetPrivateProfileStringW(L"global", L"height", L"48", hbuf,
                             static_cast<DWORD>(_countof(hbuf)), iniPath.c_str());
    int h = _wtoi(hbuf);
    if (h < MIN_HEIGHT_DIP || h > MAX_HEIGHT_DIP) h = DEFAULT_HEIGHT_DIP;

    wchar_t dirBuf[MAX_PATH * 2]{};
    GetPrivateProfileStringW(L"global", L"icon_dir", L"ico", dirBuf,
                             static_cast<DWORD>(_countof(dirBuf)), iniPath.c_str());
    WriteGlobal(root, h, TrimW(dirBuf));

    bool any = false;
    for (int mon = 1; mon <= 16; ++mon) {
        wchar_t section[64]{};
        _snwprintf_s(section, _countof(section), _TRUNCATE, L"monitor_%d", mon);

        std::vector<wchar_t> buf(32768, 0);
        DWORD n = GetPrivateProfileSectionW(section, buf.data(),
                                            static_cast<DWORD>(buf.size()), iniPath.c_str());
        if (n == 0) continue;

        int btn = 0;
        const wchar_t* cur = buf.data();
        while (*cur) {
            std::wstring line = cur;
            cur += line.size() + 1;

            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring label = TrimW(line.substr(0, eq));
            std::wstring rest  = TrimW(line.substr(eq + 1));
            if (label.empty() || rest.empty()) continue;

            ButtonCfg b;
            b.label = label;

            // "target [| icon_override] [| flags]"
            std::vector<std::wstring> parts;
            size_t pos = 0;
            while (pos <= rest.size()) {
                size_t bar = rest.find(L'|', pos);
                if (bar == std::wstring::npos) { parts.push_back(TrimW(rest.substr(pos))); break; }
                parts.push_back(TrimW(rest.substr(pos, bar - pos)));
                pos = bar + 1;
            }
            std::wstring rawTarget = parts.size() > 0 ? parts[0] : L"";
            if (parts.size() > 1) b.iconPath = parts[1];
            if (parts.size() > 2) ParseFlags(parts[2], b);
            SplitTargetAndArgs(rawTarget, b.target, b.args);
            // Readable separator form: a target of only dashes, e.g. "sep = ------".
            // (the old flags-based "sep" token is still honored above via ParseFlags.)
            if (!b.target.empty() && b.target.find_first_not_of(L'-') == std::wstring::npos) {
                b.isSeparator = true;
                b.target.clear();
                b.args.clear();
            }
            if (b.target.empty() && !b.isSeparator) continue;

            WriteButton(root, mon, ++btn, b);
            any = true;
        }
    }
    return any;
}

// ------------------------- first-run config ---------------------------
void EnsureRegistryConfig() {
    // Already configured → leave the user's keys untouched.
    if (HKEY existing = OpenRoot(KEY_READ)) {
        RegCloseKey(existing);
        return;
    }

    HKEY root = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubkey, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &root, nullptr) != ERROR_SUCCESS)
        return;

    // Migrate a legacy panel.ini next to the exe if present; otherwise seed
    // the standard defaults.
    std::wstring ini = IniPath();

    DWORD attr = GetFileAttributesW(ini.c_str());
    bool haveIni = (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);

    if (!haveIni || !ImportIniToRegistry(root, ini))
        WriteDefaultConfig(root);

    RegCloseKey(root);
}

// ------------------------- config reading ----------------------------
void LoadGlobalCfg() {
    HKEY root = OpenRoot(KEY_READ);

    int h = static_cast<int>(RegReadDword(root, L"height", DEFAULT_HEIGHT_DIP));
    if (h < MIN_HEIGHT_DIP) h = DEFAULT_HEIGHT_DIP;
    if (h > MAX_HEIGHT_DIP) h = MAX_HEIGHT_DIP;
    g_global.heightDip = h;

    // icon_dir — absolute or relative to exe dir. Normalized to end with '\\'.
    std::wstring dir = TrimW(ExpandEnv(RegReadStr(root, L"icon_dir")));
    if (!dir.empty()) {
        if (PathIsRelativeW(dir.c_str())) {
            std::wstring base = GetExeDir();
            if (!base.empty() && base.back() != L'\\') base += L'\\';
            dir = base + dir;
        }
        if (dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
    }
    g_global.iconDir = dir;

    if (root) RegCloseKey(root);
}

std::vector<ButtonCfg> LoadButtonsForMonitor(int idx) {
    std::vector<ButtonCfg> out;

    wchar_t monPath[128]{};
    _snwprintf_s(monPath, _countof(monPath), _TRUNCATE,
                 L"%s\\monitor_%d", kRegSubkey, idx);
    HKEY mon = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, monPath, 0, KEY_READ, &mon) != ERROR_SUCCESS)
        return out;

    // Collect button_<N> sub-keys and order them numerically by N, so button_10
    // sorts after button_2 and a gap left by a manual regedit edit doesn't
    // truncate the rest of the row.
    std::vector<std::pair<int, std::wstring>> subs;
    for (DWORD i = 0; ; ++i) {
        wchar_t name[128];
        DWORD cch = _countof(name);
        LONG r = RegEnumKeyExW(mon, i, name, &cch, nullptr, nullptr, nullptr, nullptr);
        if (r != ERROR_SUCCESS) break;   // ERROR_NO_MORE_ITEMS or failure — done
        int num = 0;
        if (swscanf_s(name, L"button_%d", &num) == 1)
            subs.emplace_back(num, name);
    }
    std::sort(subs.begin(), subs.end(),
              [](const std::pair<int, std::wstring>& a,
                 const std::pair<int, std::wstring>& b) { return a.first < b.first; });

    out.reserve(subs.size());
    for (const auto& s : subs) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(mon, s.second.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS)
            continue;

        ButtonCfg b;
        b.label    = RegReadStr(k, L"label");
        b.target   = RegReadStr(k, L"target");
        b.args     = RegReadStr(k, L"args");
        b.iconPath = RegReadStr(k, L"icon");
        ParseFlags(RegReadStr(k, L"flags"), b);

        RegCloseKey(k);
        // Separators carry no target — keep them; everything else needs one.
        if (!b.target.empty() || b.isSeparator) out.push_back(std::move(b));
    }
    RegCloseKey(mon);
    return out;
}

// ============================ localization ===========================
// All user-facing GUI strings live in one table with built-in Russian and
// English columns (Russian is the primary language). On startup the chosen
// language is loaded from HKCU (value "language"); on first run it is
// auto-detected from the system UI language. The resolved table can be
// overridden / extended by an external lang\<code>.lang file (INI, UTF-8),
// so anyone can drop in their own translation. ru.lang and en.lang are
// (re)written next to the exe each start so they serve as editable templates.

enum Str {
    S_MENU_EDIT, S_MENU_RELOAD, S_MENU_SETDEFAULT, S_MENU_ERASE,
    S_MENU_FOLDER, S_MENU_EXIT, S_MENU_ABOUT, S_MENU_LANGUAGE,
    S_ABOUT_VERSION, S_ABOUT_RELEASED, S_ABOUT_AUTHOR, S_ABOUT_LICENSE,
    S_DLG_SETDEF_TITLE, S_DLG_SETDEF_BODY, S_DLG_SETDEF_OK,
    S_DLG_ERASE_TITLE, S_DLG_ERASE_BODY, S_DLG_ERASE_OK,
    S_DLG_HEIGHT_TITLE, S_DLG_HEIGHT_BODY,
    S_DLG_CLOSE_TITLE, S_DLG_CLOSE_BODY, S_DLG_CLOSE_OK,
    S_DLG_RMMON_TITLE, S_DLG_RMMON_BODY, S_DLG_RMMON_OK,
    S_COMMON_CANCEL, S_COMMON_OK, S_COMMON_APPLY,
    S_ED_TITLE_SUFFIX, S_ED_MONITOR, S_ED_HEIGHT, S_ED_ICONS,
    S_ED_GRP_BUTTONS, S_ED_GRP_PROP, S_ED_LABEL, S_ED_TARGET, S_ED_ARGS,
    S_ED_ICON, S_ED_EDGE, S_ED_LEFT, S_ED_CENTER, S_ED_RIGHT,
    S_ED_ADMIN, S_ED_CONSOLE, S_ED_SEP, S_ED_ADD, S_ED_ADDSEP, S_ED_DEL,
    S_ED_MON_FMT, S_ED_GROUP_LEFT, S_ED_GROUP_CENTER, S_ED_GROUP_RIGHT,
    S_ED_ROW_UNNAMED, S_ED_FIELD_SEP, S_ED_PICKDIR,
    S_ED_CTX_COPY, S_ED_CTX_PASTE,
    S_TIP_EDGE, S_TIP_ADDSEP, S_TIP_SEP, S_TIP_CONSOLE, S_TIP_ADMIN,
    S_TIP_MONADD, S_TIP_MONDEL, S_TIP_ICONDIR,
    S_FILTER_PROGRAMS, S_FILTER_IMAGES, S_FILTER_ALL,
    S_COUNT
};

struct StrDef { const wchar_t* key; const wchar_t* ru; const wchar_t* en; };

const StrDef kStrings[] = {
    { L"menu.edit",        L"Изменить настройки…",      L"Edit config…" },
    { L"menu.reload",      L"Перезагрузить настройки",  L"Reload config" },
    { L"menu.setdefault",  L"Образцовый набор",         L"Set default config" },
    { L"menu.erase",       L"Стереть настройки",        L"Erase config" },
    { L"menu.folder",      L"Открыть папку программы (ico)", L"Open app folder (ico)" },
    { L"menu.exit",        L"Выход",                    L"Exit" },
    { L"menu.about",       L"О программе",              L"About" },
    { L"menu.language",    L"Язык / Language",          L"Язык / Language" },
    { L"about.version",    L"Версия",                   L"Version" },
    { L"about.released",   L"Дата релиза",              L"Released" },
    { L"about.author",     L"Автор",                    L"Author" },
    { L"about.license",    L"Лицензия MIT",             L"MIT License" },
    { L"dlg.setdef.title", L"Заменить кнопки образцовым набором?",
                           L"Replace buttons with the sample set?" },
    { L"dlg.setdef.body",
      L"На всех мониторах кнопки заменятся стандартным набором — Проводник, "
      L"Калькулятор, CMD и другие. Заодно это наглядный пример всех возможностей: "
      L"блоки слева, по центру и справа, разделители, запуск от имени "
      L"администратора, своя иконка и аргументы.",
      L"Buttons on every monitor will be replaced with a standard set — Explorer, "
      L"Calculator, CMD and others. It also showcases every feature: left, center "
      L"and right blocks, separators, run-as-administrator, a custom icon and "
      L"arguments." },
    { L"dlg.setdef.ok",    L"Заменить",                 L"Replace" },
    { L"dlg.erase.title",  L"Убрать все кнопки?",       L"Remove all buttons?" },
    { L"dlg.erase.body",
      L"Кнопки исчезнут со всех мониторов, и панели станут пустыми.\n\n"
      L"Стандартный набор всегда возвращается через меню → «Образцовый набор».",
      L"Buttons will disappear from every monitor and the panels will be empty.\n\n"
      L"The standard set can always be restored from the menu → “Set default config”." },
    { L"dlg.erase.ok",     L"Убрать",                   L"Remove" },
    { L"dlg.height.title", L"Высота вне диапазона",     L"Height out of range" },
    { L"dlg.height.body",  L"Высота панели должна быть числом от 24 до 200 (точек DIP).",
                           L"The panel height must be a number from 24 to 200 (DIP units)." },
    { L"dlg.close.title",  L"Закрыть без сохранения?",  L"Close without saving?" },
    { L"dlg.close.body",   L"Внесённые изменения не будут записаны в настройки.",
                           L"Your changes will not be written to the settings." },
    { L"dlg.close.ok",     L"Закрыть",                  L"Close" },
    { L"dlg.rmmon.title",  L"Убрать последний монитор?", L"Remove the last monitor?" },
    { L"dlg.rmmon.body",   L"У последнего монитора есть кнопки — они удалятся вместе с ним.",
                           L"The last monitor has buttons — they will be deleted with it." },
    { L"dlg.rmmon.ok",     L"Убрать",                   L"Remove" },
    { L"common.cancel",    L"Отмена",                   L"Cancel" },
    { L"common.ok",        L"OK",                       L"OK" },
    { L"common.apply",     L"Применить",                L"Apply" },
    { L"ed.title.suffix",  L"настройки",                L"settings" },
    { L"ed.monitor",       L"Монитор:",                 L"Monitor:" },
    { L"ed.height",        L"Высота:",                  L"Height:" },
    { L"ed.icons",         L"Папка иконок:",            L"Icon folder:" },
    { L"ed.grp.buttons",   L"Кнопки",                   L"Buttons" },
    { L"ed.grp.prop",      L"Свойство кнопки",          L"Button properties" },
    { L"ed.label",         L"Подпись:",                 L"Label:" },
    { L"ed.target",        L"Запускать:",               L"Run:" },
    { L"ed.args",          L"Аргументы:",               L"Arguments:" },
    { L"ed.icon",          L"Иконка:",                  L"Icon:" },
    { L"ed.edge",          L"Край:",                    L"Edge:" },
    { L"ed.left",          L"слева",                    L"left" },
    { L"ed.center",        L"центр",                    L"center" },
    { L"ed.right",         L"справа",                   L"right" },
    { L"ed.admin",         L"От администратора (UAC)",   L"Run as administrator (UAC)" },
    { L"ed.console",       L"Консоль (conhost)",        L"Console (conhost)" },
    { L"ed.sep",           L"Разделитель (вместо кнопки)", L"Separator (instead of a button)" },
    { L"ed.add",           L"Добавить",                 L"Add" },
    { L"ed.addsep",        L"+ Разделитель",            L"+ Separator" },
    { L"ed.del",           L"Удалить",                  L"Delete" },
    { L"ed.mon.fmt",       L"Монитор %d",               L"Monitor %d" },
    { L"ed.group.left",    L"——— слева ———",            L"——— left ———" },
    { L"ed.group.center",  L"——— центр ———",            L"——— center ———" },
    { L"ed.group.right",   L"——— справа ———",           L"——— right ———" },
    { L"ed.row.unnamed",   L"(без подписи)",            L"(no label)" },
    { L"ed.field.sep",     L"(разделитель)",            L"(separator)" },
    { L"ed.pickdir",       L"Выберите папку с иконками", L"Choose the icon folder" },
    { L"ed.ctx.copy",      L"Копировать",               L"Copy" },
    { L"ed.ctx.paste",     L"Вставить",                 L"Paste" },
    { L"tip.edge",
      L"К какому краю полосы прижать кнопку: левый край, центр или правый. "
      L"Например: папки слева, программы справа.",
      L"Which edge of the bar to pin the button to: left, center or right. "
      L"For example: folders on the left, programs on the right." },
    { L"tip.addsep",       L"Добавить тонкую вертикальную черту-разделитель (не кнопку).",
                           L"Add a thin vertical divider (not a button)." },
    { L"tip.sep",
      L"Сделать из этой записи вертикальную черту-разделитель вместо кнопки — "
      L"удобно дробить кнопки на группы.",
      L"Turn this entry into a vertical divider instead of a button — handy for "
      L"splitting buttons into groups." },
    { L"tip.console",
      L"Для cmd / PowerShell и других консолей: открыть на ТОМ мониторе, где "
      L"кнопка (через классический conhost).",
      L"For cmd / PowerShell and other consoles: open on the SAME monitor as the "
      L"button (via the classic conhost)." },
    { L"tip.admin",        L"Запустить от имени администратора (будет запрос UAC).",
                           L"Run as administrator (a UAC prompt will appear)." },
    { L"tip.monadd",       L"Добавить монитор (для экрана, который сейчас не подключён).",
                           L"Add a monitor (for a screen that is not connected right now)." },
    { L"tip.mondel",       L"Убрать последний монитор.", L"Remove the last monitor." },
    { L"tip.icondir",
      L"Папка, откуда берутся иконки по имени файла. По умолчанию «ico» — "
      L"подпапка рядом с panel.exe (так комплект остаётся переносимым). "
      L"Кнопка «…» — выбрать другую папку (запишется полный путь).",
      L"Folder the icons are taken from by file name. Default “ico” is a "
      L"subfolder next to panel.exe (keeps the kit portable). The “…” button "
      L"picks another folder (the full path is stored)." },
    { L"filter.programs",  L"Программы (*.exe;*.lnk;*.bat;*.cmd)",
                           L"Programs (*.exe;*.lnk;*.bat;*.cmd)" },
    { L"filter.images",    L"Изображения (*.png;*.ico;*.jpg;*.bmp;*.gif;*.tiff)",
                           L"Images (*.png;*.ico;*.jpg;*.bmp;*.gif;*.tiff)" },
    { L"filter.all",       L"Все файлы (*.*)",          L"All files (*.*)" },
};
static_assert(_countof(kStrings) == S_COUNT, "kStrings out of sync with enum Str");

std::wstring g_str[S_COUNT];   // g_lang lives in the globals block (needed earlier)

const wchar_t* T(Str id) { return g_str[id].c_str(); }

std::wstring LangDir() {
    std::wstring d = GetExeDir();
    if (!d.empty() && d.back() != L'\\') d += L'\\';
    return d + L"lang";
}
std::wstring LangFilePath(const std::wstring& code) {
    return LangDir() + L"\\" + code + L".lang";
}

// Read a whole file and decode UTF-8 (with optional BOM) into a wide string.
std::wstring ReadFileUtf8(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE) { CloseHandle(h); return L""; }  // check before clamp
    constexpr DWORD kMaxLang = 4 * 1024 * 1024;   // a lang file is a few KB; cap so a
    if (size > kMaxLang) size = kMaxLang;          // stray huge/corrupt file can't OOM us
    std::string bytes;
    if (size) {
        bytes.resize(size);
        DWORD got = 0;
        if (ReadFile(h, &bytes[0], size, &got, nullptr)) bytes.resize(got);
        else bytes.clear();
    }
    CloseHandle(h);
    const char* p = bytes.c_str();
    size_t n = bytes.size();
    if (n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) { p += 3; n -= 3; }   // skip UTF-8 BOM
    if (n == 0) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, p, static_cast<int>(n), nullptr, 0);
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p, static_cast<int>(n), &w[0], wlen);
    return w;
}

// Fill the resolved table from the built-in defaults (Russian, or English when
// the chosen language is anything other than "ru").
void ApplyBuiltinDefaults(bool english) {
    for (int i = 0; i < S_COUNT; ++i)
        g_str[i] = english ? kStrings[i].en : kStrings[i].ru;
}

// Overlay key=value pairs from an external lang file onto the resolved table.
// Reads the optional "lang.name = …" header into *outName.
void OverlayLangFile(const std::wstring& code, std::wstring* outName) {
    std::wstring text = ReadFileUtf8(LangFilePath(code));
    if (text.empty()) return;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        std::wstring line = text.substr(pos, eol == std::wstring::npos ? std::wstring::npos : eol - pos);
        pos = (eol == std::wstring::npos) ? text.size() : eol + 1;
        line = TrimW(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = TrimW(line.substr(0, eq));
        std::wstring val = TrimW(line.substr(eq + 1));
        // Allow \n escapes in values for multi-line dialog bodies.
        for (size_t k = 0; (k = val.find(L"\\n", k)) != std::wstring::npos; )
            val.replace(k, 2, L"\n");
        if (key == L"lang.name") { if (outName) *outName = val; continue; }
        for (int i = 0; i < S_COUNT; ++i)
            if (key == kStrings[i].key) { g_str[i] = val; break; }
    }
}

// Read only the "lang.name = …" header from a lang file (for the menu list).
std::wstring ReadLangName(const std::wstring& code) {
    std::wstring text = ReadFileUtf8(LangFilePath(code));
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        std::wstring line = TrimW(text.substr(pos, eol == std::wstring::npos ? std::wstring::npos : eol - pos));
        pos = (eol == std::wstring::npos) ? text.size() : eol + 1;
        if (line.rfind(L"lang.name", 0) == 0 &&
            (line.size() == 9 || line[9] == L'=' || line[9] == L' ' || line[9] == L'\t')) {
            size_t eq = line.find(L'=');
            if (eq != std::wstring::npos) return TrimW(line.substr(eq + 1));
        }
    }
    return L"";
}

// Resolve the full table for a language code: built-in base + file overlay.
void LoadLanguage(const std::wstring& code) {
    g_lang = code.empty() ? L"ru" : code;
    ApplyBuiltinDefaults(g_lang != L"ru");   // ru base for ru, en base for everything else
    // Overlay the file for every language: a custom code gets en base + its file,
    // and built-in ru/en still pick up any user tweaks in their .lang file.
    OverlayLangFile(g_lang, nullptr);
}

// Persist the chosen language to the registry.
void SaveLanguageCode(const std::wstring& code) {
    HKEY root = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubkey, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &root, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(root, L"language", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(code.c_str()),
                   static_cast<DWORD>((code.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(root);
}

// Decide which language to use: explicit registry value wins; otherwise
// auto-detect from the system UI language (Russian → ru, else en).
std::wstring DetectLanguageCode() {
    HKEY root = OpenRoot(KEY_READ);
    std::wstring code = TrimW(RegReadStr(root, L"language"));
    if (root) RegCloseKey(root);
    if (!code.empty()) return code;
    LANGID ui = GetUserDefaultUILanguage();
    return (PRIMARYLANGID(ui) == LANG_RUSSIAN) ? L"ru" : L"en";
}

// (Re)write lang\ru.lang and lang\en.lang from the built-in defaults so users
// have editable templates and a starting point for new translations.
void WriteLangTemplate(const std::wstring& code, bool english) {
    std::string out;
    auto append = [&out](const std::wstring& w) {
        if (w.empty()) return;
        // Pass the exact wide length (not -1) so the UTF-8 size excludes the
        // null terminator and the conversion writes the full content cleanly.
        int len = static_cast<int>(w.size());
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), len, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return;
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), len, &s[0], n, nullptr, nullptr);
        out += s;
    };
    append(L"# MultiMonitorPanel language file (UTF-8). Copy to <code>.lang and\n");
    append(L"# translate the right-hand side to add your own language.\n");
    append(L"lang.name = " + std::wstring(english ? L"English" : L"Русский") + L"\n");
    for (int i = 0; i < S_COUNT; ++i) {
        std::wstring v = english ? kStrings[i].en : kStrings[i].ru;
        // Encode embedded newlines as \n so each entry stays on one line.
        for (size_t k = 0; (k = v.find(L'\n', k)) != std::wstring::npos; k += 2)
            v.replace(k, 1, L"\\n");
        append(std::wstring(kStrings[i].key) + L" = " + v + L"\n");
    }
    HANDLE h = CreateFileW(LangFilePath(code).c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };   // UTF-8 BOM
    DWORD written = 0;
    WriteFile(h, bom, 3, &written, nullptr);
    WriteFile(h, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    CloseHandle(h);
}

// Seed ru.lang / en.lang ONLY if missing — never clobber a user's edits on a
// later start. Missing keys still fall back to the built-in defaults
// (ApplyBuiltinDefaults runs before OverlayLangFile), so a file from an older
// build keeps working; to refresh a template, delete the file and restart.
// Mirrors the "already configured → leave it" rule of EnsureRegistryConfig.
void EnsureLangFiles() {
    CreateDirectoryW(LangDir().c_str(), nullptr);
    if (GetFileAttributesW(LangFilePath(L"ru").c_str()) == INVALID_FILE_ATTRIBUTES)
        WriteLangTemplate(L"ru", false);
    if (GetFileAttributesW(LangFilePath(L"en").c_str()) == INVALID_FILE_ATTRIBUTES)
        WriteLangTemplate(L"en", true);
}

// Enumerate available languages: built-in ru/en plus any extra lang\*.lang.
std::vector<std::pair<std::wstring, std::wstring>> ListLanguages() {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    out.emplace_back(L"ru", L"Русский");
    out.emplace_back(L"en", L"English");
    WIN32_FIND_DATAW fd{};
    std::wstring glob = LangDir() + L"\\*.lang";
    HANDLE h = FindFirstFileW(glob.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            size_t dot = name.rfind(L'.');
            if (dot == std::wstring::npos) continue;
            std::wstring code = name.substr(0, dot);
            if (code == L"ru" || code == L"en") continue;
            std::wstring disp = ReadLangName(code);
            out.emplace_back(code, disp.empty() ? code : disp);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return out;
}

// ---------------------------- info.txt --------------------------------
// Drop a plain-language manual next to the exe on every launch, so the
// portable build\ kit is self-explanatory. Rewritten each start to stay in
// sync with the binary (it's reference, not a file the user is meant to edit).
void EnsureInfoFile() {
    std::wstring path = GetExeDir();
    if (!path.empty() && path.back() != L'\\') path += L'\\';
    path += L"info.txt";

    const wchar_t* infoRu =
        L"============================================================\r\n"
        L" MultiMonitorPanel — быстрый старт\r\n"
        L"============================================================\r\n"
        L"\r\n"
        L"ЧТО ЭТО\r\n"
        L"  Portable-панель запуска для Windows 10/11: свой док на\r\n"
        L"  КАЖДЫЙ монитор, и у каждого дока свой набор кнопок. Это\r\n"
        L"  решение для тех, кому нужен multi monitor dock, отдельная\r\n"
        L"  панель задач на второй монитор или быстрый запуск программ\r\n"
        L"  на нужном экране.\r\n"
        L"\r\n"
        L"  Это настоящая системная панель (AppBar): развёрнутые окна\r\n"
        L"  её не перекрывают — Windows вырезает под неё полосу. Один\r\n"
        L"  нативный exe меньше 400 КБ, без .NET и Electron.\r\n"
        L"\r\n"
        L"СКАЧАЛ РЕЛИЗ — ЧТО ДЕЛАТЬ\r\n"
        L"  1) Если скачал panel.exe, положи его в постоянную папку,\r\n"
        L"     например C:\\Tools\\MultiMonitorPanel.\r\n"
        L"  2) Если скачал zip-архив из Assets, сначала распакуй его.\r\n"
        L"  3) Запусти panel.exe двойным кликом.\r\n"
        L"  4) Не скачивай Source code и Code -> Download ZIP, если\r\n"
        L"     тебе нужна готовая программа, а не исходники.\r\n"
        L"  5) Для автозапуска: Win+R -> shell:startup -> положи туда\r\n"
        L"     ярлык на panel.exe.\r\n"
        L"\r\n"
        L"ЗАЧЕМ СОЗДАНО\r\n"
        L"  Windows показывает таскбар на нескольких мониторах, но не\r\n"
        L"  даёт удобно разложить разные закреплённые приложения по\r\n"
        L"  разным экранам. MultiMonitorPanel делает только это: папки\r\n"
        L"  могут жить на левом мониторе, IDE на центральном, терминалы\r\n"
        L"  и админские утилиты на правом.\r\n"
        L"\r\n"
        L"КАК РАБОТАЕТ\r\n"
        L"  Программа определяет мониторы слева направо, создаёт на\r\n"
        L"  каждом системную AppBar-панель и читает настройки из\r\n"
        L"  HKCU\\Software\\MultiMonitorPanel. При клике кнопка запускает\r\n"
        L"  цель через Windows Shell, а новое окно переносится на тот\r\n"
        L"  монитор, где была нажата кнопка.\r\n"
        L"\r\n"
        L"ЗАПУСК\r\n"
        L"  - Двойной клик по panel.exe.\r\n"
        L"  - Второй запуск молча выходит (работает одна копия).\r\n"
        L"  - Автозапуск: положи ярлык panel.exe в папку автозагрузки\r\n"
        L"    (Win+R -> shell:startup).\r\n"
        L"  - При подключении/отключении монитора панели сами\r\n"
        L"    пересоздаются.\r\n"
        L"\r\n"
        L"ПРАВАЯ КНОПКА МЫШИ ПО ПАНЕЛИ (меню):\r\n"
        L"  Изменить настройки…   открыть окно-редактор настроек (см. ниже)\r\n"
        L"  Перезагрузить         перечитать настройки и перерисовать панели\r\n"
        L"  Образцовый набор      записать СТАНДАРТНЫЙ образцовый набор\r\n"
        L"                        (на 3 монитора) — в нём показаны все фишки\r\n"
        L"  Стереть настройки     стереть все кнопки (панели станут пустыми)\r\n"
        L"  Язык / Language       переключить язык интерфейса\r\n"
        L"  Открыть папку         открыть папку с panel.exe (там же папка ico)\r\n"
        L"  О программе           версия, дата релиза, автор и ссылка на GitHub\r\n"
        L"  Выход                 закрыть программу\r\n"
        L"\r\n"
        L"РЕДАКТОР НАСТРОЕК (ПКМ -> Изменить настройки…)\r\n"
        L"  Окно правит настройки прямо на месте — без regedit и текстовых\r\n"
        L"  файлов.\r\n"
        L"  Сверху: выбор монитора (кнопки + / − добавляют и убирают\r\n"
        L"          монитор — можно настроить экран, который сейчас не\r\n"
        L"          подключён), высота полосы, папка иконок.\r\n"
        L"  Слева:  список кнопок выбранного монитора, СГРУППИРОВАННЫЙ по\r\n"
        L"          краю: секции «слева», «центр», «справа».\r\n"
        L"  Справа: поля выбранной кнопки — подпись, что запускать,\r\n"
        L"          аргументы, иконка (кнопка ... — выбрать файл),\r\n"
        L"          КРАЙ (к какому краю полосы прижать кнопку) и галочки\r\n"
        L"          админ / консоль / разделитель.\r\n"
        L"  Кнопки Добавить / + Разделитель / Удалить / ▲ / ▼ —\r\n"
        L"          менять состав и порядок. ▲/▼ на краю секции переносят\r\n"
        L"          запись в соседнюю группу.\r\n"
        L"  Разделитель сам берёт край от того места, где стоит — двигай\r\n"
        L"  его ▲/▼ в нужную секцию (его переключатель края неактивен).\r\n"
        L"  OK или Применить — сразу записать и перерисовать панели\r\n"
        L"  (Применить не закрывает окно). Отмена — выйти без записи.\r\n"
        L"\r\n"
        L"ГДЕ ЖИВУТ НАСТРОЙКИ\r\n"
        L"  В реестре: HKCU\\Software\\MultiMonitorPanel (не в файле).\r\n"
        L"  Правятся через окно-редактор: ПКМ -> Изменить настройки…\r\n"
        L"\r\n"
        L"КАК УСТРОЕНА КНОПКА\r\n"
        L"    label   — подпись (всплывает как подсказка)\r\n"
        L"    target  — что запускать\r\n"
        L"    args    — аргументы (необязательно)\r\n"
        L"    icon    — своя иконка (необязательно)\r\n"
        L"    flags   — доп. флаги (необязательно)\r\n"
        L"\r\n"
        L"  target может быть:\r\n"
        L"    - программа по имени:              notepad.exe, calc.exe\r\n"
        L"    - полный путь:                     C:\\Program Files\\App\\app.exe\r\n"
        L"    - папка (откроется в Проводнике):  C:\\Users\\Имя\\Downloads\r\n"
        L"    - приложение из Store:             shell:AppsFolder\\<AUMID>\r\n"
        L"\r\n"
        L"ФЛАГИ КНОПКИ (в редакторе — галочки и выбор блока)\r\n"
        L"    admin    запуск от администратора (запрос UAC)\r\n"
        L"    left     кнопка в ЛЕВОМ блоке (по умолчанию)\r\n"
        L"    center   кнопка в ЦЕНТРАЛЬНОМ блоке\r\n"
        L"    right    кнопка в ПРАВОМ блоке (прижата вправо)\r\n"
        L"    sep      это не кнопка, а вертикальный разделитель\r\n"
        L"    console  для cmd/powershell и др. консолей: открыть на ТОМ\r\n"
        L"             мониторе, где кнопка (через классический conhost).\r\n"
        L"             Без него консоль может уехать на другой монитор —\r\n"
        L"             её окном владеет Windows Terminal, а не сама команда.\r\n"
        L"\r\n"
        L"БЛОКИ (лево / центр / право)\r\n"
        L"  Полосу можно разбить на 2-3 блока. Например: папки слева\r\n"
        L"  (без флага), программы справа (флаг right). Часть иконок будет\r\n"
        L"  у левого края, часть — у правого.\r\n"
        L"\r\n"
        L"РАЗДЕЛИТЕЛИ\r\n"
        L"  Чтобы визуально разбить кнопки на группы — вставь разделитель:\r\n"
        L"  в редакторе нажми «+ Разделитель» (или у выбранной кнопки\r\n"
        L"  поставь галочку «Разделитель»). Это тонкая вертикальная черта;\r\n"
        L"  она тоже слушается края (слева/центр/справа).\r\n"
        L"\r\n"
        L"ИКОНКИ — КУДА КЛАСТЬ\r\n"
        L"  По умолчанию иконки берутся из подпапки \"ico\" РЯДОМ с panel.exe.\r\n"
        L"  Папка создаётся сама при запуске. Куда бы ни переместил exe —\r\n"
        L"  ico всегда ищется рядом с ним:\r\n"
        L"    - panel.exe в ...\\build  ->  иконки в ...\\build\\ico\r\n"
        L"    - panel.exe в C:\\ttt     ->  иконки в C:\\ttt\\ico\r\n"
        L"  В поле icon пиши просто имя файла (foo.png) — оно ищется в этой\r\n"
        L"  папке. Или укажи полный путь к файлу куда угодно.\r\n"
        L"  Сменить папку: значение icon_dir в реестре (относительное —\r\n"
        L"  рядом с exe; абсолютное — где угодно, напр. D:\\my-icons).\r\n"
        L"\r\n"
        L"  Форматы: .ico .png .jpg .bmp .gif .tiff  (SVG НЕ поддерживается).\r\n"
        L"  Любой размер ужимается до 32 px. Лучше квадрат 48..256 px,\r\n"
        L"  PNG с прозрачностью.\r\n"
        L"\r\n"
        L"ПЕРЕМЕННЫЕ ОКРУЖЕНИЯ\r\n"
        L"  В target/args/icon можно писать %USERNAME%, %USERPROFILE%,\r\n"
        L"  %APPDATA% и т.п. — подставятся автоматически.\r\n"
        L"\r\n"
        L"РАЗНЫЕ КНОПКИ НА РАЗНЫХ МОНИТОРАХ\r\n"
        L"  monitor_1 — самый левый монитор, monitor_2 — правее, и т.д.\r\n"
        L"  У каждого свой набор кнопок. На одномониторной машине\r\n"
        L"  используется только monitor_1.\r\n"
        L"\r\n"
        L"ОБРАЗЕЦ\r\n"
        L"  ПКМ -> Образцовый набор запишет образцовый набор, где сразу\r\n"
        L"  показаны все фишки: левый/центральный/правый блоки, разделители,\r\n"
        L"  запуск от админа, своя иконка, аргументы, открытие папки.\r\n"
        L"  Глянь его — и поймёшь синтаксис на примере.\r\n"
        L"\r\n"
        L"------------------------------------------------------------\r\n"
        L" © Evgenii Shapovalov 2026\r\n"
        L"============================================================\r\n";

    const wchar_t* infoEn =
        L"============================================================\r\n"
        L" MultiMonitorPanel — quick start\r\n"
        L"============================================================\r\n"
        L"\r\n"
        L"WHAT IT IS\r\n"
        L"  A portable launcher panel for Windows 10/11: one dock on\r\n"
        L"  EVERY monitor, each with its own buttons. It is for people\r\n"
        L"  who need a multi-monitor dock, a separate launcher on the\r\n"
        L"  second screen, or quick app launch on the right monitor.\r\n"
        L"\r\n"
        L"  It is a real system AppBar: maximized windows do not cover\r\n"
        L"  it because Windows reserves a strip for it. One native exe\r\n"
        L"  under 400 KB, no .NET, no Electron.\r\n"
        L"\r\n"
        L"DOWNLOADED THE RELEASE — WHAT TO DO\r\n"
        L"  1) If you downloaded panel.exe, put it into a permanent\r\n"
        L"     folder, for example C:\\Tools\\MultiMonitorPanel.\r\n"
        L"  2) If you downloaded a zip archive from Assets, extract it\r\n"
        L"     first.\r\n"
        L"  3) Double-click panel.exe.\r\n"
        L"  4) Do not download Source code or Code -> Download ZIP if\r\n"
        L"     you need the ready-to-run app, not the source files.\r\n"
        L"  5) For autostart: Win+R -> shell:startup -> put a shortcut\r\n"
        L"     to panel.exe there.\r\n"
        L"\r\n"
        L"WHY IT EXISTS\r\n"
        L"  Windows can show the taskbar on several monitors, but it does\r\n"
        L"  not let you split pinned launchers cleanly between screens.\r\n"
        L"  MultiMonitorPanel does exactly that: folders on the left\r\n"
        L"  monitor, IDEs in the center, terminals and admin tools on the\r\n"
        L"  right.\r\n"
        L"\r\n"
        L"HOW IT WORKS\r\n"
        L"  The program enumerates monitors from left to right, creates\r\n"
        L"  one system AppBar panel on each screen, and reads settings\r\n"
        L"  from HKCU\\Software\\MultiMonitorPanel. When you click a button,\r\n"
        L"  the target is launched through Windows Shell and the new window\r\n"
        L"  is moved to the monitor where the button was clicked.\r\n"
        L"\r\n"
        L"STARTING\r\n"
        L"  - Double-click panel.exe.\r\n"
        L"  - A second launch silently exits (only one copy runs).\r\n"
        L"  - Autostart: put a shortcut to panel.exe into the startup\r\n"
        L"    folder (Win+R -> shell:startup).\r\n"
        L"  - Panels rebuild themselves when a monitor is connected or\r\n"
        L"    disconnected.\r\n"
        L"\r\n"
        L"RIGHT-CLICK ON A PANEL (menu):\r\n"
        L"  Edit config…       open the settings editor window (see below)\r\n"
        L"  Reload config      re-read settings and redraw the panels\r\n"
        L"  Set default config write the STANDARD sample set (for 3\r\n"
        L"                     monitors) — it demonstrates every feature\r\n"
        L"  Erase config       remove all buttons (panels become empty)\r\n"
        L"  Language           switch the interface language\r\n"
        L"  Open app folder    open the folder with panel.exe (and ico)\r\n"
        L"  About              version, release date, author and GitHub link\r\n"
        L"  Exit               quit the program\r\n"
        L"\r\n"
        L"SETTINGS EDITOR (right-click -> Edit config…)\r\n"
        L"  The window edits settings in place — no regedit or text files.\r\n"
        L"  Top:    monitor picker (the + / − buttons add and remove a\r\n"
        L"          monitor — you can set up a screen that is not\r\n"
        L"          connected right now), bar height, icon folder.\r\n"
        L"  Left:   the selected monitor's button list, GROUPED by edge:\r\n"
        L"          “left”, “center”, “right” sections.\r\n"
        L"  Right:  the selected button's fields — label, what to run,\r\n"
        L"          arguments, icon (the … button picks a file), EDGE\r\n"
        L"          (which side of the bar to pin it to) and the\r\n"
        L"          admin / console / separator check boxes.\r\n"
        L"  The Add / + Separator / Delete / ▲ / ▼ buttons change the\r\n"
        L"          set and order. ▲/▼ at a section edge move the entry\r\n"
        L"          into the neighbouring group.\r\n"
        L"  A separator takes its edge from where it sits — move it with\r\n"
        L"  ▲/▼ into the right section (its edge radio is disabled).\r\n"
        L"  OK or Apply — write and redraw the panels at once (Apply\r\n"
        L"  keeps the window open). Cancel — leave without saving.\r\n"
        L"\r\n"
        L"WHERE SETTINGS LIVE\r\n"
        L"  In the registry: HKCU\\Software\\MultiMonitorPanel (not a file).\r\n"
        L"  Edit them through the window: right-click -> Edit config…\r\n"
        L"\r\n"
        L"HOW A BUTTON IS BUILT\r\n"
        L"    label   — caption (shown as a tooltip)\r\n"
        L"    target  — what to run\r\n"
        L"    args    — arguments (optional)\r\n"
        L"    icon    — a custom icon (optional)\r\n"
        L"    flags   — extra flags (optional)\r\n"
        L"\r\n"
        L"  target can be:\r\n"
        L"    - a program by name:              notepad.exe, calc.exe\r\n"
        L"    - a full path:                    C:\\Program Files\\App\\app.exe\r\n"
        L"    - a folder (opens in Explorer):   C:\\Users\\Name\\Downloads\r\n"
        L"    - a Store app:                    shell:AppsFolder\\<AUMID>\r\n"
        L"\r\n"
        L"BUTTON FLAGS (in the editor — check boxes and the block choice)\r\n"
        L"    admin    run as administrator (UAC prompt)\r\n"
        L"    left     button in the LEFT block (default)\r\n"
        L"    center   button in the CENTER block\r\n"
        L"    right    button in the RIGHT block (pinned right)\r\n"
        L"    sep      not a button, but a vertical separator\r\n"
        L"    console  for cmd/powershell and other consoles: open on the\r\n"
        L"             SAME monitor as the button (via classic conhost).\r\n"
        L"             Without it a console may drift to another monitor —\r\n"
        L"             its window is owned by Windows Terminal, not the cmd.\r\n"
        L"\r\n"
        L"BLOCKS (left / center / right)\r\n"
        L"  The bar can be split into 2-3 blocks. For example: folders on\r\n"
        L"  the left (no flag), programs on the right (the right flag).\r\n"
        L"  Some icons sit at the left edge, some at the right.\r\n"
        L"\r\n"
        L"SEPARATORS\r\n"
        L"  To visually split buttons into groups — insert a separator:\r\n"
        L"  in the editor press “+ Separator” (or tick the “Separator”\r\n"
        L"  box on a selected button). It is a thin vertical line; it also\r\n"
        L"  obeys the edge (left/center/right).\r\n"
        L"\r\n"
        L"ICONS — WHERE TO PUT THEM\r\n"
        L"  By default icons come from the \"ico\" subfolder NEXT TO\r\n"
        L"  panel.exe. The folder is created automatically on start.\r\n"
        L"  Wherever you move the exe, ico is always looked up next to it:\r\n"
        L"    - panel.exe in ...\\build  ->  icons in ...\\build\\ico\r\n"
        L"    - panel.exe in C:\\ttt     ->  icons in C:\\ttt\\ico\r\n"
        L"  In the icon field just write a file name (foo.png) — it is\r\n"
        L"  looked up in that folder. Or give a full path to any file.\r\n"
        L"  Change the folder: the icon_dir value in the registry\r\n"
        L"  (relative — next to the exe; absolute — anywhere, e.g.\r\n"
        L"  D:\\my-icons).\r\n"
        L"\r\n"
        L"  Formats: .ico .png .jpg .bmp .gif .tiff  (SVG NOT supported).\r\n"
        L"  Any size is shrunk to 32 px. A 48..256 px square PNG with\r\n"
        L"  transparency works best.\r\n"
        L"\r\n"
        L"ENVIRONMENT VARIABLES\r\n"
        L"  In target/args/icon you can use %USERNAME%, %USERPROFILE%,\r\n"
        L"  %APPDATA% and so on — they are expanded automatically.\r\n"
        L"\r\n"
        L"DIFFERENT BUTTONS ON DIFFERENT MONITORS\r\n"
        L"  monitor_1 — the leftmost monitor, monitor_2 — to its right,\r\n"
        L"  and so on. Each has its own button set. On a single-monitor\r\n"
        L"  machine only monitor_1 is used.\r\n"
        L"\r\n"
        L"SAMPLE\r\n"
        L"  Right-click -> Set default config writes a sample set that\r\n"
        L"  shows every feature at once: left/center/right blocks,\r\n"
        L"  separators, run-as-admin, a custom icon, arguments, opening a\r\n"
        L"  folder. Look at it to learn the syntax by example.\r\n"
        L"\r\n"
        L"------------------------------------------------------------\r\n"
        L" © Evgenii Shapovalov 2026\r\n"
        L"============================================================\r\n";

    const wchar_t* t = (g_lang == L"ru") ? infoRu : infoEn;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    unsigned char bom[2] = { 0xFF, 0xFE };          // UTF-16 LE BOM
    DWORD written = 0;
    WriteFile(h, bom, 2, &written, nullptr);
    WriteFile(h, t, static_cast<DWORD>(wcslen(t) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(h);
}

// --------------------------- icon loading ----------------------------
HICON LoadViaGdiPlus(const std::wstring& path, int px) {
    Gdiplus::Bitmap src(path.c_str(), FALSE);
    if (src.GetLastStatus() != Gdiplus::Ok) return nullptr;

    Gdiplus::Bitmap scaled(px, px, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(&scaled);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.DrawImage(&src, 0, 0, px, px);
    }
    HICON hIcon = nullptr;
    if (scaled.GetHICON(&hIcon) != Gdiplus::Ok) return nullptr;
    return hIcon;
}

HICON LoadFileIcon(const std::wstring& iconFile, int px) {
    std::wstring p = ExpandEnv(iconFile);

    // Resolve bare filenames against [global] icon_dir so the user can write
    // `icon_override = foo.png` instead of a full path.
    if (!g_global.iconDir.empty() &&
        p.find(L'\\') == std::wstring::npos &&
        p.find(L'/')  == std::wstring::npos &&
        !(p.size() >= 2 && p[1] == L':')) {
        p = g_global.iconDir + p;
    }

    // Pick decoder by extension: .ico → LoadImage; PNG/JPG/BMP/GIF/TIFF → GDI+.
    std::wstring ext;
    size_t dot = p.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext = p.substr(dot);
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    }
    if (ext == L".ico") {
        return (HICON)LoadImageW(nullptr, p.c_str(), IMAGE_ICON,
                                 px, px, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
    }
    return LoadViaGdiPlus(p, px);
}

HICON LoadShellIcon(const std::wstring& pathRaw, int px) {
    std::wstring path = ExpandEnv(pathRaw);

    // Resolve bare filenames (e.g. "notepad.exe") via PATH — SHGetFileInfo
    // does not search PATH itself, so without this bare exe names fall back
    // to the generic application icon.
    if (path.find(L'\\') == std::wstring::npos &&
        path.find(L'/')  == std::wstring::npos) {
        wchar_t resolved[MAX_PATH * 2]{};
        wchar_t* fnp = nullptr;
        DWORD n = SearchPathW(nullptr, path.c_str(), nullptr,
                              static_cast<DWORD>(_countof(resolved)),
                              resolved, &fnp);
        if (n == 0) {
            // App Paths registry (e.g. winword.exe, chrome.exe registered there).
            wchar_t assoc[MAX_PATH * 2]{};
            DWORD sz = static_cast<DWORD>(_countof(assoc));
            if (SUCCEEDED(AssocQueryStringW(ASSOCF_INIT_BYEXENAME,
                                            ASSOCSTR_EXECUTABLE,
                                            path.c_str(), nullptr,
                                            assoc, &sz))) {
                path = assoc;
            }
        } else if (n < _countof(resolved)) {
            path = resolved;
        }
    }

    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX)) {
        int which = (px >= 256) ? SHIL_JUMBO
                  : (px >= 48)  ? SHIL_EXTRALARGE
                  : (px >= 32)  ? SHIL_LARGE
                                : SHIL_SMALL;
        IImageList* pil = nullptr;
        HRESULT hr = SHGetImageList(which, IID_IImageList, reinterpret_cast<void**>(&pil));
        if (SUCCEEDED(hr) && pil) {
            HICON hIcon = nullptr;
            pil->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon);
            pil->Release();
            if (hIcon) return hIcon;
        }
    }

    // Fallback — SHGetFileInfo returning an HICON directly (32x32 typical).
    SHFILEINFOW sfi2{};
    UINT flags = SHGFI_ICON | ((px > 16) ? SHGFI_LARGEICON : SHGFI_SMALLICON);
    if (SHGetFileInfoW(path.c_str(), 0, &sfi2, sizeof(sfi2), flags) && sfi2.hIcon)
        return sfi2.hIcon;

    return LoadIconW(nullptr, IDI_APPLICATION);
}

// --------------------------- class registration ----------------------
void RegisterPanelClass() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = PanelProc;
    wc.hInstance     = g_hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(COL_PANEL_BG);
    wc.lpszClassName = kPanelClass;
    // Embedded app icon (res\app.ico, id 1). Fallback to generic if missing.
    HICON appIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    wc.hIcon         = appIcon ? appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
}

void RegisterCtrlClass() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc   = CtrlProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = kCtrlClass;
    RegisterClassExW(&wc);
}

// --------------------------- monitor enum ----------------------------
struct MonItem {
    HMONITOR h;
    RECT     rc;
    UINT     dpi;
};

BOOL CALLBACK MonEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<MonItem>*>(lp);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(hMon, &mi)) return TRUE;

    MonItem m{hMon, mi.rcMonitor, 96};
    UINT dpiX = 96, dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
        m.dpi = dpiX;
    v->push_back(m);
    return TRUE;
}

std::vector<MonItem> EnumMonitorsLR() {
    std::vector<MonItem> v;
    EnumDisplayMonitors(nullptr, nullptr, MonEnumProc, reinterpret_cast<LPARAM>(&v));
    std::sort(v.begin(), v.end(), [](const MonItem& a, const MonItem& b) {
        if (a.rc.left != b.rc.left) return a.rc.left < b.rc.left;
        return a.rc.top < b.rc.top;
    });
    return v;
}

// ------------------------- panel lifecycle ---------------------------
void CreateAllPanels() {
    auto mons = EnumMonitorsLR();

    for (size_t i = 0; i < mons.size(); ++i) {
        auto p       = std::make_unique<PanelWindow>();
        p->hMonitor  = mons[i].h;
        p->monRect   = mons[i].rc;
        p->index     = static_cast<int>(i + 1);
        p->dpi       = mons[i].dpi;
        p->buttons   = LoadButtonsForMonitor(p->index);

        int w   = p->monRect.right - p->monRect.left;
        int hPx = MulDiv(g_global.heightDip, p->dpi, 96);
        int x   = p->monRect.left;
        int y   = p->monRect.bottom - hPx;

        p->hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kPanelClass, kAppTitle,
            WS_POPUP | WS_CLIPCHILDREN,
            x, y, w, hPx,
            nullptr, nullptr, g_hInst, nullptr);
        if (!p->hwnd) continue;

        SetWindowLongPtrW(p->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p.get()));

        // Load icons.
        int iconPx = MulDiv(ICON_SIZE_DIP, p->dpi, 96);
        p->icons.reserve(p->buttons.size());
        for (const auto& b : p->buttons) {
            if (b.isSeparator) { p->icons.push_back(nullptr); continue; }
            HICON h = nullptr;
            if (!b.iconPath.empty()) h = LoadFileIcon(b.iconPath, iconPx);
            if (!h) h = LoadShellIcon(b.target, iconPx);
            p->icons.push_back(h ? h : LoadIconW(nullptr, IDI_APPLICATION));
        }

        // Create owner-drawn buttons. Separators are also BUTTON controls but
        // WS_DISABLED (no clicks, no hover tracking) — we only owner-draw a line.
        p->btnHwnds.reserve(p->buttons.size());
        for (size_t k = 0; k < p->buttons.size(); ++k) {
            DWORD style = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_FLAT;
            if (p->buttons[k].isSeparator) style |= WS_DISABLED;
            HWND btn = CreateWindowExW(0, L"BUTTON", L"",
                style,
                0, 0, 0, 0,
                p->hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ID_BASE + static_cast<int>(k))),
                g_hInst, nullptr);
            if (btn && !p->buttons[k].isSeparator)
                SetWindowSubclass(btn, BtnSubclassProc, 1, 0);
            p->btnHwnds.push_back(btn);
        }

        // Tooltip.
        p->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            0, 0, 0, 0, p->hwnd, nullptr, g_hInst, nullptr);
        SetWindowPos(p->tooltip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        for (size_t k = 0; k < p->buttons.size() && k < p->btnHwnds.size(); ++k) {
            if (p->buttons[k].isSeparator || p->buttons[k].label.empty()) continue;
            TOOLINFOW ti{sizeof(ti)};
            ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd     = p->hwnd;
            ti.uId      = reinterpret_cast<UINT_PTR>(p->btnHwnds[k]);
            ti.lpszText = const_cast<LPWSTR>(p->buttons[k].label.c_str());
            SendMessageW(p->tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        }

        SetupAppBar(p.get());
        LayoutPanelButtons(p.get());
        ShowWindow(p->hwnd, SW_SHOWNA);
        UpdateWindow(p->hwnd);

        g_panels.push_back(std::move(p));
    }
}

void DestroyAllPanels() {
    for (auto& p : g_panels) {
        if (p->hwnd) TeardownAppBar(p.get());
        if (p->tooltip) {
            DestroyWindow(p->tooltip);
            p->tooltip = nullptr;
        }
        for (HICON h : p->icons) if (h) DestroyIcon(h);
        p->icons.clear();
        if (p->hwnd) {
            DestroyWindow(p->hwnd);
            p->hwnd = nullptr;
        }
    }
    g_panels.clear();
}

void LayoutPanelButtons(PanelWindow* p) {
    int gap  = MulDiv(BTN_GAP_DIP,   p->dpi, 96);
    int sepW = MulDiv(SEP_WIDTH_DIP, p->dpi, 96);
    RECT cr;
    GetClientRect(p->hwnd, &cr);
    int client_w = cr.right - cr.left;
    int client_h = cr.bottom - cr.top;

    int btn = client_h - gap * 2;
    if (btn < 16) btn = client_h;
    int y = (client_h - btn) / 2;

    // Slot width: separators are a thin fixed strip, buttons are square.
    auto slotW = [&](size_t i) { return p->buttons[i].isSeparator ? sepW : btn; };

    // Partition slot indices into the three alignment blocks, preserving the
    // config order within each block. Skip any slot whose window failed to
    // create so it never throws off the geometry.
    std::vector<size_t> blocks[3];   // 0 = Left, 1 = Center, 2 = Right
    for (size_t i = 0; i < p->buttons.size(); ++i) {
        if (i >= p->btnHwnds.size() || !p->btnHwnds[i]) continue;
        int bi = (p->buttons[i].align == BtnAlign::Right)  ? 2
               : (p->buttons[i].align == BtnAlign::Center) ? 1 : 0;
        blocks[bi].push_back(i);
    }

    auto blockWidth = [&](const std::vector<size_t>& blk) -> int {
        int w = 0;
        for (size_t n = 0; n < blk.size(); ++n) {
            w += slotW(blk[n]);
            if (n + 1 < blk.size()) w += gap;
        }
        return w;
    };

    // Left hugs the left edge, Right hugs the right edge, Center is centred.
    int startX[3] = {
        gap,                                        // Left
        (client_w - blockWidth(blocks[1])) / 2,     // Center
        client_w - gap - blockWidth(blocks[2]),     // Right
    };

    for (int bI = 0; bI < 3; ++bI) {
        int x = startX[bI];
        for (size_t i : blocks[bI]) {
            int w = slotW(i);
            MoveWindow(p->btnHwnds[i], x, y, w, btn, TRUE);
            x += w + gap;
        }
    }
}

// ---------------------------- AppBar I/O -----------------------------
void SetupAppBar(PanelWindow* p) {
    APPBARDATA abd{};
    abd.cbSize           = sizeof(abd);
    abd.hWnd             = p->hwnd;
    abd.uCallbackMessage = WM_APPBAR_CB;
    SHAppBarMessage(ABM_NEW, &abd);
    RepositionAppBar(p);
}

void TeardownAppBar(PanelWindow* p) {
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd   = p->hwnd;
    SHAppBarMessage(ABM_REMOVE, &abd);
}

void RepositionAppBar(PanelWindow* p) {
    int hPx = MulDiv(g_global.heightDip, p->dpi, 96);

    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd   = p->hwnd;
    abd.uEdge  = ABE_BOTTOM;

    // Start with the monitor rect, trimmed to the desired height at the bottom.
    abd.rc        = p->monRect;
    abd.rc.top    = abd.rc.bottom - hPx;
    SHAppBarMessage(ABM_QUERYPOS, &abd);

    // ABM_QUERYPOS may have shifted abd.rc.top (e.g. another AppBar on this edge).
    // Re-fix our height against the (possibly new) bottom.
    abd.rc.top = abd.rc.bottom - hPx;
    SHAppBarMessage(ABM_SETPOS, &abd);

    MoveWindow(p->hwnd, abd.rc.left, abd.rc.top,
               abd.rc.right - abd.rc.left,
               abd.rc.bottom - abd.rc.top, TRUE);
    LayoutPanelButtons(p);
}

// ------------------------ launch-on-this-monitor ---------------------
// After a button launches a process, move that process's main window onto
// the monitor whose panel was clicked, centred in that monitor's work area.
//
// Reliable only for plain Win32 apps that start fresh: we wait for a top-level
// window owned by the PID we launched. Already-running single-instance apps
// (they hand the request to the existing process and exit), UWP / shell:
// activations, and launcher-stub processes own their window under a *different*
// PID — there we silently do nothing and the app opens wherever it likes.

struct PlaceCtx {
    DWORD  pid      = 0;
    HANDLE hProcess = nullptr;
    RECT   monRect  {};   // rcMonitor of the target monitor (copied by value)
};

struct PidWindowSearch { const std::vector<DWORD>* pids; HWND found; };

BOOL CALLBACK FindPidWindowProc(HWND hwnd, LPARAM lp) {
    auto* s = reinterpret_cast<PidWindowSearch*>(lp);
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (std::find(s->pids->begin(), s->pids->end(), wpid) == s->pids->end())
                                         return TRUE;   // not our process (or a child of it)
    if (!IsWindowVisible(hwnd))          return TRUE;   // not shown yet
    if (GetWindow(hwnd, GW_OWNER))       return TRUE;   // owned dialog/tool window
    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;   // splash / no title yet
    s->found = hwnd;
    return FALSE;                                       // found it — stop enumerating
}

HWND FindMainWindowOfPids(const std::vector<DWORD>& pids) {
    PidWindowSearch s{ &pids, nullptr };
    EnumWindows(FindPidWindowProc, reinterpret_cast<LPARAM>(&s));
    return s.found;
}

// The window we actually want often belongs to a *child* process, not the one we
// launched: a console app's window is owned by a child conhost.exe, and launcher
// stubs spawn the real app as a child. Collect the launched PID plus all its
// descendants so FindMainWindowOfPids can match any of them.
std::vector<DWORD> CollectProcessTree(DWORD rootPid) {
    std::vector<DWORD> tree{ rootPid };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return tree;

    std::vector<std::pair<DWORD, DWORD>> procs;        // (pid, parentPid) of every process
    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do { procs.emplace_back(pe.th32ProcessID, pe.th32ParentProcessID); }
        while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    // Breadth-first: for each PID already in the tree, pull in its children.
    for (size_t i = 0; i < tree.size(); ++i)
        for (const auto& pr : procs)
            if (pr.second == tree[i] &&
                std::find(tree.begin(), tree.end(), pr.first) == tree.end())
                tree.push_back(pr.first);
    return tree;
}

void CenterWindowOnMonitor(HWND hwnd, const RECT& monRect) {
    HMONITOR hm = MonitorFromRect(&monRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(hm, &mi)) return;
    const RECT& wa = mi.rcWork;                  // excludes taskbars / our AppBar
    const int waw = wa.right - wa.left;
    const int wah = wa.bottom - wa.top;

    WINDOWPLACEMENT wp{sizeof(wp)};
    GetWindowPlacement(hwnd, &wp);
    const bool wasMax = (wp.showCmd == SW_SHOWMAXIMIZED);
    if (wasMax) ShowWindow(hwnd, SW_RESTORE);    // restore so the move takes effect...

    RECT r{};
    GetWindowRect(hwnd, &r);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w > waw) w = waw;                         // never wider/taller than the work area
    if (h > wah) h = wah;
    const int x = wa.left + (waw - w) / 2;
    const int y = wa.top  + (wah - h) / 2;
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

    if (wasMax) ShowWindow(hwnd, SW_SHOWMAXIMIZED); // ...then re-maximize on this monitor
}

DWORD WINAPI PlaceThreadProc(LPVOID param) {
    std::unique_ptr<PlaceCtx> ctx(reinterpret_cast<PlaceCtx*>(param));

    WaitForInputIdle(ctx->hProcess, 5000);       // let the UI spin up first

    HWND target = nullptr;
    for (int i = 0; i < 100; ++i) {              // poll up to ~10 s
        // Rebuild the tree each pass — a child conhost / the real app can appear late.
        target = FindMainWindowOfPids(CollectProcessTree(ctx->pid));
        if (target) break;
        // Parent gone and still no window anywhere in its tree → single-instance
        // handoff: the real window lives under an unrelated PID, give up.
        if (WaitForSingleObject(ctx->hProcess, 0) == WAIT_OBJECT_0) {
            target = FindMainWindowOfPids(CollectProcessTree(ctx->pid));   // one last look
            break;
        }
        Sleep(100);
    }
    // One-shot move onto the target monitor. Apps that aggressively restore
    // their own saved position (e.g. Winbox) may override this — that's accepted
    // rather than fought with repeated nudging, which only makes windows jitter.
    if (target) CenterWindowOnMonitor(target, ctx->monRect);

    CloseHandle(ctx->hProcess);
    return 0;
}

// Takes ownership of hProcess (the worker thread closes it).
void PlaceProcessWindowOnMonitor(HANDLE hProcess, const RECT& monRect) {
    auto* ctx = new PlaceCtx{ GetProcessId(hProcess), hProcess, monRect };
    HANDLE th = CreateThread(nullptr, 0, PlaceThreadProc, ctx, 0, nullptr);
    if (th) {
        CloseHandle(th);
    } else {
        CloseHandle(hProcess);                   // thread never started — clean up here
        delete ctx;
    }
}

// ---------------------------- click / menu ---------------------------
void RunButton(PanelWindow* p, int idx) {
    if (idx < 0 || idx >= static_cast<int>(p->buttons.size())) return;
    if (p->buttons[idx].isSeparator) return;   // not launchable

    std::wstring target = ExpandEnv(p->buttons[idx].target);

    // For shell: URIs (e.g. shell:AppsFolder\<AUMID>) there's no meaningful
    // parent directory — leaving lpDirectory=NULL lets the shell decide.
    std::wstring workDir;
    bool isShellUri = (_wcsnicmp(target.c_str(), L"shell:", 6) == 0);
    if (!isShellUri) {
        wchar_t wd[MAX_PATH * 2]{};
        wcsncpy_s(wd, _countof(wd), target.c_str(), _TRUNCATE);
        if (PathRemoveFileSpecW(wd)) workDir = wd;
    }

    std::wstring args = ExpandEnv(p->buttons[idx].args);

    // flags=console: launch through classic conhost.exe. With the default
    // terminal set to "Let Windows decide"/Windows Terminal, a console's window
    // belongs to WindowsTerminal.exe — outside our process tree — so per-monitor
    // placement can't find it (the console opens on whatever monitor WT picks).
    // conhost.exe <cmd> forces a classic console window that IS in our tree, so
    // PlaceProcessWindowOnMonitor can move it onto the clicked panel's monitor.
    if (p->buttons[idx].console && !isShellUri) {
        std::wstring inner = target;
        if (inner.find(L' ') != std::wstring::npos) inner = L"\"" + inner + L"\"";
        if (!args.empty()) inner += L" " + args;
        target = L"conhost.exe";
        args   = inner;
    }

    SHELLEXECUTEINFOW sei{sizeof(sei)};
    // No SEE_MASK_ASYNCOK: it makes ShellExecuteEx return BEFORE hProcess is
    // filled in, so we'd never get a handle and the per-monitor move below would
    // never run. Synchronous launch populates hProcess.
    sei.fMask        = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd         = p->hwnd;
    sei.lpVerb       = p->buttons[idx].runAsAdmin ? L"runas" : nullptr;
    sei.lpFile       = target.c_str();
    sei.lpParameters = args.empty()   ? nullptr : args.c_str();
    sei.lpDirectory  = workDir.empty() ? nullptr : workDir.c_str();
    sei.nShow        = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei) && sei.hProcess) {
        // Move the launched app onto this panel's monitor. Takes ownership of
        // sei.hProcess. hProcess is NULL for shell:/UWP activations and for
        // single-instance handoffs — those just open wherever they like.
        PlaceProcessWindowOnMonitor(sei.hProcess, p->monRect);
    }
}

// --------------------------- menu icons ------------------------------
// Small per-item glyphs so the context menu reads at a glance. We pull system
// stock icons, then convert each HICON to a 32-bpp premultiplied DIB — the only
// HBITMAP form menus render with clean alpha.
HICON StockIcon(SHSTOCKICONID id) {
    SHSTOCKICONINFO sii{ sizeof(sii) };
    if (SUCCEEDED(SHGetStockIconInfo(id, SHGSI_ICON | SHGSI_LARGEICON, &sii)))
        return sii.hIcon;   // caller owns it
    return nullptr;
}

HICON ReloadMenuIcon(int px) {
    if (px <= 0) return nullptr;

    Gdiplus::Bitmap bmp(px, px, PixelFormat32bppPARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));

    COLORREF rgb = GetSysColor(COLOR_HOTLIGHT);
    Gdiplus::Color color(255, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb));
    const float stroke = std::max(2.0f, px * 0.14f);
    const float inset = stroke * 1.8f;

    Gdiplus::Pen pen(color, stroke);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::AdjustableArrowCap arrow(stroke * 1.45f, stroke * 1.25f, TRUE);
    pen.SetCustomEndCap(&arrow);

    Gdiplus::RectF arc(inset, inset,
                       static_cast<float>(px) - inset * 2.0f,
                       static_cast<float>(px) - inset * 2.0f);
    g.DrawArc(&pen, arc, 25.0f, 275.0f);

    HICON hIcon = nullptr;
    if (bmp.GetHICON(&hIcon) != Gdiplus::Ok) return nullptr;
    return hIcon;
}

HBITMAP IconToMenuBitmap(HICON hIcon, int px) {
    if (!hIcon) return nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = px;
    bi.bmiHeader.biHeight      = -px;          // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp) return nullptr;
    {
        Gdiplus::Bitmap surface(px, px, px * 4, PixelFormat32bppPARGB,
                                static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&surface);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        Gdiplus::Bitmap src(hIcon);
        g.DrawImage(&src, Gdiplus::Rect(0, 0, px, px));
    }
    return hbmp;
}

// Convert + attach an icon to a menu item. Destroys the HICON, parks the
// HBITMAP in `keep` so it outlives the (blocking) TrackPopupMenu call.
void SetMenuIcon(HMENU m, UINT id, HICON hIcon, int px, std::vector<HBITMAP>& keep) {
    if (!hIcon) return;
    HBITMAP hb = IconToMenuBitmap(hIcon, px);
    DestroyIcon(hIcon);
    if (!hb) return;
    MENUITEMINFOW mii{ sizeof(mii) };
    mii.fMask    = MIIM_BITMAP;
    mii.hbmpItem = hb;
    SetMenuItemInfoW(m, id, FALSE, &mii);
    keep.push_back(hb);
}

// --------------------------- dialogs ---------------------------------
// Modern TaskDialog wrappers (comctl32 v6) — a big main instruction + softer
// body text, far friendlier than a bare MessageBox.
void InfoDialog(HWND hwnd, PCWSTR mainInstr, PCWSTR content, PCWSTR icon) {
    TASKDIALOGCONFIG cfg{ sizeof(cfg) };
    cfg.hwndParent         = hwnd;
    cfg.hInstance          = g_hInst;
    cfg.dwFlags            = TDF_ALLOW_DIALOG_CANCELLATION;
    cfg.pszWindowTitle     = kAppTitle;
    cfg.pszMainIcon        = icon;
    cfg.pszMainInstruction = mainInstr;
    cfg.pszContent         = content;
    cfg.dwCommonButtons    = TDCBF_OK_BUTTON;
    if (FAILED(TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr)))
        MessageBoxW(hwnd, content, kAppTitle, MB_OK);   // fallback if TaskDialog unavailable
}

// Yes/No with a meaningful action label (e.g. "Заменить"). Default = Отмена.
bool ConfirmDialog(HWND hwnd, PCWSTR mainInstr, PCWSTR content,
                   PCWSTR okLabel, PCWSTR icon) {
    const TASKDIALOG_BUTTON buttons[] = {
        { IDOK,     okLabel },
        { IDCANCEL, T(S_COMMON_CANCEL) },
    };
    TASKDIALOGCONFIG cfg{ sizeof(cfg) };
    cfg.hwndParent         = hwnd;
    cfg.hInstance          = g_hInst;
    cfg.dwFlags            = TDF_ALLOW_DIALOG_CANCELLATION;
    cfg.pszWindowTitle     = kAppTitle;
    cfg.pszMainIcon        = icon;
    cfg.pszMainInstruction = mainInstr;
    cfg.pszContent         = content;
    cfg.pButtons           = buttons;
    cfg.cButtons           = ARRAYSIZE(buttons);
    cfg.nDefaultButton     = IDCANCEL;
    int pressed = 0;
    if (FAILED(TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr)))
        return MessageBoxW(hwnd, content, kAppTitle, MB_YESNO) == IDYES;
    return pressed == IDOK;
}

// Callback for the About dialog — open the repo link in the default browser
// when the user clicks it (TDF_ENABLE_HYPERLINKS makes <a href> clickable).
HRESULT CALLBACK AboutCallback(HWND, UINT msg, WPARAM, LPARAM lp, LONG_PTR) {
    if (msg == TDN_HYPERLINK_CLICKED)
        ShellExecuteW(nullptr, L"open", reinterpret_cast<PCWSTR>(lp), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}

void AboutDialog(HWND hwnd) {
    std::wstring content =
        std::wstring(T(S_ABOUT_VERSION)) + L" " + kVersion + L"\n"
        + T(S_ABOUT_RELEASED) + L": " + kReleaseDate + L"\n"
        + T(S_ABOUT_AUTHOR) + L": " + kAuthor + L"\n\n"
        L"<a href=\"" + std::wstring(kRepoUrl) + L"\">" + std::wstring(kRepoUrl) + L"</a>\n\n"
        L"© " + std::wstring(kAuthor) + L" 2026 · " + T(S_ABOUT_LICENSE);
    TASKDIALOGCONFIG cfg{ sizeof(cfg) };
    cfg.hwndParent         = hwnd;
    cfg.hInstance          = g_hInst;
    cfg.dwFlags            = TDF_ALLOW_DIALOG_CANCELLATION | TDF_ENABLE_HYPERLINKS;
    cfg.pszWindowTitle     = kAppTitle;
    cfg.pszMainIcon        = TD_INFORMATION_ICON;
    cfg.pszMainInstruction = kAppTitle;
    cfg.pszContent         = content.c_str();
    cfg.dwCommonButtons    = TDCBF_CLOSE_BUTTON;
    cfg.pfCallback         = AboutCallback;
    if (FAILED(TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr)))
        MessageBoxW(hwnd, content.c_str(), kAppTitle, MB_OK);
}

// ===================== built-in config editor ========================
// A modeless master-detail window for editing the registry config without
// regedit or an ini round-trip. Left: the selected monitor's button list.
// Right: the selected button's fields. OK/Apply serialize the in-memory model
// straight back to HKCU (the same RegDeleteTree → WriteGlobal → WriteButton
// the old ini import used) and post WM_MMP_RELOAD so the panels rebuild at once.

struct EditorState {
    int          heightDip = DEFAULT_HEIGHT_DIP;
    std::wstring iconDir   = L"ico";              // raw registry value (relative/env), not g_global
    std::vector<std::vector<ButtonCfg>> mons;     // mons[0] = monitor_1 …
    int   curMon  = 0;
    int   curBtn  = -1;                             // index into mons[curMon], or -1
    UINT  dpi     = 96;
    HFONT font    = nullptr;
    HWND  tip     = nullptr;                        // tooltip control (recreated on language change)
    bool  loading = false;                        // guard: silence EN_CHANGE/clicks while filling fields
    bool  dirty   = false;
    std::vector<int> rowMap;                       // listbox row → model index (-1 = section header)
    ButtonCfg clip;                                // copy/paste buffer for the list context menu
    bool      hasClip = false;                     // is clip populated yet?
};

// Buttons are kept grouped by alignment (Left, then Center, then Right) so the
// list mirrors the panel layout; within a group the user's order is preserved.
// A separator inherits the group it sits in — it has no independent alignment in
// the UI; ▲/▼ across a group boundary just changes which group it (or a button)
// belongs to. EditorRegroup re-establishes the invariant after edits.
int EdAlignRank(BtnAlign a) {
    return a == BtnAlign::Left ? 0 : a == BtnAlign::Center ? 1 : 2;
}
void EditorRegroup(std::vector<ButtonCfg>& vec) {
    std::stable_sort(vec.begin(), vec.end(),
        [](const ButtonCfg& a, const ButtonCfg& b) {
            return EdAlignRank(a.align) < EdAlignRank(b.align);
        });
}

EditorState* EdState(HWND h) {
    return reinterpret_cast<EditorState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
}

// ---- tiny control accessors (all addressed by id via GetDlgItem) ----
std::wstring EdGetText(HWND hwnd, int id) {
    HWND c = GetDlgItem(hwnd, id);
    int n = GetWindowTextLengthW(c);
    if (n <= 0) return L"";
    std::wstring s(n, L'\0');
    GetWindowTextW(c, &s[0], n + 1);
    return s;
}
void EdSetText(HWND hwnd, int id, const std::wstring& s) {
    SetWindowTextW(GetDlgItem(hwnd, id), s.c_str());
}
bool EdChecked(HWND hwnd, int id) {
    return SendMessageW(GetDlgItem(hwnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}
void EdCheck(HWND hwnd, int id, bool on) {
    SendMessageW(GetDlgItem(hwnd, id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}
void EdEnable(HWND hwnd, int id, bool on) {
    EnableWindow(GetDlgItem(hwnd, id), on ? TRUE : FALSE);
}

// ---- control layout table (DIP; scaled by S() at create + WM_DPICHANGED) ----
enum EdKind { K_STATIC, K_GROUP, K_EDIT, K_EDITNUM, K_COMBO, K_LIST,
              K_BTN, K_DEFBTN, K_RADIO1, K_RADIO, K_CHECK, K_CHECKG };
// Anchor flags — how a control reacts to the editor window being resized past
// its base size (kEdClientW/H). dW/dH = current client minus base client.
enum EdAnchor {
    A_NONE     = 0,
    A_WSTRETCH = 1,   // width  += dW  (field grows to the right)
    A_HSTRETCH = 2,   // height += dH  (list / groupbox grows down)
    A_MOVEX    = 4,   // x      += dW  (sticks to the right edge)
    A_MOVEY    = 8,   // y      += dH  (sticks to the bottom edge)
};
struct EdCtl { int id; EdKind kind; const wchar_t* text; int x, y, w, h; int anchor; };

// "Mute" ids for labels/groupboxes we never address by hand.
constexpr int IDC_LBL_MON=3200, IDC_LBL_H=3201, IDC_LBL_ICODIR=3202,
              IDC_GRP_BTN=3203, IDC_GRP_PROP=3204, IDC_LBL_LABEL=3205,
              IDC_LBL_TARGET=3206, IDC_LBL_ARGS=3207, IDC_LBL_ICON=3208,
              IDC_LBL_ALIGN=3209;

const EdCtl kEd[] = {
    // top row — monitor picker + global height / icon dir
    { IDC_LBL_MON,       K_STATIC,  L"Монитор:",   12,  16,  54, 18 },
    { IDC_MON_COMBO,     K_COMBO,   L"",           66,  12, 100, 200 },
    { IDC_MON_ADD,       K_BTN,     L"+",         170,  12,  24, 23 },
    { IDC_MON_DEL,       K_BTN,     L"−",         196,  12,  24, 23 },
    { IDC_LBL_H,         K_STATIC,  L"Высота:",   238,  16,  46, 18 },
    { IDC_HEIGHT_EDIT,   K_EDITNUM, L"",          286,  12,  44, 23 },
    { IDC_LBL_ICODIR,    K_STATIC,  L"Папка иконок:", 344, 16, 90, 18 },
    { IDC_ICONDIR_EDIT,  K_EDIT,    L"",          436,  12, 100, 23, A_WSTRETCH },
    { IDC_ICONDIR_BROWSE,K_BTN,     L"...",        540,  12,  32, 23, A_MOVEX },
    // left column — button list
    { IDC_GRP_BTN,       K_GROUP,   L"Кнопки",     12,  44, 250, 250, A_HSTRETCH },
    { IDC_BTN_LIST,      K_LIST,    L"",           24,  64, 226, 218, A_HSTRETCH },
    { IDC_BTN_ADD,       K_BTN,     L"Добавить",     12, 300, 118, 26, A_MOVEY },
    { IDC_BTN_ADDSEP,    K_BTN,     L"+ Разделитель",136, 300, 118, 26, A_MOVEY },
    { IDC_BTN_DEL,       K_BTN,     L"Удалить",      12, 332, 118, 26, A_MOVEY },
    { IDC_BTN_UP,        K_BTN,     L"▲",           136, 332,  56, 26, A_MOVEY },
    { IDC_BTN_DOWN,      K_BTN,     L"▼",           198, 332,  56, 26, A_MOVEY },
    // right column — selected button's properties
    { IDC_GRP_PROP,      K_GROUP,   L"Свойство кнопки", 274, 44, 298, 314, A_WSTRETCH | A_HSTRETCH },
    { IDC_LBL_LABEL,     K_STATIC,  L"Подпись:",   286,  68,  80, 18 },
    { IDC_LABEL_EDIT,    K_EDIT,    L"",           372,  64, 188, 23, A_WSTRETCH },
    { IDC_LBL_TARGET,    K_STATIC,  L"Запускать:", 286,  98,  80, 18 },
    { IDC_TARGET_EDIT,   K_EDIT,    L"",           372,  94, 154, 23, A_WSTRETCH },
    { IDC_TARGET_BROWSE, K_BTN,     L"...",        528,  94,  32, 23, A_MOVEX },
    { IDC_LBL_ARGS,      K_STATIC,  L"Аргументы:", 286, 128,  80, 18 },
    { IDC_ARGS_EDIT,     K_EDIT,    L"",           372, 124, 188, 23, A_WSTRETCH },
    { IDC_LBL_ICON,      K_STATIC,  L"Иконка:",    286, 158,  80, 18 },
    { IDC_ICON_EDIT,     K_EDIT,    L"",           372, 154, 154, 23, A_WSTRETCH },
    { IDC_ICON_BROWSE,   K_BTN,     L"...",        528, 154,  32, 23, A_MOVEX },
    { IDC_LBL_ALIGN,     K_STATIC,  L"Край:",      286, 190,  44, 18 },
    { IDC_ALIGN_L,       K_RADIO1,  L"слева",      332, 189,  70, 20 },
    { IDC_ALIGN_C,       K_RADIO,   L"центр",      404, 189,  70, 20 },
    { IDC_ALIGN_R,       K_RADIO,   L"справа",     476, 189,  82, 20 },
    { IDC_CHK_ADMIN,     K_CHECKG,  L"От администратора (UAC)",    332, 220, 228, 20 },
    { IDC_CHK_CONSOLE,   K_CHECK,   L"Консоль (conhost)",          332, 246, 228, 20 },
    { IDC_CHK_SEP,       K_CHECK,   L"Разделитель (вместо кнопки)",332, 272, 228, 20 },
    // bottom — actions
    { IDOK,              K_DEFBTN,  L"OK",        322, 372,  78, 26, A_MOVEX | A_MOVEY },
    { IDCANCEL,          K_BTN,     L"Отмена",    408, 372,  78, 26, A_MOVEX | A_MOVEY },
    { IDC_APPLY,         K_BTN,     L"Применить", 494, 372,  78, 26, A_MOVEX | A_MOVEY },
};
constexpr int kEdClientW = 584, kEdClientH = 404;   // DIP — base design / minimum size
constexpr int kEdOpenW   = 1000;                    // DIP — initial client width (anchors
                                                    // stretch the fields to fill it; still
                                                    // shrinkable down to kEdClientW)

void EditorMakeFont(EditorState* st) {
    if (st->font) { DeleteObject(st->font); st->font = nullptr; }
    NONCLIENTMETRICSW ncm{ sizeof(ncm) };
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, st->dpi))
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    st->font = CreateFontIndirectW(&ncm.lfMessageFont);
}

BOOL CALLBACK EditorSetFontProc(HWND child, LPARAM lp) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
    return TRUE;
}
void EditorApplyFont(HWND hwnd, EditorState* st) {
    EnumChildWindows(hwnd, EditorSetFontProc, reinterpret_cast<LPARAM>(st->font));
}

void EditorLayout(HWND hwnd, EditorState* st) {
    // dW/dH: how far the client area has been stretched past the base design.
    RECT cr; GetClientRect(hwnd, &cr);
    int dW = cr.right  - MulDiv(kEdClientW, st->dpi, 96);
    int dH = cr.bottom - MulDiv(kEdClientH, st->dpi, 96);
    if (dW < 0) dW = 0;
    if (dH < 0) dH = 0;
    HDWP dwp = BeginDeferWindowPos(_countof(kEd));
    for (const EdCtl& c : kEd) {
        HWND ctl = GetDlgItem(hwnd, c.id);
        if (!ctl) continue;
        int x = MulDiv(c.x, st->dpi, 96), y = MulDiv(c.y, st->dpi, 96);
        int w = MulDiv(c.w, st->dpi, 96), h = MulDiv(c.h, st->dpi, 96);
        if (c.anchor & A_MOVEX)    x += dW;
        if (c.anchor & A_MOVEY)    y += dH;
        if (c.anchor & A_WSTRETCH) w += dW;
        if (c.anchor & A_HSTRETCH) h += dH;
        if (dwp) dwp = DeferWindowPos(dwp, ctl, nullptr, x, y, w, h,
                                      SWP_NOZORDER | SWP_NOACTIVATE);
        else MoveWindow(ctl, x, y, w, h, TRUE);
    }
    if (dwp) EndDeferWindowPos(dwp);
    // Groupbox borders span the stretched area — repaint to avoid ghost edges.
    InvalidateRect(hwnd, nullptr, TRUE);
}

void EditorCreateControls(HWND hwnd, EditorState* st) {
    for (const EdCtl& c : kEd) {
        const wchar_t* cls = L"STATIC";
        DWORD s = WS_CHILD | WS_VISIBLE;
        switch (c.kind) {
            case K_STATIC:  cls = L"STATIC";   s |= SS_LEFT; break;
            case K_GROUP:   cls = L"BUTTON";   s |= BS_GROUPBOX; break;
            case K_EDIT:    cls = L"EDIT";     s |= WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL; break;
            case K_EDITNUM: cls = L"EDIT";     s |= WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER; break;
            case K_COMBO:   cls = L"COMBOBOX"; s |= WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST; break;
            case K_LIST:    cls = L"LISTBOX";  s |= WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT; break;
            case K_BTN:     cls = L"BUTTON";   s |= WS_TABSTOP | BS_PUSHBUTTON; break;
            case K_DEFBTN:  cls = L"BUTTON";   s |= WS_TABSTOP | BS_DEFPUSHBUTTON; break;
            case K_RADIO1:  cls = L"BUTTON";   s |= WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON; break;
            case K_RADIO:   cls = L"BUTTON";   s |= BS_AUTORADIOBUTTON; break;
            case K_CHECK:   cls = L"BUTTON";   s |= WS_TABSTOP | BS_AUTOCHECKBOX; break;
            case K_CHECKG:  cls = L"BUTTON";   s |= WS_TABSTOP | WS_GROUP | BS_AUTOCHECKBOX; break;
        }
        CreateWindowExW(0, cls, c.text, s,
                        MulDiv(c.x, st->dpi, 96), MulDiv(c.y, st->dpi, 96),
                        MulDiv(c.w, st->dpi, 96), MulDiv(c.h, st->dpi, 96),
                        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(c.id)),
                        g_hInst, nullptr);
    }
    EditorApplyFont(hwnd, st);
}

// Set the localized caption on every labeled control (kEd holds layout only;
// symbol buttons +/−/▲/▼/… keep their glyphs). Called once after creation.
void EditorApplyTexts(HWND hwnd) {
    struct { int id; Str s; } map[] = {
        { IDC_LBL_MON, S_ED_MONITOR }, { IDC_LBL_H, S_ED_HEIGHT },
        { IDC_LBL_ICODIR, S_ED_ICONS }, { IDC_GRP_BTN, S_ED_GRP_BUTTONS },
        { IDC_BTN_ADD, S_ED_ADD }, { IDC_BTN_ADDSEP, S_ED_ADDSEP },
        { IDC_BTN_DEL, S_ED_DEL }, { IDC_GRP_PROP, S_ED_GRP_PROP },
        { IDC_LBL_LABEL, S_ED_LABEL }, { IDC_LBL_TARGET, S_ED_TARGET },
        { IDC_LBL_ARGS, S_ED_ARGS }, { IDC_LBL_ICON, S_ED_ICON },
        { IDC_LBL_ALIGN, S_ED_EDGE }, { IDC_ALIGN_L, S_ED_LEFT },
        { IDC_ALIGN_C, S_ED_CENTER }, { IDC_ALIGN_R, S_ED_RIGHT },
        { IDC_CHK_ADMIN, S_ED_ADMIN }, { IDC_CHK_CONSOLE, S_ED_CONSOLE },
        { IDC_CHK_SEP, S_ED_SEP }, { IDOK, S_COMMON_OK },
        { IDCANCEL, S_COMMON_CANCEL }, { IDC_APPLY, S_COMMON_APPLY },
    };
    for (auto& m : map)
        SetWindowTextW(GetDlgItem(hwnd, m.id), T(m.s));
}

// ---- model <-> registry ----
void EditorLoadModel(EditorState* st) {
    HKEY root = OpenRoot(KEY_READ);
    int h = static_cast<int>(RegReadDword(root, L"height", DEFAULT_HEIGHT_DIP));
    if (h < MIN_HEIGHT_DIP) h = DEFAULT_HEIGHT_DIP;
    if (h > MAX_HEIGHT_DIP) h = MAX_HEIGHT_DIP;
    st->heightDip = h;
    std::wstring dir = RegReadStr(root, L"icon_dir");
    st->iconDir = dir.empty() ? std::wstring(L"ico") : dir;
    if (root) RegCloseKey(root);

    // Show one tab per physical monitor, but never fewer than the highest
    // monitor_<k> present in the registry (so a 3-monitor config stays editable
    // on a laptop that currently has one screen). One enum pass over the root's
    // subkeys instead of probing monitor_1..16 individually.
    int n = static_cast<int>(g_panels.size());
    HKEY mroot = OpenRoot(KEY_READ);
    if (mroot) {
        for (DWORD i = 0; ; ++i) {
            wchar_t name[128];
            DWORD cch = _countof(name);
            if (RegEnumKeyExW(mroot, i, name, &cch, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            int k = 0;
            if (swscanf_s(name, L"monitor_%d", &k) == 1 && k > n) n = k;
        }
        RegCloseKey(mroot);
    }
    if (n < 1) n = 1;
    if (n > 16) n = 16;   // editor caps monitors at 16

    st->mons.assign(n, {});
    for (int i = 0; i < n; ++i) {
        st->mons[i] = LoadButtonsForMonitor(i + 1);
        EditorRegroup(st->mons[i]);   // establish the grouped invariant up front
    }
}

void EditorSaveModel(EditorState* st) {
    HKEY root = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubkey, 0, nullptr, 0,
                        KEY_READ | KEY_WRITE | DELETE, nullptr, &root, nullptr) != ERROR_SUCCESS)
        return;
    RegDeleteTree(root, nullptr);
    WriteGlobal(root, st->heightDip, st->iconDir);
    WriteLanguageValue(root);           // keep the language; RegDeleteTree wiped it
    for (size_t mon = 0; mon < st->mons.size(); ++mon) {
        int btnNo = 0;
        for (const ButtonCfg& b : st->mons[mon]) {
            if (b.target.empty() && !b.isSeparator) continue;   // drop blank rows
            if (b.isSeparator) {
                // Keep the entry's content (label/target/args/icon) so toggling
                // the separator back into a button restores it; only admin/console
                // are dropped — meaningless for a divider, and RunButton early-outs
                // on a separator anyway.
                ButtonCfg sepCfg = b;
                sepCfg.runAsAdmin = false;
                sepCfg.console    = false;
                WriteButton(root, static_cast<int>(mon + 1), ++btnNo, sepCfg);
            } else {
                WriteButton(root, static_cast<int>(mon + 1), ++btnNo, b);
            }
        }
    }
    RegCloseKey(root);
}

// ---- fields <-> model ----
bool EditorHasSel(EditorState* st) {
    return st->curMon >= 0 && st->curMon < static_cast<int>(st->mons.size()) &&
           st->curBtn >= 0 && st->curBtn < static_cast<int>(st->mons[st->curMon].size());
}

void EditorFlushFields(HWND hwnd, EditorState* st) {
    if (!EditorHasSel(st)) return;
    ButtonCfg& b = st->mons[st->curMon][st->curBtn];
    // A separator shows disabled placeholder fields and an inherited edge — there
    // is nothing to read back. Its isSeparator flag is toggled in the CHK_SEP
    // handler, and its edge is set by ▲/▼ moves, not here.
    if (b.isSeparator) return;
    b.label      = EdGetText(hwnd, IDC_LABEL_EDIT);
    b.target     = EdGetText(hwnd, IDC_TARGET_EDIT);
    b.args       = EdGetText(hwnd, IDC_ARGS_EDIT);
    b.iconPath   = EdGetText(hwnd, IDC_ICON_EDIT);
    b.runAsAdmin = EdChecked(hwnd, IDC_CHK_ADMIN);
    b.console    = EdChecked(hwnd, IDC_CHK_CONSOLE);
    b.align = EdChecked(hwnd, IDC_ALIGN_R) ? BtnAlign::Right
            : EdChecked(hwnd, IDC_ALIGN_C) ? BtnAlign::Center : BtnAlign::Left;
}

void EditorLoadFields(HWND hwnd, EditorState* st) {
    st->loading = true;
    bool has = EditorHasSel(st);
    bool sep = has && st->mons[st->curMon][st->curBtn].isSeparator;
    if (!has) {
        EdSetText(hwnd, IDC_LABEL_EDIT,  L"");
        EdSetText(hwnd, IDC_TARGET_EDIT, L"");
        EdSetText(hwnd, IDC_ARGS_EDIT,   L"");
        EdSetText(hwnd, IDC_ICON_EDIT,   L"");
        EdCheck(hwnd, IDC_CHK_ADMIN,   false);
        EdCheck(hwnd, IDC_CHK_CONSOLE, false);
        EdCheck(hwnd, IDC_CHK_SEP,     false);
        EdCheck(hwnd, IDC_ALIGN_L, true);
        EdCheck(hwnd, IDC_ALIGN_C, false);
        EdCheck(hwnd, IDC_ALIGN_R, false);
    } else if (sep) {
        // A separator: the per-button fields don't apply — show a hint in the
        // (disabled) label and blank the rest, but leave the model untouched.
        EdSetText(hwnd, IDC_LABEL_EDIT,  T(S_ED_FIELD_SEP));
        EdSetText(hwnd, IDC_TARGET_EDIT, L"");
        EdSetText(hwnd, IDC_ARGS_EDIT,   L"");
        EdSetText(hwnd, IDC_ICON_EDIT,   L"");
        EdCheck(hwnd, IDC_CHK_ADMIN,   false);
        EdCheck(hwnd, IDC_CHK_CONSOLE, false);
        EdCheck(hwnd, IDC_CHK_SEP,     true);
        const ButtonCfg& b = st->mons[st->curMon][st->curBtn];
        EdCheck(hwnd, IDC_ALIGN_L, b.align == BtnAlign::Left);
        EdCheck(hwnd, IDC_ALIGN_C, b.align == BtnAlign::Center);
        EdCheck(hwnd, IDC_ALIGN_R, b.align == BtnAlign::Right);
    } else {
        const ButtonCfg& b = st->mons[st->curMon][st->curBtn];
        EdSetText(hwnd, IDC_LABEL_EDIT,  b.label);
        EdSetText(hwnd, IDC_TARGET_EDIT, b.target);
        EdSetText(hwnd, IDC_ARGS_EDIT,   b.args);
        EdSetText(hwnd, IDC_ICON_EDIT,   b.iconPath);
        EdCheck(hwnd, IDC_CHK_ADMIN,   b.runAsAdmin);
        EdCheck(hwnd, IDC_CHK_CONSOLE, b.console);
        EdCheck(hwnd, IDC_CHK_SEP,     false);
        EdCheck(hwnd, IDC_ALIGN_L, b.align == BtnAlign::Left);
        EdCheck(hwnd, IDC_ALIGN_C, b.align == BtnAlign::Center);
        EdCheck(hwnd, IDC_ALIGN_R, b.align == BtnAlign::Right);
    }
    // label/target/args/icon/admin/console apply only to real buttons; a
    // separator only carries an alignment block + the separator toggle itself.
    EdEnable(hwnd, IDC_LABEL_EDIT,    has && !sep);
    EdEnable(hwnd, IDC_TARGET_EDIT,   has && !sep);
    EdEnable(hwnd, IDC_TARGET_BROWSE, has && !sep);
    EdEnable(hwnd, IDC_ARGS_EDIT,     has && !sep);
    EdEnable(hwnd, IDC_ICON_EDIT,     has && !sep);
    EdEnable(hwnd, IDC_ICON_BROWSE,   has && !sep);
    EdEnable(hwnd, IDC_CHK_ADMIN,     has && !sep);
    EdEnable(hwnd, IDC_CHK_CONSOLE,   has && !sep);
    // A separator's edge is determined by which group it sits in (move it with
    // ▲/▼), so its edge radios are read-only — they just show the inherited group.
    EdEnable(hwnd, IDC_ALIGN_L, has && !sep);
    EdEnable(hwnd, IDC_ALIGN_C, has && !sep);
    EdEnable(hwnd, IDC_ALIGN_R, has && !sep);
    EdEnable(hwnd, IDC_CHK_SEP,  has);
    EdEnable(hwnd, IDC_BTN_DEL,  has);
    EdEnable(hwnd, IDC_BTN_UP,   has && st->curBtn > 0);
    EdEnable(hwnd, IDC_BTN_DOWN, has &&
             st->curBtn < static_cast<int>(st->mons[st->curMon].size()) - 1);
    st->loading = false;
}

// Returns false (and explains) when the height field is out of range.
bool EditorFlushGlobal(HWND hwnd, EditorState* st) {
    int h = _wtoi(TrimW(EdGetText(hwnd, IDC_HEIGHT_EDIT)).c_str());
    if (h < MIN_HEIGHT_DIP || h > MAX_HEIGHT_DIP) {
        InfoDialog(hwnd, T(S_DLG_HEIGHT_TITLE), T(S_DLG_HEIGHT_BODY), TD_WARNING_ICON);
        return false;
    }
    st->heightDip = h;
    std::wstring dir = TrimW(EdGetText(hwnd, IDC_ICONDIR_EDIT));
    st->iconDir = dir.empty() ? std::wstring(L"ico") : dir;
    return true;
}

// ---- list view of the current monitor's buttons ----
std::wstring EditorRowText(const ButtonCfg& b) {
    if (b.isSeparator)   return L"——————————";
    if (b.label.empty()) return T(S_ED_ROW_UNNAMED);
    return b.label;
}

const wchar_t* EditorGroupName(BtnAlign a) {
    return a == BtnAlign::Left   ? T(S_ED_GROUP_LEFT)
         : a == BtnAlign::Center ? T(S_ED_GROUP_CENTER)
                                 : T(S_ED_GROUP_RIGHT);
}

// The list shows three fixed section headers (слева / центр / справа); the
// buttons of each group are listed (indented) under their header. rowMap maps
// every listbox row back to a model index, or -1 for a header row.
void EditorRefreshList(HWND hwnd, EditorState* st) {
    HWND lb = GetDlgItem(hwnd, IDC_BTN_LIST);
    SendMessageW(lb, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    st->rowMap.clear();
    if (st->curMon >= 0 && st->curMon < static_cast<int>(st->mons.size())) {
        const auto& vec = st->mons[st->curMon];
        for (BtnAlign g : { BtnAlign::Left, BtnAlign::Center, BtnAlign::Right }) {
            SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(EditorGroupName(g)));
            st->rowMap.push_back(-1);
            for (int i = 0; i < static_cast<int>(vec.size()); ++i) {
                if (vec[i].align != g) continue;
                std::wstring text = L"    " + EditorRowText(vec[i]);
                SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
                st->rowMap.push_back(i);
            }
        }
    }
    if (st->curBtn >= 0)
        for (size_t r = 0; r < st->rowMap.size(); ++r)
            if (st->rowMap[r] == st->curBtn) {
                SendMessageW(lb, LB_SETCURSEL, r, 0);
                break;
            }
    SendMessageW(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, nullptr, TRUE);
}

int EditorRowForModel(EditorState* st, int modelIdx) {
    for (size_t r = 0; r < st->rowMap.size(); ++r)
        if (st->rowMap[r] == modelIdx) return static_cast<int>(r);
    return -1;
}

// Resolve the current listbox selection to a model index, snapping a header row
// to the nearest button below it (else above). -1 if there are no buttons.
int EditorSelToModel(HWND hwnd, EditorState* st) {
    HWND lb = GetDlgItem(hwnd, IDC_BTN_LIST);
    int r = static_cast<int>(SendMessageW(lb, LB_GETCURSEL, 0, 0));
    int n = static_cast<int>(st->rowMap.size());
    if (r < 0 || r >= n) return -1;
    if (st->rowMap[r] >= 0) return st->rowMap[r];
    for (int k = r + 1; k < n; ++k) if (st->rowMap[k] >= 0) return st->rowMap[k];
    for (int k = r - 1; k >= 0; --k) if (st->rowMap[k] >= 0) return st->rowMap[k];
    return -1;
}

// In-place refresh of a single button's row text (label typing) — align (and
// thus the row's group/position) is unchanged here, so the mapping stays valid.
void EditorUpdateRow(HWND hwnd, EditorState* st, int idx) {
    if (st->curMon < 0 || st->curMon >= static_cast<int>(st->mons.size())) return;
    if (idx < 0 || idx >= static_cast<int>(st->mons[st->curMon].size())) return;
    int r = EditorRowForModel(st, idx);
    if (r < 0) return;
    HWND lb = GetDlgItem(hwnd, IDC_BTN_LIST);
    SendMessageW(lb, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lb, LB_DELETESTRING, r, 0);
    std::wstring text = L"    " + EditorRowText(st->mons[st->curMon][idx]);
    SendMessageW(lb, LB_INSERTSTRING, r, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(lb, LB_SETCURSEL, r, 0);
    SendMessageW(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, nullptr, TRUE);
}

void EditorFillMonCombo(HWND hwnd, EditorState* st) {
    HWND cb = GetDlgItem(hwnd, IDC_MON_COMBO);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < st->mons.size(); ++i) {
        wchar_t buf[48];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, T(S_ED_MON_FMT), static_cast<int>(i) + 1);
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
    }
    SendMessageW(cb, CB_SETCURSEL, st->curMon, 0);
}

// ---- file pickers for target / icon ----
void EditorBrowseTarget(HWND hwnd) {
    wchar_t file[MAX_PATH * 2] = L"";
    // OPENFILENAME filters are '\0'-delimited (desc, pattern, …) — build the
    // buffer with localized descriptions and fixed patterns.
    std::wstring filter;
    filter += T(S_FILTER_PROGRAMS); filter.push_back(L'\0');
    filter += L"*.exe;*.lnk;*.bat;*.cmd"; filter.push_back(L'\0');
    filter += T(S_FILTER_ALL); filter.push_back(L'\0');
    filter += L"*.*"; filter.push_back(L'\0');
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = _countof(file);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn))
        EdSetText(hwnd, IDC_TARGET_EDIT, file);   // EN_CHANGE → flush + dirty
}

void EditorBrowseIcon(HWND hwnd) {
    wchar_t file[MAX_PATH * 2] = L"";
    std::wstring filter;
    filter += T(S_FILTER_IMAGES); filter.push_back(L'\0');
    filter += L"*.png;*.ico;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff"; filter.push_back(L'\0');
    filter += T(S_FILTER_ALL); filter.push_back(L'\0');
    filter += L"*.*"; filter.push_back(L'\0');
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = _countof(file);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    // If the picked file lives directly in the resolved icon dir, store just its
    // bare name (icon_dir resolution will find it) — otherwise the full path.
    std::wstring full = file, out = full;
    const std::wstring& dir = g_global.iconDir;   // absolute, ends with '\\'
    if (!dir.empty() && full.size() > dir.size() &&
        _wcsnicmp(full.c_str(), dir.c_str(), dir.size()) == 0 &&
        full.find(L'\\', dir.size()) == std::wstring::npos)
        out = full.substr(dir.size());
    EdSetText(hwnd, IDC_ICON_EDIT, out);
}

// Folder picker for icon_dir. Uses the modern IFileOpenDialog (FOS_PICKFOLDERS)
// to match the rest of the UI; COM is already initialised on this (main) thread.
// If the chosen folder is exactly <exeDir>\ico, store the relative "ico" so the
// portable default is preserved — otherwise store the absolute path.
void EditorBrowseIconDir(HWND hwnd) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))) || !dlg)
        return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(T(S_ED_PICKDIR));
    if (SUCCEEDED(dlg->Show(hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                std::wstring full = path, ico = GetExeDir();
                if (!ico.empty() && ico.back() != L'\\') ico += L'\\';
                ico += L"ico";
                std::wstring out = (_wcsicmp(full.c_str(), ico.c_str()) == 0)
                                       ? std::wstring(L"ico") : full;
                EdSetText(hwnd, IDC_ICONDIR_EDIT, out.c_str());  // EN_CHANGE → flush + dirty
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
}

// ---- explanatory tooltips on the less-obvious controls ----
void EditorAddTip(HWND tip, HWND hwnd, int id, const wchar_t* text) {
    TOOLINFOW ti{ sizeof(ti) };
    ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd     = hwnd;
    ti.uId      = reinterpret_cast<UINT_PTR>(GetDlgItem(hwnd, id));
    ti.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

void EditorCreateTooltips(HWND hwnd, EditorState* st) {
    if (st && st->tip) { DestroyWindow(st->tip); st->tip = nullptr; }  // re-create on relang
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd, nullptr, g_hInst, nullptr);          // child of the editor → auto-destroyed
    if (!tip) return;
    if (st) st->tip = tip;
    SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, 340);  // allow wrapped multi-line tips
    SendMessageW(tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);

    const wchar_t* edgeTip = T(S_TIP_EDGE);
    EditorAddTip(tip, hwnd, IDC_LBL_ALIGN, edgeTip);
    EditorAddTip(tip, hwnd, IDC_ALIGN_L,   edgeTip);
    EditorAddTip(tip, hwnd, IDC_ALIGN_C,   edgeTip);
    EditorAddTip(tip, hwnd, IDC_ALIGN_R,   edgeTip);
    EditorAddTip(tip, hwnd, IDC_BTN_ADDSEP,  T(S_TIP_ADDSEP));
    EditorAddTip(tip, hwnd, IDC_CHK_SEP,     T(S_TIP_SEP));
    EditorAddTip(tip, hwnd, IDC_CHK_CONSOLE, T(S_TIP_CONSOLE));
    EditorAddTip(tip, hwnd, IDC_CHK_ADMIN,   T(S_TIP_ADMIN));
    EditorAddTip(tip, hwnd, IDC_MON_ADD,     T(S_TIP_MONADD));
    EditorAddTip(tip, hwnd, IDC_MON_DEL,     T(S_TIP_MONDEL));
    const wchar_t* dirTip = T(S_TIP_ICONDIR);
    EditorAddTip(tip, hwnd, IDC_ICONDIR_EDIT,   dirTip);
    EditorAddTip(tip, hwnd, IDC_ICONDIR_BROWSE, dirTip);
}

// ---- grouping / reordering ----
// Move the selected entry into alignment group g (used by the edge radios for a
// real button): drop it, then re-insert at the end of that group so the grouped
// invariant holds. Updates curBtn to the new position.
void EditorSetAlign(EditorState* st, BtnAlign g) {
    if (!EditorHasSel(st)) return;
    auto& vec = st->mons[st->curMon];
    ButtonCfg moved = vec[st->curBtn];
    moved.align = g;
    vec.erase(vec.begin() + st->curBtn);
    int insertAt = 0;
    for (int i = 0; i < static_cast<int>(vec.size()); ++i)
        if (EdAlignRank(vec[i].align) <= EdAlignRank(g)) insertAt = i + 1;
    vec.insert(vec.begin() + insertAt, std::move(moved));
    st->curBtn = insertAt;
}

// ▲/▼: swap with the neighbour inside the same group; at a group boundary, shift
// the entry's edge by one rank so it crosses into the adjacent group (this is how
// a separator — or a button — changes group by moving). Keeps the invariant.
void EditorMoveSelection(EditorState* st, int dir) {   // dir: -1 up, +1 down
    if (!EditorHasSel(st)) return;
    auto& vec = st->mons[st->curMon];
    int i = st->curBtn, n = static_cast<int>(vec.size());
    if (dir < 0) {
        bool firstInGroup = (i == 0) || (vec[i - 1].align != vec[i].align);
        if (!firstInGroup)                              { std::swap(vec[i], vec[i - 1]); st->curBtn = i - 1; }
        else if (vec[i].align == BtnAlign::Right)       vec[i].align = BtnAlign::Center;
        else if (vec[i].align == BtnAlign::Center)      vec[i].align = BtnAlign::Left;
    } else {
        bool lastInGroup = (i == n - 1) || (vec[i + 1].align != vec[i].align);
        if (!lastInGroup)                               { std::swap(vec[i], vec[i + 1]); st->curBtn = i + 1; }
        else if (vec[i].align == BtnAlign::Left)        vec[i].align = BtnAlign::Center;
        else if (vec[i].align == BtnAlign::Center)      vec[i].align = BtnAlign::Right;
    }
}

// Refresh combo + list + fields after the monitor set or current monitor changed.
void EditorRebuildMonState(HWND hwnd, EditorState* st) {
    EditorFillMonCombo(hwnd, st);
    st->curBtn = (st->curMon >= 0 && st->curMon < static_cast<int>(st->mons.size()) &&
                  !st->mons[st->curMon].empty()) ? 0 : -1;
    EditorRefreshList(hwnd, st);
    EditorLoadFields(hwnd, st);
}

// Right-click on the buttons list → copy / paste one button. Copy stashes the
// selected entry into st->clip; paste drops a clone into the current monitor. So
// a button can be duplicated (copy then paste on the same monitor) or carried to
// another monitor (copy, switch the combo, paste). The clone keeps its own edge
// group and lands at the end of it, just like a freshly added button.
void EditorListContextMenu(HWND hwnd, EditorState* st, int sx, int sy) {
    HWND lb = GetDlgItem(hwnd, IDC_BTN_LIST);

    if (sx == -1 && sy == -1) {
        // Keyboard invocation (Shift+F10 / menu key) — anchor under the current row.
        int r = static_cast<int>(SendMessageW(lb, LB_GETCURSEL, 0, 0));
        RECT ri{};
        POINT p{ 0, 0 };
        if (r >= 0 && SendMessageW(lb, LB_GETITEMRECT, r, reinterpret_cast<LPARAM>(&ri)) != LB_ERR)
            p = POINT{ ri.left, ri.bottom };
        ClientToScreen(lb, &p);
        sx = p.x; sy = p.y;
    } else {
        // A list-box doesn't select on right-click — do it ourselves so the menu
        // acts on the row under the cursor (header rows snap to a nearby button).
        POINT cp{ sx, sy };
        ScreenToClient(lb, &cp);
        DWORD hit = static_cast<DWORD>(
            SendMessageW(lb, LB_ITEMFROMPOINT, 0, MAKELPARAM(cp.x, cp.y)));
        if (HIWORD(hit) == 0) {                     // 0 = a real item sits under the point
            EditorFlushFields(hwnd, st);
            SendMessageW(lb, LB_SETCURSEL, LOWORD(hit), 0);
            st->curBtn = EditorSelToModel(hwnd, st);
            int rr = EditorRowForModel(st, st->curBtn);
            if (rr >= 0) SendMessageW(lb, LB_SETCURSEL, rr, 0);
            EditorLoadFields(hwnd, st);
        }
    }

    constexpr UINT CTX_COPY = 1, CTX_PASTE = 2;
    bool canCopy = EditorHasSel(st);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (canCopy     ? 0u : MF_GRAYED), CTX_COPY,  T(S_ED_CTX_COPY));
    AppendMenuW(m, MF_STRING | (st->hasClip ? 0u : MF_GRAYED), CTX_PASTE, T(S_ED_CTX_PASTE));

    SetForegroundWindow(hwnd);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              sx, sy, 0, hwnd, nullptr);
    DestroyMenu(m);

    if (cmd == CTX_COPY && canCopy) {
        EditorFlushFields(hwnd, st);                // capture in-flight edits first
        st->clip    = st->mons[st->curMon][st->curBtn];
        st->hasClip = true;
    } else if (cmd == CTX_PASTE && st->hasClip) {
        EditorFlushFields(hwnd, st);
        auto& vec = st->mons[st->curMon];
        vec.push_back(st->clip);                    // clone keeps its own edge group
        EditorRegroup(vec);
        st->curBtn = -1;                            // select the freshly pasted clone
        for (int i = 0; i < static_cast<int>(vec.size()); ++i)
            if (vec[i].align == st->clip.align) st->curBtn = i;
        st->dirty = true;
        EditorRefreshList(hwnd, st);
        EditorLoadFields(hwnd, st);
    }
}

LRESULT CALLBACK EditorProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    EditorState* st = EdState(hwnd);
    switch (msg) {
        case WM_CREATE: {
            st = new EditorState();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            st->dpi = GetDpiForWindow(hwnd);
            st->loading = true;                 // suppress EN_CHANGE while we fill fields
            EditorMakeFont(st);
            EditorCreateControls(hwnd, st);
            EditorApplyTexts(hwnd);
            EditorLoadModel(st);
            st->curMon = 0;
            st->curBtn = st->mons.empty() || st->mons[0].empty() ? -1 : 0;
            EditorFillMonCombo(hwnd, st);
            EdSetText(hwnd, IDC_HEIGHT_EDIT,  std::to_wstring(st->heightDip));
            EdSetText(hwnd, IDC_ICONDIR_EDIT, st->iconDir);
            EditorRefreshList(hwnd, st);
            EditorLoadFields(hwnd, st);          // its tail resets st->loading = false
            EditorCreateTooltips(hwnd, st);
            st->dirty = false;                   // a freshly opened editor is clean
            return 0;
        }

        case WM_MMP_RELANG:
            // Language changed while open — relabel everything from the model in
            // memory (no reload, so unsaved edits survive). dirty stays as it was.
            if (st) {
                EditorFlushFields(hwnd, st);     // capture the field being edited
                EditorApplyTexts(hwnd);          // control captions
                std::wstring title = L"MultiMonitorPanel v" + std::wstring(kVersion) +
                                     L" — " + T(S_ED_TITLE_SUFFIX);
                SetWindowTextW(hwnd, title.c_str());
                EditorFillMonCombo(hwnd, st);    // "Монитор %d" / "Monitor %d"
                EditorRefreshList(hwnd, st);     // section headers + row placeholders
                EditorLoadFields(hwnd, st);      // separator placeholder text
                EditorCreateTooltips(hwnd, st);  // tips in the new language (recreated)
            }
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(w), code = HIWORD(w);
            switch (id) {
                case IDC_MON_COMBO:
                    if (code == CBN_SELCHANGE) {
                        EditorFlushFields(hwnd, st);
                        st->curMon = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_MON_COMBO), CB_GETCURSEL, 0, 0));
                        st->curBtn = st->mons[st->curMon].empty() ? -1 : 0;
                        EditorRefreshList(hwnd, st);
                        EditorLoadFields(hwnd, st);
                    }
                    break;
                case IDC_BTN_LIST:
                    if (code == LBN_SELCHANGE) {
                        EditorFlushFields(hwnd, st);
                        st->curBtn = EditorSelToModel(hwnd, st);   // skips header rows
                        int r = EditorRowForModel(st, st->curBtn);
                        if (r >= 0) SendMessageW(GetDlgItem(hwnd, IDC_BTN_LIST), LB_SETCURSEL, r, 0);
                        EditorLoadFields(hwnd, st);
                    }
                    break;
                case IDC_LABEL_EDIT:
                    if (code == EN_CHANGE && !st->loading) {
                        EditorFlushFields(hwnd, st);
                        st->dirty = true;
                        EditorUpdateRow(hwnd, st, st->curBtn);
                    }
                    break;
                case IDC_TARGET_EDIT:
                case IDC_ARGS_EDIT:
                case IDC_ICON_EDIT:
                case IDC_HEIGHT_EDIT:
                case IDC_ICONDIR_EDIT:
                    if (code == EN_CHANGE && !st->loading) st->dirty = true;
                    if (code == EN_CHANGE && !st->loading &&
                        (id == IDC_TARGET_EDIT || id == IDC_ARGS_EDIT || id == IDC_ICON_EDIT))
                        EditorFlushFields(hwnd, st);
                    break;
                case IDC_ALIGN_L: case IDC_ALIGN_C: case IDC_ALIGN_R:
                    // Changing a button's edge moves it into that group; regroup + reselect.
                    if (code == BN_CLICKED && !st->loading && EditorHasSel(st)) {
                        EditorFlushFields(hwnd, st);
                        EditorSetAlign(st, id == IDC_ALIGN_R ? BtnAlign::Right
                                         : id == IDC_ALIGN_C ? BtnAlign::Center : BtnAlign::Left);
                        st->dirty = true;
                        EditorRefreshList(hwnd, st);
                        EditorLoadFields(hwnd, st);
                    }
                    break;
                case IDC_CHK_ADMIN: case IDC_CHK_CONSOLE:
                    if (code == BN_CLICKED && !st->loading) {
                        EditorFlushFields(hwnd, st);
                        st->dirty = true;
                    }
                    break;
                case IDC_CHK_SEP:
                    // Toggle separator-ness directly (don't flush the placeholder fields).
                    // The model's label/target are preserved so it can be toggled back.
                    if (code == BN_CLICKED && !st->loading && EditorHasSel(st)) {
                        st->mons[st->curMon][st->curBtn].isSeparator = EdChecked(hwnd, IDC_CHK_SEP);
                        st->dirty = true;
                        EditorLoadFields(hwnd, st);             // repaint fields for the new mode
                        EditorUpdateRow(hwnd, st, st->curBtn);
                    }
                    break;
                case IDC_BTN_ADD:
                case IDC_BTN_ADDSEP:
                    if (st->curMon >= 0 && st->curMon < static_cast<int>(st->mons.size())) {
                        EditorFlushFields(hwnd, st);
                        auto& vec = st->mons[st->curMon];
                        ButtonCfg nb;
                        nb.isSeparator = (id == IDC_BTN_ADDSEP);
                        nb.align = EditorHasSel(st) ? vec[st->curBtn].align : BtnAlign::Left;
                        vec.push_back(nb);
                        EditorRegroup(vec);                     // settle into its group
                        st->curBtn = -1;                        // new entry = last of its group
                        for (int i = 0; i < static_cast<int>(vec.size()); ++i)
                            if (vec[i].align == nb.align) st->curBtn = i;
                        st->dirty = true;
                        EditorRefreshList(hwnd, st);
                        EditorLoadFields(hwnd, st);
                        if (id == IDC_BTN_ADD) SetFocus(GetDlgItem(hwnd, IDC_LABEL_EDIT));
                    }
                    break;
                case IDC_BTN_DEL:
                    if (EditorHasSel(st)) {
                        auto& vec = st->mons[st->curMon];
                        vec.erase(vec.begin() + st->curBtn);
                        if (st->curBtn >= static_cast<int>(vec.size())) st->curBtn = static_cast<int>(vec.size()) - 1;
                        st->dirty = true;
                        EditorRefreshList(hwnd, st);
                        EditorLoadFields(hwnd, st);
                    }
                    break;
                case IDC_BTN_UP:
                case IDC_BTN_DOWN:
                    if (EditorHasSel(st)) {
                        EditorFlushFields(hwnd, st);
                        EditorMoveSelection(st, id == IDC_BTN_UP ? -1 : 1);
                        st->dirty = true;
                        EditorRefreshList(hwnd, st);
                        EditorLoadFields(hwnd, st);
                    }
                    break;
                case IDC_MON_ADD:
                    if (st->mons.size() < 16) {
                        EditorFlushFields(hwnd, st);
                        st->mons.push_back({});
                        st->curMon = static_cast<int>(st->mons.size()) - 1;
                        st->dirty = true;
                        EditorRebuildMonState(hwnd, st);
                    }
                    break;
                case IDC_MON_DEL:
                    if (st->mons.size() > 1) {
                        EditorFlushFields(hwnd, st);
                        bool nonEmpty = !st->mons.back().empty();
                        if (!nonEmpty || ConfirmDialog(hwnd, T(S_DLG_RMMON_TITLE),
                                T(S_DLG_RMMON_BODY), T(S_DLG_RMMON_OK), TD_WARNING_ICON)) {
                            st->mons.pop_back();
                            if (st->curMon >= static_cast<int>(st->mons.size()))
                                st->curMon = static_cast<int>(st->mons.size()) - 1;
                            st->dirty = true;
                            EditorRebuildMonState(hwnd, st);
                        }
                    }
                    break;
                case IDC_TARGET_BROWSE:  EditorBrowseTarget(hwnd);  break;
                case IDC_ICON_BROWSE:    EditorBrowseIcon(hwnd);    break;
                case IDC_ICONDIR_BROWSE: EditorBrowseIconDir(hwnd); break;

                case IDOK:
                    EditorFlushFields(hwnd, st);
                    if (!EditorFlushGlobal(hwnd, st)) break;
                    EditorSaveModel(st);
                    PostMessageW(g_hCtrl, WM_MMP_RELOAD, 0, 0);
                    DestroyWindow(hwnd);
                    break;
                case IDC_APPLY:
                    EditorFlushFields(hwnd, st);
                    if (!EditorFlushGlobal(hwnd, st)) break;
                    EditorSaveModel(st);
                    PostMessageW(g_hCtrl, WM_MMP_RELOAD, 0, 0);
                    st->dirty = false;
                    break;
                case IDCANCEL:
                    if (!st->dirty ||
                        ConfirmDialog(hwnd, T(S_DLG_CLOSE_TITLE), T(S_DLG_CLOSE_BODY),
                                      T(S_DLG_CLOSE_OK), TD_WARNING_ICON))
                        DestroyWindow(hwnd);
                    break;
            }
            return 0;
        }

        case WM_CONTEXTMENU:
            // Right-click on the buttons list → copy / paste. lParam carries signed
            // screen coords (or -1,-1 from the keyboard), so cast through short.
            if (st && reinterpret_cast<HWND>(w) == GetDlgItem(hwnd, IDC_BTN_LIST)) {
                EditorListContextMenu(hwnd, st,
                    static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l)));
                return 0;
            }
            return DefWindowProcW(hwnd, msg, w, l);

        case WM_CTLCOLORSTATIC:
            // Labels / groupboxes / checkboxes paint cleanly on the dialog face.
            SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));

        case WM_SIZE:
            if (st) EditorLayout(hwnd, st);   // re-anchor controls to the new size
            return 0;

        case WM_GETMINMAXINFO: {
            // Don't let the window shrink below the base design size.
            UINT dpi = st ? st->dpi : GetDpiForWindow(hwnd);
            RECT rc{ 0, 0, MulDiv(kEdClientW, dpi, 96), MulDiv(kEdClientH, dpi, 96) };
            DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
            DWORD ex    = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            AdjustWindowRectExForDpi(&rc, style, FALSE, ex, dpi);
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(l);
            mmi->ptMinTrackSize.x = rc.right - rc.left;
            mmi->ptMinTrackSize.y = rc.bottom - rc.top;
            return 0;
        }

        case WM_DPICHANGED:
            if (st) {
                st->dpi = HIWORD(w);
                EditorMakeFont(st);
                EditorApplyFont(hwnd, st);
                EditorLayout(hwnd, st);
                const RECT* pr = reinterpret_cast<const RECT*>(l);
                SetWindowPos(hwnd, nullptr, pr->left, pr->top,
                             pr->right - pr->left, pr->bottom - pr->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;

        case WM_CLOSE:
            SendMessageW(hwnd, WM_COMMAND, IDCANCEL, 0);
            return 0;

        case WM_DESTROY:
            if (st) {
                if (st->font) DeleteObject(st->font);
                delete st;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            g_hEditor = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

void RegisterEditorClass() {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = EditorProc;
    wc.hInstance     = g_hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kEditorClass;
    HICON appIcon    = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    wc.hIcon         = appIcon ? appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm       = wc.hIcon;
    RegisterClassExW(&wc);
}

void OpenConfigEditor(HWND owner) {
    (void)owner;
    if (g_hEditor) {                       // single instance — bring the open one forward
        ShowWindow(g_hEditor, SW_RESTORE);
        SetForegroundWindow(g_hEditor);
        return;
    }
    POINT pt; GetCursorPos(&pt);
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(hm, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(hm, &mi);

    DWORD style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                    WS_MAXIMIZEBOX | WS_THICKFRAME;   // resizable
    DWORD exStyle = WS_EX_CONTROLPARENT;
    RECT rc{ 0, 0, MulDiv(kEdOpenW, dpiX, 96), MulDiv(kEdClientH, dpiX, 96) };
    AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, dpiX);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int wx = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww) / 2;
    int wy = mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top) - wh) / 2;

    std::wstring title = L"MultiMonitorPanel v" + std::wstring(kVersion) + L" — " + T(S_ED_TITLE_SUFFIX);
    g_hEditor = CreateWindowExW(exStyle, kEditorClass, title.c_str(),
                                style, wx, wy, ww, wh, nullptr, nullptr, g_hInst, nullptr);
    if (!g_hEditor) return;
    ShowWindow(g_hEditor, SW_SHOW);
    SetForegroundWindow(g_hEditor);
}

void ShowContextMenu(HWND hwnd) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING,             MENU_GUIEDIT,    T(S_MENU_EDIT));
    AppendMenuW(m, MF_STRING,             MENU_RELOAD,     T(S_MENU_RELOAD));
    AppendMenuW(m, MF_STRING,             MENU_SETDEFAULT, T(S_MENU_SETDEFAULT));
    AppendMenuW(m, MF_STRING,             MENU_ERASE,      T(S_MENU_ERASE));
    AppendMenuW(m, MF_SEPARATOR,          0,               nullptr);
    AppendMenuW(m, MF_STRING,             MENU_FOLDER, T(S_MENU_FOLDER));

    // Language submenu — built-in ru/en plus any extra lang\*.lang. A radio
    // mark shows the active language; items map to MENU_LANG_BASE + index.
    std::vector<std::pair<std::wstring, std::wstring>> langs = ListLanguages();
    HMENU langMenu = CreatePopupMenu();
    for (size_t i = 0; i < langs.size(); ++i) {
        UINT flags = MF_STRING | (langs[i].first == g_lang ? MF_CHECKED : 0);
        AppendMenuW(langMenu, flags, MENU_LANG_BASE + static_cast<int>(i),
                    langs[i].second.c_str());
    }
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(langMenu), T(S_MENU_LANGUAGE));

    AppendMenuW(m, MF_SEPARATOR,          0,           nullptr);
    AppendMenuW(m, MF_STRING,             MENU_EXIT,   T(S_MENU_EXIT));
    AppendMenuW(m, MF_SEPARATOR,          0,           nullptr);
    std::wstring aboutLabel = std::wstring(T(S_MENU_ABOUT)) + L" (v" + kVersion + L")";
    AppendMenuW(m, MF_STRING,             MENU_ABOUT,  aboutLabel.c_str());

    // Per-item icons. Bitmaps must stay alive until after TrackPopupMenu.
    int px = MulDiv(16, GetDpiForWindow(hwnd), 96);
    if (px < 16) px = 16;
    std::vector<HBITMAP> keep;
    SetMenuIcon(m, MENU_GUIEDIT,    StockIcon(SIID_RENAME),      px, keep);
    SetMenuIcon(m, MENU_RELOAD,     ReloadMenuIcon(px),          px, keep);
    SetMenuIcon(m, MENU_SETDEFAULT, StockIcon(SIID_APPLICATION), px, keep);
    SetMenuIcon(m, MENU_ERASE,      StockIcon(SIID_RECYCLER),    px, keep);
    SetMenuIcon(m, MENU_FOLDER,     StockIcon(SIID_FOLDER),      px, keep);
    SetMenuIcon(m, MENU_EXIT,       StockIcon(SIID_DELETE),      px, keep);
    SetMenuIcon(m, MENU_ABOUT,      StockIcon(SIID_INFO),        px, keep);

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);

    UINT cmd = TrackPopupMenu(m,
                              TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(m);   // also destroys the attached langMenu submenu
    for (HBITMAP hb : keep) if (hb) DeleteObject(hb);

    // Language picked: persist + reload strings + rebuild panels (and editor).
    if (static_cast<int>(cmd) >= MENU_LANG_BASE &&
        static_cast<int>(cmd) < MENU_LANG_BASE + static_cast<int>(langs.size())) {
        const std::wstring& code = langs[cmd - MENU_LANG_BASE].first;
        if (code != g_lang) {
            SaveLanguageCode(code);
            LoadLanguage(code);
            EnsureInfoFile();                   // regenerate info.txt in the new language
            // Refresh the open editor IN PLACE (don't destroy it — that would lose
            // unsaved edits). It re-labels controls from the model already in memory.
            if (g_hEditor) SendMessageW(g_hEditor, WM_MMP_RELANG, 0, 0);
        }
        return;
    }

    switch (cmd) {
        case MENU_GUIEDIT:
            OpenConfigEditor(hwnd);
            break;
        case MENU_ABOUT:
            AboutDialog(hwnd);
            break;
        case MENU_RELOAD:
            PostMessageW(g_hCtrl, WM_MMP_RELOAD, 0, 0);
            break;
        case MENU_SETDEFAULT:
            if (ConfirmDialog(hwnd, T(S_DLG_SETDEF_TITLE), T(S_DLG_SETDEF_BODY),
                    T(S_DLG_SETDEF_OK), TD_WARNING_ICON)) {
                SetDefaultConfig();
                PostMessageW(g_hCtrl, WM_MMP_RELOAD, 0, 0);
            }
            break;
        case MENU_ERASE:
            if (ConfirmDialog(hwnd, T(S_DLG_ERASE_TITLE), T(S_DLG_ERASE_BODY),
                    T(S_DLG_ERASE_OK), TD_WARNING_ICON)) {
                EraseConfig();
                PostMessageW(g_hCtrl, WM_MMP_RELOAD, 0, 0);
            }
            break;
        case MENU_FOLDER: {
            // Open the exe's folder in Explorer (ico\ sits inside). Launch
            // explorer.exe with the path directly instead of "open" on the
            // folder: a bare folder-"open" can route through DDE, which silently
            // no-ops under our COINIT_DISABLE_OLE1DDE apartment. explorer.exe
            // with an argument always opens the folder.
            std::wstring arg = L"\"" + GetExeDir() + L"\"";
            ShellExecuteW(hwnd, nullptr, L"explorer.exe", arg.c_str(),
                          nullptr, SW_SHOWNORMAL);
            break;
        }
        case MENU_EXIT:
            PostMessageW(g_hCtrl, WM_CLOSE, 0, 0);
            break;
    }
}

// ---------------------------- panel WndProc --------------------------
LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* p = reinterpret_cast<PanelWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_APPBAR_CB:
            if (p) {
                switch (static_cast<UINT>(w)) {
                    case ABN_POSCHANGED:
                        RepositionAppBar(p);
                        break;
                    case ABN_FULLSCREENAPP:
                        SetWindowPos(hwnd, l ? HWND_BOTTOM : HWND_TOPMOST,
                                     0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                        break;
                }
            }
            return 0;

        case WM_DPICHANGED:
            if (p) {
                p->dpi = HIWORD(w);
                RepositionAppBar(p);
            }
            return 0;

        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(w);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH br = CreateSolidBrush(COL_PANEL_BG);
            FillRect(hdc, &rc, br);
            DeleteObject(br);
            return 1;
        }

        case WM_DRAWITEM: {
            auto* d = reinterpret_cast<LPDRAWITEMSTRUCT>(l);
            if (d->CtlType == ODT_BUTTON && p) {
                int idx = static_cast<int>(d->CtlID) - BTN_ID_BASE;
                if (idx < 0 || idx >= static_cast<int>(p->icons.size())) return TRUE;

                // Separator: just a centred vertical line on the panel background.
                if (p->buttons[idx].isSeparator) {
                    HBRUSH bgbr = CreateSolidBrush(COL_PANEL_BG);
                    FillRect(d->hDC, &d->rcItem, bgbr);
                    DeleteObject(bgbr);

                    int cw = d->rcItem.right  - d->rcItem.left;
                    int ch = d->rcItem.bottom - d->rcItem.top;
                    int lineW = MulDiv(SEP_LINE_DIP, p->dpi, 96);
                    if (lineW < 1) lineW = 1;
                    int lx  = d->rcItem.left + (cw - lineW) / 2;
                    int pad = ch / 5;                       // ~20% inset top & bottom
                    RECT lr{ lx, d->rcItem.top + pad, lx + lineW, d->rcItem.bottom - pad };
                    HBRUSH lb = CreateSolidBrush(COL_SEP);
                    FillRect(d->hDC, &lr, lb);
                    DeleteObject(lb);
                    return TRUE;
                }

                bool pressed = (d->itemState & ODS_SELECTED) != 0;
                bool hover   = GetPropW(d->hwndItem, kPropHover) != nullptr;
                COLORREF bg  = pressed ? COL_BTN_PRESS
                             : hover   ? COL_BTN_HOVER
                                       : COL_BTN_BG;

                HBRUSH br = CreateSolidBrush(bg);
                FillRect(d->hDC, &d->rcItem, br);
                DeleteObject(br);

                HICON icon = p->icons[idx];
                int bw = d->rcItem.right  - d->rcItem.left;
                int bh = d->rcItem.bottom - d->rcItem.top;
                int is = MulDiv(ICON_SIZE_DIP, p->dpi, 96);
                if (is > bw - 4) is = bw - 4;
                if (is > bh - 4) is = bh - 4;
                int ix = d->rcItem.left + (bw - is) / 2;
                int iy = d->rcItem.top  + (bh - is) / 2;
                DrawIconEx(d->hDC, ix, iy, icon, is, is, 0, nullptr, DI_NORMAL);
                return TRUE;
            }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(w);
            int code = HIWORD(w);
            if (code == BN_CLICKED && id >= BTN_ID_BASE && p)
                RunButton(p, id - BTN_ID_BASE);
            return 0;
        }

        case WM_CONTEXTMENU:
            ShowContextMenu(hwnd);
            return 0;

        case WM_CLOSE:
            PostMessageW(g_hCtrl, WM_CLOSE, 0, 0);
            return 0;

        case WM_DESTROY:
            return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

// --------------------------- controller WndProc ----------------------
// --------------------------- button subclass -------------------------
// Owner-draw buttons don't get ODS_HOTLIGHT reliably — track hover ourselves.
LRESULT CALLBACK BtnSubclassProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l,
                                 UINT_PTR idSubclass, DWORD_PTR) {
    switch (msg) {
        case WM_MOUSEMOVE:
            if (!GetPropW(hwnd, kPropTracking)) {
                TRACKMOUSEEVENT tme{sizeof(tme)};
                tme.dwFlags   = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                SetPropW(hwnd, kPropTracking, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1)));
                SetPropW(hwnd, kPropHover,    reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1)));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;

        case WM_MOUSELEAVE:
            RemovePropW(hwnd, kPropTracking);
            RemovePropW(hwnd, kPropHover);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;

        case WM_NCDESTROY:
            RemovePropW(hwnd, kPropTracking);
            RemovePropW(hwnd, kPropHover);
            RemoveWindowSubclass(hwnd, BtnSubclassProc, idSubclass);
            break;
    }
    return DefSubclassProc(hwnd, msg, w, l);
}

LRESULT CALLBACK CtrlProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_DISPLAYCHANGE:
            PostMessageW(hwnd, WM_MMP_REBUILD, 0, 0);
            return 0;

        case WM_MMP_REBUILD:
        case WM_MMP_RELOAD:
            DestroyAllPanels();
            LoadGlobalCfg();
            CreateAllPanels();
            return 0;

        case WM_CLOSE:
            DestroyAllPanels();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

} // namespace
