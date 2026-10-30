#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

#include "system_tray.h"

#define CROSE_TRAY_MESSAGE (WM_APP + 38)
#define CROSE_MENU_TOGGLE 1001
#define CROSE_MENU_EXIT 1002

struct CroseTray {
    HWND window;
    WNDPROC previousProc;
    NOTIFYICONDATAW notification;
    HICON icon;
    LONG_PTR previousStyle;
    UINT taskbarCreated;
    int pending;
    int hidden;
};

static CroseTray *activeTray = NULL;

static void show_menu(CroseTray *tray) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, CROSE_MENU_TOGGLE,
                tray->hidden ? L"\u663e\u793a\u56e2\u5b50" : L"\u9690\u85cf\u56e2\u5b50");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, CROSE_MENU_EXIT, L"\u9000\u51fa");
    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(tray->window);
    const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                         cursor.x, cursor.y, 0, tray->window, NULL);
    if (selected == CROSE_MENU_TOGGLE) tray->pending |= CROSE_TRAY_TOGGLE;
    if (selected == CROSE_MENU_EXIT) tray->pending |= CROSE_TRAY_EXIT;
    DestroyMenu(menu);
    PostMessageW(tray->window, WM_NULL, 0, 0);
}

static LRESULT CALLBACK tray_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    CroseTray *tray = activeTray;
    if (tray && tray->window == window) {
        if (message == tray->taskbarCreated) {
            Shell_NotifyIconW(NIM_ADD, &tray->notification);
            return 0;
        }
        if (message == CROSE_TRAY_MESSAGE) {
            if (lparam == WM_LBUTTONUP) tray->pending |= CROSE_TRAY_TOGGLE;
            else if (lparam == WM_RBUTTONUP) show_menu(tray);
            return 0;
        }
        return CallWindowProcW(tray->previousProc, window, message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static HICON create_icon(const unsigned char *rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0 || width > 256 || height > 256) return NULL;
    BITMAPINFO bitmapInfo;
    memset(&bitmapInfo, 0, sizeof(bitmapInfo));
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP color = CreateDIBSection(NULL, &bitmapInfo, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!color || !bits) return NULL;
    unsigned char *bgra = (unsigned char *)bits;
    for (int i = 0; i < width * height; ++i) {
        bgra[4*i + 0] = rgba[4*i + 2];
        bgra[4*i + 1] = rgba[4*i + 1];
        bgra[4*i + 2] = rgba[4*i + 0];
        bgra[4*i + 3] = rgba[4*i + 3];
    }
    const int maskStride = ((width + 15)/16)*2;
    unsigned char *maskBits = (unsigned char *)calloc((size_t)maskStride*(size_t)height, 1);
    HBITMAP mask = maskBits ? CreateBitmap(width, height, 1, 1, maskBits) : NULL;
    free(maskBits);
    ICONINFO iconInfo;
    memset(&iconInfo, 0, sizeof(iconInfo));
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = color;
    iconInfo.hbmMask = mask;
    HICON icon = mask ? CreateIconIndirect(&iconInfo) : NULL;
    if (mask) DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

CroseTray *crose_tray_create(void *nativeWindow) {
    if (!nativeWindow || activeTray) return NULL;
    CroseTray *tray = (CroseTray *)calloc(1, sizeof(*tray));
    if (!tray) return NULL;
    tray->window = (HWND)nativeWindow;
    tray->taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    tray->previousProc = (WNDPROC)SetWindowLongPtrW(tray->window, GWLP_WNDPROC, (LONG_PTR)tray_window_proc);
    if (!tray->previousProc) { free(tray); return NULL; }
    activeTray = tray;
    tray->notification.cbSize = sizeof(tray->notification);
    tray->notification.hWnd = tray->window;
    tray->notification.uID = 1;
    tray->notification.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray->notification.uCallbackMessage = CROSE_TRAY_MESSAGE;
    tray->notification.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    wcscpy_s(tray->notification.szTip, sizeof(tray->notification.szTip)/sizeof(wchar_t),
             L"\u56e2\u5b50 3D \u684c\u5ba0");
    if (!Shell_NotifyIconW(NIM_ADD, &tray->notification)) {
        SetWindowLongPtrW(tray->window, GWLP_WNDPROC, (LONG_PTR)tray->previousProc);
        activeTray = NULL;
        free(tray);
        return NULL;
    }
    // Tool windows stay visible on the desktop but do not appear in the taskbar or Alt+Tab.
    tray->previousStyle = GetWindowLongPtrW(tray->window, GWL_EXSTYLE);
    ShowWindow(tray->window, SW_HIDE);
    SetWindowLongPtrW(tray->window, GWL_EXSTYLE,
                      (tray->previousStyle | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
    SetWindowPos(tray->window, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    ShowWindow(tray->window, SW_SHOWNOACTIVATE);
    return tray;
}

void crose_tray_update_icon(CroseTray *tray, const unsigned char *rgba, int width, int height) {
    if (!tray) return;
    HICON icon = create_icon(rgba, width, height);
    if (!icon) return;
    HICON previous = tray->icon;
    tray->notification.hIcon = icon;
    if (Shell_NotifyIconW(NIM_MODIFY, &tray->notification)) {
        tray->icon = icon;
        if (previous) DestroyIcon(previous);
    } else {
        tray->notification.hIcon = previous ? previous : LoadIconW(NULL, MAKEINTRESOURCEW(32512));
        DestroyIcon(icon);
    }
}

int crose_tray_take_requests(CroseTray *tray) {
    if (!tray) return 0;
    const int pending = tray->pending;
    tray->pending = 0;
    if (pending & CROSE_TRAY_TOGGLE) tray->hidden = !tray->hidden;
    return pending;
}

void crose_tray_destroy(CroseTray *tray) {
    if (!tray) return;
    Shell_NotifyIconW(NIM_DELETE, &tray->notification);
    if (tray->icon) DestroyIcon(tray->icon);
    SetWindowLongPtrW(tray->window, GWLP_WNDPROC, (LONG_PTR)tray->previousProc);
    activeTray = NULL;
    free(tray);
}

int crose_cursor_in_window(void *nativeWindow, int *x, int *y) {
    if (!nativeWindow || !x || !y) return -1;
    POINT cursor;
    RECT client;
    if (!GetCursorPos(&cursor) || !ScreenToClient((HWND)nativeWindow, &cursor) ||
        !GetClientRect((HWND)nativeWindow, &client)) return -1;
    *x = cursor.x;
    *y = cursor.y;
    return cursor.x >= client.left && cursor.y >= client.top &&
           cursor.x < client.right && cursor.y < client.bottom;
}
