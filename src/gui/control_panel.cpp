/*
 * Sony LDAC Control Panel & System Tray Application
 * Modern Dark-Mode GUI with Live VU Meters, Bitrate Gauges, and Auto-Connection
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <chrono>

#include "../../include/bth_manager.h"
#include "../../include/wasapi_streamer.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#define WM_TRAYICON (WM_USER + 101)
#define ID_TRAY_EXIT 2001
#define ID_TRAY_OPEN 2002
#define ID_TRAY_990  2003
#define ID_TRAY_660  2004
#define ID_TRAY_330  2005
#define ID_TRAY_AUTO 2006
#define ID_TRAY_TOGGLE 2007

#define IDC_DEVICE_COMBO 3001
#define IDC_BTN_REFRESH  3002
#define IDC_BTN_START    3003
#define IDC_BTN_HQ       3004
#define IDC_BTN_SQ       3005
#define IDC_BTN_MQ       3006
#define IDC_BTN_AUTO     3007

static HINSTANCE g_hInstance = NULL;
static HWND g_hWnd = NULL;
static NOTIFYICONDATAW g_nid = {0};
static std::vector<BTH_DEVICE_ENTRY> g_devices;
static int g_selected_device_idx = -1;
static int g_selected_bitrate_mode = LDACBT_EQMID_HQ; /* 990 kbps */
static HBTH_SESSION g_active_session = NULL;
static std::atomic<bool> g_is_streaming(false);

static HFONT g_hFontTitle = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontSmall = NULL;

static HWND g_hComboDevices = NULL;
static HWND g_hBtnRefresh = NULL;
static HWND g_hBtnStart = NULL;
static HWND g_hBtnHQ = NULL;
static HWND g_hBtnSQ = NULL;
static HWND g_hBtnMQ = NULL;
static HWND g_hBtnAuto = NULL;

static void RefreshDevices() {
    g_devices = bth_enumerate_audio_devices();
    SendMessage(g_hComboDevices, CB_RESETCONTENT, 0, 0);

    g_selected_device_idx = -1;
    for (size_t i = 0; i < g_devices.size(); i++) {
        std::wstring item = g_devices[i].name;
        if (g_devices[i].is_sony_device) {
            item += L"  [SONY LDAC Hi-Res]";
        }
        item += L" (" + std::wstring(g_devices[i].addr_str.begin(), g_devices[i].addr_str.end()) + L")";

        SendMessageW(g_hComboDevices, CB_ADDSTRING, 0, (LPARAM)item.c_str());

        /* Auto-select first Sony device (or user's WH-1000XM4) */
        if (g_selected_device_idx == -1 && g_devices[i].is_sony_device) {
            g_selected_device_idx = (int)i;
        }
    }

    if (g_selected_device_idx == -1 && !g_devices.empty()) {
        g_selected_device_idx = 0;
    }

    if (g_selected_device_idx >= 0) {
        SendMessage(g_hComboDevices, CB_SETCURSEL, g_selected_device_idx, 0);
    }
}

static void ToggleStreaming() {
    if (g_is_streaming) {
        if (g_active_session) {
            bth_stream_session_destroy(g_active_session);
            g_active_session = NULL;
        }
        g_is_streaming = false;
        SetWindowTextW(g_hBtnStart, L"START LDAC STREAMING");
    } else {
        int cur = (int)SendMessage(g_hComboDevices, CB_GETCURSEL, 0, 0);
        if (cur < 0 || cur >= (int)g_devices.size()) {
            MessageBoxW(g_hWnd, L"Please select a Bluetooth audio device.", L"Sony LDAC Driver", MB_OK | MB_ICONWARNING);
            return;
        }

        uint64_t addr = g_devices[cur].bth_addr;
        g_active_session = bth_stream_session_create(addr, 96000, g_selected_bitrate_mode);
        if (!g_active_session) {
            MessageBoxW(g_hWnd, L"Failed to initialize LDAC streaming session.", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        if (!bth_stream_session_start(g_active_session)) {
            MessageBoxW(g_hWnd, L"Failed to start audio stream. Verify Bluetooth device is powered on.", L"Error", MB_OK | MB_ICONERROR);
            bth_stream_session_destroy(g_active_session);
            g_active_session = NULL;
            return;
        }

        g_is_streaming = true;
        SetWindowTextW(g_hBtnStart, L"STOP STREAMING");
    }
    InvalidateRect(g_hWnd, NULL, FALSE);
}

static void DrawVUMeter(HDC hdc, int x, int y, int w, int h, float level, COLORREF colNormal, COLORREF colPeak) {
    HBRUSH bgBrush = CreateSolidBrush(RGB(20, 24, 30));
    RECT bgRect = { x, y, x + w, y + h };
    FillRect(hdc, &bgRect, bgBrush);
    DeleteObject(bgBrush);

    int fillW = (int)(level * (float)w);
    if (fillW > w) fillW = w;
    if (fillW < 0) fillW = 0;

    int normalW = (int)((float)w * 0.75f);
    if (fillW <= normalW) {
        HBRUSH fillBrush = CreateSolidBrush(colNormal);
        RECT r = { x, y, x + fillW, y + h };
        FillRect(hdc, &r, fillBrush);
        DeleteObject(fillBrush);
    } else {
        HBRUSH fillBrush = CreateSolidBrush(colNormal);
        RECT r1 = { x, y, x + normalW, y + h };
        FillRect(hdc, &r1, fillBrush);
        DeleteObject(fillBrush);

        HBRUSH peakBrush = CreateSolidBrush(colPeak);
        RECT r2 = { x + normalW, y, x + fillW, y + h };
        FillRect(hdc, &r2, fillBrush);
        DeleteObject(peakBrush);
    }

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(45, 55, 68));
    SelectObject(hdc, borderPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, x, y, x + w, y + h);
    DeleteObject(borderPen);
}

static void OnPaint(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    int width = clientRect.right;
    int height = clientRect.bottom;

    /* Double-buffering */
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    SelectObject(memDC, memBitmap);

    /* Background fill (Dark Charcoal) */
    HBRUSH bgBrush = CreateSolidBrush(RGB(18, 22, 28));
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    /* Header Banner */
    RECT headerRect = { 0, 0, width, 70 };
    HBRUSH headerBrush = CreateSolidBrush(RGB(12, 15, 20));
    FillRect(memDC, &headerRect, headerBrush);
    DeleteObject(headerBrush);

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));
    SelectObject(memDC, g_hFontTitle);
    TextOutW(memDC, 24, 14, L"SONY LDAC™ AUDIO DRIVER", 24);

    SetTextColor(memDC, RGB(0, 210, 255));
    SelectObject(memDC, g_hFontSmall);
    TextOutW(memDC, 24, 42, L"High-Resolution 96.0 kHz / 24-bit Bluetooth Wireless Engine", 59);

    /* Status Card */
    RECT cardRect = { 20, 85, width - 20, 160 };
    HBRUSH cardBrush = CreateSolidBrush(RGB(26, 32, 42));
    FillRect(memDC, &cardRect, cardBrush);
    DeleteObject(cardBrush);

    HPEN cardPen = CreatePen(PS_SOLID, 1, RGB(40, 50, 65));
    SelectObject(memDC, cardPen);
    SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, cardRect.left, cardRect.top, cardRect.right, cardRect.bottom);
    DeleteObject(cardPen);

    SelectObject(memDC, g_hFontBold);
    SetTextColor(memDC, RGB(220, 230, 245));
    TextOutW(memDC, 35, 96, L"STREAMING STATUS:", 17);

    BTH_STREAM_STATS stats = {0};
    if (g_active_session) {
        stats = bth_stream_session_get_stats(g_active_session);
    }

    SelectObject(memDC, g_hFontNormal);
    if (g_is_streaming) {
        if (!stats.is_connected) {
            SetTextColor(memDC, RGB(255, 80, 80));
            TextOutW(memDC, 185, 96, L"BLOCKED: L2CAP KERNEL LOCK (10050)", 35);
        } else {
            SetTextColor(memDC, RGB(50, 230, 120));
            TextOutW(memDC, 185, 96, L"ACTIVE (HIGH-RES STREAMING)", 27);
        }
    } else {
        SetTextColor(memDC, RGB(180, 190, 205));
        TextOutW(memDC, 185, 96, L"STANDBY (READY)", 15);
    }

    SelectObject(memDC, g_hFontSmall);
    SetTextColor(memDC, RGB(140, 160, 185));

    std::wostringstream ossStats;
    if (g_is_streaming && !stats.is_connected) {
        SetTextColor(memDC, RGB(255, 160, 70));
        ossStats << L"Windows BthA2dp.sys locks AVDTP channel. Encoded, but user-mode L2CAP socket rejected.";
    } else {
        ossStats << L"Bitrate: " << (g_is_streaming ? stats.current_bitrate_kbps : 990) << L" kbps  |  "
                 << L"Sample Rate: 96,000 Hz  |  "
                 << L"Buffer Latency: " << std::fixed << std::setprecision(1) << stats.buffer_latency_ms << L" ms  |  "
                 << L"Packets: " << stats.packets_sent << L" (Lost: " << stats.packets_lost << L")";
    }
    std::wstring statsStr = ossStats.str();
    TextOutW(memDC, 35, 128, statsStr.c_str(), (int)statsStr.size());

    /* Audio VU Meters */
    SelectObject(memDC, g_hFontSmall);
    SetTextColor(memDC, RGB(200, 215, 230));
    TextOutW(memDC, 35, 180, L"CH L:", 5);
    DrawVUMeter(memDC, 80, 182, width - 115, 12, stats.peak_left, RGB(0, 210, 255), RGB(255, 60, 60));

    TextOutW(memDC, 35, 202, L"CH R:", 5);
    DrawVUMeter(memDC, 80, 204, width - 115, 12, stats.peak_right, RGB(0, 210, 255), RGB(255, 60, 60));

    /* Codec preset label */
    SelectObject(memDC, g_hFontBold);
    SetTextColor(memDC, RGB(220, 230, 245));
    TextOutW(memDC, 24, 235, L"LDAC QUALITY MODE / BITRATE PRESET:", 35);

    /* Device label */
    TextOutW(memDC, 24, 305, L"TARGET BLUETOOTH DEVICE:", 24);

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hWnd, &ps);
}

static void ShowTrayMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_OPEN, L"Open LDAC Control Panel");
    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING, ID_TRAY_TOGGLE, g_is_streaming ? L"Stop Streaming" : L"Start Streaming");
    InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, 4, MF_BYPOSITION | (g_selected_bitrate_mode == LDACBT_EQMID_HQ ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_990, L"990 kbps (Quality Priority)");
    InsertMenuW(hMenu, 5, MF_BYPOSITION | (g_selected_bitrate_mode == LDACBT_EQMID_SQ ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_660, L"660 kbps (Standard)");
    InsertMenuW(hMenu, 6, MF_BYPOSITION | (g_selected_bitrate_mode == LDACBT_EQMID_MQ ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_330, L"330 kbps (Connection Priority)");
    InsertMenuW(hMenu, 7, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, 8, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Exit Driver Control Panel");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g_hFontTitle = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_hFontBold = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_hFontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_hFontSmall = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

            /* Preset buttons */
            g_hBtnHQ = CreateWindowW(L"BUTTON", L"990 kbps (HQ)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                     24, 260, 115, 28, hWnd, (HMENU)IDC_BTN_HQ, g_hInstance, NULL);
            g_hBtnSQ = CreateWindowW(L"BUTTON", L"660 kbps (SQ)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                     145, 260, 115, 28, hWnd, (HMENU)IDC_BTN_SQ, g_hInstance, NULL);
            g_hBtnMQ = CreateWindowW(L"BUTTON", L"330 kbps (MQ)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                     265, 260, 115, 28, hWnd, (HMENU)IDC_BTN_MQ, g_hInstance, NULL);
            g_hBtnAuto = CreateWindowW(L"BUTTON", L"Adaptive (ABR)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                       385, 260, 125, 28, hWnd, (HMENU)IDC_BTN_AUTO, g_hInstance, NULL);

            SendMessage(g_hBtnHQ, BM_SETCHECK, BST_CHECKED, 0);

            /* Device ComboBox */
            g_hComboDevices = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                           24, 330, 420, 200, hWnd, (HMENU)IDC_DEVICE_COMBO, g_hInstance, NULL);

            /* Refresh Button */
            g_hBtnRefresh = CreateWindowW(L"BUTTON", L"Scan", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         452, 329, 65, 26, hWnd, (HMENU)IDC_BTN_REFRESH, g_hInstance, NULL);

            /* Start / Stop Button */
            g_hBtnStart = CreateWindowW(L"BUTTON", L"START LDAC STREAMING", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                        24, 375, 493, 42, hWnd, (HMENU)IDC_BTN_START, g_hInstance, NULL);

            SendMessage(g_hBtnHQ, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            SendMessage(g_hBtnSQ, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            SendMessage(g_hBtnMQ, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            SendMessage(g_hBtnAuto, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            SendMessage(g_hComboDevices, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            SendMessage(g_hBtnRefresh, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            SendMessage(g_hBtnStart, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            /* System Tray Icon */
            g_nid.cbSize = sizeof(NOTIFYICONDATA);
            g_nid.hWnd = hWnd;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAYICON;
            g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            wcscpy_s(g_nid.szTip, L"Sony LDAC Hi-Res Audio Driver");
            Shell_NotifyIconW(NIM_ADD, &g_nid);

            RefreshDevices();

            /* Set Timer for live VU meter and stats refresh (30 FPS) */
            SetTimer(hWnd, 1, 33, NULL);
            break;
        }

        case WM_TIMER:
            InvalidateRect(hWnd, NULL, FALSE);
            break;

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDC_BTN_REFRESH:
                    RefreshDevices();
                    break;
                case IDC_BTN_START:
                case ID_TRAY_TOGGLE:
                    ToggleStreaming();
                    break;
                case IDC_BTN_HQ:
                case ID_TRAY_990:
                    g_selected_bitrate_mode = LDACBT_EQMID_HQ;
                    SendMessage(g_hBtnHQ, BM_SETCHECK, BST_CHECKED, 0);
                    SendMessage(g_hBtnSQ, BM_SETCHECK, BST_UNCHECKED, 0);
                    SendMessage(g_hBtnMQ, BM_SETCHECK, BST_UNCHECKED, 0);
                    SendMessage(g_hBtnAuto, BM_SETCHECK, BST_UNCHECKED, 0);
                    if (g_active_session) bth_stream_session_set_bitrate_mode(g_active_session, LDACBT_EQMID_HQ);
                    break;
                case IDC_BTN_SQ:
                case ID_TRAY_660:
                    g_selected_bitrate_mode = LDACBT_EQMID_SQ;
                    SendMessage(g_hBtnHQ, BM_SETCHECK, BST_UNCHECKED, 0);
                    SendMessage(g_hBtnSQ, BM_SETCHECK, BST_CHECKED, 0);
                    SendMessage(g_hBtnMQ, BM_SETCHECK, BST_UNCHECKED, 0);
                    SendMessage(g_hBtnAuto, BM_SETCHECK, BST_UNCHECKED, 0);
                    if (g_active_session) bth_stream_session_set_bitrate_mode(g_active_session, LDACBT_EQMID_SQ);
                    break;
                case IDC_BTN_MQ:
                case ID_TRAY_330:
                    g_selected_bitrate_mode = LDACBT_EQMID_MQ;
                    SendMessage(g_hBtnHQ, BM_SETCHECK, BST_UNCHECKED, 0);
                    SendMessage(g_hBtnSQ, BM_SETCHECK, BST_UNCHECKED, 0);
                    SendMessage(g_hBtnMQ, BM_SETCHECK, BST_CHECKED, 0);
                    SendMessage(g_hBtnAuto, BM_SETCHECK, BST_UNCHECKED, 0);
                    if (g_active_session) bth_stream_session_set_bitrate_mode(g_active_session, LDACBT_EQMID_MQ);
                    break;
                case IDC_BTN_AUTO:
                case ID_TRAY_AUTO:
                    g_selected_bitrate_mode = LDACBT_EQMID_ABR;
                    SendMessage(g_hBtnHQ, BM_SETCHECK, BST_UNCHECKED, 0);
                    SendMessage(g_hBtnSQ, BM_SETCHECK, BST_UNCHECKED, 0);
                    SendMessage(g_hBtnMQ, BM_SETCHECK, BST_UNCHECKED, 0);
                    SendMessage(g_hBtnAuto, BM_SETCHECK, BST_CHECKED, 0);
                    if (g_active_session) bth_stream_session_set_bitrate_mode(g_active_session, LDACBT_EQMID_ABR);
                    break;
                case ID_TRAY_OPEN:
                    ShowWindow(hWnd, SW_RESTORE);
                    SetForegroundWindow(hWnd);
                    break;
                case ID_TRAY_EXIT:
                    DestroyWindow(hWnd);
                    break;
            }
            break;
        }

        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP) {
                ShowTrayMenu(hWnd);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
            }
            break;

        case WM_SYSCOMMAND:
            if (wParam == SC_MINIMIZE) {
                ShowWindow(hWnd, SW_HIDE);
                return 0;
            }
            return DefWindowProc(hWnd, message, wParam, lParam);

        case WM_PAINT:
            OnPaint(hWnd);
            break;

        case WM_DESTROY:
            KillTimer(hWnd, 1);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_active_session) {
                bth_stream_session_destroy(g_active_session);
                g_active_session = NULL;
            }
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontBold) DeleteObject(g_hFontBold);
            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            if (g_hFontSmall) DeleteObject(g_hFontSmall);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;
    g_hInstance = hInstance;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"SonyLdacControlPanelClass";
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wcex);

    g_hWnd = CreateWindowW(
        L"SonyLdacControlPanelClass",
        L"Sony LDAC™ High-Resolution Audio Driver Control Panel",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 558, 470,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWnd) return FALSE;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
