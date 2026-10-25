#include "Setup.h"
#include <commctrl.h>
#include <thread>
#include <atomic>
#include <array>
#include <algorithm>
#include <stdexcept>

namespace {
constexpr COLORREF Background = RGB(16, 20, 28), Panel = RGB(25, 31, 42), Text = RGB(236, 241, 249), Blue = RGB(46, 116, 245);
HWND window{}, list{}, details{}, summary{}, progress{}, restart{};
std::array<HWND, 6> buttons{};
HFONT font{}, titleFont{}; HBRUSH background{}, panel{};
std::thread worker;
std::atomic_bool finished = false;
bool busy = false;
Setup::Result result;
Setup::Inventory inventory;
HWND licenseEdit{};

LRESULT CALLBACK LicenseProcedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        auto creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto text = static_cast<const std::wstring*>(creation->lpCreateParams);
        licenseEdit = CreateWindowExW(0, L"EDIT", text->c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 16, 16, 800, 570, handle, nullptr, nullptr, nullptr);
        SendMessageW(licenseEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return 0;
    }
    case WM_SIZE: MoveWindow(licenseEdit, 16, 16, std::max(1, static_cast<int>(LOWORD(lparam)) - 32), std::max(1, static_cast<int>(HIWORD(lparam)) - 32), TRUE); return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), Text); SetBkColor(reinterpret_cast<HDC>(wparam), Panel); return reinterpret_cast<LRESULT>(panel);
    case WM_ERASEBKGND: { RECT bounds{}; GetClientRect(handle, &bounds); FillRect(reinterpret_cast<HDC>(wparam), &bounds, background); return 1; }
    case WM_CLOSE: DestroyWindow(handle); return 0;
    }
    return DefWindowProcW(handle, message, wparam, lparam);
}
void ShowLicense(HWND parent) {
    try {
        auto text = Setup::LicenseText();
        WNDCLASSW type{}; type.lpfnWndProc = LicenseProcedure; type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"TitanAudioLinkLicense"; type.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&type);
        HWND viewer = CreateWindowExW(WS_EX_DLGMODALFRAME, type.lpszClassName, L"Third-party notices and Microsoft Public License",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT, 850, 650,
            parent, nullptr, type.hInstance, &text);
        if (!viewer) throw std::runtime_error("Could not create license viewer");
        EnableWindow(parent, FALSE); ShowWindow(viewer, SW_SHOW); SetFocus(licenseEdit);
        MSG message{};
        while (IsWindow(viewer)) {
            BOOL received = GetMessageW(&message, nullptr, 0, 0);
            if (received <= 0) { if (!received) PostQuitMessage(static_cast<int>(message.wParam)); break; }
            if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { DestroyWindow(viewer); break; }
            if (!IsDialogMessageW(viewer, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        if (IsWindow(viewer)) DestroyWindow(viewer);
        EnableWindow(parent, TRUE); SetActiveWindow(parent);
    } catch (...) { MessageBoxW(parent, L"The embedded license could not be displayed. This installer build is incomplete.", L"License", MB_OK | MB_ICONERROR); }
}

void Refresh() {
    inventory = Setup::Inspect();
    std::wstring title = inventory.error ? L"Audio inventory unavailable" : L"Virtual audio pairs: " + std::to_wstring(inventory.devices.size()) + L" / 4";
    title += inventory.serviceError ? L"  ·  Service status unavailable" : inventory.serviceRunning ? L"  ·  Driver running" : inventory.serviceInstalled ? L"  ·  Driver stopped" : L"  ·  Driver not installed";
    SetWindowTextW(summary, title.c_str()); SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& device : inventory.devices) {
        std::wstring row = device.instance + L"  —  " + (device.started ? L"Started" : device.present ? L"Not started" : L"Not present") +
            L"  ·  Problem " + std::to_wstring(device.problem);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
    if (!inventory.devices.empty()) SendMessageW(list, LB_SETCURSEL, 0, 0);
    EnableWindow(buttons[1], !busy && !inventory.error && Setup::CanAdd(static_cast<unsigned>(inventory.devices.size())));
    EnableWindow(buttons[2], !busy && !inventory.error && !inventory.devices.empty());
}
void Idle() {
    busy = false; KillTimer(window, 1); SendMessageW(progress, PBM_SETMARQUEE, FALSE, 0); ShowWindow(progress, SW_HIDE);
    for (HWND button : buttons) EnableWindow(button, TRUE);
    EnableWindow(list, TRUE); Refresh();
}
void Run(const Setup::Command& command) {
    if (busy) return;
    if (worker.joinable()) worker.join();
    busy = true; finished.store(false, std::memory_order_release);
    for (HWND button : buttons) EnableWindow(button, FALSE);
    EnableWindow(list, FALSE); ShowWindow(restart, SW_HIDE);
    SetWindowTextW(details, L"Working… Windows may take a moment to finish the device operation.\r\nNo automatic restart will be performed.");
    ShowWindow(progress, SW_SHOW); SendMessageW(progress, PBM_SETMARQUEE, TRUE, 25); SetTimer(window, 1, 50, nullptr);
    try { worker = std::thread([command] { result = Setup::Execute(command); finished.store(true, std::memory_order_release); }); }
    catch (...) { Idle(); SetWindowTextW(details, L"The operation could not start because resources are unavailable. No device operation was performed."); }
}
void SelectedDetails() {
    LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || static_cast<size_t>(selected) >= inventory.devices.size()) return;
    Setup::Inventory single; single.serviceInstalled = inventory.serviceInstalled; single.serviceRunning = inventory.serviceRunning;
    single.serviceError = inventory.serviceError; single.devices.push_back(inventory.devices[static_cast<size_t>(selected)]);
    std::wstring text = Setup::Describe(single); SetWindowTextW(details, text.c_str());
}
HWND Button(HWND parent, const wchar_t* label, int id, int x, int width) {
    return CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        x, 132, width, 42, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
}
LRESULT CALLBACK Procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        font = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        titleFont = CreateFontW(29, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        background = CreateSolidBrush(Background); panel = CreateSolidBrush(Panel);
        HWND title = CreateWindowW(L"STATIC", L"Titan Audio Link Setup", WS_CHILD | WS_VISIBLE, 26, 22, 740, 38, handle, nullptr, nullptr, nullptr);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        HWND subtitle = CreateWindowW(L"STATIC", L"Independent virtual speaker / microphone pairs. Physical audio defaults stay under your control.",
            WS_CHILD | WS_VISIBLE, 28, 66, 755, 25, handle, nullptr, nullptr, nullptr);
        summary = CreateWindowW(L"STATIC", L"Reading installed devices…", WS_CHILD | WS_VISIBLE, 28, 99, 755, 25, handle, nullptr, nullptr, nullptr);
        buttons[0] = Button(handle, L"Install / update", 1, 28, 140);
        buttons[1] = Button(handle, L"Add pair", 2, 178, 96);
        buttons[2] = Button(handle, L"Remove selected", 3, 284, 138);
        buttons[3] = Button(handle, L"Uninstall", 4, 432, 96);
        buttons[4] = Button(handle, L"Refresh", 5, 538, 96);
        buttons[5] = Button(handle, L"License", 7, 644, 96);
        list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            28, 192, 748, 136, handle, reinterpret_cast<HMENU>(11), nullptr, nullptr);
        details = CreateWindowExW(0, L"EDIT", L"Select Install / update to install the embedded Microsoft-signed evaluation package. Setup validates its catalog and INF/SYS membership before installation.\r\n\r\nInstall / update reuses existing pairs. Add pair creates one additional pair. Select a row to view its exact Windows state.",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            28, 346, 748, 150, handle, nullptr, nullptr, nullptr);
        progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_MARQUEE, 28, 514, 560, 8, handle, nullptr, nullptr, nullptr);
        SendMessageW(progress, PBM_SETBARCOLOR, 0, Blue); SendMessageW(progress, PBM_SETBKCOLOR, 0, Panel);
        restart = CreateWindowW(L"BUTTON", L"Restart Windows", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
            608, 510, 168, 40, handle, reinterpret_cast<HMENU>(6), nullptr, nullptr);
        for (HWND control : {subtitle, summary, list, details, restart}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (HWND control : buttons) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        Refresh(); return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == 11 && HIWORD(wparam) == LBN_SELCHANGE && !busy) { SelectedDetails(); return 0; }
        if (HIWORD(wparam) == BN_CLICKED && !busy) {
            switch (LOWORD(wparam)) {
            case 1: Run({Setup::Action::Install, {}}); break;
            case 2: Run({Setup::Action::AddPair, {}}); break;
            case 3: {
                LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
                if (selected == LB_ERR || static_cast<size_t>(selected) >= inventory.devices.size()) break;
                std::wstring instance = inventory.devices[static_cast<size_t>(selected)].instance;
                std::wstring question = L"Remove only this virtual pair?\r\n\r\n" + instance;
                if (MessageBoxW(handle, question.c_str(), L"Remove selected pair", MB_YESNO | MB_ICONQUESTION) == IDYES)
                    Run({Setup::Action::RemovePair, instance});
                break;
            }
            case 4: if (MessageBoxW(handle, L"Remove all Titan Audio Link pairs and its unused driver packages? Other audio devices are retained.", L"Uninstall Titan Audio Link", MB_YESNO | MB_ICONQUESTION) == IDYES) Run({Setup::Action::Uninstall, {}}); break;
            case 5: Refresh(); SetWindowTextW(details, Setup::Describe(inventory).c_str()); break;
            case 6: if (MessageBoxW(handle, L"Restart Windows now? Save your work first.", L"Restart Windows", MB_YESNO | MB_ICONQUESTION) == IDYES && !Setup::Restart())
                MessageBoxW(handle, L"Windows did not accept the restart request. Restart manually.", L"Restart Windows", MB_OK | MB_ICONERROR); break;
            case 7: ShowLicense(handle); break;
            }
        }
        return 0;
    case WM_TIMER:
        if (wparam == 1 && busy && finished.load(std::memory_order_acquire)) {
            if (worker.joinable()) worker.join();
            std::wstring text = result.detail;
            if (result.error) text += L"\r\nWindows error: " + std::to_wstring(result.error);
            if (result.reboot) text += L"\r\nRestart Windows to complete this operation.";
            Idle(); SetWindowTextW(details, text.c_str()); ShowWindow(restart, result.reboot ? SW_SHOW : SW_HIDE);
        }
        return 0;
    case WM_DRAWITEM: {
        auto item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (item->CtlType != ODT_BUTTON) break;
        bool disabled = (item->itemState & ODS_DISABLED) != 0;
        COLORREF fill = item->CtlID == 1 ? Blue : Panel;
        HBRUSH brush = CreateSolidBrush((item->itemState & ODS_SELECTED) ? RGB(27, 76, 155) : fill);
        FillRect(item->hDC, &item->rcItem, brush); DeleteObject(brush);
        SetBkMode(item->hDC, TRANSPARENT); SetTextColor(item->hDC, disabled ? RGB(107, 118, 136) : Text);
        wchar_t label[128]{}; GetWindowTextW(item->hwndItem, label, 128);
        HFONT old = static_cast<HFONT>(SelectObject(item->hDC, font)); RECT bounds = item->rcItem;
        DrawTextW(item->hDC, label, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(item->hDC, old);
        if (item->itemState & ODS_FOCUS) { InflateRect(&bounds, -3, -3); DrawFocusRect(item->hDC, &bounds); }
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wparam); SetTextColor(dc, Text);
        bool inside = reinterpret_cast<HWND>(lparam) == details || reinterpret_cast<HWND>(lparam) == list;
        SetBkColor(dc, inside ? Panel : Background); return reinterpret_cast<LRESULT>(inside ? panel : background);
    }
    case WM_ERASEBKGND: {
        RECT bounds{}; GetClientRect(handle, &bounds); FillRect(reinterpret_cast<HDC>(wparam), &bounds, background); return 1;
    }
    case WM_SETCURSOR: {
        HWND control = reinterpret_cast<HWND>(wparam);
        if (IsWindowEnabled(control) && (control == restart || std::find(buttons.begin(), buttons.end(), control) != buttons.end())) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        if (busy) { MessageBoxW(handle, L"Wait for Windows to finish the device operation.", L"Operation in progress", MB_OK); return 0; }
        DestroyWindow(handle); return 0;
    case WM_DESTROY:
        if (worker.joinable()) worker.join(); DeleteObject(font); DeleteObject(titleFont); DeleteObject(background); DeleteObject(panel); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(handle, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        MessageBoxW(nullptr, L"Could not establish the protected system DLL search path. No device operation was performed.", L"Titan Audio Link Setup", MB_OK | MB_ICONERROR);
        return ERROR_DLL_INIT_FAILED;
    }
    SetProcessDPIAware(); INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
    WNDCLASSW type{}; type.lpfnWndProc = Procedure; type.hInstance = instance; type.lpszClassName = L"TitanAudioLinkSetup";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&type);
    window = CreateWindowW(type.lpszClassName, L"Titan Audio Link Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 824, 604, nullptr, nullptr, instance, nullptr);
    if (!window) return 1; ShowWindow(window, show); MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return static_cast<int>(message.wParam);
}
