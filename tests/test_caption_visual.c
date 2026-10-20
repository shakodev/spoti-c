#include "ui/ui_surface.h"
#include "ui/app.h"
#include "support/test_check.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <string.h>

/* Explicit desktop/GPU opt-in only. Capture the composed window, including
 * native nonclient pixels (an offscreen DX11 readback cannot test captions). */
static UiSurface surface;
static App app;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static unsigned char pixels[4096u * 2160u * 4u];

static void settle(UiWindow *w)
{
    uint64_t end = GetTickCount64() + 250;
    do { CHECK(ui_window_step(w, 10)); } while (GetTickCount64() < end);
    CHECK(SUCCEEDED(DwmFlush()));
}

static void capture(UiWindow *w, const char *directory, const char *name)
{
    RECT rect; CHECK(GetWindowRect(w->hwnd, &rect));
    (void)DwmGetWindowAttribute(w->hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    CHECK(width > 0 && width <= 4096 && height > 0 && height <= 2160);
    HDC screen = GetDC(NULL); CHECK(screen);
    HDC dc = CreateCompatibleDC(screen); CHECK(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height); CHECK(bitmap);
    HGDIOBJ previous = SelectObject(dc, bitmap); CHECK(previous);
    CHECK(BitBlt(dc, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY));
    CHECK(SelectObject(dc, previous));
    BITMAPINFO info = {0};
    info.bmiHeader = (BITMAPINFOHEADER){sizeof(BITMAPINFOHEADER), width, -height,
        1, 32, BI_RGB, 0, 0, 0, 0, 0};
    CHECK(GetDIBits(dc, bitmap, 0, (UINT)height, pixels, &info, DIB_RGB_COLORS) == height);
    CHECK(DeleteObject(bitmap)); CHECK(DeleteDC(dc)); CHECK(ReleaseDC(NULL, screen));
    char path[MAX_PATH];
    CHECK(snprintf(path, sizeof(path), "%s/%s-%u.bmp", directory, name, GetDpiForWindow(w->hwnd)) > 0);
    FILE *file = NULL; CHECK(fopen_s(&file, path, "wb") == 0);
    DWORD bytes = (DWORD)width * (DWORD)height * 4;
    BITMAPFILEHEADER header = {0};
    header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + bytes;
    CHECK(fwrite(&header, sizeof(header), 1, file) == 1);
    CHECK(fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, file) == 1);
    CHECK(fwrite(pixels, bytes, 1, file) == 1); CHECK(fclose(file) == 0);
    printf("capture %s: actual DPI %u, %dx%d\n", name, GetDpiForWindow(w->hwnd), width, height);
}

typedef struct PointerTest {
    UiWindow *window;
    const char *directory, *pressed_name;
    POINT point;
} PointerTest;

static DWORD WINAPI pointer_click(void *context)
{
    PointerTest *test = context;
    Sleep(50);
    INPUT input = {0}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    CHECK(SendInput(1, &input, sizeof(input)) == 1);
    Sleep(100); CHECK(SUCCEEDED(DwmFlush()));
    capture(test->window, test->directory, test->pressed_name);
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    CHECK(SendInput(1, &input, sizeof(input)) == 1);
    return 0;
}

static void pointer_button(UiWindow *w, unsigned child, const char *directory,
    const char *hover_name, const char *pressed_name)
{
    TITLEBARINFOEX title = {0}; title.cbSize = sizeof(title);
    SendMessageW(w->hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&title);
    RECT button = title.rgrect[child];
    CHECK(button.right > button.left && button.bottom > button.top);
    PointerTest test = {.window=w, .directory=directory, .pressed_name=pressed_name,
        .point={(button.left + button.right) / 2, (button.top + button.bottom) / 2}};
    CHECK(SetCursorPos(test.point.x, test.point.y));
    settle(w); capture(w, directory, hover_name);
    HANDLE thread = CreateThread(NULL, 0, pointer_click, &test, 0, NULL); CHECK(thread);
    uint64_t deadline = GetTickCount64() + 5000;
    while (WaitForSingleObject(thread, 0) == WAIT_TIMEOUT) {
        CHECK(GetTickCount64() < deadline);
        (void)ui_window_step(w, 10);
    }
    CHECK(CloseHandle(thread));
    /* Delivery of the injected release can follow the input worker's exit. */
    uint64_t end = GetTickCount64() + 200;
    while (!w->closed && GetTickCount64() < end) {
        if (!ui_window_step(w, 10)) { CHECK(w->closed && !w->failed); break; }
    }
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CHECK(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    static unsigned char memory[8192]; Arena arena = {0}; EventQueue *queue = NULL;
    CHECK(arena_init(&arena, memory, sizeof(memory)));
    CHECK(event_queue_create(&arena, 16, &queue) == CORE_OK);
    UiWindow w = {0};
    UiWindowConfig config = {.instance=GetModuleHandleW(NULL), .title=L"spoti-c caption verification",
        .width=1100, .height=720, .workers=queue, .frame=ui_surface_frame,
        .prepare=ui_surface_prepare, .context=&surface};
    CHECK(ui_window_create(&w, &config));
    CHECK(ui_surface_create(&surface, &w, true) == UI_RENDER_OK);
    AppServices services = {0};
    CHECK(app_init(&app, &services, &w.scheduler, clay, sizeof(clay), scratch, sizeof(scratch)) == APP_OK);
    surface.app = &app; surface.text.config.weight = 400;
    app_bind_fonts(&app, &surface.text);
    CHECK(SetWindowPos(w.hwnd, HWND_TOPMOST, 30, 30, 1100, 720, 0));
    ShowWindow(w.hwnd, SW_SHOW); (void)SetForegroundWindow(w.hwnd); settle(&w);
    POINT original_cursor; CHECK(GetCursorPos(&original_cursor));
    const int widths[] = {420, 1100, 1440};
    const char *names[] = {"narrow", "default", "wide"};
    MONITORINFO monitor = {0}; monitor.cbSize = sizeof(monitor);
    CHECK(GetMonitorInfoW(MonitorFromWindow(w.hwnd, MONITOR_DEFAULTTONEAREST), &monitor));
    for (unsigned i = 0; i < 3; ++i) {
        int width = MulDiv(widths[i], (int)w.dpi, 96), height = MulDiv(720, (int)w.dpi, 96);
        int available_width = monitor.rcWork.right - monitor.rcWork.left - 60;
        int available_height = monitor.rcWork.bottom - monitor.rcWork.top - 60;
        if (width > available_width) { width = available_width; }
        if (height > available_height) { height = available_height; }
        CHECK(SetWindowPos(w.hwnd, NULL, monitor.rcWork.left + 30, monitor.rcWork.top + 30,
            width, height, SWP_NOZORDER));
        settle(&w); capture(&w, argv[1], names[i]);
        SendMessageW(w.hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0); settle(&w);
        char maximized_name[64];
        CHECK(snprintf(maximized_name, sizeof(maximized_name), "%s-maximized", names[i]) > 0);
        capture(&w, argv[1], maximized_name);
        SendMessageW(w.hwnd, WM_SYSCOMMAND, SC_RESTORE, 0); settle(&w);
    }
    SendMessageW(w.hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    settle(&w); capture(&w, argv[1], "maximized");
    SendMessageW(w.hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
    settle(&w);
    HWND other = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"caption activation test",
        WS_POPUP, -20000, -20000, 100, 100, NULL, NULL, config.instance, NULL); CHECK(other);
    ShowWindow(other, SW_SHOW); CHECK(SetForegroundWindow(other)); settle(&w);
    CHECK(GetForegroundWindow() != w.hwnd); capture(&w, argv[1], "inactive");
    CHECK(SetForegroundWindow(w.hwnd)); CHECK(DestroyWindow(other)); settle(&w);
    pointer_button(&w, 3, argv[1], "maximize-hover", "maximize-pressed"); CHECK(IsZoomed(w.hwnd));
    pointer_button(&w, 3, argv[1], "restore-hover", "restore-pressed"); CHECK(!IsZoomed(w.hwnd));
    pointer_button(&w, 2, argv[1], "minimize-hover", "minimize-pressed"); CHECK(IsIconic(w.hwnd));
    SendMessageW(w.hwnd, WM_SYSCOMMAND, SC_RESTORE, 0); settle(&w);
    pointer_button(&w, 5, argv[1], "close-hover", "close-pressed"); CHECK(w.closed && IsWindow(w.hwnd));
    CHECK(SetCursorPos(original_cursor.x, original_cursor.y));
    ui_surface_destroy(&surface); app_dispose(&app); CHECK(ui_window_destroy(&w));
    CHECK(event_queue_close(queue) == CORE_OK); CHECK(event_queue_destroy(queue) == CORE_OK);
    arena_dispose(&arena); return 0;
}
