#include "http_internal.h"
#include <wchar.h>

HttpResult http_classify_status(unsigned status)
{
    if (status >= 200 && status < 300) { return HTTP_OK; }
    if (status == 401 || status == 403) { return HTTP_AUTH; }
    if (status == 429) { return HTTP_RATE_LIMIT; }
    if (status >= 500 && status <= 599) { return HTTP_SERVER_ERROR; }
    if (status >= 300 && status <= 399) { return HTTP_REDIRECT; }
    return HTTP_STATUS_ERROR;
}

uint64_t http_utc_ms(void)
{
    FILETIME ft;
    ULARGE_INTEGER ticks;
    GetSystemTimeAsFileTime(&ft);
    ticks.LowPart = ft.dwLowDateTime; ticks.HighPart = ft.dwHighDateTime;
    return ticks.QuadPart / 10000;
}

uint32_t http_retry_delay_ms(unsigned retry, const wchar_t *value, uint64_t utc_ms)
{
    uint64_t delay = core_backoff_ms(250, 30000, retry);
    if (value != NULL && *value != L'\0') {
        uint64_t seconds = 0;
        const wchar_t *p = value;
        while (*p >= L'0' && *p <= L'9') {
            if (seconds <= 30) { seconds = seconds * 10 + (unsigned)(*p - L'0'); }
            ++p;
        }
        if (*p == L'\0') {
            uint64_t requested = seconds > 30 ? 30000 : seconds * 1000;
            if (requested > delay) { delay = requested; }
        } else {
            SYSTEMTIME time = {0};
            FILETIME ft;
            wchar_t canonical[WINHTTP_TIME_FORMAT_BUFSIZE];
            if (wcslen(value) == 29 && WinHttpTimeToSystemTime(value, &time) &&
                SystemTimeToFileTime(&time, &ft) && WinHttpTimeFromSystemTime(&time, canonical) &&
                _wcsicmp(value, canonical) == 0) {
                ULARGE_INTEGER ticks;
                ticks.LowPart = ft.dwLowDateTime; ticks.HighPart = ft.dwHighDateTime;
                uint64_t target = ticks.QuadPart / 10000;
                uint64_t requested = target > utc_ms ? target - utc_ms : 0;
                if (requested > delay) { delay = requested; }
            }
        }
    }
    return (uint32_t)(delay > 30000 ? 30000 : delay);
}
