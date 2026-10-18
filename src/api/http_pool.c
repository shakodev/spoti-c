#include "http_internal.h"
#include <string.h>
#include <wchar.h>

bool http_origin_equal(const HttpOrigin *a, const HttpOrigin *b)
{
    return a->secure == b->secure && a->port == b->port && _wcsicmp(a->host, b->host) == 0;
}

bool http_origin_parse(const wchar_t *url, HttpOrigin *origin, wchar_t *path, size_t capacity)
{
    URL_COMPONENTS parts = {0};
    size_t length = 0;
    if (url == NULL || origin == NULL) { return false; }
    while (length < HTTP_URL_CHARS && url[length] != L'\0') {
        if (url[length] <= L' ' || url[length] == L'#' || url[length] == L'\\') { return false; }
        ++length;
    }
    if (length == 0 || length == HTTP_URL_CHARS) { return false; }
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = (DWORD)-1;
    parts.dwUserNameLength = parts.dwPasswordLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url, (DWORD)length, 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
        parts.dwHostNameLength == 0 || parts.dwHostNameLength >= 256 ||
        parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0) { return false; }
    for (DWORD i = 0; i < parts.dwHostNameLength; ++i) {
        wchar_t c = parts.lpszHostName[i];
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
            (c >= L'0' && c <= L'9') || c == L'.' || c == L'-' || c == L':')) { return false; }
    }
    memcpy(origin->host, parts.lpszHostName, parts.dwHostNameLength * sizeof(wchar_t));
    origin->host[parts.dwHostNameLength] = L'\0';
    origin->secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    origin->port = parts.nPort;
    if (path != NULL) {
        size_t n = parts.dwUrlPathLength, extra = parts.dwExtraInfoLength;
        size_t prefix = n == 0 ? 1 : n;
        if (prefix + extra >= capacity) { return false; }
        if (n == 0) { path[0] = L'/'; }
        else { memcpy(path, parts.lpszUrlPath, n * sizeof(wchar_t)); }
        if (extra != 0) { memcpy(path + prefix, parts.lpszExtraInfo, extra * sizeof(wchar_t)); }
        path[prefix + extra] = L'\0';
    }
    return true;
}

bool http_origin_valid(const HttpClient *client, const HttpOrigin *origin)
{
    return origin->secure || (client->config.allow_loopback_http &&
        wcscmp(origin->host, L"127.0.0.1") == 0);
}

bool http_destination_allowed(const HttpClient *client, const HttpOrigin *origin)
{
    if (!http_origin_valid(client, origin)) { return false; }
    for (size_t i = 0; i < client->config.header_origin_count; ++i) {
        if (http_origin_equal(origin, &client->allowed[i])) { return true; }
    }
    return false;
}

HttpResult http_host_acquire(HttpClient *client, const HttpOrigin *origin, HttpHost **output)
{
    HttpHost *empty = NULL;
    for (size_t i = 0; i < HTTP_HOST_COUNT; ++i) {
        HttpHost *host = &client->hosts[i];
        if (host->handle != NULL && http_origin_equal(origin, &host->origin)) {
            ++host->users; *output = host; return HTTP_OK;
        }
        if (host->users == 0 && (empty == NULL || (empty->handle != NULL && host->handle == NULL))) {
            empty = host;
        }
    }
    if (empty == NULL) { return HTTP_FULL; }
    if (empty->handle != NULL) {
        if (!WinHttpCloseHandle(empty->handle)) { return HTTP_OS_ERROR; }
        empty->handle = NULL;
        if (empty->metric.owner != NULL) { (void)metrics_handle_release(&empty->metric); }
    }
    empty->handle = WinHttpConnect(client->session, origin->host, origin->port, 0);
    if (empty->handle == NULL) { return HTTP_OS_ERROR; }
    if (client->config.metrics != NULL && !metrics_handle_acquire(client->config.metrics,
        &empty->metric, (uintptr_t)empty->handle)) {
        WinHttpCloseHandle(empty->handle); empty->handle = NULL; return HTTP_EXHAUSTED;
    }
    empty->origin = *origin; empty->users = 1; *output = empty;
    return HTTP_OK;
}

void http_host_release(HttpSlot *slot)
{
    if (slot->host != NULL) { --slot->host->users; slot->host = NULL; }
}

void http_callback_enqueue(HttpSlot *slot, DWORD status, DWORD bytes, DWORD error)
{
    HttpClient *client = slot->client;
    HttpCallback record = {{(uint32_t)(slot - client->slots), slot->generation},
        slot->attempt, status, bytes, error};
    AcquireSRWLockExclusive(&client->lock);
    /* One operation in flight per slot: at most operation/error/closing, 24
     * descriptors total. No informational callbacks are registered. Never drop
     * a record silently if the WinHTTP contract is violated. */
    if (client->count == HTTP_CALLBACK_CAPACITY) { client->overflow = true; }
    else {
        client->queue[(client->head + client->count) % HTTP_CALLBACK_CAPACITY] = record;
        ++client->count;
    }
    SetEvent(client->wake);
    /* Unlock is the last access to client/slot by this callback, including
     * HANDLE_CLOSING. The owner drains with this same lock. */
    ReleaseSRWLockExclusive(&client->lock);
}

bool http_callback_take(HttpClient *client, HttpCallback *output)
{
    bool found = false;
    AcquireSRWLockExclusive(&client->lock);
    if (client->count != 0) {
        *output = client->queue[client->head];
        client->head = (client->head + 1) % HTTP_CALLBACK_CAPACITY;
        --client->count; found = true;
    }
    if (client->count == 0) { ResetEvent(client->wake); }
    ReleaseSRWLockExclusive(&client->lock);
    return found;
}

void CALLBACK http_status_callback(HINTERNET handle, DWORD_PTR context, DWORD status,
    void *info, DWORD bytes)
{
    DWORD error = 0;
    (void)handle;
    if (context == 0) { return; }
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        if (info == NULL || bytes != sizeof(WINHTTP_ASYNC_RESULT)) { error = ERROR_INVALID_DATA; }
        else { error = ((const WINHTTP_ASYNC_RESULT *)info)->dwError; }
    } else if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
        status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
        status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE &&
        status != WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { return; }
    http_callback_enqueue((HttpSlot *)context, status, bytes, error);
}

HttpResult http_client_create(Arena *arena, const HttpConfig *config, HttpClient **output)
{
    ArenaMark mark;
    HttpClient *client;
    if (output == NULL) { return HTTP_INVALID; }
    *output = NULL;
    if (arena == NULL || config == NULL || arena->capacity > MEMORY_BUDGET_HTTP ||
        config->header_origin_count > HTTP_HOST_COUNT) { return HTTP_INVALID; }
    mark = arena_mark(arena);
    client = arena_alloc(arena, sizeof(*client), _Alignof(HttpClient));
    if (client == NULL) { return HTTP_NO_MEMORY; }
    *client = (HttpClient){0};
    client->config = *config;
    for (size_t i = 0; i < config->header_origin_count; ++i) {
        wchar_t path[HTTP_URL_CHARS];
        if (!http_origin_parse(config->header_origins[i], &client->allowed[i], path, HTTP_URL_CHARS) ||
            wcscmp(path, L"/") != 0 || !http_origin_valid(client, &client->allowed[i])) {
            (void)arena_rewind(arena, mark); return HTTP_INVALID;
        }
        client->config.header_origins[i] = NULL;
    }
    InitializeSRWLock(&client->lock);
    client->wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    client->session = WinHttpOpen(L"spoti-c/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    bool tracked = true;
    if (config->metrics != NULL) {
        if (client->wake != NULL) { tracked &= metrics_handle_acquire(config->metrics,
            &client->wake_metric, (uintptr_t)client->wake); }
        if (client->session != NULL) { tracked &= metrics_handle_acquire(config->metrics,
            &client->session_metric, (uintptr_t)client->session); }
    }
    if (!tracked || client->wake == NULL || client->session == NULL ||
        WinHttpSetStatusCallback(client->session, http_status_callback,
            WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
            WINHTTP_CALLBACK_FLAG_READ_COMPLETE | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
            WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        if (client->session != NULL) { WinHttpCloseHandle(client->session); }
        if (client->wake != NULL) { CloseHandle(client->wake); }
        if (client->session_metric.owner != NULL) { (void)metrics_handle_release(&client->session_metric); }
        if (client->wake_metric.owner != NULL) { (void)metrics_handle_release(&client->wake_metric); }
        (void)arena_rewind(arena, mark); return HTTP_OS_ERROR;
    }
    for (size_t i = 0; i < HTTP_SLOT_COUNT; ++i) { client->slots[i].client = client; }
    *output = client; return HTTP_OK;
}
