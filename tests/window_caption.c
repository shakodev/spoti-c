#include "ui/window.h"
#include "support/test_check.h"
#include <windowsx.h>
#include <dwmapi.h>

static LRESULT hit(HWND hwnd, int x, int y)
{
    return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(x, y));
}

static void caption_geometry(UiWindow *w)
{
    TITLEBARINFOEX title = {0}; title.cbSize = sizeof(title);
    SendMessageW(w->hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&title);
    RECT window, client; POINT origin = {0};
    CHECK(GetWindowRect(w->hwnd, &window)); CHECK(GetClientRect(w->hwnd, &client));
    CHECK(ClientToScreen(w->hwnd, &origin));
    /* Regression: the old extension placed the opaque client at window.top,
     * over the title/button drawing region, even when hit tests succeeded. */
    CHECK(origin.y > title.rcTitleBar.top && origin.y >= title.rcTitleBar.bottom);
    const unsigned children[] = {2, 3, 5};
    for (unsigned i = 0; i < 3; ++i) {
        RECT button = title.rgrect[children[i]];
        CHECK(!(title.rgstate[children[i]] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE)));
        CHECK(button.right > button.left && button.bottom > button.top);
        CHECK(button.bottom <= origin.y && button.left >= window.left && button.right <= window.right);
        /* Compare with Windows itself. Synthetic WM_NCHITTEST on a standard
         * frame can report legacy frame metrics instead of DWM button bounds.
         * Real pointer activation is tested in the opt-in desktop harness. */
        LPARAM point = MAKELPARAM((button.left + button.right) / 2, (button.top + button.bottom) / 2);
        LRESULT native;
        if (!DwmDefWindowProc(w->hwnd, WM_NCHITTEST, 0, point, &native)) {
            native = DefWindowProcW(w->hwnd, WM_NCHITTEST, 0, point);
        }
        CHECK(SendMessageW(w->hwnd, WM_NCHITTEST, 0, point) == native);
        for (unsigned j = 0; j < i; ++j) {
            RECT overlap; CHECK(!IntersectRect(&overlap, &button, &title.rgrect[children[j]]));
        }
    }
    CHECK(hit(w->hwnd, window.left + (window.right - window.left) / 2,
        (title.rcTitleBar.top + title.rcTitleBar.bottom) / 2) == HTCAPTION);
    CHECK(hit(w->hwnd, origin.x + client.right - 4, origin.y + 4) == HTCLIENT);
    if (!IsZoomed(w->hwnd)) {
        CHECK(hit(w->hwnd, window.left + 1, (window.top + window.bottom) / 2) == HTLEFT);
        CHECK(hit(w->hwnd, window.right - 1, (window.top + window.bottom) / 2) == HTRIGHT);
        CHECK(hit(w->hwnd, (window.left + window.right) / 2, window.top + 1) == HTTOP);
        CHECK(hit(w->hwnd, (window.left + window.right) / 2, window.bottom - 1) == HTBOTTOM);
    }
    CHECK((GetWindowLongPtrW(w->hwnd, GWL_STYLE) & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW);
    CHECK(GetSystemMenu(w->hwnd, FALSE));
    HMENU menu = GetSystemMenu(w->hwnd, FALSE);
    UINT command = IsZoomed(w->hwnd) ? SC_RESTORE : SC_MAXIMIZE;
    CHECK(!(GetMenuState(menu, command, MF_BYCOMMAND) & (MF_DISABLED | MF_GRAYED)));
}

void window_caption_tests(UiWindow *w)
{
    const UINT dpis[] = {96, 120, 144, 192};
    const int widths[] = {420, 1100, 1440};
    for (unsigned d = 0; d < 4; ++d) {
        for (unsigned i = 0; i < 3; ++i) {
            /* Exercise suggested-rectangle application at all required scales.
             * Native drawing still uses actual monitor DPI; visual QA records
             * that separately rather than claiming synthetic messages change it. */
            int width = MulDiv(widths[i], (int)dpis[d], 96);
            if (width > GetSystemMetrics(SM_CXMAXTRACK)) { width = GetSystemMetrics(SM_CXMAXTRACK); }
            RECT rect = {-20000, -20000, -20000 + width,
                -20000 + MulDiv(360, (int)dpis[d], 96)};
            SendMessageW(w->hwnd, WM_DPICHANGED, MAKEWPARAM(dpis[d], dpis[d]), (LPARAM)&rect);
            CHECK(w->dpi == dpis[d]); RECT actual; CHECK(GetWindowRect(w->hwnd, &actual));
            CHECK(EqualRect(&rect, &actual));
            ShowWindow(w->hwnd, SW_SHOWNOACTIVATE); caption_geometry(w);
            SendMessageW(w->hwnd, WM_NCACTIVATE, FALSE, 0); caption_geometry(w);
            SendMessageW(w->hwnd, WM_NCACTIVATE, TRUE, 0); caption_geometry(w);
            SendMessageW(w->hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
            CHECK(IsZoomed(w->hwnd)); caption_geometry(w);
            SendMessageW(w->hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
            CHECK(!IsZoomed(w->hwnd)); caption_geometry(w);
        }
    }
    SendMessageW(w->hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0); CHECK(IsIconic(w->hwnd));
    CHECK(ui_window_step(w, 0)); CHECK(!w->scheduler.visible);
    SendMessageW(w->hwnd, WM_SYSCOMMAND, SC_RESTORE, 0); CHECK(!IsIconic(w->hwnd));
    CHECK(ui_window_step(w, 0)); CHECK(w->scheduler.visible);
    SendMessageW(w->hwnd, WM_NCLBUTTONDBLCLK, HTCAPTION, 0); CHECK(IsZoomed(w->hwnd));
    SendMessageW(w->hwnd, WM_NCLBUTTONDBLCLK, HTCAPTION, 0); CHECK(!IsZoomed(w->hwnd));
    puts("native captions: buttons outside client, hit targets, states, DPI notifications and system commands passed");
}
